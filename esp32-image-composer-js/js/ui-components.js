/**
 * ESP32-P4 OTA Assembly Tool - UI Components
 * Handles user interface interactions, drag-and-drop, and visual feedback
 */

class UIComponents {
    constructor() {
        this.draggedFiles = [];
        this.validationResults = new Map();
        this.progressCallbacks = new Map();
        this.initializeComponents();
    }

    /**
     * Initialize UI components
     */
    initializeComponents() {
        // this.initializeDragAndDrop(); // Disabled - using main.js drag system instead
        this.initializeModals();
        this.initializeTooltips();
        this.initializeProgressIndicators();
        this.initializeAlertSystem();
    }

    /**
     * Initialize drag-and-drop functionality
     */
    initializeDragAndDrop() {
        // Global drag events
        document.addEventListener('dragenter', this.handleGlobalDragEnter.bind(this));
        document.addEventListener('dragover', this.handleGlobalDragOver.bind(this));
        document.addEventListener('dragleave', this.handleGlobalDragLeave.bind(this));
        document.addEventListener('drop', this.handleGlobalDrop.bind(this));

        // File input handlers
        this.initializeFileInputs();
    }

    /**
     * Initialize file input handlers
     */
    initializeFileInputs() {
        // Create hidden file inputs for different partition types
        const partitionTypes = ['bootloader', 'factory', 'nvs', 'otadata', 'config'];

        partitionTypes.forEach(type => {
            const input = document.getElementById(`${type}-file`);
            if (input) {
                input.addEventListener('change', (e) => {
                    this.handleFileInputChange(type, e.target.files);
                });
            }
        });

        // Create global file input for OTA files
        this.createOTAFileInput();
    }

    /**
     * Create file input for OTA files
     */
    createOTAFileInput() {
        const input = document.createElement('input');
        input.type = 'file';
        input.id = 'ota-file-input';
        input.multiple = true;
        input.accept = '.bin';
        input.style.display = 'none';
        input.addEventListener('change', (e) => {
            this.handleOTAFileInput(e.target.files);
        });
        document.body.appendChild(input);
    }

    /**
     * Handle global drag enter event
     * @param {DragEvent} e - Drag event
     */
    handleGlobalDragEnter(e) {
        e.preventDefault();
        this.draggedFiles = Array.from(e.dataTransfer.items);
        this.showGlobalDropOverlay(true);
    }

    /**
     * Handle global drag over event
     * @param {DragEvent} e - Drag event
     */
    handleGlobalDragOver(e) {
        e.preventDefault();
    }

    /**
     * Handle global drag leave event
     * @param {DragEvent} e - Drag event
     */
    handleGlobalDragLeave(e) {
        if (e.target === document.body) {
            this.draggedFiles = [];
            this.showGlobalDropOverlay(false);
        }
    }

    /**
     * Handle global drop event
     * @param {DragEvent} e - Drag event
     */
    handleGlobalDrop(e) {
        e.preventDefault();
        this.draggedFiles = [];
        this.showGlobalDropOverlay(false);

        // Find the target drop zone
        const dropZone = e.target.closest('.drop-zone');
        if (dropZone) {
            const files = Array.from(e.dataTransfer.files);
            this.handleFilesDrop(dropZone, files);
        }
    }

    /**
     * Show/hide global drop overlay
     * @param {boolean} show - Whether to show the overlay
     */
    showGlobalDropOverlay(show) {
        let overlay = document.getElementById('global-drop-overlay');

        if (show && !overlay) {
            overlay = document.createElement('div');
            overlay.id = 'global-drop-overlay';
            overlay.style.cssText = `
                position: fixed;
                top: 0;
                left: 0;
                width: 100%;
                height: 100%;
                background: rgba(0, 0, 0, 0.7);
                color: white;
                display: flex;
                align-items: center;
                justify-content: center;
                z-index: 9999;
                font-size: 24px;
                backdrop-filter: blur(5px);
            `;
            overlay.innerHTML = `
                <div style="text-align: center;">
                    <i class="fas fa-cloud-upload-alt fa-4x mb-4"></i>
                    <div>Drop files to upload</div>
                    <div style="font-size: 14px; margin-top: 10px; opacity: 0.7;">
                        Release to drop files here
                    </div>
                </div>
            `;
            document.body.appendChild(overlay);
        } else if (!show && overlay) {
            overlay.remove();
        }
    }

