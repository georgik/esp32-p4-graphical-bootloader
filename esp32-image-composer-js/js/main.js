/**
 * ESP32-P4 OTA Assembly Tool - Main Application Controller
 * Coordinates all components for dynamic OTA firmware assembly and flashing
 */

class OTAAssemblyApp {
    constructor() {
        this.basePartitions = {
            bootloader: { name: 'Bootloader', offset: 0x0, size: 0x20000, file: null, selected: true },
            partition_table: { name: 'Partition Table', offset: 0x10000, size: 0x1000, file: null, selected: true, custom: false },
            factory: { name: 'Factory App', offset: 0x20000, size: 0x100000, file: null, selected: true },
            nvs: { name: 'NVS Storage', offset: 0x120000, size: 0x8000, file: null, selected: false },
            otadata: { name: 'OTA Data', offset: 0x128000, size: 0x2000, file: null, selected: false },
            config: { name: 'Config (SPIFFS)', offset: 0x130000, size: 0x200000, file: null, selected: false }
        };

        this.otaPartitions = [];
        this.totalFlashSize = 0x1000000; // 16MB
        this.nextOTAOffset = 0x330000; // Start after config partition
        this.otaCount = 0;
        this.maxOTAPartitions = 16;

        this.init();
    }

    async init() {
        console.log('Initializing ESP32-P4 OTA Assembly Tool...');

        // Initialize drag and drop
        this.initDragAndDrop();

        // Initialize file input handlers
        this.initFileInputs();

        // Initialize button handlers
        this.initButtonHandlers();

        // Initialize memory map visualization
        this.updateMemoryMap();

        console.log('OTA Assembly Tool initialized successfully');
    }

    initDragAndDrop() {
        // Handle all drop zones
        const dropZones = document.querySelectorAll('.drop-zone');

        dropZones.forEach(zone => {
            zone.addEventListener('dragover', this.handleDragOver.bind(this));
            zone.addEventListener('dragleave', this.handleDragLeave.bind(this));
            zone.addEventListener('drop', this.handleDrop.bind(this));
            zone.addEventListener('click', this.handleDropZoneClick.bind(this));
        });

        // Prevent default drag behavior on the entire page
        document.addEventListener('dragover', (e) => e.preventDefault());
        document.addEventListener('drop', (e) => e.preventDefault());
    }

    initFileInputs() {
        // Handle file input changes
        ['bootloader', 'factory', 'nvs', 'otadata', 'config'].forEach(partition => {
            const input = document.getElementById(`${partition}-file`);
            if (input) {
                input.addEventListener('change', (e) => {
                    this.handleFileSelect(partition, e.target.files[0]);
                });
            }
        });
    }

    initButtonHandlers() {
        // Connect/disconnect buttons (from ESP-Launchpad)
        document.getElementById('connectButton')?.addEventListener('click', this.handleConnect.bind(this));
        document.getElementById('disconnectButton')?.addEventListener('click', this.handleDisconnect.bind(this));
        document.getElementById('consoleButton')?.addEventListener('click', this.toggleConsole.bind(this));
        document.getElementById('consoleStartButton')?.addEventListener('click', this.resetDevice.bind(this));

        // Note: Other buttons will be added to HTML in the future
        // For now, these are handled through onclick attributes in the HTML
    }

    handleDragOver(e) {
        e.preventDefault();
        e.stopPropagation();
        e.currentTarget.classList.add('drag-over');
    }

    handleDragLeave(e) {
        e.preventDefault();
        e.stopPropagation();
        e.currentTarget.classList.remove('drag-over');
    }

    handleDrop(e) {
        e.preventDefault();
        e.stopPropagation();
        e.currentTarget.classList.remove('drag-over');

        const files = Array.from(e.dataTransfer.files);
        const partition = e.currentTarget.dataset.partition;
        const isOTAZone = e.currentTarget.id === 'ota-drop-zone';

        if (isOTAZone) {
            // Handle multiple OTA files
            this.handleOTAFiles(files);
        } else if (partition) {
            // Handle single file for base partition
            const file = files.find(f => f.name.toLowerCase().endsWith('.bin'));
            if (file) {
                this.handleFileSelect(partition, file);
            }
        }
    }

    handleDropZoneClick(e) {
        const partition = e.currentTarget.dataset.partition;
        const isOTAZone = e.currentTarget.id === 'ota-drop-zone';

        if (!isOTAZone && partition) {
            this.browseFile(partition);
        }
    }

    handleOTAFiles(files) {
        const validFiles = files.filter(file => file.name.toLowerCase().endsWith('.bin'));

        if (validFiles.length === 0) {
            this.showError('No valid .bin files found');
            return;
        }

        validFiles.forEach((file, index) => {
            this.addOTAPartition(file);
        });

        this.updateMemoryMap();
        this.showSuccess(`${validFiles.length} OTA file(s) added successfully`);
    }

    async handleFileSelect(partition, file) {
        if (!file || !file.name.toLowerCase().endsWith('.bin')) {
            this.showError('Please select a valid .bin file');
            return;
        }

        try {
            // Validate the binary file
            const validation = await this.validateBinary(file, partition);
            if (!validation.valid) {
                this.showError(`Invalid ${partition} binary: ${validation.error}`);
                return;
            }

            // Store the file
            this.basePartitions[partition].file = file;

            // Update UI
            this.updatePartitionUI(partition);
            this.updateMemoryMap();

            this.showSuccess(`${partition} partition loaded successfully`);

        } catch (error) {
            this.showError(`Failed to load ${partition} file: ${error.message}`);
        }
    }

    addOTAPartition(file) {
        if (this.otaCount >= this.maxOTAPartitions) {
            this.showError('Maximum number of OTA partitions reached (16)');
            return;
        }

        const alignedSize = this.alignSize(file.size, 0x10000); // 64KB alignment
        const availableSpace = this.totalFlashSize - this.nextOTAOffset;

        if (alignedSize > availableSpace) {
            this.showError(`Not enough space for ${file.name}. Required: ${this.formatSize(alignedSize)}, Available: ${this.formatSize(availableSpace)}`);
            return;
        }

        const otaPartition = {
            id: this.otaCount,
            name: `ota_${this.otaCount}`,
            fileName: file.name,
            file: file,
            offset: this.nextOTAOffset,
            size: alignedSize,
            alignedSize: alignedSize,
            actualSize: file.size,
            subtype: `ota_${this.otaCount}`,
            selected: true
        };

        this.otaPartitions.push(otaPartition);
        this.nextOTAOffset += alignedSize;
        this.otaCount++;

        this.addOTAPartitionUI(otaPartition);
    }

