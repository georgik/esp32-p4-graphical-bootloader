/**
 * ESP32-P4 OTA Assembly Tool - OTA Partition Manager
 * Handles dynamic OTA partition creation, validation, and optimization
 */

class OTAPartitionManager {
    constructor() {
        this.maxOTAPartitions = 16;
        this.minOTAPartitionSize = 0x10000; // 64KB minimum
        this.defaultOTAPartitionSize = 4 * 1024 * 1024; // 4MB default
        this.alignmentSize = 0x10000; // 64KB alignment
        this.maxOTAPartitionNumber = 15; // OTA_0 through OTA_15
    }

    /**
     * Calculate optimal partition layout for a given set of OTA binaries
     * @param {File[]} otaFiles - Array of OTA binary files
     * @param {number} startOffset - Starting offset for OTA partitions
     * @param {number} totalFlashSize - Total flash size
     * @returns {Object} Optimized partition layout
     */
    calculateOptimalLayout(otaFiles, startOffset, totalFlashSize) {
        const availableSpace = totalFlashSize - startOffset;
        const partitions = [];
        let currentOffset = startOffset;

        // Validate input files
        const validFiles = otaFiles.filter(file => this.validateOTAFile(file));

        if (validFiles.length === 0) {
            throw new Error('No valid OTA files provided');
        }

        if (validFiles.length > this.maxOTAPartitions) {
            throw new Error(`Too many OTA files. Maximum supported: ${this.maxOTAPartitions}`);
        }

        // Calculate partition sizes and positions
        for (let i = 0; i < validFiles.length && i < this.maxOTAPartitions; i++) {
            const file = validFiles[i];
            const actualSize = file.size;
            const alignedSize = this.alignToPartitionSize(actualSize);

            // Check if we have enough space
            const requiredSpace = alignedSize;
            const remainingSpace = totalFlashSize - currentOffset;

            if (requiredSpace > remainingSpace) {
                console.warn(`Not enough space for ${file.name}. Required: ${this.formatBytes(requiredSpace)}, Available: ${this.formatBytes(remainingSpace)}`);
                break;
            }

            const partition = {
                id: i,
                name: `ota_${i}`,
                subtype: `ota_${i}`,
                fileName: file.name,
                file: file,
                offset: currentOffset,
                actualSize: actualSize,
                alignedSize: alignedSize,
                waste: alignedSize - actualSize,
                utilization: (actualSize / alignedSize) * 100,
                selected: true
            };

            partitions.push(partition);
            currentOffset += alignedSize;
        }

        // Calculate statistics
        const totalUsed = currentOffset - startOffset;
        const totalActual = partitions.reduce((sum, p) => sum + p.actualSize, 0);
        const totalAligned = partitions.reduce((sum, p) => sum + p.alignedSize, 0);
        const totalWaste = totalAligned - totalActual;

        return {
            partitions: partitions,
            statistics: {
                partitionCount: partitions.length,
                totalUsedSpace: totalUsed,
                totalActualSize: totalActual,
                totalAlignedSize: totalAligned,
                totalWaste: totalWaste,
                utilization: (totalActual / totalAligned) * 100,
                remainingSpace: totalFlashSize - currentOffset,
                availableSpace: availableSpace - totalUsed
            }
        };
    }

    /**
     * Validate an OTA binary file
     * @param {File} file - Binary file to validate
     * @returns {boolean} True if valid OTA file
     */
    validateOTAFile(file) {
        if (!file) return false;

        // Check file extension
        if (!file.name.toLowerCase().endsWith('.bin')) {
            return false;
        }

        // Check file size
        if (file.size === 0) {
            return false;
        }

        if (file.size > this.defaultOTAPartitionSize) {
            console.warn(`OTA file ${file.name} is larger than default size: ${this.formatBytes(file.size)} > ${this.formatBytes(this.defaultOTAPartitionSize)}`);
        }

        return true;
    }

    /**
     * Align size to partition boundaries
     * @param {number} size - Original size
     * @returns {number} Aligned size
     */
    alignToPartitionSize(size) {
        return Math.ceil(size / this.alignmentSize) * this.alignmentSize;
    }

    /**
     * Get recommended partition size for a given file size
     * @param {number} fileSize - Size of the OTA file
     * @returns {number} Recommended partition size
     */
    getRecommendedPartitionSize(fileSize) {
        const alignedSize = this.alignToPartitionSize(fileSize);

        // Round up to common partition sizes
        const commonSizes = [
            1 * 1024 * 1024,  // 1MB
            2 * 1024 * 1024,  // 2MB
            4 * 1024 * 1024,  // 4MB
            8 * 1024 * 1024   // 8MB
        ];

        for (const size of commonSizes) {
            if (alignedSize <= size) {
                return size;
            }
        }

        return this.alignToPartitionSize(fileSize);
    }

