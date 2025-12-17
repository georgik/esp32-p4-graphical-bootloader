/**
 * ESP32-P4 OTA Assembly Tool - Partition Table Generator
 * Generates ESP32 partition tables and visualizations
 */

class PartitionTableGenerator {
    constructor() {
        this.magicNumber = 0xAA50;
        this.md5SumPlaceholder = '00000000000000000000000000000000';
        this.version = '1.0';
    }

    /**
     * Generate complete partition table including base partitions and OTA partitions
     * @param {Object} basePartitions - Base partition configuration
     * @param {Array} otaPartitions - OTA partition array
     * @param {number} totalFlashSize - Total flash size
     * @returns {Object} Generated partition table
     */
    generateCompletePartitionTable(basePartitions, otaPartitions, totalFlashSize = 0x1000000) {
        const partitions = [];
        let currentOffset = 0;

        // Add base partitions (excluding bootloader components)
        Object.entries(basePartitions).forEach(([key, config]) => {
            if (config.selected && key !== 'partition_table' && key !== 'bootloader') {
                const hasFile = !!config.file;
                const partition = {
                    name: this.getESPIDFPartitionName(key),
                    type: this.getPartitionType(key),
                    subtype: this.getPartitionSubtype(key),
                    offset: config.offset,
                    size: hasFile ? this.alignPartitionSize(config.file.size, key) : this.getDefaultPartitionSize(key),
                    file: config.file,
                    flags: this.getPartitionFlags(key),
                    hasContent: hasFile
                };

                partitions.push(partition);
                currentOffset = partition.offset + partition.size;
            }
        });

        // Add OTA partitions
        otaPartitions.forEach(ota => {
            if (ota.selected) {
                const hasFile = !!ota.file;
                const partition = {
                    name: ota.name,
                    type: 'app',
                    subtype: ota.subtype,
                    offset: ota.offset,
                    size: hasFile ? ota.alignedSize : this.getDefaultPartitionSize('ota'),
                    file: ota.file,
                    flags: '',
                    hasContent: hasFile
                };

                partitions.push(partition);
                currentOffset = partition.offset + partition.size;
            }
        });

        // Sort partitions by offset
        partitions.sort((a, b) => a.offset - b.offset);

        // Check for overlaps
        this.validatePartitions(partitions, totalFlashSize);

        // Generate statistics
        const statistics = this.calculateStatistics(partitions, totalFlashSize);

        return {
            partitions: partitions,
            csv: this.generateCSV(partitions),
            binary: this.generateBinary(partitions),
            statistics: statistics,
            visualization: this.generateVisualization(partitions, totalFlashSize)
        };
    }

    /**
     * Generate CSV format partition table
     * @param {Array} partitions - Array of partitions
     * @returns {string} CSV content
     */
    generateCSV(partitions) {
        const header = '# ESP-IDF Partition Table\n# Name, Type, SubType, Offset, Size, Flags\n';
        const rows = partitions.map(p => {
            const offset = p.offset ? `0x${p.offset.toString(16).toUpperCase()}` : '0x0';
            const size = this.formatSizeForCSV(p.size);
            return `${p.name},${p.type},${p.subtype},${offset},${size},${p.flags}`;
        }).join('\n');

        return header + rows;
    }

    /**
     * Generate binary partition table data
     * @param {Array} partitions - Array of partitions
     * @returns {ArrayBuffer} Binary partition table
     */
    generateBinary(partitions) {
        const entrySize = 32; // Each partition entry is 32 bytes
        const maxEntries = 95; // Maximum entries per ESP-IDF specification
        const tableSize = 32 + (maxEntries * entrySize); // Header + entries

        const buffer = new ArrayBuffer(tableSize);
        const view = new DataView(buffer);

        // Write header (32 bytes)
        view.setUint8(0, this.magicNumber & 0xFF);
        view.setUint8(1, (this.magicNumber >> 8) & 0xFF);
        view.setUint8(2, this.version & 0xFF);
        view.setUint8(3, (this.version >> 8) & 0xFF);

        // Write MD5 placeholder (16 bytes)
        for (let i = 0; i < 16; i++) {
            view.setUint8(4 + i, 0);
        }

        // Write partition entries
        let offset = 32;
        partitions.forEach((partition, index) => {
            if (index >= maxEntries) return;

            // Name (16 bytes, null-terminated)
            const nameBytes = new TextEncoder().encode(partition.name);
            for (let i = 0; i < 16; i++) {
                view.setUint8(offset + i, i < nameBytes.length ? nameBytes[i] : 0);
            }
            offset += 16;

            // Type (1 byte)
            view.setUint8(offset, this.getPartitionTypeValue(partition.type));
            offset += 1;

            // Subtype (1 byte)
            view.setUint8(offset, this.getPartitionSubtypeValue(partition.subtype));
            offset += 1;

            // Offset (4 bytes)
            view.setUint32(offset, partition.offset, true); // Little-endian
            offset += 4;

            // Size (4 bytes)
            view.setUint32(offset, partition.size, true); // Little-endian
            offset += 4;

            // Flags (4 bytes)
            view.setUint32(offset, this.parseFlags(partition.flags), true);
            offset += 6; // Skip to next entry (32 bytes total)
        });

        return buffer;
    }

