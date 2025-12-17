/**
 * ESP32-P4 OTA Assembly Tool - Binary Validator
 * Validates ESP32-P4 firmware binaries and checks compatibility
 */

class BinaryValidator {
    constructor() {
        this.esp32p4MagicNumbers = [
            0xE9,  // ESP32 application image
            0xEA   // ESP32 bootloader image
        ];

        this.minPartitionSizes = {
            bootloader: 0x2000,     // 8KB minimum
            application: 0x10000,   // 64KB minimum
            nvs: 0x3000,            // 12KB minimum (3 sectors)
            otadata: 0x2000,        // 8KB minimum
            spiffs: 0x1000          // 4KB minimum
        };

        this.maxPartitionSizes = {
            bootloader: 0x20000,    // 128KB maximum
            application: 0x300000,  // 3MB maximum typical
            nvs: 0x10000,           // 64KB maximum
            otadata: 0x2000,        // 8KB maximum
            spiffs: 0x400000        // 4MB maximum
        };

        this.alignmentSize = 0x1000; // 4KB alignment
    }

    /**
     * Validate a binary file for ESP32-P4 compatibility
     * @param {File} file - Binary file to validate
     * @param {string} partitionType - Type of partition (bootloader, factory, nvs, etc.)
     * @returns {Object} Validation result
     */
    async validateBinary(file, partitionType) {
        const result = {
            valid: false,
            errors: [],
            warnings: [],
            info: {},
            partitionType: partitionType
        };

        try {
            // Basic file checks
            this.validateFile(file, result);

            // Read and validate binary content
            const buffer = await this.readFileAsBuffer(file);
            this.validateBinaryContent(buffer, partitionType, result);

            // Extract information from binary
            this.extractBinaryInfo(buffer, partitionType, result);

            // Check ESP32-P4 specific requirements
            this.validateESP32P4Requirements(buffer, partitionType, result);

            // Determine final validity
            result.valid = result.errors.length === 0;

        } catch (error) {
            result.errors.push(`Validation failed: ${error.message}`);
            result.valid = false;
        }

        return result;
    }

    /**
     * Perform basic file validation
     * @param {File} file - File to validate
     * @param {Object} result - Validation result object
     */
    validateFile(file, result) {
        // Check file existence
        if (!file) {
            result.errors.push('No file provided');
            return;
        }

        // Check file name
        if (!file.name) {
            result.errors.push('File has no name');
            return;
        }

        // Check file extension
        if (!file.name.toLowerCase().endsWith('.bin')) {
            result.warnings.push('File does not have .bin extension');
        }

        // Check file size
        if (file.size === 0) {
            result.errors.push('File is empty');
            return;
        }

        // Check maximum file size (50MB limit)
        const maxFileSize = 50 * 1024 * 1024;
        if (file.size > maxFileSize) {
            result.errors.push(`File too large: ${this.formatBytes(file.size)} > ${this.formatBytes(maxFileSize)}`);
            return;
        }

        result.info.fileSize = file.size;
        result.info.fileName = file.name;
    }

    /**
     * Validate binary content
     * @param {ArrayBuffer} buffer - Binary data
     * @param {string} partitionType - Partition type
     * @param {Object} result - Validation result object
     */
    validateBinaryContent(buffer, partitionType, result) {
        const view = new DataView(buffer);
        const size = buffer.byteLength;

        // Check minimum size
        const minSize = this.minPartitionSizes[partitionType] || this.minPartitionSizes.application;
        if (size < minSize) {
            result.errors.push(`Binary too small for ${partitionType}: ${this.formatBytes(size)} < ${this.formatBytes(minSize)}`);
            return;
        }

        // Check maximum size
        const maxSize = this.maxPartitionSizes[partitionType] || this.maxPartitionSizes.application;
        if (size > maxSize) {
            result.warnings.push(`Binary large for ${partitionType}: ${this.formatBytes(size)} > ${this.formatBytes(maxSize)}`);
        }

        // Check magic number for application/bootloader images
        if (partitionType === 'bootloader' || partitionType === 'factory' || partitionType === 'ota_0' || partitionType.includes('ota_')) {
            if (size >= 1) {
                const magicByte = view.getUint8(0);
                if (!this.esp32p4MagicNumbers.includes(magicByte)) {
                    result.errors.push(`Invalid magic number: 0x${magicByte.toString(16)}. Expected ESP32 image.`);
                } else {
                    result.info.magicNumber = `0x${magicByte.toString(16)}`;
                    result.info.imageType = magicByte === 0xE9 ? 'Application' : 'Bootloader';
                }
            }
        }

        result.info.binarySize = size;
    }