    /**
     * Generate partition table CSV entries for OTA partitions
     * @param {Array} partitions - Array of OTA partitions
     * @returns {string} CSV entries for partition table
     */
    generatePartitionTableEntries(partitions) {
        if (!partitions || partitions.length === 0) {
            return '';
        }

        return partitions.map(partition => {
            const offsetHex = `0x${partition.offset.toString(16).toUpperCase()}`;
            const sizeHex = `0x${partition.alignedSize.toString(16).toUpperCase()}`;

            return `${partition.name},app,${partition.subtype},${offsetHex},${sizeHex},`;
        }).join('\n');
    }

    /**
     * Analyze partition efficiency and suggest optimizations
     * @param {Array} partitions - Array of OTA partitions
     * @returns {Object} Optimization suggestions
     */
    analyzeEfficiency(partitions) {
        const suggestions = [];
        let totalWaste = 0;
        let totalActual = 0;

        partitions.forEach(partition => {
            totalWaste += partition.waste;
            totalActual += partition.actualSize;

            // Check for poor utilization
            if (partition.utilization < 50) {
                suggestions.push({
                    type: 'poor_utilization',
                    partition: partition.name,
                    utilization: partition.utilization,
                    message: `${partition.name} has low utilization (${partition.utilization.toFixed(1)}%). Consider reducing partition size.`
                });
            }

            // Check for over-sized partitions
            const recommendedSize = this.getRecommendedPartitionSize(partition.actualSize);
            if (partition.alignedSize > recommendedSize) {
                suggestions.push({
                    type: 'oversized',
                    partition: partition.name,
                    currentSize: partition.alignedSize,
                    recommendedSize: recommendedSize,
                    message: `${partition.name} is oversized. Recommended size: ${this.formatBytes(recommendedSize)}.`
                });
            }
        });

        const overallUtilization = totalActual / (totalActual + totalWaste) * 100;

        return {
            overallUtilization: overallUtilization,
            totalWaste: totalWaste,
            suggestions: suggestions,
            grade: this.getEfficiencyGrade(overallUtilization)
        };
    }

    /**
     * Get efficiency grade based on utilization percentage
     * @param {number} utilization - Utilization percentage
     * @returns {string} Efficiency grade
     */
    getEfficiencyGrade(utilization) {
        if (utilization >= 90) return 'A+ (Excellent)';
        if (utilization >= 80) return 'A (Very Good)';
        if (utilization >= 70) return 'B (Good)';
        if (utilization >= 60) return 'C (Fair)';
        if (utilization >= 50) return 'D (Poor)';
        return 'F (Very Poor)';
    }

    /**
     * Simulate different partition layout strategies
     * @param {File[]} otaFiles - Array of OTA files
     * @param {number} startOffset - Starting offset
     * @param {number} totalFlashSize - Total flash size
     * @returns {Array} Different layout strategies
     */
    simulateLayoutStrategies(otaFiles, startOffset, totalFlashSize) {
        const strategies = [];

        // Strategy 1: Minimum size (tight packing)
        try {
            const minimalLayout = this.calculateOptimalLayout(otaFiles, startOffset, totalFlashSize);
            strategies.push({
                name: 'Minimum Size',
                description: 'Use minimum aligned sizes for maximum efficiency',
                layout: minimalLayout,
                pros: ['Maximum space efficiency', 'Fits more partitions'],
                cons: ['Less flexibility for future updates', 'Complex calculations']
            });
        } catch (error) {
            console.warn('Minimum size strategy failed:', error.message);
        }

        // Strategy 2: Standard sizes (1MB, 2MB, 4MB, etc.)
        try {
            const standardLayout = this.calculateStandardLayout(otaFiles, startOffset, totalFlashSize);
            strategies.push({
                name: 'Standard Sizes',
                description: 'Use standard partition sizes (1MB, 2MB, 4MB)',
                layout: standardLayout,
                pros: ['Easy to understand', 'Compatible with common tools'],
                cons: ['Less efficient space usage', 'May waste space']
            });
        } catch (error) {
            console.warn('Standard sizes strategy failed:', error.message);
        }

        // Strategy 3: Balanced (suggested sizes)
        try {
            const balancedLayout = this.calculateBalancedLayout(otaFiles, startOffset, totalFlashSize);
            strategies.push({
                name: 'Balanced',
                description: 'Use recommended sizes based on file requirements',
                layout: balancedLayout,
                pros: ['Good balance of efficiency and flexibility', 'Optimal for most use cases'],
                cons: ['Requires more calculations']
            });
        } catch (error) {
            console.warn('Balanced strategy failed:', error.message);
        }

        return strategies;
    }

