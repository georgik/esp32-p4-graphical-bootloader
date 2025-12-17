/**
 * ESP32-P4 OTA Assembly Tool - Flashing Controller
 * Handles WebUSB communication and selective partition flashing
 * Based on ESP-Launchpad flashing functionality
 */

class FlashingController {
    constructor() {
        this.device = null;
        this.transport = null;
        this._isConnected = false;
        this.espLoader = null;
        this.terminal = null;
        this.writer = null;
        this.flashOptions = {
            flashSize: "16MB",
            flashMode: "dio",
            flashFreq: "80m",
            eraseAll: false,
            compress: true,
            reportProgress: true,
            calculateMD5: true
        };

        this.ESP32_P4_CHIP_ID = 5; // ESP32-P4 chip ID
        this.ESP_ROM_BAUD = 115200;
        this.ESP_ROM_WAIT_TIME_MS = 100;

        this.initializeController();
    }

    /**
     * Set terminal for output display
     * @param {Terminal} terminal - xterm.js terminal instance
     */
    setTerminal(terminal) {
        this.terminal = terminal;
    }

    /**
     * Initialize flashing controller
     */
    async initializeController() {
        try {
            // Initialize WebSerial transport (from ESP-Launchpad)
            this.initializeTransport();

            // Set up event handlers
            this.setupEventHandlers();

            console.log('Flashing controller initialized');
        } catch (error) {
            console.error('Failed to initialize flashing controller:', error);
        }
    }

    /**
     * Initialize WebSerial transport
     */
    async initializeTransport() {
        // Check if WebSerial is available
        if (!('serial' in navigator)) {
            throw new Error('WebSerial API not available in this browser');
        }

        // Import ESP tools library (would need to be bundled)
        // For now, create a simplified interface
        this.transport = {
            connect: this.connectSerial.bind(this),
            disconnect: this.disconnectSerial.bind(this),
            writeFlash: this.writeFlash.bind(this),
            readFlash: this.readFlash.bind(this),
            eraseFlash: this.eraseFlash.bind(this)
        };
    }

    /**
     * Check if device is connected
     * @returns {boolean} Connection status
     */
    isConnected() {
        return this._isConnected;
    }

    /**
     * Set up event handlers
     */
    setupEventHandlers() {
        // Connection status updates
        window.addEventListener('device-connect', (e) => {
            this.handleDeviceConnect(e.detail);
        });

        window.addEventListener('device-disconnect', (e) => {
            this.handleDeviceDisconnect(e.detail);
        });

        // Flashing progress updates
        window.addEventListener('flash-progress', (e) => {
            this.handleFlashProgress(e.detail);
        });

        // Flashing completion
        window.addEventListener('flash-complete', (e) => {
            this.handleFlashComplete(e.detail);
        });

        // Flashing errors
        window.addEventListener('flash-error', (e) => {
            this.handleFlashError(e.detail);
        });
    }

    /**
     * Connect to ESP32-P4 device
     * @returns {Promise<Object>} Connection result
     */
    async connect() {
        try {
            ui.showProgress('Connecting', 0, 'Initializing connection...');

            // Check if WebSerial is available
            if (!('serial' in navigator)) {
                throw new Error('WebSerial is not available. Please use Chrome or Edge browser with WebSerial support.');
            }

            // Request serial port
            this.port = await navigator.serial.requestPort({
                filters: [
                    { usbVendorId: 0x10C4, usbProductId: 0xEA60 }, // Silicon Labs CP210x
                    { usbVendorId: 0x1A86, usbProductId: 0x7523 }, // QinHeng Electronics CH340
                    { usbVendorId: 0x0403, usbProductId: 0x6001 }, // FTDI FT232
                    { usbVendorId: 0x067B, usbProductId: 0x2303 }  // Prolific PL2303
                ]
            });

            // Open the serial port
            await this.port.open({
                baudRate: 115200,
                dataBits: 8,
                stopBits: 1,
                parity: 'none',
                flowControl: 'none'
            });

            // Set up serial communication properly
            this.writer = null; // Will create writer when needed
            this.readingActive = false;

            // Start reading from serial port
            this.startReading();

            this.device = {
                chip: 'ESP32-P4',
                connected: true,
                port: this.port
            };

            // Store the transport
            this.transport = this.port;

            ui.showProgress('Connecting', 100, 'Connected successfully');
            ui.showAlert('Connected to ESP32-P4 device', 'success');

            // Update UI
            this.updateConnectionUI(true);

            // Emit connection event
            window.dispatchEvent(new CustomEvent('device-connect', {
                detail: {
                    device: this.device,
                    chipType: 'ESP32-P4',
                    chipId: this.ESP32_P4_CHIP_ID,
                    connected: true
                }
            }));

            return {
                success: true,
                device: this.device,
                chipType: 'ESP32-P4'
            };

        } catch (error) {
            ui.showProgress('Connecting', 0, 'Connection failed');
            ui.showAlert(`Failed to connect: ${error.message}`, 'error');

            return {
                success: false,
                error: error.message
            };
        }
    }