    /**
     * Get partition type string
     * @param {string} key - Partition key
     * @returns {string} Partition type
     */
    getPartitionType(key) {
        const typeMap = {
            bootloader: 'app',
            partition_table: 'data',
            factory: 'app',
            nvs: 'data',
            otadata: 'data',
            config: 'data'
        };
        return typeMap[key] || 'data';
    }

    /**
     * Get partition subtype string
     * @param {string} key - Partition key
     * @returns {string} Partition subtype
     */
    getPartitionSubtype(key) {
        const subtypeMap = {
            bootloader: 'bootloader',
            partition_table: 'data',
            factory: 'factory',
            nvs: 'nvs',
            otadata: 'ota',
            config: 'spiffs'
        };
        return subtypeMap[key] || 'data';
    }

    /**
     * Get ESP-IDF compliant partition name
     * @param {string} key - Partition key
     * @returns {string} ESP-IDF compliant partition name
     */
    getESPIDFPartitionName(key) {
        const nameMap = {
            bootloader: 'bootloader',
            partition_table: 'partition_table',
            factory: 'factory_app',
            nvs: 'nvs',
            otadata: 'otadata',
            config: 'bootloader_config'
        };
        return nameMap[key] || key;
    }

    /**
     * Format size for ESP-IDF CSV (e.g., "1M", "32K", "8K")
     * @param {number} size - Size in bytes
     * @returns {string} Formatted size string
     */
    formatSizeForCSV(size) {
        // Handle undefined or null size
        if (size === undefined || size === null || size === 0) {
            return '0x0';
        }

        if (size % (1024 * 1024) === 0) {
            return `${size / (1024 * 1024)}M`;
        }
        if (size % 1024 === 0) {
            return `${size / 1024}K`;
        }
        return `0x${size.toString(16).toUpperCase()}`;
    }

    /**
     * Get partition type value for binary format
     * @param {string} type - Partition type string
     * @returns {number} Type value
     */
    getPartitionTypeValue(type) {
        const typeValues = {
            'app': 0x00,
            'data': 0x01
        };
        return typeValues[type] || 0x01;
    }

    /**
     * Get partition subtype value for binary format
     * @param {string} subtype - Partition subtype string
     * @returns {number} Subtype value
     */
    getPartitionSubtypeValue(subtype) {
        const subtypeValues = {
            'bootloader': 0x00,
            'factory': 0x00,
            'ota_0': 0x10,
            'ota_1': 0x11,
            'ota_2': 0x12,
            'ota_3': 0x13,
            'ota_4': 0x14,
            'ota_5': 0x15,
            'ota_6': 0x16,
            'ota_7': 0x17,
            'ota_8': 0x18,
            'ota_9': 0x19,
            'ota_10': 0x1A,
            'ota_11': 0x1B,
            'ota_12': 0x1C,
            'ota_13': 0x1D,
            'ota_14': 0x1E,
            'ota_15': 0x1F,
            'nvs': 0x02,
            'ota': 0x00,
            'spiffs': 0x82
        };

        // Handle OTA_0 through OTA_15
        const otaMatch = subtype.match(/ota_(\d+)/);
        if (otaMatch) {
            return 0x10 + parseInt(otaMatch[1]);
        }

        return subtypeValues[subtype] || 0x00;
    }

    /**
     * Parse flags string to numeric value
     * @param {string} flags - Flags string
     * @returns {number} Flags value
     */
    parseFlags(flags) {
        const flagValues = {
            'encrypted': 0x01,
            'readonly': 0x02
        };

        let value = 0;
        if (flags) {
            flags.split(',').forEach(flag => {
                const flagName = flag.trim().toLowerCase();
                if (flagValues[flagName]) {
                    value |= flagValues[flagName];
                }
            });
        }
        return value;
    }