    /**
     * Handle file input changes
     * @param {string} partitionType - Partition type
     * @param {FileList} files - Selected files
     */
    handleFileInputChange(partitionType, files) {
        if (files.length > 0) {
            this.handleFileSelect(partitionType, files[0]);
        }
    }

    /**
     * Handle OTA file input
     * @param {FileList} files - Selected files
     */
    handleOTAFileInput(files) {
        const validFiles = Array.from(files).filter(file =>
            file.name.toLowerCase().endsWith('.bin')
        );

        if (validFiles.length > 0) {
            this.handleOTAFilesSelect(validFiles);
        } else {
            this.showAlert('No valid .bin files selected', 'warning');
        }
    }

    /**
     * Handle file selection
     * @param {string} partitionType - Partition type
     * @param {File} file - Selected file
     */
    async handleFileSelect(partitionType, file) {
        try {
            // Show loading state
            this.showPartitionLoading(partitionType, true);

            // Validate file
            const validation = await window.app.validateBinary(file, partitionType);
            this.validationResults.set(file.name, validation);

            if (validation.valid) {
                // Update UI with success
                this.updatePartitionDropZone(partitionType, file, validation);
                this.showAlert(`${partitionType} loaded successfully`, 'success');
            } else {
                // Show validation errors
                this.showValidationErrors(partitionType, validation);
                this.showAlert(`${partitionType} validation failed`, 'error');
            }

        } catch (error) {
            this.showAlert(`Error loading ${partitionType}: ${error.message}`, 'error');
        } finally {
            this.showPartitionLoading(partitionType, false);
        }
    }

    /**
     * Handle OTA files selection
     * @param {File[]} files - Selected OTA files
     */
    async handleOTAFilesSelect(files) {
        try {
            // Show loading state
            this.showOTALoading(true);

            // Initialize BinaryValidator if needed
            if (!window.app.binaryValidator) {
                window.app.binaryValidator = new window.BinaryValidator();
            }

            // Batch validate OTA files
            const results = await window.app.binaryValidator.validateBatch(files, 'ota_0');

            // Process valid files
            const validFiles = results.files.filter(f => f.result.valid);
            const invalidFiles = results.files.filter(f => !f.result.valid);

            if (validFiles.length > 0) {
                validFiles.forEach(({ file, result, index }) => {
                    this.addOTAPartition(file, index, result);
                });

                this.showAlert(`${validFiles.length} OTA files added successfully`, 'success');
            }

            if (invalidFiles.length > 0) {
                const errorMessages = invalidFiles.map(f =>
                    `${f.file.name}: ${f.result.errors.join(', ')}`
                ).join('; ');
                this.showAlert(`Some OTA files failed validation: ${errorMessages}`, 'error');
            }

            // Update statistics
            this.updateBatchValidationStats(results);

        } catch (error) {
            this.showAlert(`Error processing OTA files: ${error.message}`, 'error');
        } finally {
            this.showOTALoading(false);
        }
    }

    /**
     * Handle files drop on drop zone
     * @param {HTMLElement} dropZone - Drop zone element
     * @param {File[]} files - Dropped files
     */
    handleFilesDrop(dropZone, files) {
        const partition = dropZone.dataset.partition;
        const isOTAZone = dropZone.id === 'ota-drop-zone';

        if (isOTAZone) {
            // Handle OTA files
            const validFiles = files.filter(file =>
                file.name.toLowerCase().endsWith('.bin')
            );

            if (validFiles.length > 0) {
                this.handleOTAFilesSelect(validFiles);
            } else {
                this.showAlert('Please drop valid .bin files', 'warning');
            }
        } else if (partition) {
            // Handle single partition file
            const validFile = files.find(file =>
                file.name.toLowerCase().endsWith('.bin')
            );

            if (validFile) {
                this.handleFileSelect(partition, validFile);
            } else {
                this.showAlert('Please drop a valid .bin file', 'warning');
            }
        }

        // Update drop zone appearance
        dropZone.classList.remove('drag-over');
    }