    /**
     * Calculate layout using standard partition sizes
     * @param {File[]} otaFiles - Array of OTA files
     * @param {number} startOffset - Starting offset
     * @param {number} totalFlashSize - Total flash size
     * @returns {Object} Layout using standard sizes
     */
    calculateStandardLayout(otaFiles, startOffset, totalFlashSize) {
        const partitions = [];
        let currentOffset = startOffset;

        const validFiles = otaFiles.filter(file => this.validateOTAFile(file));

        for (let i = 0; i < validFiles.length && i < this.maxOTAPartitions; i++) {
            const file = validFiles[i];
            const actualSize = file.size;
            const standardSize = this.getRecommendedPartitionSize(actualSize);

            if (currentOffset + standardSize > totalFlashSize) {
                break;
            }

            partitions.push({
                id: i,
                name: `ota_${i}`,
                subtype: `ota_${i}`,
                fileName: file.name,
                file: file,
                offset: currentOffset,
                actualSize: actualSize,
                alignedSize: standardSize,
                waste: standardSize - actualSize,
                utilization: (actualSize / standardSize) * 100,
                selected: true
            });

            currentOffset += standardSize;
        }

        return {
            partitions: partitions,
            statistics: this.calculateStatistics(partitions, startOffset, totalFlashSize)
        };
    }

    /**
     * Calculate balanced layout
     * @param {File[]} otaFiles - Array of OTA files
     * @param {number} startOffset - Starting offset
     * @param {number} totalFlashSize - Total flash size
     * @returns {Object} Balanced layout
     */
    calculateBalancedLayout(otaFiles, startOffset, totalFlashSize) {
        const partitions = [];
        let currentOffset = startOffset;

        const validFiles = otaFiles.filter(file => this.validateOTAFile(file));

        for (let i = 0; i < validFiles.length && i < this.maxOTAPartitions; i++) {
            const file = validFiles[i];
            const actualSize = file.size;

            // Add 20% buffer for future updates
            const bufferedSize = Math.ceil(actualSize * 1.2);
            const balancedSize = this.alignToPartitionSize(Math.min(bufferedSize, this.defaultOTAPartitionSize));

            if (currentOffset + balancedSize > totalFlashSize) {
                break;
            }

            partitions.push({
                id: i,
                name: `ota_${i}`,
                subtype: `ota_${i}`,
                fileName: file.name,
                file: file,
                offset: currentOffset,
                actualSize: actualSize,
                alignedSize: balancedSize,
                waste: balancedSize - actualSize,
                utilization: (actualSize / balancedSize) * 100,
                selected: true
            });

            currentOffset += balancedSize;
        }

        return {
            partitions: partitions,
            statistics: this.calculateStatistics(partitions, startOffset, totalFlashSize)
        };
    }

    /**
     * Calculate statistics for a partition layout
     * @param {Array} partitions - Array of partitions
     * @param {number} startOffset - Starting offset
     * @param {number} totalFlashSize - Total flash size
     * @returns {Object} Statistics
     */
    calculateStatistics(partitions, startOffset, totalFlashSize) {
        if (partitions.length === 0) {
            return {
                partitionCount: 0,
                totalUsedSpace: 0,
                totalActualSize: 0,
                totalAlignedSize: 0,
                totalWaste: 0,
                utilization: 0,
                remainingSpace: totalFlashSize - startOffset
            };
        }

        const totalActual = partitions.reduce((sum, p) => sum + p.actualSize, 0);
        const totalAligned = partitions.reduce((sum, p) => sum + p.alignedSize, 0);
        const totalWaste = totalAligned - totalActual;
        const lastPartition = partitions[partitions.length - 1];
        const totalUsed = (lastPartition.offset + lastPartition.alignedSize) - startOffset;

        return {
            partitionCount: partitions.length,
            totalUsedSpace: totalUsed,
            totalActualSize: totalActual,
            totalAlignedSize: totalAligned,
            totalWaste: totalWaste,
            utilization: (totalActual / totalAligned) * 100,
            remainingSpace: totalFlashSize - (startOffset + totalUsed)
        };
    }

    /**
     * Format bytes to human readable string
     * @param {number} bytes - Number of bytes
     * @returns {string} Formatted string
     */
    formatBytes(bytes) {
        if (bytes === 0) return '0 B';
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
    }

    /**
     * Export OTA configuration to JSON
     * @param {Array} partitions - Array of OTA partitions
     * @returns {string} JSON string
     */
    exportConfiguration(partitions) {
        const config = {
            version: '1.0',
            created: new Date().toISOString(),
            partitions: partitions.map(p => ({
                name: p.name,
                subtype: p.subtype,
                fileName: p.fileName,
                actualSize: p.actualSize,
                alignedSize: p.alignedSize,
                offset: p.offset,
                selected: p.selected
            }))
        };

        return JSON.stringify(config, null, 2);
    }

    /**
     * Import OTA configuration from JSON
     * @param {string} jsonConfig - JSON configuration string
     * @returns {Array} Array of OTA partitions
     */
    importConfiguration(jsonConfig) {
        try {
            const config = JSON.parse(jsonConfig);

            if (!config.partitions || !Array.isArray(config.partitions)) {
                throw new Error('Invalid configuration format');
            }

            return config.partitions.map(p => ({
                ...p,
                id: parseInt(p.name.replace('ota_', '')),
                waste: p.alignedSize - p.actualSize,
                utilization: (p.actualSize / p.alignedSize) * 100
            }));

        } catch (error) {
            throw new Error(`Failed to import configuration: ${error.message}`);
        }
    }
}

// Export for use in main application
window.OTAPartitionManager = OTAPartitionManager;