    /**
     * Get partition flags
     * @param {string} key - Partition key
     * @returns {string} Flags string
     */
    getPartitionFlags(key) {
        const flagMap = {
            nvs: 'encrypted,readonly',
            otadata: 'readonly'
        };
        return flagMap[key] || '';
    }

    /**
     * Get default partition size for partitions without files
     * @param {string} key - Partition key
     * @returns {number} Default size in bytes
     */
    getDefaultPartitionSize(key) {
        const defaultSizes = {
            bootloader: 0x20000,      // 128KB
            partition_table: 0x1000,  // 4KB
            factory: 0x100000,        // 1MB
            nvs: 0x4000,              // 16KB
            otadata: 0x2000,          // 8KB
            config: 0x100000,         // 1MB (SPIFFS)
            ota: 0x100000             // 1MB default for OTA
        };
        return defaultSizes[key] || 0x100000; // Default to 1MB for unknown types
    }

    /**
     * Align partition size according to ESP-IDF requirements
     * @param {number} size - Original size
     * @param {string} key - Partition key
     * @returns {number} Aligned size
     */
    alignPartitionSize(size, key) {
        const minSizes = {
            bootloader: 0x2000,    // 8KB minimum
            nvs: 0x3000,           // 12KB minimum (3 sectors)
            otadata: 0x2000,       // 8KB minimum
            spiffs: 0x1000         // 4KB minimum
        };

        const minSize = minSizes[key] || 0x1000; // 4KB default minimum
        const alignment = 0x1000; // 4KB alignment

        return Math.max(this.alignUp(size, alignment), minSize);
    }

    /**
     * Align value up to specified alignment
     * @param {number} value - Value to align
     * @param {number} alignment - Alignment boundary
     * @returns {number} Aligned value
     */
    alignUp(value, alignment) {
        return Math.ceil(value / alignment) * alignment;
    }

    /**
     * Validate partition table for overlaps and other issues
     * @param {Array} partitions - Array of partitions
     * @param {number} totalFlashSize - Total flash size
     * @throws {Error} If validation fails
     */
    validatePartitions(partitions, totalFlashSize) {
        for (let i = 0; i < partitions.length - 1; i++) {
            const current = partitions[i];
            const next = partitions[i + 1];

            // Check for overlap
            if (current.offset + current.size > next.offset) {
                throw new Error(`Partition overlap detected: ${current.name} (${current.offset} + ${current.size}) overlaps with ${next.name} (${next.offset})`);
            }

            // Check if partition exceeds flash size
            if (current.offset + current.size > totalFlashSize) {
                throw new Error(`Partition ${current.name} exceeds flash size: ends at ${current.offset + current.size}, flash size is ${totalFlashSize}`);
            }
        }

        // Check last partition
        if (partitions.length > 0) {
            const last = partitions[partitions.length - 1];
            if (last.offset + last.size > totalFlashSize) {
                throw new Error(`Partition ${last.name} exceeds flash size: ends at ${last.offset + last.size}, flash size is ${totalFlashSize}`);
            }
        }
    }

    /**
     * Calculate partition table statistics
     * @param {Array} partitions - Array of partitions
     * @param {number} totalFlashSize - Total flash size
     * @returns {Object} Statistics
     */
    calculateStatistics(partitions, totalFlashSize) {
        const totalUsed = partitions.reduce((sum, p) => sum + p.size, 0);
        const totalActual = partitions.reduce((sum, p) => sum + (p.file ? p.file.size : 0), 0);
        const totalWaste = totalUsed - totalActual;
        const utilization = totalActual / totalUsed * 100;

        const typeStats = {};
        partitions.forEach(p => {
            if (!typeStats[p.type]) {
                typeStats[p.type] = { count: 0, size: 0 };
            }
            typeStats[p.type].count++;
            typeStats[p.type].size += p.size;
        });

        return {
            partitionCount: partitions.length,
            totalUsedSpace: totalUsed,
            totalActualSize: totalActual,
            totalWaste: totalWaste,
            utilization: utilization,
            availableSpace: totalFlashSize - totalUsed,
            typeBreakdown: typeStats,
            flashUtilization: (totalUsed / totalFlashSize) * 100
        };
    }