    /**
     * Update partition drop zone UI
     * @param {string} partitionType - Partition type
     * @param {File} file - Loaded file
     * @param {Object} validation - Validation result
     */
    updatePartitionDropZone(partitionType, file, validation) {
        const dropZone = document.querySelector(`[data-partition="${partitionType}"]`);
        if (!dropZone) return;

        const sizeText = this.formatBytes(file.size);
        const utilization = validation.info.binaryDensity || 'Unknown';

        dropZone.innerHTML = `
            <div class="selected-file valid">
                <div class="file-header">
                    <i class="fas fa-file-binary me-2"></i>
                    <strong>${file.name}</strong>
                    <button class="btn btn-sm btn-outline-secondary ms-2" onclick="ui.clearPartition('${partitionType}')">
                        <i class="fas fa-times"></i>
                    </button>
                </div>
                <div class="file-details">
                    <span class="badge bg-success me-2">✓ Valid</span>
                    <span class="text-muted">${sizeText}</span>
                    ${validation.info.imageType ? `<span class="badge bg-info ms-2">${validation.info.imageType}</span>` : ''}
                </div>
                <div class="validation-summary mt-2">
                    <small class="text-muted">Density: ${utilization}</small>
                </div>
            </div>
        `;

        dropZone.classList.add('valid-drop');
        dropZone.classList.remove('invalid-drop');
    }

    /**
     * Show validation errors for a partition
     * @param {string} partitionType - Partition type
     * @param {Object} validation - Validation result
     */
    showValidationErrors(partitionType, validation) {
        const dropZone = document.querySelector(`[data-partition="${partitionType}"]`);
        if (!dropZone) return;

        dropZone.innerHTML = `
            <div class="selected-file invalid">
                <div class="file-header">
                    <i class="fas fa-exclamation-triangle me-2 text-danger"></i>
                    <strong>Validation Failed</strong>
                    <button class="btn btn-sm btn-outline-secondary ms-2" onclick="ui.clearPartition('${partitionType}')">
                        <i class="fas fa-times"></i>
                    </button>
                </div>
                <div class="validation-errors mt-2">
                    ${validation.errors.map(error =>
                        `<div class="text-danger small">• ${error}</div>`
                    ).join('')}
                </div>
                ${validation.warnings.length > 0 ? `
                <div class="validation-warnings mt-2">
                    ${validation.warnings.map(warning =>
                        `<div class="text-warning small">⚠️ ${warning}</div>`
                    ).join('')}
                </div>
                ` : ''}
            </div>
        `;

        dropZone.classList.add('invalid-drop');
        dropZone.classList.remove('valid-drop');
    }

    /**
     * Add OTA partition to UI
     * @param {File} file - OTA file
     * @param {number} index - Partition index
     * @param {Object} validation - Validation result
     */
    addOTAPartition(file, index, validation) {
        const otaName = `ota_${index}`;
        const sizeText = this.formatBytes(file.size);

        // Add to main app
        window.app.addOTAPartition(file);

        // Show toast notification
        this.showToast(`Added ${otaName}: ${file.name} (${sizeText})`, 'success');
    }

    /**
     * Show partition loading state
     * @param {string} partitionType - Partition type
     * @param {boolean} loading - Whether to show loading state
     */
    showPartitionLoading(partitionType, loading) {
        const dropZone = document.querySelector(`[data-partition="${partitionType}"]`);
        if (!dropZone) return;

        if (loading) {
            dropZone.classList.add('loading');
            dropZone.innerHTML = `
                <div class="loading-spinner">
                    <i class="fas fa-spinner fa-spin me-2"></i>
                    Validating ${partitionType}...
                </div>
            `;
        } else {
            dropZone.classList.remove('loading');
        }
    }

    /**
     * Show OTA loading state
     * @param {boolean} loading - Whether to show loading state
     */
    showOTALoading(loading) {
        const dropZone = document.getElementById('ota-drop-zone');
        if (!dropZone) return;

        if (loading) {
            dropZone.classList.add('loading');
            dropZone.innerHTML = `
                <div class="loading-spinner">
                    <i class="fas fa-spinner fa-spin fa-2x mb-3"></i>
                    <h5>Processing OTA files...</h5>
                    <p>Validating and preparing partitions</p>
                </div>
            `;
        } else {
            dropZone.classList.remove('loading');
            this.resetOTADropZone();
        }
    }