    /**
     * Extract information from binary
     * @param {ArrayBuffer} buffer - Binary data
     * @param {string} partitionType - Partition type
     * @param {Object} result - Validation result object
     */
    extractBinaryInfo(buffer, partitionType, result) {
        const view = new DataView(buffer);
        const size = buffer.byteLength;

        result.info.partitionType = partitionType;

        // Extract ESP application image information
        if (size >= 24) {
            const magicByte = view.getUint8(0);
            if (magicByte === 0xE9) {
                // ESP application image format
                try {
                    const segmentCount = view.getUint8(1);
                    const entryPoint = view.getUint32(4, true); // Little-endian
                    const flashOffset = view.getUint32(16, true);

                    result.info.segmentCount = segmentCount;
                    result.info.entryPoint = `0x${entryPoint.toString(16).toUpperCase()}`;
                    result.info.flashOffset = `0x${flashOffset.toString(16).toUpperCase()}`;

                    // Calculate hash for integrity checking
                    result.info.md5 = this.calculateMD5(buffer);
                } catch (error) {
                    result.warnings.push(`Failed to extract image info: ${error.message}`);
                }
            }
        }

        // Check if image is padded correctly
        const alignment = this.alignmentSize;
        if (size % alignment !== 0) {
            result.warnings.push(`Binary not aligned to ${this.formatBytes(alignment)} boundary`);
        }

        // Analyze binary content patterns
        this.analyzeBinaryPatterns(buffer, result);
    }

    /**
     * Validate ESP32-P4 specific requirements
     * @param {ArrayBuffer} buffer - Binary data
     * @param {string} partitionType - Partition type
     * @param {Object} result - Validation result object
     */
    validateESP32P4Requirements(buffer, partitionType, result) {
        // ESP32-P4 specific checks
        result.info.targetChip = 'ESP32-P4';

        // Check for ESP32-P4 specific features in the binary
        if (partitionType === 'factory' || partitionType.includes('ota_')) {
            this.checkForESP32P4Features(buffer, result);
        }

        // Validate partition-specific requirements
        this.validatePartitionSpecificRequirements(buffer, partitionType, result);

        // Check memory layout compatibility
        this.checkMemoryLayoutCompatibility(buffer, partitionType, result);
    }

    /**
     * Check for ESP32-P4 specific features
     * @param {ArrayBuffer} buffer - Binary data
     * @param {Object} result - Validation result object
     */
    checkForESP32P4Features(buffer, result) {
        const text = new TextDecoder('utf-8', { fatal: false }).decode(buffer);

        // Check for ESP32-P4 specific strings
        const esp32p4Features = [
            'ESP32-P4',
            'esp32p4',
            'P4',
            'Pixel Processing Accelerator',
            'PPA',
            'H264',
            'JPEG',
            'MIPI',
            'DSI',
            'RGB',
            'LCD'
        ];

        const foundFeatures = [];
        esp32p4Features.forEach(feature => {
            if (text.includes(feature)) {
                foundFeatures.push(feature);
            }
        });

        if (foundFeatures.length > 0) {
            result.info.esp32p4Features = foundFeatures;
            result.info.esp32p4Optimized = true;
        } else {
            result.warnings.push('No ESP32-P4 specific features detected. Binary may not be optimized for ESP32-P4.');
            result.info.esp32p4Optimized = false;
        }
    }

    /**
     * Validate partition-specific requirements
     * @param {ArrayBuffer} buffer - Binary data
     * @param {string} partitionType - Partition type
     * @param {Object} result - Validation result object
     */
    validatePartitionSpecificRequirements(buffer, partitionType, result) {
        switch (partitionType) {
            case 'bootloader':
                this.validateBootloader(buffer, result);
                break;
            case 'factory':
            case 'ota_0':
            case 'ota_1':
            case 'ota_2':
                this.validateApplication(buffer, result);
                break;
            case 'nvs':
                this.validateNVS(buffer, result);
                break;
            case 'otadata':
                this.validateOTADATA(buffer, result);
                break;
            case 'config':
                this.validateConfig(buffer, result);
                break;
        }
    }

    /**
     * Validate bootloader binary
     * @param {ArrayBuffer} buffer - Binary data
     * @param {Object} result - Validation result object
     */
    validateBootloader(buffer, result) {
        const view = new DataView(buffer);
        const size = buffer.byteLength;

        // Check for bootloader magic number
        if (size >= 1) {
            const magicByte = view.getUint8(0);
            if (magicByte !== 0xEA) {
                result.warnings.push('Bootloader may not have correct magic number (expected 0xEA)');
            }
        }

        // Check bootloader size (should be reasonable)
        if (size > 0x10000) { // 64KB
            result.warnings.push(`Bootloader seems large: ${this.formatBytes(size)}`);
        }

        // Check for bootloader strings
        const text = new TextDecoder('utf-8', { fatal: false }).decode(buffer.slice(0, Math.min(size, 1024)));
        if (text.includes('bootloader') || text.includes('ESP32')) {
            result.info.bootloaderDetected = true;
        }
    }