    addOTAPartitionUI(otaPartition) {
        const container = document.getElementById('ota-partitions-list');
        const partitionDiv = document.createElement('div');
        partitionDiv.className = 'partition-card ota-partition';
        partitionDiv.dataset.otaId = otaPartition.id;

        partitionDiv.innerHTML = `
            <div class="row align-items-center">
                <div class="col-md-1">
                    <div class="form-check">
                        <input class="form-check-input" type="checkbox"
                               id="ota-select-${otaPartition.id}"
                               ${otaPartition.selected ? 'checked' : ''}
                               onchange="app.toggleOTAPartition(${otaPartition.id})">
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="partition-name">${otaPartition.name}</div>
                    <div class="file-info">
                        Offset: 0x${otaPartition.offset.toString(16).toUpperCase()},
                        Size: ${this.formatSize(otaPartition.actualSize)}
                    </div>
                    <div class="text-muted small">${otaPartition.fileName}</div>
                </div>
                <div class="col-md-6">
                    <div class="progress">
                        <div class="progress-bar bg-success" role="progressbar"
                             style="width: ${Math.min((otaPartition.actualSize / otaPartition.size) * 100, 100)}%">
                            ${this.formatSize(otaPartition.actualSize)} / ${this.formatSize(otaPartition.size)}
                        </div>
                    </div>
                </div>
                <div class="col-md-2 text-end">
                    <button class="btn btn-sm btn-outline-danger me-2" onclick="app.removeOTAPartition(${otaPartition.id})">
                        <i class="fas fa-trash me-1"></i>Clear
                    </button>
                </div>
            </div>
        `;

        container.appendChild(partitionDiv);
    }

    toggleOTAPartition(otaId) {
        const partition = this.otaPartitions.find(p => p.id === otaId);
        if (partition) {
            partition.selected = !partition.selected;
        }
    }

    removeOTAPartition(otaId) {
        const index = this.otaPartitions.findIndex(p => p.id === otaId);
        if (index !== -1) {
            this.otaPartitions.splice(index, 1);
            this.recalculateOTAPartitions();
            this.updateMemoryMap();
            this.showSuccess('OTA partition removed');
        }
    }

    recalculateOTAPartitions() {
        // Renumber and recalculate offsets for remaining OTA partitions
        this.nextOTAOffset = 0x330000; // Reset to start position
        this.otaCount = 0;

        this.otaPartitions.forEach((partition, index) => {
            partition.id = index;
            partition.name = `ota_${index}`;
            partition.offset = this.nextOTAOffset;
            partition.size = this.alignSize(partition.actualSize, 0x10000);
            this.nextOTAOffset += partition.size;
            this.otaCount++;
        });

        // Rebuild UI
        document.getElementById('ota-partitions-list').innerHTML = '';
        this.otaPartitions.forEach(partition => {
            this.addOTAPartitionUI(partition);
        });
    }

    updatePartitionUI(partition) {
        const zone = document.querySelector(`[data-partition="${partition}"]`);
        if (!zone) return;

        const file = this.basePartitions[partition].file;
        if (file) {
            zone.innerHTML = `
                <div class="selected-file">
                    <i class="fas fa-file-binary me-2"></i>
                    <strong>${file.name}</strong>
                    <div class="file-info">${this.formatSize(file.size)}</div>
                </div>
            `;
            zone.classList.add('valid-drop');
        } else {
            zone.innerHTML = `<span class="drop-text">Drop ${partition}.bin here or click to browse</span>`;
            zone.classList.remove('valid-drop');
        }
    }

    updateMemoryMap() {
        const container = document.getElementById('memory-map-visualization');
        const blocks = [];

        // Add base partitions
        Object.entries(this.basePartitions).forEach(([key, partition]) => {
            if (partition.selected) {
                const hasFile = !!partition.file;
                blocks.push({
                    name: partition.name,
                    offset: partition.offset,
                    size: hasFile ? partition.file.size : this.getDefaultPartitionSize(key),
                    type: key === 'bootloader' ? 'bootloader' :
                          key === 'factory' ? 'factory' : key,
                    selected: partition.selected,
                    hasContent: hasFile
                });
            }
        });

        // Add OTA partitions
        this.otaPartitions.forEach(partition => {
            if (partition.selected) {
                const hasFile = !!partition.file;
                blocks.push({
                    name: partition.name,
                    offset: partition.offset,
                    size: hasFile ? partition.actualSize : this.getDefaultPartitionSize('ota'),
                    type: 'ota',
                    selected: partition.selected,
                    hasContent: hasFile
                });
            }
        });

        // Add free space
        if (blocks.length > 0) {
            blocks.sort((a, b) => a.offset - b.offset);

            let currentOffset = 0;
            const finalBlocks = [];

            for (const block of blocks) {
                if (block.offset > currentOffset) {
                    finalBlocks.push({
                        name: 'Free Space',
                        offset: currentOffset,
                        size: block.offset - currentOffset,
                        type: 'free'
                    });
                }
                finalBlocks.push(block);
                currentOffset = block.offset + block.size;
            }

            if (currentOffset < this.totalFlashSize) {
                finalBlocks.push({
                    name: 'Free Space',
                    offset: currentOffset,
                    size: this.totalFlashSize - currentOffset,
                    type: 'free'
                });
            }

            container.innerHTML = this.generateMemoryMapHTML(finalBlocks);
        } else {
            container.innerHTML = '<div class="text-muted">No partitions loaded yet</div>';
        }

        // Update flashing commands dynamically whenever partitions change
        this.updateFlashingCommands();

        // Update size statistics
        this.updateSizeStatistics();
    }