    /**
     * Reset OTA drop zone to default state
     */
    resetOTADropZone() {
        const dropZone = document.getElementById('ota-drop-zone');
        if (!dropZone) return;

        dropZone.innerHTML = `
            <div>
                <i class="fas fa-cloud-upload-alt fa-3x mb-3" style="color: #9c27b0;"></i>
                <h5>Drop OTA firmware files here</h5>
                <p class="text-muted">Supports .bin files - will be automatically arranged as ota_0, ota_1, etc.</p>
                <button class="btn btn-outline-primary mt-3" onclick="ui.browseOTAFiles()">
                    <i class="fas fa-folder-open me-2"></i>Browse Files
                </button>
            </div>
        `;
    }

    /**
     * Browse for OTA files
     */
    browseOTAFiles() {
        const input = document.getElementById('ota-file-input');
        if (input) {
            input.click();
        }
    }

    /**
     * Clear partition
     * @param {string} partitionType - Partition type to clear
     */
    clearPartition(partitionType) {
        const dropZone = document.querySelector(`[data-partition="${partitionType}"]`);
        if (!dropZone) return;

        // Reset UI
        dropZone.innerHTML = `<span class="drop-text">Drop ${partitionType}.bin here or click to browse</span>`;
        dropZone.classList.remove('valid-drop', 'invalid-drop', 'loading');

        // Clear file input
        const input = document.getElementById(`${partitionType}-file`);
        if (input) {
            input.value = '';
        }

        // Clear validation result
        const files = Array.from(this.validationResults.keys()).filter(name =>
            name.includes(partitionType)
        );
        files.forEach(name => this.validationResults.delete(name));

        // Notify main app
        if (window.app) {
            window.app.clearPartition(partitionType);
        }

        this.showAlert(`${partitionType} partition cleared`, 'info');
    }

    /**
     * Update batch validation statistics
     * @param {Object} results - Batch validation results
     */
    updateBatchValidationStats(results) {
        const statsElement = document.getElementById('validation-stats');
        if (statsElement) {
            statsElement.innerHTML = `
                <div class="row text-center">
                    <div class="col-md-3">
                        <div class="stat-item">
                            <div class="stat-value">${results.valid}</div>
                            <div class="stat-label">Valid</div>
                        </div>
                    </div>
                    <div class="col-md-3">
                        <div class="stat-item">
                            <div class="stat-value text-danger">${results.invalid}</div>
                            <div class="stat-label">Invalid</div>
                        </div>
                    </div>
                    <div class="col-md-3">
                        <div class="stat-item">
                            <div class="stat-value text-warning">${results.warnings}</div>
                            <div class="stat-label">Warnings</div>
                        </div>
                    </div>
                    <div class="col-md-3">
                        <div class="stat-item">
                            <div class="stat-value">${results.files.length}</div>
                            <div class="stat-label">Total</div>
                        </div>
                    </div>
                </div>
            `;
            statsElement.style.display = 'block';
        }
    }

    /**
     * Initialize modal components
     */
    initializeModals() {
        // Handle modal events
        document.addEventListener('shown.bs.modal', (e) => {
            this.onModalShown(e.target);
        });

        document.addEventListener('hidden.bs.modal', (e) => {
            this.onModalHidden(e.target);
        });
    }

    /**
     * Handle modal shown event
     * @param {HTMLElement} modal - Modal element
     */
    onModalShown(modal) {
        // Initialize modal-specific functionality
        if (modal.id === 'validationModal') {
            this.populateValidationModal();
        }
    }

    /**
     * Handle modal hidden event
     * @param {HTMLElement} modal - Modal element
     */
    onModalHidden(modal) {
        // Clean up modal-specific functionality
        if (modal.id === 'validationModal') {
            this.cleanupValidationModal();
        }
    }