    /**
     * Validate application binary
     * @param {ArrayBuffer} buffer - Binary data
     * @param {Object} result - Validation result object
     */
    validateApplication(buffer, result) {
        const view = new DataView(buffer);
        const size = buffer.byteLength;

        // Check for application magic number
        if (size >= 1) {
            const magicByte = view.getUint8(0);
            if (magicByte !== 0xE9) {
                result.errors.push('Application must have magic number 0xE9');
            }
        }

        // Validate segment count
        if (size >= 2) {
            const segmentCount = view.getUint8(1);
            if (segmentCount === 0 || segmentCount > 16) {
                result.warnings.push(`Unusual segment count: ${segmentCount}`);
            }
        }

        // Check entry point (should be reasonable)
        if (size >= 8) {
            const entryPoint = view.getUint32(4, true);
            if (entryPoint === 0) {
                result.warnings.push('Entry point is 0x00000000');
            }
        }
    }

    /**
     * Validate NVS partition
     * @param {ArrayBuffer} buffer - Binary data
     * @param {Object} result - Validation result object
     */
    validateNVS(buffer, result) {
        const size = buffer.byteLength;

        // NVS should be multiple of 4096
        if (size % 4096 !== 0) {
            result.warnings.push('NVS partition should be multiple of 4096 bytes');
        }

        // Check for NVS magic numbers at sector boundaries
        const view = new DataView(buffer);
        let nvsMagicFound = false;

        for (let offset = 0; offset < size; offset += 4096) {
            if (offset + 32 <= size) {
                const nvsMagic = view.getUint32(offset, true);
                if (nvsMagic === 0xC0FFEE) {
                    nvsMagicFound = true;
                    break;
                }
            }
        }

        if (!nvsMagicFound) {
            result.warnings.push('NVS magic number (0xC0FFEE) not found');
        }
    }

    /**
     * Validate OTA data partition
     * @param {ArrayBuffer} buffer - Binary data
     * @param {Object} result - Validation result object
     */
    validateOTADATA(buffer, result) {
        const size = buffer.byteLength;

        // OTA data should be exactly 0x2000 bytes
        if (size !== 0x2000) {
            result.warnings.push(`OTA data partition should be exactly ${this.formatBytes(0x2000)}, got ${this.formatBytes(size)}`);
        }
    }

    /**
     * Validate config partition
     * @param {ArrayBuffer} buffer - Binary data
     * @param {Object} result - Validation result object
     */
    validateConfig(buffer, result) {
        const size = buffer.byteLength;

        // Check for SPIFFS magic number
        if (size >= 512) { // SPIFFS magic is at offset 0
            const view = new DataView(buffer);
            const spiffsMagic = view.getUint32(0, true);

            if (spiffsMagic === 0x20150516) {
                result.info.filesystemType = 'SPIFFS';
            } else {
                result.warnings.push('SPIFFS magic number (0x20150516) not found');
            }
        }
    }

    /**
     * Check memory layout compatibility
     * @param {ArrayBuffer} buffer - Binary data
     * @param {string} partitionType - Partition type
     * @param {Object} result - Validation result object
     */
    checkMemoryLayoutCompatibility(buffer, partitionType, result) {
        // Check for incompatible memory addresses
        const view = new DataView(buffer);
        const size = buffer.byteLength;

        if (size >= 24 && partitionType.includes('ota_')) {
            const flashOffset = view.getUint32(16, true);

            // Check for typical ESP32 memory layout constraints
            if (flashOffset < 0x10000) {
                result.warnings.push(`Flash offset seems low: 0x${flashOffset.toString(16)}`);
            }

            if (flashOffset > 0x800000) {
                result.warnings.push(`Flash offset seems high: 0x${flashOffset.toString(16)}`);
            }
        }
    }

    /**
     * Analyze binary patterns for additional insights
     * @param {ArrayBuffer} buffer - Binary data
     * @param {Object} result - Validation result object
     */
    analyzeBinaryPatterns(buffer, result) {
        const size = buffer.byteLength;
        const view = new DataView(buffer);

        // Check for binary density (non-zero bytes)
        let nonZeroBytes = 0;
        const sampleSize = Math.min(size, 4096); // Sample first 4KB

        for (let i = 0; i < sampleSize; i++) {
            if (view.getUint8(i) !== 0) {
                nonZeroBytes++;
            }
        }

        const density = (nonZeroBytes / sampleSize) * 100;
        result.info.binaryDensity = `${density.toFixed(1)}%`;

        if (density < 10) {
            result.warnings.push('Binary has very low data density');
        } else if (density > 95) {
            result.warnings.push('Binary has very high data density (may be compressed/encrypted)');
        }

        // Check for common patterns
        this.checkForCommonPatterns(buffer, result);
    }