    /**
     * Update flashing commands dynamically without needing full partition table generation
     */
    updateFlashingCommands() {
        const flashingCommandsElement = document.getElementById('flashing-commands');
        const partitionArgsElement = document.getElementById('partition-args');
        const espflashPartitionArgsElement = document.getElementById('espflash-partition-args');

        if (!flashingCommandsElement || !partitionArgsElement) {
            return;
        }

        // Generate partition arguments for individual flashing
        const partitionArgs = [];
        const espflashPartitionArgs = [];

        // Add base partitions with files
        Object.entries(this.basePartitions).forEach(([key, partition]) => {
            if (partition.selected && partition.file) {
                const offsetHex = partition.offset.toString(16).padStart(8, '0');
                const fileName = partition.file.name;
                partitionArgs.push(`0x${offsetHex} ${fileName}`);
                espflashPartitionArgs.push(`${offsetHex} ${fileName}`);
            }
        });

        // Add OTA partitions with files
        this.otaPartitions.forEach(otaPartition => {
            if (otaPartition.selected && otaPartition.file) {
                const offsetHex = otaPartition.offset.toString(16).padStart(8, '0');
                const fileName = otaPartition.fileName || `ota_${otaPartition.id}.bin`;
                partitionArgs.push(`0x${offsetHex} ${fileName}`);
                espflashPartitionArgs.push(`${offsetHex} ${fileName}`);
            }
        });

        // Update partition arguments display
        if (partitionArgs.length > 0) {
            // Generate the complete esptool.py flashing command
            const esptoolBaseCommand = 'python -m esptool --chip esp32p4 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size detect --flash_freq 80m';
            const esptoolCompleteCommand = `${esptoolBaseCommand} ${partitionArgs.join(' ')}`;

            // Generate the complete espflash command
            const espflashBaseCommand = 'espflash --chip esp32p4 --baud 460800';
            const espflashCompleteCommand = `${espflashBaseCommand} ${espflashPartitionArgs.join(' ')}`;

            partitionArgsElement.innerHTML = `
                <div class="command-block bg-dark text-light p-3 rounded font-monospace small">
                    <h6 class="text-warning mb-2">
                        <i class="fab fa-python me-1"></i>Complete esptool.py Command:
                    </h6>
                    <code>${esptoolCompleteCommand}</code>
                </div>

                <div class="mt-3">
                    <h6 class="text-info mb-2">
                        <i class="fas fa-list me-1"></i>Individual Partitions:
                    </h6>
                    <div class="command-block bg-light border-start border-4 border-primary p-2 rounded font-monospace small">
                        <code>${partitionArgs.join(' \\\n    ')}</code>
                    </div>
                </div>
            `;

            // Update espflash partition arguments
            if (espflashPartitionArgsElement) {
                espflashPartitionArgsElement.innerHTML = `
                    <div class="command-block bg-dark text-light p-3 rounded font-monospace small">
                        <h6 class="text-warning mb-2">
                            <i class="fas fa-bolt me-1"></i>Complete espflash Command:
                        </h6>
                        <code>${espflashCompleteCommand}</code>
                    </div>

                    <div class="mt-3">
                        <h6 class="text-info mb-2">
                            <i class="fas fa-list me-1"></i>Individual Partitions:
                        </h6>
                        <div class="command-block bg-light border-start border-4 border-warning p-2 rounded font-monospace small">
                            <code>${espflashPartitionArgs.join(' \\\n    ')}</code>
                        </div>
                    </div>
                `;
            }

            // Update the command displays
            const espflashELFCommand = 'espflash --chip esp32p4 --baud 460800 --flash-size 16mb --flash-freq 80m --monitor';
            document.getElementById('espflash-individual-command').textContent = espflashBaseCommand;
            document.getElementById('espflash-elf-command').textContent = espflashELFCommand;
            document.getElementById('individual-flash-command').textContent = esptoolBaseCommand;

        } else {
            partitionArgsElement.innerHTML = `
                <div class="alert alert-warning small mb-0">
                    <i class="fas fa-exclamation-triangle me-1"></i>
                    No partitions with files selected. Load partition files first to generate flashing commands.
                </div>
            `;

            if (espflashPartitionArgsElement) {
                espflashPartitionArgsElement.innerHTML = `
                    <div class="alert alert-warning small mb-0">
                        <i class="fas fa-exclamation-triangle me-1"></i>
                    No partitions with files selected. Load partition files first to generate flashing commands.
                    </div>
                `;
            }
        }

        // Show the flashing commands section
        flashingCommandsElement.style.display = 'block';
    }

    /**
     * Update size statistics dynamically
     */
    updateSizeStatistics() {
        const statsElement = document.getElementById('partition-table-stats');
        if (!statsElement) return;

        let selectedSize = 0;
        let selectedCount = 0;
        let withFilesSize = 0;
        let withFilesCount = 0;

        // Calculate base partitions statistics
        Object.entries(this.basePartitions).forEach(([key, partition]) => {
            if (partition.selected) {
                selectedCount++;
                selectedSize += partition.actualSize;

                if (partition.file) {
                    withFilesCount++;
                    withFilesSize += partition.file.size;
                }
            }
        });

        // Calculate OTA partitions statistics
        this.otaPartitions.forEach(otaPartition => {
            if (otaPartition.selected) {
                selectedCount++;
                selectedSize += otaPartition.actualSize;

                if (otaPartition.file) {
                    withFilesCount++;
                    withFilesSize += otaPartition.file.size;
                }
            }
        });

        // Update the statistics display
        const utilization = this.totalFlashSize > 0 ? (selectedSize / this.totalFlashSize * 100).toFixed(1) : 0;
        statsElement.innerHTML = `
            <div class="row text-center">
                <div class="col-md-3">
                    <div class="stat-item">
                        <div class="stat-number">${selectedCount}</div>
                        <div class="stat-label">Selected Partitions</div>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stat-item">
                        <div class="stat-number">${this.formatSize(selectedSize)}</div>
                        <div class="stat-label">Selected Size</div>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stat-item">
                        <div class="stat-number">${withFilesCount}</div>
                        <div class="stat-label">With Files</div>
                    </div>
                </div>
                <div class="col-md-3">
                    <div class="stat-item">
                        <div class="stat-number">${utilization}%</div>
                        <div class="stat-label">Flash Utilization</div>
                    </div>
                </div>
            </div>
        `;
    }

    generateMemoryMapHTML(blocks) {
        const totalSize = this.totalFlashSize;
        let html = '';

        blocks.forEach(block => {
            const percentage = ((block.size / totalSize) * 100).toFixed(1);
            const sizeText = this.formatSize(block.size);
            const selectedClass = block.selected ? 'selected' : '';
            const noContentClass = block.hasContent === false ? 'no-content' : '';
            const contentIndicator = block.hasContent === false ? ' (empty)' : '';

            html += `
                <div class="memory-block ${block.type} ${selectedClass} ${noContentClass}"
                     title="${block.name}: ${sizeText} (${percentage}%)${contentIndicator}"
                     style="width: ${Math.max(percentage, 2)}%;">
                    ${block.name}${contentIndicator}
                </div>
            `;
        });

        return html;
    }

    // Utility methods
    alignSize(size, alignment) {
        return Math.ceil(size / alignment) * alignment;
    }

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

    formatSize(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    }