    /**
     * Initialize tooltips
     */
    initializeTooltips() {
        // Initialize Bootstrap tooltips
        const tooltipTriggerList = [].slice.call(document.querySelectorAll('[data-bs-toggle="tooltip"]'));
        tooltipTriggerList.map(function (tooltipTriggerEl) {
            return new bootstrap.Tooltip(tooltipTriggerEl);
        });

        // Add custom tooltips for drop zones
        document.querySelectorAll('.drop-zone').forEach(zone => {
            zone.title = 'Drop .bin files here or click to browse';
        });
    }

    /**
     * Initialize progress indicators
     */
    initializeProgressIndicators() {
        // Create global progress container
        if (!document.getElementById('progress-container')) {
            const container = document.createElement('div');
            container.id = 'progress-container';
            container.className = 'progress-container';
            container.style.cssText = `
                position: fixed;
                top: 20px;
                right: 20px;
                z-index: 1000;
                max-width: 300px;
            `;
            document.body.appendChild(container);
        }
    }

    /**
     * Show progress indicator
     * @param {string} operation - Operation name
     * @param {number} progress - Progress percentage (0-100)
     * @param {string} message - Progress message
     */
    showProgress(operation, progress, message = '') {
        const container = document.getElementById('progress-container');
        if (!container) return;

        let progressElement = document.getElementById(`progress-${operation}`);

        if (!progressElement && progress < 100) {
            progressElement = document.createElement('div');
            progressElement.id = `progress-${operation}`;
            progressElement.className = 'progress-item mb-2';
            progressElement.innerHTML = `
                <div class="d-flex justify-content-between align-items-center mb-1">
                    <small>${operation}</small>
                    <small class="progress-percent">${progress}%</small>
                </div>
                <div class="progress">
                    <div class="progress-bar" role="progressbar" style="width: ${progress}%"></div>
                </div>
                ${message ? `<div class="text-muted small mt-1">${message}</div>` : ''}
            `;
            container.appendChild(progressElement);
        } else if (progressElement) {
            if (progress >= 100) {
                setTimeout(() => {
                    progressElement.remove();
                }, 2000);
            } else {
                progressElement.querySelector('.progress-bar').style.width = `${progress}%`;
                progressElement.querySelector('.progress-percent').textContent = `${progress}%`;
                const messageElement = progressElement.querySelector('.text-muted');
                if (messageElement) {
                    messageElement.textContent = message;
                } else if (message) {
                    const messageDiv = document.createElement('div');
                    messageDiv.className = 'text-muted small mt-1';
                    messageDiv.textContent = message;
                    progressElement.appendChild(messageDiv);
                }
            }
        }
    }

    /**
     * Initialize alert system
     */
    initializeAlertSystem() {
        // Create alert container if it doesn't exist
        let alertContainer = document.getElementById('floating-alerts');
        if (!alertContainer) {
            alertContainer = document.createElement('div');
            alertContainer.id = 'floating-alerts';
            alertContainer.style.cssText = `
                position: fixed;
                top: 20px;
                left: 50%;
                transform: translateX(-50%);
                z-index: 9999;
                max-width: 500px;
                width: 90%;
            `;
            document.body.appendChild(alertContainer);
        }
    }

    /**
     * Show alert message
     * @param {string} message - Alert message
     * @param {string} type - Alert type (success, error, warning, info)
     * @param {number} duration - Duration in milliseconds (0 for persistent)
     */
    showAlert(message, type = 'info', duration = 5000) {
        const alertContainer = document.getElementById('floating-alerts');
        if (!alertContainer) return;

        const alert = document.createElement('div');
        alert.className = `alert alert-${type} alert-dismissible fade show`;
        alert.style.cssText = `
            margin-bottom: 10px;
            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
        `;

        const icon = this.getAlertIcon(type);
        alert.innerHTML = `
            <div class="d-flex align-items-center">
                <i class="fas ${icon} me-2"></i>
                <span>${message}</span>
                <button type="button" class="btn-close ms-auto" data-bs-dismiss="alert"></button>
            </div>
        `;

        alertContainer.appendChild(alert);

        // Auto-remove after duration
        if (duration > 0) {
            setTimeout(() => {
                alert.remove();
            }, duration);
        }
    }

    /**
     * Get alert icon for type
     * @param {string} type - Alert type
     * @returns {string} Icon class
     */
    getAlertIcon(type) {
        const icons = {
            success: 'fa-check-circle',
            error: 'fa-exclamation-circle',
            warning: 'fa-exclamation-triangle',
            info: 'fa-info-circle'
        };
        return icons[type] || icons.info;
    }