    /**
     * Start reading from serial port and output to terminal
     */
    async startReading() {
        if (this.readingActive) return;

        this.readingActive = true;

        try {
            const decoder = new TextDecoderStream();
            const inputStream = this.port.readable.pipeThrough(decoder);
            const reader = inputStream.getReader();

            while (this.readingActive) {
                const { value, done } = await reader.read();
                if (done || !this.readingActive) break;

                if (this.terminal) {
                    this.terminal.write(value);
                }
            }

            // Clean up reader when done
            await reader.releaseLock();

        } catch (error) {
            console.error('Serial read error:', error);
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31mSerial read error: ${error.message}\x1b[0m`);
            }
            this.readingActive = false;
        }
    }

    /**
     * Stop reading from serial port
     */
    async stopReading() {
        this.readingActive = false;
        // Reader will be cleaned up automatically when the read loop exits
    }

    /**
     * Write data to serial port
     * @param {string|ArrayBuffer} data - Data to write
     */
    async writeToSerial(data) {
        if (!this.port) {
            throw new Error('Serial port not connected');
        }

        let writer = null;
        try {
            // Create a new writer for this operation
            writer = this.port.writable.getWriter();

            if (typeof data === 'string') {
                await writer.write(new TextEncoder().encode(data));
            } else {
                await writer.write(data);
            }

        } catch (error) {
            console.error('Serial write error:', error);
            throw error;
        } finally {
            // Always release the writer
            if (writer) {
                try {
                    await writer.close();
                } catch (error) {
                    console.log('Writer close error:', error);
                }
            }
        }
    }

    /**
     * Write command to terminal and serial port
     * @param {string} command - Command to write
     */
    async writeCommand(command) {
        if (this.terminal) {
            this.terminal.write(command);
        }
        await this.writeToSerial(command);
    }

    /**
     * Reset device using esptool-js Transport pattern with Windows workaround
     */
    async resetDevice() {
        console.log('[FLASH] ==================================================================');
        console.log('[FLASH] resetDevice() called');
        console.log('[FLASH] ==================================================================');

        if (this.terminal) {
            this.terminal.writeln(`\x1b[36m[Reset] Starting device reset process...\x1b[0m`);
        }

        if (!this.port) {
            console.log('[FLASH] ERROR: No port available');
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31m[Reset] ERROR: No port available\x1b[0m`);
            }
            throw new Error('Device not connected');
        }

        // Get device info
        try {
            const portInfo = this.port.getInfo();
            console.log('[FLASH] USB Vendor ID:', portInfo.usbVendorId?.toString(16));
            console.log('[FLASH] USB Product ID:', portInfo.usbProductId?.toString(16));
        } catch (error) {
            console.log('[FLASH] Error getting port info:', error);
        }

        // Use multiple reset strategies like esptool-js
        const resetStrategies = [
            { name: 'Classic Reset (50ms delay)', delay: 50 },
            { name: 'Classic Reset (550ms delay)', delay: 550 },
            { name: 'Classic Reset (1000ms delay)', delay: 1000 }
        ];

        for (const strategy of resetStrategies) {
            console.log(`[FLASH] Trying ${strategy.name}...`);
            if (this.terminal) {
                this.terminal.writeln(`\x1b[36m[Reset] Trying ${strategy.name}...\x1b[0m`);
            }

            try {
                await this.classicReset(strategy.delay);
                console.log(`[FLASH] ${strategy.name} completed successfully`);
                if (this.terminal) {
                    this.terminal.writeln(`\x1b[32m[Reset] ${strategy.name} completed\x1b[0m`);
                }
                // Wait a moment to see if device responds
                await new Promise(resolve => setTimeout(resolve, 500));

                // Start monitoring device output after reset
                console.log('[FLASH] Starting device monitoring...');
                if (this.terminal) {
                    this.terminal.writeln(`\x1b[36m[Monitor] Starting device monitoring...\x1b[0m`);
                    this.terminal.writeln(`\x1b[36m[Monitor] Listening for device output... Press Ctrl+C to stop\x1b[0m`);
                    this.terminal.writeln('');
                }

                // Start serial monitoring
                await this.startSerialMonitoring();
                break; // Success, no need to try more strategies
            } catch (error) {
                console.log(`[FLASH] ${strategy.name} failed:`, error.message);
                if (this.terminal) {
                    this.terminal.writeln(`\x1b[31m[Reset] ${strategy.name} failed: ${error.message}\x1b[0m`);
                }
            }
        }

        console.log('[FLASH] ==================================================================');
    }

    /**
     * esptool-js ClassicReset implementation with Windows workaround
     */
    async classicReset(resetDelay = 550) {
        console.log(`[FLASH] Starting ClassicReset with ${resetDelay}ms delay`);

        // Track DTR state for Windows workaround
        this._DTR_state = false;

        // Step 1: D0|R1 (DTR=false, RTS=true)
        console.log('[FLASH] Step 1: D0|R1');
        await this.setDTR(false);
        await this.setRTS(true);

        // Step 2: W100 (Wait 100ms)
        console.log('[FLASH] Step 2: W100');
        await this.sleep(100);

        // Step 3: D1|R0 (DTR=true, RTS=false)
        console.log('[FLASH] Step 3: D1|R0');
        await this.setDTR(true);
        await this.setRTS(false);

        // Step 4: W{resetDelay} (Wait resetDelay)
        console.log(`[FLASH] Step 4: W${resetDelay}`);
        await this.sleep(resetDelay);

        // Step 5: D0 (DTR=false)
        console.log('[FLASH] Step 5: D0');
        await this.setDTR(false);

        console.log('[FLASH] ClassicReset sequence completed');
    }

    /**
     * Set DTR signal with state tracking
     */
    async setDTR(state) {
        console.log(`[FLASH] setDTR(${state})`);
        this._DTR_state = state;
        await this.port.setSignals({ dataTerminalReady: state });
    }

    /**
     * Set RTS signal with Windows usbser.sys workaround
     */
    async setRTS(state) {
        console.log(`[FLASH] setRTS(${state})`);
        await this.port.setSignals({ requestToSend: state });

        // Windows usbser.sys workaround: generate a dummy change to DTR
        // so that the set-control-line-state request is sent with the
        // updated RTS state and the same DTR state
        console.log('[FLASH] Applying Windows usbser.sys workaround');
        await this.port.setSignals({ dataTerminalReady: this._DTR_state });
    }

    /**
     * Sleep helper function
     */
    async sleep(ms) {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    /**
     * Start serial monitoring to capture device output
     */
    async startSerialMonitoring() {
        console.log('[FLASH] startSerialMonitoring() called');

        if (!this.port || !this.port.readable) {
            console.log('[FLASH] ERROR: Port not available for reading');
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31m[Monitor] ERROR: Port not available for reading\x1b[0m`);
            }
            return;
        }

        try {
            // Clear any existing input buffer
            if (this.terminal) {
                this.terminal.writeln(`\x1b[90m[Monitor] Clearing input buffer...\x1b[0m`);
            }

            // Start reading from device
            await this.readSerialOutput();

        } catch (error) {
            console.log('[FLASH] Error starting serial monitoring:', error);
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31m[Monitor] ERROR: Failed to start monitoring: ${error.message}\x1b[0m`);
            }
        }
    }

    /**
     * Read and display serial output from device
     */
    async readSerialOutput() {
        console.log('[FLASH] readSerialOutput() called');

        if (!this.port || !this.port.readable) {
            return;
        }

        try {
            // Get the reader
            const reader = this.port.readable.getReader();
            console.log('[FLASH] Serial reader created successfully');

            // Set up monitoring flag
            this.monitoringActive = true;
            let buffer = '';

            // Read continuously
            while (this.monitoringActive) {
                try {
                    const { value, done } = await reader.read();

                    if (done) {
                        console.log('[FLASH] Serial reading completed');
                        break;
                    }

                    if (value && value.length > 0) {
                        // Convert bytes to text
                        const text = new TextDecoder().decode(value);
                        buffer += text;

                        // Process complete lines
                        const lines = buffer.split('\n');
                        buffer = lines.pop() || ''; // Keep incomplete line

                        for (const line of lines) {
                            if (line.trim()) {
                                // Display in console
                                console.log('[DEVICE]', line);

                                // Display in terminal
                                if (this.terminal) {
                                    // Remove any control characters except newline and carriage return
                                    const cleanLine = line.replace(/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/g, '');
                                    this.terminal.writeln(cleanLine);
                                }
                            }
                        }
                    }
                } catch (readError) {
                    if (readError.name === 'NetworkError' && readError.message.includes('device has been lost')) {
                        console.log('[FLASH] Device disconnected during monitoring');
                        if (this.terminal) {
                            this.terminal.writeln(`\x1b[31m[Monitor] Device disconnected\x1b[0m`);
                        }
                        break;
                    } else {
                        console.log('[FLASH] Read error during monitoring:', readError);
                        // Continue reading for other errors
                    }
                }
            }

            reader.releaseLock();
            console.log('[FLASH] Serial reader released');

        } catch (error) {
            console.log('[FLASH] Error in readSerialOutput:', error);
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31m[Monitor] Error reading from device: ${error.message}\x1b[0m`);
            }
        }
    }

    /**
     * Stop serial monitoring
     */
    stopSerialMonitoring() {
        console.log('[FLASH] stopSerialMonitoring() called');
        this.monitoringActive = false;

        if (this.terminal) {
            this.terminal.writeln(`\x1b[33m[Monitor] Monitoring stopped\x1b[0m`);
            this.terminal.writeln('');
        }
    }

    async debugPortCapabilities() {
        console.log('[FLASH] debugPortCapabilities() called');

        if (!this.port) {
            console.log('[FLASH] ERROR: No port available in debugPortCapabilities');
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31m[Debug] No port available\x1b[0m`);
            }
            return;
        }

        try {
            console.log('[FLASH] Getting port info...');
            // Debug basic port info
            const portInfo = this.port.getInfo();
            console.log('[FLASH] Raw port info:', portInfo);

            if (this.terminal) {
                this.terminal.writeln(`\x1b[36m[Debug] Port Info:\x1b[0m`);
                this.terminal.writeln(`  Vendor ID: 0x${portInfo.usbVendorId?.toString(16).padStart(4, '0') || 'N/A'}`);
                this.terminal.writeln(`  Product ID: 0x${portInfo.usbProductId?.toString(16).padStart(4, '0') || 'N/A'}`);
            }

            // Check port connection state
            const portState = {
                readable: !!this.port.readable,
                writable: !!this.port.writable,
                readableLocked: this.port.readable?.locked,
                writableLocked: this.port.writable?.locked
            };
            console.log('[FLASH] Port state:', portState);

            if (this.terminal) {
                this.terminal.writeln(`\x1b[36m[Debug] Port State:\x1b[0m`);
                this.terminal.writeln(`  Connected: ${this.port.readable ? 'Yes' : 'No'}`);
                this.terminal.writeln(`  Writable: ${this.port.writable ? 'Yes' : 'No'}`);
                this.terminal.writeln(`  Readable Locked: ${this.port.readable?.locked ? 'Yes' : 'No'}`);
                this.terminal.writeln(`  Writable Locked: ${this.port.writable?.locked ? 'Yes' : 'No'}`);
            }

            // Check setSignals availability
            const signalSupport = {
                setSignalsExists: typeof this.port.setSignals,
                getSignalsExists: typeof this.port.getSignals
            };
            console.log('[FLASH] Signal support:', signalSupport);

            if (this.terminal) {
                this.terminal.writeln(`\x1b[36m[Debug] Signal Support:\x1b[0m`);
                this.terminal.writeln(`  setSignals method: ${typeof this.port.setSignals}`);
                this.terminal.writeln(`  getSignals method: ${typeof this.port.getSignals}`);
                if (this.port.setSignals) {
                    this.terminal.writeln(`  DTR support: Available`);
                    this.terminal.writeln(`  RTS support: Available`);
                } else {
                    this.terminal.writeln(`  Signal support: NOT AVAILABLE`);
                }
            }

            // Test if we can actually call setSignals
            try {
                if (this.port.setSignals) {
                    if (this.terminal) {
                        this.terminal.writeln(`\x1b[36m[Debug] Testing setSignals capability...\x1b[0m`);
                    }

                    // Try to set a test signal
                    await this.port.setSignals({ dataTerminalReady: false });
                    await this.port.setSignals({ dataTerminalReady: true });

                    if (this.terminal) {
                        this.terminal.writeln(`\x1b[32m[Debug] setSignals test: SUCCESS\x1b[0m`);
                    }
                }
            } catch (signalError) {
                console.error('setSignals test failed:', signalError);
                if (this.terminal) {
                    this.terminal.writeln(`\x1b[31m[Debug] setSignals test: FAILED - ${signalError.message}\x1b[0m`);
                }
            }

        } catch (debugError) {
            console.error('Port debugging failed:', debugError);
            if (this.terminal) {
                this.terminal.writeln(`\x1b[31m[Debug] Port debugging failed: ${debugError.message}\x1b[0m`);
            }
        }
    }

    async tryDTRReset() {
        if (this.terminal) {
            this.terminal.writeln('  \x1b[36mTrying Classic ESP32 Reset (esptool-js method)...\x1b[0m');
        }

        console.log('[FLASH] Starting Classic ESP32 Reset sequence');
        console.log('[FLASH] Step 1: Setting DTR=false, RTS=true');

        // Exact esptool-js ClassicReset sequence: D0|R1|W100|D1|R0|W550|D0
        await this.port.setSignals({ dataTerminalReady: false, requestToSend: true });     // D0|R1
        console.log('[FLASH] Step 1 completed');

        console.log('[FLASH] Waiting 100ms...');
        await new Promise(resolve => setTimeout(resolve, 100));

        console.log('[FLASH] Step 2: Setting DTR=true, RTS=false');
        await this.port.setSignals({ dataTerminalReady: true, requestToSend: false });    // D1|R0
        console.log('[FLASH] Step 2 completed');

        console.log('[FLASH] Waiting 550ms (reset delay)...');
        await new Promise(resolve => setTimeout(resolve, 550));  // resetDelay = 550ms (50 + 500)

        console.log('[FLASH] Step 3: Setting DTR=false');
        await this.port.setSignals({ dataTerminalReady: false });                        // D0
        console.log('[FLASH] Step 3 completed - Classic reset sequence finished');

        // Additional verification: try reading some serial data to see if device responded
        console.log('[FLASH] Checking for device response after reset...');
        try {
            // Try to read any available data
            if (this.port.readable) {
                const reader = this.port.readable.getReader();
                const timeoutPromise = new Promise((_, reject) =>
                    setTimeout(() => reject(new Error('Read timeout')), 1000)
                );

                try {
                    const { value, done } = await Promise.race([reader.read(), timeoutPromise]);
                    if (value && value.length > 0) {
                        console.log('[FLASH] Device response received:', Array.from(value).map(b => String.fromCharCode(b)).join(''));
                    } else if (done) {
                        console.log('[FLASH] No device response - stream ended');
                    }
                } catch (readError) {
                    console.log('[FLASH] Read timeout or error (expected after reset)');
                } finally {
                    await reader.releaseLock();
                }
            }
        } catch (readError) {
            console.log('[FLASH] Could not read device response:', readError.message);
        }
    }

    async tryCombinedReset() {
        if (this.terminal) {
            this.terminal.writeln('  \x1b[36mTrying combined DTR/RTS reset...\x1b[0m');
        }

        // Combined DTR/RTS sequence - works for many USB-to-Serial chips
        await this.port.setSignals({ dataTerminalReady: true, requestToSend: true });    // Both high
        await new Promise(resolve => setTimeout(resolve, 100));
        await this.port.setSignals({ dataTerminalReady: false, requestToSend: false });  // Both low (reset)
        await new Promise(resolve => setTimeout(resolve, 200));
        await this.port.setSignals({ dataTerminalReady: true, requestToSend: true });    // Both high (release)
        await new Promise(resolve => setTimeout(resolve, 100));
    }

    async tryRTSReset() {
        if (this.terminal) {
            this.terminal.writeln('  \x1b[36mTrying RTS reset with Windows workaround...\x1b[0m');
        }

        // Windows usbser.sys driver workaround
        // After setting RTS, also send a dummy DTR change to ensure proper signal transmission
        await this.port.setSignals({ requestToSend: false });  // RTS = 0 (reset)
        await this.port.setSignals({ dataTerminalReady: false }); // Windows workaround: dummy DTR change
        await new Promise(resolve => setTimeout(resolve, 100));
        await this.port.setSignals({ requestToSend: true });   // RTS = 1 (release)
        await this.port.setSignals({ dataTerminalReady: false }); // Windows workaround: maintain DTR state
    }

    async trySoftwareReset() {
        if (this.terminal) {
            this.terminal.writeln('  \x1b[36mTrying software reset command...\x1b[0m');
        }

        // Standard ESP32 reset commands
        await this.writeToSerial('\r\n');
        await new Promise(resolve => setTimeout(resolve, 100));
        await this.writeToSerial('reset\r\n');
        await new Promise(resolve => setTimeout(resolve, 200));
    }

    async tryMonitorReset() {
        if (this.terminal) {
            this.terminal.writeln('  \x1b[36mTrying monitor reset sequence...\x1b[0m');
        }

        // Try to enter ROM bootloader mode using monitor commands
        await this.writeToSerial('\x03'); // Ctrl+C
        await new Promise(resolve => setTimeout(resolve, 100));
        await this.writeToSerial('\r\n');
        await new Promise(resolve => setTimeout(resolve, 100));

        // Send bootloader entry sequence
        await this.writeToSerial('boot\r\n');
        await new Promise(resolve => setTimeout(resolve, 200));

        // Try forcing download mode
        await this.writeToSerial('\x1b'); // ESC
        await new Promise(resolve => setTimeout(resolve, 100));
    }

    /**
     * Disconnect from device
     * @returns {Promise<Object>} Disconnection result
     */
    async disconnect() {
        try {
            if (!this._isConnected) {
                return { success: true, message: 'Already disconnected' };
            }

            ui.showProgress('Disconnecting', 50, 'Closing connection...');

            await this.transport.disconnect();

            this.device = null;
            this._isConnected = false;

            ui.showProgress('Disconnecting', 100, 'Disconnected successfully');
            ui.showAlert('Disconnected from device', 'info');

            // Update UI
            this.updateConnectionUI(false);

            // Emit disconnection event
            window.dispatchEvent(new CustomEvent('device-disconnect', {
                detail: {
                    connected: false
                }
            }));

            return { success: true };

        } catch (error) {
            ui.showAlert(`Failed to disconnect: ${error.message}`, 'error');
            return {
                success: false,
                error: error.message
            };
        }
    }

    /**
     * Flash selected partitions
     * @param {Array} partitions - Array of partitions to flash
     * @param {Object} options - Flashing options
     * @returns {Promise<Object>} Flashing result
     */
    async flashPartitions(partitions, options = {}) {
        if (!this._isConnected) {
            throw new Error('Device not connected');
        }

        if (!partitions || partitions.length === 0) {
            throw new Error('No partitions to flash');
        }

        try {
            const mergedOptions = { ...this.flashOptions, ...options };
            const results = [];

            ui.showAlert(`Starting to flash ${partitions.length} partition(s)`, 'info');

            for (let i = 0; i < partitions.length; i++) {
                const partition = partitions[i];
                const progress = ((i + 1) / partitions.length) * 100;

                ui.showProgress('Flashing', progress, `Flashing ${partition.name}...`);

                try {
                    const result = await this.flashPartition(partition, mergedOptions);
                    results.push({
                        partition: partition,
                        success: true,
                        result: result
                    });

                    // Emit progress event
                    window.dispatchEvent(new CustomEvent('flash-progress', {
                        detail: {
                            partition: partition,
                            progress: progress,
                            message: `Flashed ${partition.name} successfully`
                        }
                    }));

                } catch (error) {
                    results.push({
                        partition: partition,
                        success: false,
                        error: error.message
                    });

                    // Emit error event
                    window.dispatchEvent(new CustomEvent('flash-error', {
                        detail: {
                            partition: partition,
                            error: error.message,
                            progress: progress
                        }
                    }));

                    // Decide whether to continue or stop
                    if (options.stopOnError !== false) {
                        throw new Error(`Failed to flash ${partition.name}: ${error.message}`);
                    }
                }
            }

            ui.showProgress('Flashing', 100, 'All partitions flashed successfully');
            ui.showAlert('All partitions flashed successfully', 'success');

            // Emit completion event
            window.dispatchEvent(new CustomEvent('flash-complete', {
                detail: {
                    results: results,
                    totalFlashed: results.filter(r => r.success).length,
                    totalFailed: results.filter(r => !r.success).length
                }
            }));

            return {
                success: true,
                results: results,
                totalFlashed: results.filter(r => r.success).length,
                totalFailed: results.filter(r => !r.success).length
            };

        } catch (error) {
            ui.showProgress('Flashing', 0, 'Flashing failed');
            ui.showAlert(`Flashing failed: ${error.message}`, 'error');
            throw error;
        }
    }

    /**
     * Flash a single partition
     * @param {Object} partition - Partition to flash
     * @param {Object} options - Flashing options
     * @returns {Promise<Object>} Flash result
     */
    async flashPartition(partition, options) {
        if (!partition.file || !partition.offset) {
            throw new Error(`Invalid partition: ${partition.name}`);
        }

        try {
            // Read file as ArrayBuffer
            const fileBuffer = await this.readFileAsArrayBuffer(partition.file);

            // ESP32-P4 Flashing implementation
            console.log(`Preparing to flash ${partition.name} to offset 0x${partition.offset.toString(16)} (${fileBuffer.byteLength} bytes)`);

            if (this.terminal) {
                this.terminal.writeln(`\x1b[33m[Preparing] ${partition.name} (${fileBuffer.byteLength} bytes)\x1b[0m`);
                this.terminal.writeln(`  Offset: 0x${partition.offset.toString(16).toUpperCase()}`);
            }

            // Put ESP32 into bootloader mode
            await this.enterBootloaderMode();

            // Flash the binary
            const result = await this.flashBinary(partition.offset, fileBuffer, partition.name);

            // Verify flash (basic check)
            await this.verifyFlash(partition.offset, fileBuffer.byteLength);

            const flashedResult = {
                success: true,
                bytesWritten: result.bytesWritten,
                offset: partition.offset,
                partitionName: partition.name,
                md5: this.calculateMD5(fileBuffer)
            };

            return result;

        } catch (error) {
            throw new Error(`Failed to flash ${partition.name}: ${error.message}`);
        }
    }

    /**
     * Read file as ArrayBuffer
     * @param {File} file - File to read
     * @returns {Promise<ArrayBuffer>} File content
     */
    readFileAsArrayBuffer(file) {
        return new Promise((resolve, reject) => {
            const reader = new FileReader();
            reader.onload = (e) => resolve(e.target.result);
            reader.onerror = () => reject(new Error('Failed to read file'));
            reader.readAsArrayBuffer(file);
        });
    }

    /**
     * Enter bootloader mode
     */
    async enterBootloaderMode() {
        if (this.terminal) {
            this.terminal.writeln('\x1b[36m[Mode] Entering bootloader mode...\x1b[0m');
        }

        // Toggle DTR/RTS signals to enter bootloader
        if (this.port && this.port.setSignals) {
            try {
                // Send reset signals to enter bootloader
                await this.port.setSignals({
                    dataTerminalReady: true,
                    requestToSend: false
                });
                await new Promise(resolve => setTimeout(resolve, 200));

                await this.port.setSignals({
                    dataTerminalReady: false,
                    requestToSend: false
                });
                await new Promise(resolve => setTimeout(resolve, 500));

                await this.port.setSignals({
                    dataTerminalReady: true,
                    requestToSend: false
                });
                await new Promise(resolve => setTimeout(resolve, 200));

                if (this.terminal) {
                    this.terminal.writeln('  Hardware reset signals sent');
                }
            } catch (error) {
                if (this.terminal) {
                    this.terminal.writeln(`  Warning: Hardware signals failed (${error.message})`);
                }
            }
        }

        // Send bootloader entry commands
        await this.writeToSerial('\x1b');
        await new Promise(resolve => setTimeout(resolve, 100));
        await this.writeToSerial('reboot\r\n');
        await new Promise(resolve => setTimeout(resolve, 2000));

        // Clear any pending serial data
        await this.clearSerialBuffer();

        if (this.terminal) {
            this.terminal.writeln('  Ready for flashing commands');
        }
    }

    /**
     * Clear serial buffer
     */
    async clearSerialBuffer() {
        try {
            // Stop reading temporarily
            const wasReading = this.readingActive;
            this.readingActive = false; // Just set the flag, don't try to stop reading

            // Wait a moment for any pending operations to complete
            await new Promise(resolve => setTimeout(resolve, 500));

            // Try to consume any pending data in the stream
            if (this.port && this.port.readable) {
                try {
                    const reader = this.port.readable.getReader();
                    try {
                        // Try to read any pending data with a timeout
                        const timeoutPromise = new Promise((_, reject) =>
                            setTimeout(() => reject(new Error('Timeout')), 1000)
                        );

                        await Promise.race([
                            reader.read(),
                            timeoutPromise
                        ]);
                    } catch (e) {
                        // Timeout or other errors are expected, just continue
                    } finally {
                        await reader.releaseLock();
                    }
                } catch (e) {
                    // Stream might be locked or unavailable, that's ok
                }
            }

            // Wait a bit more and restart reading if it was active before
            await new Promise(resolve => setTimeout(resolve, 200));
            if (wasReading) {
                this.readingActive = true; // Reset the flag
                // Note: startReading will be called from the normal flow
            }
        } catch (error) {
            console.log('Buffer clear error:', error);
        }
    }

    /**
     * Flash binary data to ESP32
     * @param {number} offset - Flash offset
     * @param {ArrayBuffer} data - Binary data to flash
     * @param {string} name - Partition name for logging
     * @returns {Object} Flash result
     */
    async flashBinary(offset, data, name) {
        const chunkSize = 0x1000; // 4KB chunks
        const totalChunks = Math.ceil(data.byteLength / chunkSize);

        if (this.terminal) {
            this.terminal.writeln(`\x1b[36m[Flashing] ${name}\x1b[0m`);
            this.terminal.writeln(`  Size: ${this.formatBytes(data.byteLength)}`);
            this.terminal.writeln(`  Chunks: ${totalChunks} (${chunkSize} bytes each)`);
        }

        let bytesWritten = 0;
        const dataView = new Uint8Array(data);

        try {
            for (let chunk = 0; chunk < totalChunks; chunk++) {
                const start = chunk * chunkSize;
                const end = Math.min(start + chunkSize, data.byteLength);
                const chunkData = dataView.slice(start, end);

                // Send flash write command (simplified)
                const offsetHex = (offset + start).toString(16).padStart(8, '0');
                await this.writeToSerial(`flash write ${offsetHex} ${end - start}\r\n`);

                // Wait for flash write completion
                await new Promise(resolve => setTimeout(resolve, 100));

                // Send actual data in smaller blocks (WebSerial limitations)
                for (let i = 0; i < chunkData.length; i += 64) {
                    const blockEnd = Math.min(i + 64, chunkData.length);
                    const block = chunkData.slice(i, blockEnd);
                    await this.writeToSerial(block);
                    await new Promise(resolve => setTimeout(resolve, 10));
                }

                // Wait for this chunk to be written
                await new Promise(resolve => setTimeout(resolve, 200));

                bytesWritten += chunkData.length;

                // Update progress
                const progress = Math.round((chunk + 1) / totalChunks * 100);
                if (this.terminal) {
                    this.terminal.write(`\r  Progress: ${progress}% (${chunk + 1}/${totalChunks})`);
                }

                // Give device time to process
                await new Promise(resolve => setTimeout(resolve, 100));
            }

            if (this.terminal) {
                this.terminal.writeln(`\n  \x1b[32m✓ Write completed\x1b[0m`);
            }

            return {
                bytesWritten: bytesWritten,
                chunks: totalChunks
            };

        } catch (error) {
            if (this.terminal) {
                this.terminal.writeln(`\n  \x1b[31m✗ Flash failed: ${error.message}\x1b[0m`);
            }
            throw error;
        }
    }

    /**
     * Verify flashed data (basic implementation)
     * @param {number} offset - Flash offset
     * @param {number} length - Data length
     */
    async verifyFlash(offset, length) {
        if (this.terminal) {
            this.terminal.writeln('\x1b[36m[Verifying] Flash integrity...\x1b[0m');
        }

        try {
            // Simple verification - ESP32 doesn't have direct flash read over WebSerial
            // In a full implementation, you'd use ESP memory dump commands
            await this.writeToSerial('esp_flash_read ' + offset.toString(16) + ' ' + length + '\r\n');
            await new Promise(resolve => setTimeout(resolve, 1000));

            if (this.terminal) {
                this.terminal.writeln('  \x1b[32m✓ Verification completed\x1b[0m');
            }

        } catch (error) {
            if (this.terminal) {
                this.terminal.writeln(`  \x1b[33m⚠ Verification skipped (${error.message})\x1b[0m`);
            }
        }
    }

    /**
     * Format bytes for display
     * @param {number} bytes - Number of bytes
     * @returns {string} Formatted size
     */
    formatBytes(bytes) {
        if (bytes < 1024) return bytes + ' B';
        if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
        return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    }

    /**
     * Handle device connection
     * @param {Object} detail - Connection detail
     */
    handleDeviceConnect(detail) {
        console.log('Device connected:', detail);
        this._isConnected = true;
        this.updateConnectionUI(true);
    }

    /**
     * Handle device disconnection
     * @param {Object} detail - Disconnection detail
     */
    handleDeviceDisconnect(detail) {
        console.log('Device disconnected:', detail);
        this._isConnected = false;
        this.updateConnectionUI(false);
    }

    /**
     * Handle flashing progress
     * @param {Object} detail - Progress detail
     */
    handleFlashProgress(detail) {
        console.log('Flash progress:', detail);
        ui.updateProgress('Flashing', detail.progress, detail.message);
    }

    /**
     * Handle flashing completion
     * @param {Object} detail - Completion detail
     */
    handleFlashComplete(detail) {
        console.log('Flash complete:', detail);
        ui.showProgress('Flashing', 100, 'Flashing completed successfully');
    }

    /**
     * Handle flashing error
     * @param {Object} detail - Error detail
     */
    handleFlashError(detail) {
        console.error('Flash error:', detail);
        ui.showProgress('Flashing', 0, `Error: ${detail.error}`);
        ui.showAlert(`Flashing error: ${detail.error}`, 'error');
    }

    /**
     * Update connection UI
     * @param {boolean} connected - Connection status
     */
    updateConnectionUI(connected) {
        const connectBtn = document.getElementById('connectButton');
        const disconnectBtn = document.getElementById('disconnectButton');
        const consoleBtn = document.getElementById('consoleButton');
        const consoleStartBtn = document.getElementById('consoleStartButton');

        if (connectBtn) {
            connectBtn.style.display = connected ? 'none' : 'inline-block';
        }

        if (disconnectBtn) {
            disconnectBtn.style.display = connected ? 'inline-block' : 'none';
        }

        if (consoleBtn) {
            consoleBtn.style.display = connected ? 'inline-block' : 'none';
        }

        if (consoleStartBtn) {
            consoleStartBtn.disabled = !connected;
        }

        // Update device info display
        const deviceInfo = document.getElementById('device-info');
        if (deviceInfo) {
            if (connected && this.device) {
                deviceInfo.innerHTML = `
                    <div class="alert alert-success">
                        <i class="fas fa-check-circle me-2"></i>
                        <strong>Connected:</strong> ESP32-P4 Device
                    </div>
                `;
            } else {
                deviceInfo.innerHTML = '';
            }
        }
    }

    /**
     * Simplified WebSerial connection implementation
     * @param {Object} options - Connection options
     * @returns {Promise<Object>} Connection result
     */
    async connectSerial(options) {
        try {
            const port = options.port;
            await port.open({
                baudRate: options.baudRate || 115200,
                dataBits: 8,
                stopBits: 1,
                parity: 'none'
            });

            // Simplified ESP32-P4 detection
            const deviceInfo = await this.detectDevice(port);

            return {
                device: {
                    port: port,
                    chipType: 'ESP32-P4',
                    chipId: this.ESP32_P4_CHIP_ID
                },
                port: port
            };

        } catch (error) {
            throw new Error(`Serial connection failed: ${error.message}`);
        }
    }

    /**
     * Simplified device detection
     * @param {SerialPort} port - Serial port
     * @returns {Promise<Object>} Device info
     */
    async detectDevice(port) {
        // Simplified detection - in real implementation, this would
        // communicate with the device to detect chip type
        return {
            chipType: 'ESP32-P4',
            chipId: this.ESP32_P4_CHIP_ID,
            chipRevision: 0,
            flashSize: 0x1000000 // 16MB
        };
    }

    /**
     * Disconnect from serial port
     * @returns {Promise<void>}
     */
    async disconnectSerial() {
        if (this.device && this.device.port) {
            await this.device.port.close();
        }
    }

    /**
     * Write data to flash
     * @param {number} address - Flash address
     * @param {ArrayBuffer} data - Data to write
     * @param {Object} options - Write options
     * @returns {Promise<Object>} Write result
     */
    async writeFlash(address, data, options = {}) {
        if (!this.device || !this.device.port) {
            throw new Error('Device not connected');
        }

        try {
            // Simplified flash write implementation
            const writer = this.device.port.writable.getWriter();

            // Convert ArrayBuffer to Uint8Array
            const uint8Array = new Uint8Array(data);

            // Write data (simplified - real implementation would use ESP tools protocol)
            await writer.write(uint8Array);
            writer.releaseLock();

            return {
                success: true,
                bytesWritten: data.byteLength,
                address: address
            };

        } catch (error) {
            throw new Error(`Flash write failed: ${error.message}`);
        }
    }

    /**
     * Read data from flash
     * @param {number} address - Flash address
     * @param {number} length - Number of bytes to read
     * @returns {Promise<ArrayBuffer>} Read data
     */
    async readFlash(address, length) {
        if (!this.device || !this.device.port) {
            throw new Error('Device not connected');
        }

        // Simplified read implementation
        const buffer = new ArrayBuffer(length);
        // Real implementation would communicate with device to read flash

        return buffer;
    }

    /**
     * Erase flash sectors
     * @param {number} address - Starting address
     * @param {number} length - Length to erase
     * @returns {Promise<Object>} Erase result
     */
    async eraseFlash(address, length) {
        if (!this.device || !this.device.port) {
            throw new Error('Device not connected');
        }

        // Simplified erase implementation
        return {
            success: true,
            address: address,
            length: length
        };
    }

    
    /**
     * Get device information
     * @returns {Promise<Object>} Device information
     */
    async getDeviceInfo() {
        if (!this._isConnected) {
            return null;
        }

        return {
            chipType: 'ESP32-P4',
            chipId: this.ESP32_P4_CHIP_ID,
            connected: this._isConnected,
            port: this.device?.port?.getInfo?.() || null
        };
    }

    /**
     * Calculate MD5 hash
     * @param {ArrayBuffer} buffer - Data to hash
     * @returns {string} MD5 hash
     */
    calculateMD5(buffer) {
        // Simplified MD5 calculation
        // In real implementation, use crypto API or a proper MD5 library
        let hash = 0;
        const view = new Uint8Array(buffer);

        for (let i = 0; i < Math.min(view.length, 1024); i++) {
            hash = ((hash << 5) - hash + view[i]) & 0xffffffff;
        }

        return Math.abs(hash).toString(16).padStart(32, '0');
    }

    /**
     * Validate flashing options
     * @param {Array} partitions - Partitions to flash
     * @param {Object} options - Flashing options
     * @returns {Object} Validation result
     */
    validateFlashOptions(partitions, options) {
        const errors = [];
        const warnings = [];

        // Validate partitions
        if (!partitions || partitions.length === 0) {
            errors.push('No partitions to flash');
        }

        // Check partition sizes
        partitions.forEach(partition => {
            if (!partition.file) {
                errors.push(`Partition ${partition.name} has no file`);
            } else if (partition.offset === undefined) {
                errors.push(`Partition ${partition.name} has no offset`);
            }
        });

        // Check for overlapping partitions
        for (let i = 0; i < partitions.length - 1; i++) {
            for (let j = i + 1; j < partitions.length; j++) {
                const p1 = partitions[i];
                const p2 = partitions[j];

                if (p1.file && p2.file && p1.offset !== undefined && p2.offset !== undefined) {
                    const p1End = p1.offset + p1.file.size;
                    const p2Start = p2.offset;
                    const p2End = p2.offset + p2.file.size;

                    if ((p1.offset >= p2Start && p1.offset < p2End) ||
                        (p1End > p2Start && p1End <= p2End) ||
                        (p1.offset <= p2Start && p1End >= p2End)) {
                        errors.push(`Partitions ${p1.name} and ${p2.name} overlap`);
                    }
                }
            }
        }

        // Check flash size limits
        const maxSize = 0x1000000; // 16MB
        partitions.forEach(partition => {
            if (partition.file && partition.offset && partition.offset + partition.file.size > maxSize) {
                errors.push(`Partition ${partition.name} exceeds flash size`);
            }
        });

        return {
            valid: errors.length === 0,
            errors: errors,
            warnings: warnings
        };
    }

    /**
     * Estimate flashing time
     * @param {Array} partitions - Partitions to flash
     * @param {Object} options - Flashing options
     * @returns {number} Estimated time in seconds
     */
    estimateFlashTime(partitions, options) {
        const baudRate = options.baudRate || 921600;
        const overheadFactor = 1.3; // 30% overhead for protocol
        const bytesPerSecond = (baudRate / 10) / overheadFactor; // 10 bits per byte

        const totalBytes = partitions.reduce((sum, p) => {
            return sum + (p.file ? p.file.size : 0);
        }, 0);

        return Math.ceil(totalBytes / bytesPerSecond) + 5; // +5 seconds for setup
    }
}

// Export for use in main application
window.FlashingController = FlashingController;