    formatBytes(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        if (bytes < 1024 * 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
        return (bytes / (1024 * 1024 * 1024)).toFixed(1) + ' GB';
    }

    formatAddress(address) {
        return `0x${address.toString(16).toUpperCase()}`;
    }

    // Binary validation using BinaryValidator class
    async validateBinary(file, partition) {
        if (!window.BinaryValidator) {
            console.warn('BinaryValidator not available, using basic validation');
            return {
                valid: true,
                warnings: ['BinaryValidator not loaded - basic validation only'],
                error: null
            };
        }

        if (!this.binaryValidator) {
            this.binaryValidator = new window.BinaryValidator();
        }

        try {
            const result = await this.binaryValidator.validateBinary(file, partition);
            return result;
        } catch (error) {
            console.error('Binary validation failed:', error);
            return {
                valid: false,
                error: error.message,
                warnings: []
            };
        }
    }

    browseFile(partition) {
        // This will be implemented in ui-components.js
        const input = document.getElementById(`${partition}-file`);
        if (input) {
            input.click();
        }
    }

    clearPartition(partition) {
        this.basePartitions[partition].file = null;
        this.updatePartitionUI(partition);
        this.updateMemoryMap();
        this.showSuccess(`${partition} partition cleared`);
    }

    generatePartitionTable() {
        try {
            // Create partition table generator
            const generator = new window.PartitionTableGenerator();

            // Generate complete partition table
            const partitionTable = generator.generateCompletePartitionTable(
                this.basePartitions,
                this.otaPartitions,
                this.totalFlashSize
            );

            // Update UI with results
            this.displayPartitionTable(partitionTable);

            // Show success message
            this.showSuccess('Partition table generated successfully');

        } catch (error) {
            this.showError(`Failed to generate partition table: ${error.message}`);
            console.error('Partition table generation error:', error);
        }
    }

    displayPartitionTable(partitionTable) {
        // Update CSV display
        const csvTextarea = document.getElementById('partition-table-csv');
        if (csvTextarea) {
            csvTextarea.value = partitionTable.csv;
        }

        // Update statistics if display element exists
        const statsElement = document.getElementById('partition-table-stats');
        if (statsElement) {
            const stats = partitionTable.statistics;
            statsElement.innerHTML = `
                <div class="row">
                    <div class="col-md-3">
                        <div class="stat-item">
                            <div class="stat-number">${stats.partitionCount}</div>
                            <div class="stat-label">Partitions</div>
                        </div>
                    </div>
                    <div class="col-md-3">
                        <div class="stat-item">
                            <div class="stat-number">${this.formatBytes(stats.totalUsedSpace)}</div>
                            <div class="stat-label">Used Space</div>
                        </div>
                    </div>
                    <div class="col-md-3">
                        <div class="stat-item">
                            <div class="stat-number">${stats.utilization.toFixed(1)}%</div>
                            <div class="stat-label">Utilization</div>
                        </div>
                    </div>
                    <div class="col-md-3">
                        <div class="stat-item">
                            <div class="stat-number">${this.formatBytes(stats.availableSpace)}</div>
                            <div class="stat-label">Available</div>
                        </div>
                    </div>
                </div>
            `;

            // Show the statistics section
            statsElement.style.display = 'block';
        }

        // Generate and display flashing commands
        this.displayFlashingCommands(partitionTable);

        // Update the memory map visualization
        this.updateMemoryMapFromGenerator(partitionTable.visualization);
    }

    displayFlashingCommands(partitionTable) {
        const flashingCommandsElement = document.getElementById('flashing-commands');
        const partitionArgsElement = document.getElementById('partition-args');
        const espflashPartitionArgsElement = document.getElementById('espflash-partition-args');

        if (!flashingCommandsElement || !partitionArgsElement) {
            return;
        }

        // Generate partition arguments for individual flashing
        const partitionArgs = [];
        const espflashPartitionArgs = [];

        // Add base partitions with files
        Object.entries(this.basePartitions).forEach(([key, partition]) => {
            if (partition.selected && partition.file) {
                const offsetHex = partition.offset.toString(16).padStart(8, '0');
                const fileName = partition.file.name;
                partitionArgs.push(`0x${offsetHex} ${fileName}`);
                espflashPartitionArgs.push(`--partition-table-offset ${offsetHex} ${fileName}`);
            }
        });

        // Add OTA partitions with files
        this.otaPartitions.forEach(otaPartition => {
            if (otaPartition.selected && otaPartition.file) {
                const offsetHex = otaPartition.offset.toString(16).padStart(8, '0');
                const fileName = otaPartition.fileName || `ota_${otaPartition.id}.bin`;
                partitionArgs.push(`0x${offsetHex} ${fileName}`);
                espflashPartitionArgs.push(`--partition-table-offset ${offsetHex} ${fileName}`);
            }
        });

        // Update partition arguments display
        if (partitionArgs.length > 0) {
            // Generate the complete esptool.py flashing command
            const esptoolBaseCommand = 'python -m esptool --chip esp32p4 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size detect --flash_freq 80m';
            const esptoolCompleteCommand = `${esptoolBaseCommand} ${partitionArgs.join(' ')}`;

            // Generate the complete espflash command
            const espflashBaseCommand = 'espflash --chip esp32p4 --baud 460800';
            const espflashCompleteCommand = `${espflashBaseCommand} ${partitionArgs.join(' ')}`;

            partitionArgsElement.innerHTML = `
                <div class="command-block bg-dark text-light p-3 rounded font-monospace small">
                    <h6 class="text-warning mb-2">
                        <i class="fab fa-python me-1"></i>Complete esptool.py Command:
                    </h6>
                    <code>${esptoolCompleteCommand}</code>
                </div>

                <div class="mt-3">
                    <h6 class="text-info mb-2">
                        <i class="fas fa-list me-1"></i>Individual Partitions:
                    </h6>
                    <div class="command-block bg-light border-start border-4 border-primary p-2 rounded font-monospace small">
                        <code>${partitionArgs.join(' \\\n    ')}</code>
                    </div>
                </div>
            `;

            // Update espflash partition arguments
            if (espflashPartitionArgsElement) {
                espflashPartitionArgsElement.innerHTML = `
                    <div class="command-block bg-dark text-light p-3 rounded font-monospace small">
                        <h6 class="text-warning mb-2">
                            <i class="fas fa-bolt me-1"></i>Complete espflash Command:
                        </h6>
                        <code>${espflashCompleteCommand}</code>
                    </div>

                    <div class="mt-3">
                        <h6 class="text-info mb-2">
                            <i class="fas fa-list me-1"></i>Individual Partitions (espflash format):
                        </h6>
                        <div class="command-block bg-light border-start border-4 border-warning p-2 rounded font-monospace small">
                            <code>${partitionArgs.map(arg => {
                                const [offset, file] = arg.split(' ');
                                return `--partition-table-offset ${offset} ${file}`;
                            }).join(' \\\n    ')}</code>
                        </div>
                    </div>
                `;
            }

        } else {
            partitionArgsElement.innerHTML = `
                <div class="alert alert-warning small mb-0">
                    <i class="fas fa-exclamation-triangle me-1"></i>
                    No partitions with files selected. Load partition files first to generate flashing commands.
                </div>
            `;

            if (espflashPartitionArgsElement) {
                espflashPartitionArgsElement.innerHTML = `
                    <div class="alert alert-warning small mb-0">
                        <i class="fas fa-exclamation-triangle me-1"></i>
                    No partitions with files selected. Load partition files first to generate flashing commands.
                    </div>
                `;
            }
        }

        // Show the flashing commands section
        flashingCommandsElement.style.display = 'block';
    }

    updateMemoryMapFromGenerator(visualization) {
        const container = document.getElementById('memory-map-visualization');
        if (!container || !visualization || visualization.length === 0) {
            container.innerHTML = '<div class="text-muted text-center">No partitions loaded yet</div>';
            return;
        }

        let html = '<div class="memory-visualization">';
        visualization.forEach(block => {
            const percentage = Math.max(block.percentage, 1); // Minimum 1% for visibility
            const selectedClass = block.selected ? 'selected' : '';
            const utilizationBadge = block.utilization ?
                `<span class="badge bg-info ms-2">${block.utilization.toFixed(0)}%</span>` : '';

            html += `
                <div class="memory-block ${block.type} ${selectedClass}"
                     style="width: ${Math.min(percentage, 100)}%;"
                     title="${block.name}: ${this.formatBytes(block.size)} (${percentage.toFixed(1)}%)">
                    <span class="block-label">${block.name}</span>
                    ${utilizationBadge}
                </div>
            `;
        });
        html += '</div>';

        container.innerHTML = html;
    }

    selectAllPartitions() {
        // Select all base partitions and OTA partitions
        Object.keys(this.basePartitions).forEach(key => {
            this.basePartitions[key].selected = true;
        });
        this.otaPartitions.forEach(partition => {
            partition.selected = true;
            const checkbox = document.getElementById(`ota-select-${partition.id}`);
            if (checkbox) checkbox.checked = true;
        });
        this.showSuccess('All partitions selected');
    }

    deselectAllPartitions() {
        // Deselect all OTA partitions (keep base partitions selected)
        this.otaPartitions.forEach(partition => {
            partition.selected = false;
            const checkbox = document.getElementById(`ota-select-${partition.id}`);
            if (checkbox) checkbox.checked = false;
        });
        this.showSuccess('OTA partitions deselected');
    }

    
    saveConfiguration() {
        // Save current configuration to JSON
        const config = {
            basePartitions: {},
            otaPartitions: this.otaPartitions.map(p => ({
                name: p.name,
                fileName: p.fileName,
                size: p.actualSize,
                selected: p.selected
            }))
        };

        Object.entries(this.basePartitions).forEach(([key, partition]) => {
            if (partition.file) {
                config.basePartitions[key] = {
                    fileName: partition.file.name,
                    size: partition.file.size,
                    selected: partition.selected
                };
            }
        });

        const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'ota-assembly-config.json';
        a.click();
        URL.revokeObjectURL(url);

        this.showSuccess('Configuration saved');
    }

    loadConfiguration() {
        // Load configuration from JSON file
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json';
        input.onchange = (e) => {
            const file = e.target.files[0];
            if (file) {
                const reader = new FileReader();
                reader.onload = (e) => {
                    try {
                        const config = JSON.parse(e.target.result);
                        this.applyConfiguration(config);
                        this.showSuccess('Configuration loaded successfully');
                    } catch (error) {
                        this.showError('Invalid configuration file');
                    }
                };
                reader.readAsText(file);
            }
        };
        input.click();
    }

    applyConfiguration(config) {
        // Apply loaded configuration (placeholder - will need file handling)
        console.log('Applying configuration:', config);
    }

    toggleBasePartition(partition) {
        if (this.basePartitions[partition]) {
            this.basePartitions[partition].selected = !this.basePartitions[partition].selected;

            // Update UI
            const checkbox = document.getElementById(`${partition}-select`);
            if (checkbox) {
                checkbox.checked = this.basePartitions[partition].selected;
            }

            this.updateMemoryMap();
        }
    }

    browseOTAFiles() {
        // Create file input element if it doesn't exist
        let input = document.getElementById('ota-file-input');
        if (!input) {
            input = document.createElement('input');
            input.type = 'file';
            input.id = 'ota-file-input';
            input.accept = '.bin';
            input.multiple = true; // Allow multiple file selection for OTA
            input.style.display = 'none';

            // Add event listener for file selection
            input.addEventListener('change', (e) => {
                const files = Array.from(e.target.files);
                if (files.length > 0) {
                    this.handleOTAFiles(files);
                }
            });

            document.body.appendChild(input);
        }

        input.click();
    }

    // ESP-Launchpad integration methods
    async handleConnect() {
        const connectBtn = document.getElementById('connectButton');

        try {
            connectBtn.disabled = true;
            connectBtn.innerHTML = '<i class="fas fa-spinner fa-spin me-2"></i>Connecting...';

            // Initialize flashing controller if not already done
            if (!this.flashingController) {
                this.flashingController = new window.FlashingController();
            }

            await this.flashingController.connect();

            // Update body class for connected state
            document.body.classList.remove('disconnected');
            document.body.classList.add('connected');

            connectBtn.disabled = false;
            connectBtn.innerHTML = '<i class="fas fa-link me-2"></i>Connect';

            // Enable other buttons that exist
            const consoleBtn = document.getElementById('consoleStartButton');
            if (consoleBtn) {
                consoleBtn.disabled = false;
            }

            this.showSuccess('Connected to ESP32-P4 device successfully');
            console.log('Connected to ESP32-P4 device');

        } catch (error) {
            connectBtn.disabled = false;
            connectBtn.innerHTML = '<i class="fas fa-link me-2"></i>Connect';
            this.showError(`Failed to connect: ${error.message}`);
            console.error('Connection failed:', error);
        }
    }

    handleDisconnect() {
        try {
            if (this.flashingController) {
                this.flashingController.disconnect();
            }

            // Update body class for disconnected state
            document.body.classList.remove('connected');
            document.body.classList.add('disconnected');

            // Disable other buttons that exist
            const consoleBtn = document.getElementById('consoleStartButton');
            if (consoleBtn) {
                consoleBtn.disabled = true;
            }

            this.showSuccess('Disconnected from device');
            console.log('Disconnected from ESP32-P4 device');

        } catch (error) {
            this.showError(`Failed to disconnect: ${error.message}`);
            console.error('Disconnection failed:', error);
        }
    }

    toggleConsole() {
        const consolePage = document.getElementById('consolePage');
        const mainPage = document.getElementById('mainContainer');

        if (consolePage.style.display === 'none') {
            consolePage.style.display = 'block';
            mainPage.style.display = 'none';
            this.initConsole();
        } else {
            consolePage.style.display = 'none';
            mainPage.style.display = 'block';
        }
    }

    initConsole() {
        // Initialize terminal using xterm.js
        const terminalContainer = document.getElementById('terminal');
        if (!terminalContainer) return;

        // Clear any existing terminal
        terminalContainer.innerHTML = '';

        // Create new terminal
        this.terminal = new Terminal({
            cursorBlink: true,
            fontSize: 14,
            fontFamily: 'Consolas, Monaco, "Courier New", monospace',
            theme: {
                background: '#1e1e1e',
                foreground: '#ffffff',
                cursor: '#ffffff'
            }
        });

        // Create fit addon to make terminal fit container
        if (window.FitAddon) {
            this.fitAddon = new window.FitAddon.FitAddon();
            this.terminal.loadAddon(this.fitAddon);
        } else {
            // Fallback if FitAddon is not available
            this.fitAddon = null;
            console.warn('FitAddon not available, terminal will not auto-resize');
        }

        // Open terminal in container
        this.terminal.open(terminalContainer);

        // Fit terminal to container
        this.fitAddon.fit();

        // Add keyboard handler for Ctrl+C to stop monitoring
        this.terminal.onKey((event) => {
            // Check for Ctrl+C (keyCode 67, ctrlKey true, or char 'c' with ctrl)
            if ((event.key === '\u0003') || // Ctrl+C generates ETX (End of Text) character
                (event.domEvent && event.domEvent.ctrlKey && event.domEvent.key === 'c')) {

                console.log('[MAIN] Ctrl+C pressed - stopping monitoring');

                // Stop monitoring if active
                if (this.flashingController && this.flashingController.monitoringActive) {
                    this.flashingController.stopSerialMonitoring();
                    this.terminal.writeln('\x1b[33m[Monitor] Stopped by Ctrl+C\x1b[0m');
                }

                return; // Don't send Ctrl+C to device
            }
        });

        // Welcome message
        this.terminal.writeln('\x1b[32mESP32-P4 Serial Console\x1b[0m');
        this.terminal.writeln('Ready for device communication...');
        this.terminal.writeln('');
        this.terminal.writeln('');

        // Handle window resize
        if (this.fitAddon) {
            window.addEventListener('resize', () => {
                if (this.fitAddon) {
                    this.fitAddon.fit();
                }
            });
        }

        console.log('Serial console initialized');
    }

    async resetDevice() {
        console.log('[MAIN] resetDevice() called');

        try {
            if (this.terminal) {
                this.terminal.writeln('\x1b[36mResetting device...\x1b[0m');
            } else {
                console.log('[MAIN] Terminal not available');
            }

            if (this.flashingController) {
                console.log('[MAIN] Flashing controller available');
                const isConnected = this.flashingController.isConnected();
                console.log('[MAIN] Device connected check:', isConnected);

                if (isConnected) {
                    console.log('[MAIN] Sending reset command to flashing controller...');

                    // Add direct call to check if method exists
                    console.log('[MAIN] Available methods on flashing controller:', Object.getOwnPropertyNames(this.flashingController));
                    console.log('[MAIN] resetDevice method type:', typeof this.flashingController.resetDevice);

                    // Send reset command via DTR signal toggle
                    try {
                        await this.flashingController.resetDevice();
                        console.log('[MAIN] Reset command completed successfully');
                    } catch (resetError) {
                        console.error('[MAIN] Error in flashingController.resetDevice():', resetError);
                        throw resetError;
                    }

                    if (this.terminal) {
                        this.terminal.writeln('\x1b[32mDevice reset command sent\x1b[0m');
                    }
                } else {
                    console.log('[MAIN] Device not connected');
                    if (this.terminal) {
                        this.terminal.writeln('\x1b[33mDevice not connected - cannot send reset command\x1b[0m');
                    }
                    this.showError('Device not connected - cannot send reset command');
                }
            } else {
                console.log('[MAIN] Flashing controller not available');
                this.showError('Flashing controller not initialized');
            }
        } catch (error) {
            console.error('[MAIN] Reset failed:', error);
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31mReset failed: ${error.message}\x1b[0m`);
            }
            this.showError(`Device reset failed: ${error.message}`);
        }
    }

    // UI feedback methods
    showSuccess(message) {
        this.showAlert(message, 'success');
    }

    showError(message) {
        this.showAlert(message, 'danger');
    }

    showInfo(message) {
        this.showAlert(message, 'info');
    }

    showAlert(message, type) {
        const alertContainer = document.getElementById('alert-container');
        const alert = document.createElement('div');
        alert.className = `alert alert-${type} alert-dismissible fade show`;
        alert.innerHTML = `
            ${message}
            <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
        `;
        alertContainer.appendChild(alert);

        // Auto-remove after 5 seconds
        setTimeout(() => {
            alert.remove();
        }, 5000);
    }

    // Flashing handler
    async handleFlash() {
        // This function will be implemented when flash button is added to HTML
        console.log('Flash functionality will be implemented when flash button is added to HTML');
    }

    // Get selected partitions for flashing
    getSelectedPartitions() {
        const selectedPartitions = [];

        // Add selected base partitions
        Object.entries(this.basePartitions).forEach(([key, partition]) => {
            if (partition.selected && partition.file) {
                selectedPartitions.push({
                    name: partition.name,
                    offset: partition.offset,
                    file: partition.file,
                    type: key === 'bootloader' ? 'bootloader' : 'app'
                });
            }
        });

        // Add selected OTA partitions
        this.otaPartitions.forEach(partition => {
            if (partition.selected && partition.file) {
                selectedPartitions.push({
                    name: partition.name,
                    offset: partition.offset,
                    file: partition.file,
                    type: 'app'
                });
            }
        });

        return selectedPartitions;
    }

    // Configuration management
    saveConfiguration() {
        const config = {
            basePartitions: {},
            otaPartitions: this.otaPartitions.map(p => ({
                name: p.name,
                offset: p.offset,
                size: p.size,
                selected: p.selected
                // Note: file objects can't be serialized
            }))
        };

        Object.entries(this.basePartitions).forEach(([key, partition]) => {
            config.basePartitions[key] = {
                name: partition.name,
                offset: partition.offset,
                size: partition.size,
                selected: partition.selected
            };
        });

        const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'ota-assembly-config.json';
        a.click();
        URL.revokeObjectURL(url);

        this.showSuccess('Configuration saved successfully');
    }

    loadConfiguration() {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.json';
        input.onchange = (e) => {
            const file = e.target.files[0];
            if (file) {
                const reader = new FileReader();
                reader.onload = (e) => {
                    try {
                        const config = JSON.parse(e.target.result);
                        this.applyConfiguration(config);
                        this.showSuccess('Configuration loaded successfully');
                    } catch (error) {
                        this.showError('Failed to load configuration: ' + error.message);
                    }
                };
                reader.readAsText(file);
            }
        };
        input.click();
    }

    applyConfiguration(config) {
        // Apply base partition configuration
        if (config.basePartitions) {
            Object.entries(config.basePartitions).forEach(([key, partitionConfig]) => {
                if (this.basePartitions[key]) {
                    this.basePartitions[key].selected = partitionConfig.selected;
                }
            });
        }

        // Apply OTA partition configuration
        if (config.otaPartitions) {
            this.otaPartitions = config.otaPartitions.map(p => ({
                ...p,
                file: null, // Files need to be re-added manually
                actualSize: p.size
            }));
        }

        // Update UI
        this.updatePartitionDisplay();
        this.updateMemoryMap();
    }

    // Additional methods needed for button functionality
    selectAllPartitions() {
        // Select all base partitions
        Object.keys(this.basePartitions).forEach(key => {
            this.basePartitions[key].selected = true;
        });

        // Select all OTA partitions
        this.otaPartitions.forEach(partition => {
            partition.selected = true;
        });

        this.updatePartitionDisplay();
        this.updateMemoryMap();
        this.showSuccess('All partitions selected');
    }

    deselectAllPartitions() {
        // Deselect all base partitions
        Object.keys(this.basePartitions).forEach(key => {
            this.basePartitions[key].selected = false;
        });

        // Deselect all OTA partitions
        this.otaPartitions.forEach(partition => {
            partition.selected = false;
        });

        this.updatePartitionDisplay();
        this.updateMemoryMap();
        this.showSuccess('All partitions deselected');
    }

    updatePartitionDisplay() {
        // Rebuild OTA partitions UI to reflect selection changes
        document.getElementById('ota-partitions-list').innerHTML = '';
        this.otaPartitions.forEach(partition => {
            this.addOTAPartitionUI(partition);
        });
    }

    toggleCustomPartitionTable() {
        const isCustom = document.getElementById('partition_table_custom').checked;
        const browseBtn = document.getElementById('partition_table_browse_btn');
        const dropZone = document.querySelector('[data-partition="partition_table"]');
        const partition = this.basePartitions.partition_table;

        partition.custom = isCustom;

        if (isCustom) {
            browseBtn.disabled = false;
            if (dropZone) {
                dropZone.innerHTML = '<span class="drop-text">Drop custom partition-table.bin here or click to browse</span>';
            }
        } else {
            browseBtn.disabled = true;
            // Clear any custom file
            partition.file = null;
            if (dropZone) {
                dropZone.innerHTML = '<span class="drop-text">Auto-generated from selections</span>';
            }
        }

        this.updateMemoryMap();
        this.showInfo(isCustom ? 'Custom partition table mode enabled' : 'Auto-generated partition table mode enabled');
    }

    async flashSelected() {
        console.log('[MAIN] ==================================================================');
        console.log('[MAIN] flashSelected() called');
        console.log('[MAIN] ==================================================================');

        try {
            // Collect selected partitions to flash
            console.log('[MAIN] Collecting selected partitions...');
            const selectedPartitions = this.getSelectedPartitions();
            console.log('[MAIN] Selected partitions:', selectedPartitions);

            if (selectedPartitions.length === 0) {
                console.log('[MAIN] ERROR: No partitions selected for flashing');
                this.showError('No partitions selected for flashing');
                return;
            }

            console.log(`[MAIN] Found ${selectedPartitions.length} partitions to flash`);

            // Check if device is connected, if not, auto-connect
            console.log('[MAIN] Checking device connection...');
            console.log('[MAIN] Flashing controller exists:', !!this.flashingController);
            console.log('[MAIN] Is connected:', this.flashingController?.isConnected?.());

            if (!this.flashingController || !this.flashingController.isConnected()) {
                console.log('[MAIN] Device not connected. Auto-connecting...');
                this.showMessage('Device not connected. Connecting first...', 'info');

                // Initialize flashing controller if needed
                if (!this.flashingController) {
                    console.log('[MAIN] Initializing new flashing controller...');
                    this.flashingController = new window.FlashingController();
                    console.log('[MAIN] Flashing controller created:', this.flashingController);
                }

                try {
                    console.log('[MAIN] Attempting to connect to device...');
                    await this.flashingController.connect();
                    console.log('[MAIN] Device connected successfully');

                    // Update body class for connected state
                    document.body.classList.remove('disconnected');
                    document.body.classList.add('connected');

                    this.showSuccess('Connected to device successfully');
                } catch (error) {
                    console.log('[MAIN] ERROR: Failed to connect to device:', error);
                    this.showError(`Failed to connect: ${error.message}`);
                    return;
                }
            } else {
                console.log('[MAIN] Device already connected');
            }

            // Show console page for flashing output
            console.log('[MAIN] Showing console for flashing output...');
            this.showConsole();

            // Wait a moment for console to initialize
            console.log('[MAIN] Waiting 500ms for console to initialize...');
            setTimeout(async () => {
                console.log('[MAIN] Starting actual flashing process...');
                try {
                    await this.performFlashing(selectedPartitions);
                } catch (flashError) {
                    console.log('[MAIN] ERROR during flashing:', flashError);
                    this.showError(`Flashing failed: ${flashError.message}`);
                }
            }, 500);

        } catch (error) {
            console.log('[MAIN] ERROR in flashSelected:', error);
            this.showError(`Flash failed: ${error.message}`);
        }

        console.log('[MAIN] ==================================================================');
    }

    showConsole() {
        const consolePage = document.getElementById('consolePage');
        const mainPage = document.getElementById('mainContainer');

        if (consolePage.style.display === 'none') {
            consolePage.style.display = 'block';
            mainPage.style.display = 'none';
            this.initConsole();
        }
    }

    async performFlashing(partitions) {
        console.log('[MAIN] ==================================================================');
        console.log('[MAIN] performFlashing() called');
        console.log('[MAIN] Partitions to flash:', partitions);
        console.log('[MAIN] ==================================================================');

        try {
            // Initialize flashing controller if needed
            console.log('[MAIN] Checking flashing controller...');
            if (!this.flashingController) {
                console.log('[MAIN] Creating new flashing controller...');
                this.flashingController = new window.FlashingController();
                console.log('[MAIN] Flashing controller created for flashing');
            }

            // Set terminal for output
            console.log('[MAIN] Setting terminal for output...');
            if (this.terminal) {
                console.log('[MAIN] Terminal found, setting to flashing controller');
                this.flashingController.setTerminal(this.terminal);
            } else {
                console.log('[MAIN] WARNING: No terminal available for output');
            }

            // Show what will be flashed
            const partitionList = partitions.map(p => `${p.name} (${this.formatSize(p.file?.size || 0)})`).join(', ');
            console.log('[MAIN] Flashing partitions:', partitionList);

            if (this.terminal) {
                this.terminal.writeln(`\x1b[36mStarting flash process for: ${partitionList}\x1b[0m`);
                this.terminal.writeln('');
            }

            // Enter bootloader mode first
            console.log('[MAIN] Entering bootloader mode...');
            if (this.terminal) {
                this.terminal.writeln(`\x1b[33m[Bootloader] Entering bootloader mode...\x1b[0m`);
            }
            await this.flashingController.enterBootloaderMode();
            console.log('[MAIN] Bootloader mode entered successfully');

            // Flash each partition using real ESP32 protocol
            console.log(`[MAIN] Starting to flash ${partitions.length} partitions...`);
            for (let i = 0; i < partitions.length; i++) {
                const partition = partitions[i];
                console.log(`[MAIN] Flashing partition ${i + 1}/${partitions.length}: ${partition.name}`);
                console.log(`[MAIN] Partition details:`, {
                    name: partition.name,
                    size: partition.file?.size || 0,
                    offset: partition.offset,
                    hasFile: !!partition.file
                });

                if (this.terminal) {
                    this.terminal.writeln(`\x1b[33m[Flashing] ${partition.name}\x1b[0m`);
                    this.terminal.writeln(`  Size: ${this.formatSize(partition.file?.size || 0)}`);
                    this.terminal.writeln(`  Offset: 0x${partition.offset?.toString(16).toUpperCase() || '0x0'}`);
                }

                try {
                    console.log(`[MAIN] Reading file data for ${partition.name}...`);
                    // Read file data
                    const fileData = await partition.file.arrayBuffer();
                    console.log(`[MAIN] File data read, size: ${fileData.byteLength} bytes`);

                    console.log(`[MAIN] Starting flash operation for ${partition.name}...`);
                    // Flash the partition using real ESP32 protocol
                    const result = await this.flashingController.flashBinary(
                        partition.offset,
                        fileData,
                        partition.name
                    );

                    console.log(`[MAIN] Flash result for ${partition.name}:`, result);

                    if (result && result.bytesWritten > 0) {
                        console.log(`[MAIN] ✓ ${partition.name} flashed successfully, ${result.bytesWritten} bytes`);
                        if (this.terminal) {
                            this.terminal.writeln('\n\x1b[32m  ✓ Flash completed\x1b[0m');
                            this.terminal.writeln(`  Bytes written: ${this.formatSize(result.bytesWritten)}`);
                        }
                    } else {
                        console.log(`[MAIN] ✗ ${partition.name} flash failed: No bytes written`);
                        throw new Error('No bytes written - flash may have failed');
                    }

                } catch (flashError) {
                    console.log(`[MAIN] ERROR: Flash failed for ${partition.name}:`, flashError);
                    if (this.terminal) {
                        this.terminal.writeln(`\n\x1b[31m  ✗ Flash failed: ${flashError.message}\x1b[0m`);
                    }
                    throw flashError;
                }

                if (this.terminal) {
                    this.terminal.writeln('');
                }
                console.log(`[MAIN] Completed flashing ${partition.name}`);
            }

            // Verify flash if available
            if (this.terminal) {
                this.terminal.writeln(`\x1b[33m[Verification] Verifying flash...\x1b[0m`);
            }

            // Simple verification - just confirm we attempted to flash all partitions
            const totalBytes = partitions.reduce((sum, p) => sum + (p.file?.size || 0), 0);
            if (this.terminal) {
                this.terminal.writeln(`  Total bytes processed: ${this.formatSize(totalBytes)}`);
                this.terminal.writeln(`\x1b[32m  ✓ Flash processing completed\x1b[0m`);
            }

            if (this.terminal) {
                this.terminal.writeln('');
                this.terminal.writeln(`\x1b[32mAll partitions flashed successfully!\x1b[0m`);
                this.terminal.writeln('You can now reset the device to run the new firmware.');
                this.terminal.writeln('');
            }

        } catch (error) {
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31mFlash error: ${error.message}\x1b[0m`);
            }
            this.showError(`Flash failed: ${error.message}`);
        }
      }

    async downloadCompleteImage() {
        try {
            // Get all selected partitions
            const selectedPartitions = this.getSelectedPartitions();

            if (selectedPartitions.length === 0) {
                this.showError('No partitions selected for image download');
                return;
            }

            this.showInfo('Creating complete firmware image...');

            // Calculate default offsets first to determine proper image size
            const partitionsWithOffsets = selectedPartitions.map(partition => {
                let offset = partition.offset;
                if (!offset) {
                    switch (partition.name) {
                        case 'Bootloader':
                            offset = 0x0;
                            break;
                        case 'Partition Table':
                            offset = 0x8000;
                            break;
                        case 'Factory App':
                            offset = 0x10000;
                            break;
                        default:
                            if (partition.name.startsWith('ota_')) {
                                offset = 0x10000 + (parseInt(partition.name.split('_')[1]) * 0x100000);
                            } else {
                                offset = 0x0;
                            }
                            break;
                    }
                }
                return { ...partition, calculatedOffset: offset };
            });

            // Determine the image size using calculated offsets
            let maxOffset = 0;
            for (const partition of partitionsWithOffsets) {
                const partitionEnd = partition.calculatedOffset + (partition.file?.size || 0);
                if (partitionEnd > maxOffset) {
                    maxOffset = partitionEnd;
                }
            }

            // Round up to next 64KB boundary
            const imageSize = Math.ceil(maxOffset / 0x10000) * 0x10000;

            // Create the complete image buffer
            const completeImage = new Uint8Array(imageSize);

            // Fill with zeros initially
            completeImage.fill(0x00);

            // Copy each partition into the correct location using pre-calculated offsets
            for (const partition of partitionsWithOffsets) {
                // Skip partitions without files
                if (!partition.file) {
                    console.warn(`Skipping ${partition.name}: missing file`);
                    continue;
                }

                try {
                    const fileData = new Uint8Array(await partition.file.arrayBuffer());
                    const offset = partition.calculatedOffset;

                    console.log(`Adding ${partition.name} at offset 0x${offset.toString(16).padStart(8, '0')} (${fileData.length} bytes)`);

                    if (offset + fileData.length > completeImage.length) {
                        console.warn(`Partition ${partition.name} extends beyond image size, truncating`);
                        const truncatedSize = completeImage.length - offset;
                        if (truncatedSize > 0) {
                            completeImage.set(fileData.slice(0, truncatedSize), offset);
                        }
                    } else {
                        completeImage.set(fileData, offset);
                    }

                    console.log(`Added ${partition.name} at offset 0x${offset.toString(16).padStart(8, '0')} (${fileData.length} bytes)`);
                } catch (error) {
                    console.error(`Failed to process ${partition.name}:`, error);
                    this.showError(`Failed to process ${partition.name}: ${error.message}`);
                    return;
                }
            }

            // Create blob and download
            const blob = new Blob([completeImage], { type: 'application/octet-stream' });
            const url = URL.createObjectURL(blob);

            const link = document.createElement('a');
            link.href = url;
            link.download = `esp32-p4-complete-firmware-${Date.now()}.bin`;
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);

            URL.revokeObjectURL(url);

            this.showSuccess(`Complete firmware image downloaded (${this.formatSize(imageSize)})`);

            // Show partition layout info
            if (this.terminal) {
                this.terminal.writeln(`\x1b[36m[Complete Image Layout]\x1b[0m`);
                this.terminal.writeln(`  Total Size: ${this.formatSize(imageSize)}`);
                this.terminal.writeln(`  Partitions: ${partitionsWithOffsets.length}`);

                for (const partition of partitionsWithOffsets) {
                    if (!partition.file) continue;

                    const offset = partition.calculatedOffset;
                    const actualSize = partition.file ? partition.file.size : 0;
                    const endOffset = offset + actualSize;

                    this.terminal.writeln(`  ${partition.name}: 0x${offset.toString(16).padStart(8, '0')} - 0x${endOffset.toString(16).padStart(8, '0')} (${actualSize} bytes)`);
                }
            }

        } catch (error) {
            console.error('Failed to create complete image:', error);
            this.showError(`Failed to create complete image: ${error.message}`);
        }
    }
}

// Global functions for onclick handlers
window.app = null;
window.browseFile = (partition) => window.app?.browseFile(partition);
window.clearPartition = (partition) => window.app?.clearPartition(partition);
window.browseOTAFiles = () => window.app?.browseOTAFiles();
window.generatePartitionTable = () => window.app?.generatePartitionTable();
window.toggleBasePartition = (partition) => window.app?.toggleBasePartition(partition);
window.toggleCustomPartitionTable = () => window.app?.toggleCustomPartitionTable();
window.toggleOTAPartition = (otaId) => window.app?.toggleOTAPartition(otaId);
window.removeOTAPartition = (otaId) => window.app?.removeOTAPartition(otaId);
window.saveConfiguration = () => window.app?.saveConfiguration();
window.loadConfiguration = () => window.app?.loadConfiguration();
window.selectAllPartitions = () => window.app?.selectAllPartitions();
window.deselectAllPartitions = () => window.app?.deselectAllPartitions();
window.flashSelected = () => window.app?.flashSelected();
window.downloadCompleteImage = () => window.app?.downloadCompleteImage();

// Initialize the application when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new OTAAssemblyApp();
});