    /**
     * Show toast notification
     * @param {string} message - Toast message
     * @param {string} type - Toast type
     */
    showToast(message, type = 'info') {
        // Simple toast implementation
        this.showAlert(message, type, 3000);
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
     * Update memory map visualization
     * @param {Array} blocks - Memory blocks to display
     */
    updateMemoryMap(blocks) {
        const container = document.getElementById('memory-map-visualization');
        if (!container) return;

        if (blocks.length === 0) {
            container.innerHTML = '<div class="text-muted text-center">No partitions loaded yet</div>';
            return;
        }

        let html = '<div class="memory-visualization">';
        blocks.forEach(block => {
            const percentage = Math.max(block.percentage, 1); // Minimum 1% for visibility
            const selectedClass = block.selected ? 'selected' : '';

            html += `
                <div class="memory-block ${block.type} ${selectedClass}"
                     style="width: ${Math.min(percentage, 100)}%;"
                     title="${block.name}: ${this.formatBytes(block.size)} (${percentage.toFixed(1)}%)">
                    <span class="block-label">${block.name}</span>
                    ${block.fileName ? `<span class="block-filename">${block.fileName}</span>` : ''}
                </div>
            `;
        });
        html += '</div>';

        container.innerHTML = html;
    }

    /**
     * Show confirmation dialog
     * @param {string} message - Confirmation message
     * @param {Function} onConfirm - Callback for confirmation
     * @param {Function} onCancel - Callback for cancellation
     */
    showConfirmation(message, onConfirm, onCancel) {
        const modal = document.createElement('div');
        modal.className = 'modal fade';
        modal.innerHTML = `
            <div class="modal-dialog">
                <div class="modal-content">
                    <div class="modal-header">
                        <h5 class="modal-title">Confirm Action</h5>
                        <button type="button" class="btn-close" data-bs-dismiss="modal"></button>
                    </div>
                    <div class="modal-body">
                        <p>${message}</p>
                    </div>
                    <div class="modal-footer">
                        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
                        <button type="button" class="btn btn-danger" id="confirm-btn">Confirm</button>
                    </div>
                </div>
            </div>
        `;

        document.body.appendChild(modal);
        const bsModal = new bootstrap.Modal(modal);

        modal.querySelector('#confirm-btn').addEventListener('click', () => {
            bsModal.hide();
            if (onConfirm) onConfirm();
        });

        modal.addEventListener('hidden.bs.modal', () => {
            modal.remove();
            if (onCancel) onCancel();
        });

        bsModal.show();
    }

    /**
     * Animate element with CSS transitions
     * @param {HTMLElement} element - Element to animate
     * @param {string} animationClass - CSS animation class
     * @param {number} duration - Animation duration in ms
     */
    animateElement(element, animationClass, duration = 300) {
        if (!element) return;

        element.classList.add(animationClass);
        setTimeout(() => {
            element.classList.remove(animationClass);
        }, duration);
    }

    /**
     * Highlight element temporarily
     * @param {HTMLElement} element - Element to highlight
     * @param {number} duration - Highlight duration in ms
     */
    highlightElement(element, duration = 1000) {
        if (!element) return;

        element.classList.add('highlighted');
        this.animateElement(element, 'pulse', duration);

        setTimeout(() => {
            element.classList.remove('highlighted');
        }, duration);
    }

    /**
     * Create loading spinner
     * @param {string} message - Loading message
     * @param {string} size - Spinner size (sm, md, lg)
     * @returns {HTMLElement} Loading element
     */
    createLoadingSpinner(message = 'Loading...', size = 'md') {
        const spinner = document.createElement('div');
        spinner.className = `loading-spinner loading-${size}`;
        spinner.innerHTML = `
            <div class="spinner-border text-primary me-2" role="status">
                <span class="visually-hidden">Loading...</span>
            </div>
            <span>${message}</span>
        `;
        return spinner;
    }
}

// Create global instance
const ui = new UIComponents();

// Export for use in main application
window.UIComponents = UIComponents;
window.ui = ui;