    /**
     * Check for common binary patterns
     * @param {ArrayBuffer} buffer - Binary data
     * @param {Object} result - Validation result object
     */
    checkForCommonPatterns(buffer, result) {
        const size = buffer.byteLength;
        const view = new DataView(buffer);

        // Check for strings that indicate compilation info
        const text = new TextDecoder('utf-8', { fatal: false }).decode(buffer.slice(0, Math.min(size, 1024)));

        const patterns = {
            'FreeRTOS': /freertos/i.test(text),
            'ESP-IDF': /esp-idf|espidf/i.test(text),
            'Debug build': /debug/i.test(text),
            'Release build': /release/i.test(text),
            'Optimized': /opt(imized)?/i.test(text),
            'LwIP': /lwip/i.test(text),
            'WiFi': /wifi|wi-fi/i.test(text),
            'Bluetooth': /bluetooth|ble/i.test(text)
        };

        result.info.detectedFeatures = Object.entries(patterns)
            .filter(([name, detected]) => detected)
            .map(([name]) => name);

        if (result.info.detectedFeatures.length === 0) {
            result.warnings.push('No common firmware features detected in binary header');
        }
    }

    /**
     * Read file as ArrayBuffer
     * @param {File} file - File to read
     * @returns {Promise<ArrayBuffer>} File content as buffer
     */
    readFileAsBuffer(file) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onload = (e) => resolve(e.target.result);
            reader.onerror = () => reject(new Error('Failed to read file'));
            reader.readAsArrayBuffer(file);
        });
    }

    /**
     * Calculate MD5 hash of binary data
     * @param {ArrayBuffer} buffer - Binary data
     * @returns {string} MD5 hash
     */
    calculateMD5(buffer) {
        // Simple hash implementation (in production, use crypto API)
        let hash = 0;
        const view = new Uint8Array(buffer);

        for (let i = 0; i < Math.min(view.length, 1024); i++) {
            hash = ((hash << 5) - hash + view[i]) & 0xffffffff;
        }

        return Math.abs(hash).toString(16).padStart(8, '0');
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
     * Validate multiple files for batch processing
     * @param {File[]} files - Array of files to validate
     * @param {string} partitionType - Base partition type
     * @returns {Promise<Object>} Batch validation result
     */
    async validateBatch(files, partitionType = 'ota_0') {
        const results = {
            valid: 0,
            invalid: 0,
            warnings: 0,
            files: []
        };

        for (let i = 0; i < files.length; i++) {
            const file = files[i];
            const otaType = partitionType.replace('ota_0', `ota_${i}`);

            try {
                const result = await this.validateBinary(file, otaType);
                results.files.push({
                    file: file,
                    result: result,
                    index: i
                });

                if (result.valid) {
                    results.valid++;
                } else {
                    results.invalid++;
                }

                results.warnings += result.warnings.length;

            } catch (error) {
                results.files.push({
                    file: file,
                    result: {
                        valid: false,
                        errors: [`Validation error: ${error.message}`],
                        warnings: [],
                        info: {}
                    },
                    index: i
                });
                results.invalid++;
            }
        }

        return results;
    }

    /**
     * Generate validation report
     * @param {Object} result - Validation result
     * @returns {string} Formatted report
     */
    generateValidationReport(result) {
        let report = `Binary Validation Report\n`;
        report += `========================\n\n`;
        report += `File: ${result.info.fileName || 'Unknown'}\n`;
        report += `Size: ${this.formatBytes(result.info.binarySize || 0)}\n`;
        report += `Type: ${result.partitionType}\n`;
        report += `Status: ${result.valid ? '✅ VALID' : '❌ INVALID'}\n\n`;

        if (result.errors.length > 0) {
            report += `Errors:\n`;
            result.errors.forEach(error => {
                report += `  ❌ ${error}\n`;
            });
            report += '\n';
        }

        if (result.warnings.length > 0) {
            report += `Warnings:\n`;
            result.warnings.forEach(warning => {
                report += `  ⚠️  ${warning}\n`;
            });
            report += '\n';
        }

        report += `Information:\n`;
        Object.entries(result.info).forEach(([key, value]) => {
            report += `  📊 ${key}: ${value}\n`;
        });

        return report;
    }
}

// Export for use in main application
window.BinaryValidator = BinaryValidator;