    /**
     * Generate visual representation of partition table
     * @param {Array} partitions - Array of partitions
     * @param {number} totalFlashSize - Total flash size
     * @returns {Array} Visual blocks
     */
    generateVisualization(partitions, totalFlashSize) {
        const blocks = [];
        let currentOffset = 0;

        // Add partitions
        partitions.forEach(partition => {
            // Add gap if there's one
            if (partition.offset > currentOffset) {
                blocks.push({
                    name: 'Free Space',
                    type: 'free',
                    offset: currentOffset,
                    size: partition.offset - currentOffset,
                    percentage: ((partition.offset - currentOffset) / totalFlashSize) * 100
                });
                currentOffset = partition.offset;
            }

            blocks.push({
                name: partition.name,
                type: partition.type,
                offset: partition.offset,
                size: partition.size,
                actualSize: partition.file ? partition.file.size : 0,
                utilization: partition.file ? (partition.file.size / partition.size) * 100 : 100,
                percentage: (partition.size / totalFlashSize) * 100,
                fileName: partition.file ? partition.file.name : null
            });

            currentOffset = partition.offset + partition.size;
        });

        // Add remaining free space
        if (currentOffset < totalFlashSize) {
            blocks.push({
                name: 'Free Space',
                type: 'free',
                offset: currentOffset,
                size: totalFlashSize - currentOffset,
                percentage: ((totalFlashSize - currentOffset) / totalFlashSize) * 100
            });
        }

        return blocks;
    }

    /**
     * Export partition table to various formats
     * @param {Array} partitions - Array of partitions
     * @returns {Object} Exported formats
     */
    exportFormats(partitions) {
        return {
            csv: this.generateCSV(partitions),
            json: JSON.stringify(partitions, null, 2),
            html: this.generateHTMLTable(partitions),
            binary: this.generateBinary(partitions)
        };
    }

    /**
     * Generate HTML table representation
     * @param {Array} partitions - Array of partitions
     * @returns {string} HTML table
     */
    generateHTMLTable(partitions) {
        let html = '<table class="table table-striped table-hover">';
        html += '<thead><tr>';
        html += '<th>Name</th><th>Type</th><th>SubType</th><th>Offset</th><th>Size</th><th>Utilization</th><th>File</th>';
        html += '</tr></thead><tbody>';

        partitions.forEach(partition => {
            const offsetHex = `0x${partition.offset.toString(16).toUpperCase()}`;
            const sizeHex = `0x${partition.size.toString(16).toUpperCase()}`;
            const utilization = partition.file ? ((partition.file.size / partition.size) * 100).toFixed(1) : 'N/A';

            html += '<tr>';
            html += `<td>${partition.name}</td>`;
            html += `<td>${partition.type}</td>`;
            html += `<td>${partition.subtype}</td>`;
            html += `<td>${offsetHex}</td>`;
            html += `<td>${sizeHex} (${this.formatBytes(partition.size)})</td>`;
            html += `<td>${utilization}%</td>`;
            html += `<td>${partition.file ? partition.file.name : 'No file'}</td>`;
            html += '</tr>';
        });

        html += '</tbody></table>';
        return html;
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
     * Compare two partition tables
     * @param {Array} partitions1 - First partition table
     * @param {Array} partitions2 - Second partition table
     * @returns {Object} Comparison result
     */
    comparePartitionTables(partitions1, partitions2) {
        const changes = [];

        // Find added/removed/modified partitions
        const map1 = new Map(partitions1.map(p => [p.name, p]));
        const map2 = new Map(partitions2.map(p => [p.name, p]));

        // Check for removed partitions
        for (const [name, partition] of map1) {
            if (!map2.has(name)) {
                changes.push({
                    type: 'removed',
                    name: name,
                    partition: partition
                });
            }
        }

        // Check for added and modified partitions
        for (const [name, partition] of map2) {
            if (!map1.has(name)) {
                changes.push({
                    type: 'added',
                    name: name,
                    partition: partition
                });
            } else {
                const oldPartition = map1.get(name);
                if (JSON.stringify(oldPartition) !== JSON.stringify(partition)) {
                    changes.push({
                        type: 'modified',
                        name: name,
                        oldPartition: oldPartition,
                        newPartition: partition
                    });
                }
            }
        }

        return {
            changes: changes,
            summary: {
                added: changes.filter(c => c.type === 'added').length,
                removed: changes.filter(c => c.type === 'removed').length,
                modified: changes.filter(c => c.type === 'modified').length
            }
        };
    }
}

// Export for use in main application
window.PartitionTableGenerator = PartitionTableGenerator;