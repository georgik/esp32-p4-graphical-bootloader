#!/bin/bash

# ESP32-P4 Graphical Bootloader Package Creator
# This script creates release packages for distribution

set -e

# Configuration
TARGET="esp32p4"
BOARD="esp32_p4_function_ev_board"
VERSION=${1:-"local-$(date +%Y%m%d-%H%M%S)"}
OUTPUT_DIR="dist"

echo "=== ESP32-P4 Graphical Bootloader Package Creator ==="
echo "Version: $VERSION"
echo "Target: $TARGET"
echo "Board: $BOARD"
echo ""

# Clean previous builds
echo "Cleaning previous builds..."
idf.py fullclean

# Build the project
echo "Building project..."
idf.py build

# Create output directory
echo "Creating output directory..."
mkdir -p "$OUTPUT_DIR"

# Copy individual binary files for ESP-Launchpad compatibility
echo "Copying binary files..."
cp build/bootloader/bootloader.bin "$OUTPUT_DIR/bootloader-$VERSION-$BOARD-bootloader.bin"
cp build/partition_table/partition-table.bin "$OUTPUT_DIR/bootloader-$VERSION-$BOARD-partition-table.bin"
cp build/esp32_p4_graphical_bootloader.bin "$OUTPUT_DIR/bootloader-$VERSION-$BOARD-factory.bin"
cp build/ota_data_initial.bin "$OUTPUT_DIR/bootloader-$VERSION-$BOARD-ota-data.bin"

# Copy additional files
echo "Copying additional files..."
cp partitions.csv "$OUTPUT_DIR/"
cp README.md "$OUTPUT_DIR/" 2>/dev/null || echo "README.md not found, skipping"

# Generate flashing scripts
echo "Generating flashing scripts..."

# Unix/Linux/macOS script
cat > "$OUTPUT_DIR/flash-$BOARD.sh" << EOF
#!/bin/bash
echo "ESP32-P4 Graphical Bootloader Flasher"
echo "====================================="
echo "Version: $VERSION"
echo "Board: $BOARD"
echo ""

# Detect esptool
if command -v esptool.py &> /dev/null; then
    ESP_TOOL="esptool.py"
elif python -m esptool --help &> /dev/null; then
    ESP_TOOL="python -m esptool"
else
    echo "❌ esptool not found. Please install ESP-IDF or esptool.py"
    exit 1
fi

echo "Using: $ESP_TOOL"
echo ""

# Check if binary exists
BINARY="bootloader-$VERSION-$BOARD.bin"
if [ ! -f "\$BINARY" ]; then
    echo "❌ Binary file not found: \$BINARY"
    exit 1
fi

echo "📁 Binary: \$BINARY ($(stat -c%s "\$BINARY" 2>/dev/null || stat -f%z "\$BINARY" 2>/dev/null) bytes)"
echo ""

# Detect port (try common ports)
PORTS=("/dev/ttyUSB0" "/dev/ttyUSB1" "/dev/ttyACM0" "/dev/ttyACM1" "/dev/cu.usbserial-*" "/dev/cu.usbmodem-*")
DETECTED_PORT=""

for port in "\${PORTS[@]}"; do
    if ls \$port &> /dev/null; then
        DETECTED_PORT=\$(ls \$port 2>/dev/null | head -1)
        break
    fi
done

if [ -n "\$DETECTED_PORT" ]; then
    echo "🔌 Detected port: \$DETECTED_PORT"
    PORT_ARG="--port \$DETECTED_PORT"
else
    echo "⚠️  No port detected, you may need to specify it manually"
    PORT_ARG=""
fi

echo ""
echo "🚀 Ready to flash. Press Enter to continue or Ctrl+C to cancel..."
read -r

echo "Flashing bootloader components..."
\$ESP_TOOL --chip $TARGET \$PORT_ARG write_flash \
  0x2000 "bootloader-$VERSION-$BOARD-bootloader.bin" \
  0x10000 "bootloader-$VERSION-$BOARD-partition-table.bin" \
  0x20000 "bootloader-$VERSION-$BOARD-factory.bin" \
  0x128000 "bootloader-$VERSION-$BOARD-ota-data.bin"

if [ \$? -eq 0 ]; then
    echo ""
    echo "✅ Flashing completed successfully!"
    echo "🔄 Your ESP32-P4 will now restart and boot the graphical bootloader"
    echo ""
    echo "🌐 Web interface: https://georgik.github.io/esp32-p4-graphical-bootloader/"
    echo "📖 Documentation: https://github.com/georgik/esp32-p4-graphical-bootloader"
else
    echo ""
    echo "❌ Flashing failed!"
    echo "💡 Please check:"
    echo "   - Device is connected properly"
    echo "   - Device is in flash mode (hold BOOT button while pressing RESET)"
    echo "   - No other processes are using the serial port"
    exit 1
fi
EOF

chmod +x "$OUTPUT_DIR/flash-$BOARD.sh"

# Windows batch script
cat > "$OUTPUT_DIR/flash-$BOARD.bat" << EOF
@echo off
echo ESP32-P4 Graphical Bootloader Flasher
echo =====================================
echo Version: $VERSION
echo Board: $BOARD
echo.

REM Try to detect esptool
where esptool.py >nul 2>&1
if %errorlevel% == 0 (
    set ESP_TOOL=esptool.py
    goto :found
)

python -m esptool --help >nul 2>&1
if %errorlevel% == 0 (
    set ESP_TOOL=python -m esptool
    goto :found
)

echo ❌ esptool not found. Please install ESP-IDF or esptool.py
pause
exit /b 1

:found
echo Using: %ESP_TOOL%
echo.

REM Check if binary exists
set BINARY=bootloader-$VERSION-$BOARD.bin
if not exist "%BINARY%" (
    echo ❌ Binary file not found: %BINARY%
    pause
    exit /b 1
)

echo 📁 Binary: %BINARY%
echo.

echo 🚀 Ready to flash. Press any key to continue...
pause >nul

echo Flashing...
%ESP_TOOL% --chip $TARGET write_flash 0x0 "%BINARY%"

if %errorlevel% == 0 (
    echo.
    echo ✅ Flashing completed successfully!
    echo 🔄 Your ESP32-P4 will now restart and boot the graphical bootloader
    echo.
    echo 🌐 Web interface: https://georgik.github.io/esp32-p4-graphical-bootloader/
    echo 📖 Documentation: https://github.com/georgik/esp32-p4-graphical-bootloader
) else (
    echo.
    echo ❌ Flashing failed!
    echo 💡 Please check:
    echo    - Device is connected properly
    echo    - Device is in flash mode (hold BOOT button while pressing RESET)
    echo    - No other processes are using the serial port
)

echo.
pause
EOF

# Create manifest file
cat > "$OUTPUT_DIR/MANIFEST.txt" << EOF
ESP32-P4 Graphical Bootloader Package
=====================================
Version: $VERSION
Build Date: $(date)
Target: $TARGET
Board: $BOARD

Package Contents:
-----------------
- bootloader-$VERSION-$BOARD-bootloader.bin: Custom bootloader with RTC support
- bootloader-$VERSION-$BOARD-partition-table.bin: Partition table definition
- bootloader-$VERSION-$BOARD-factory.bin: Factory GUI application
- bootloader-$VERSION-$BOARD-ota-data.bin: OTA metadata
- partitions.csv: Partition table definition (for reference)
- flash-$BOARD.sh: Flashing script for Unix/Linux/macOS
- flash-$BOARD.bat: Flashing script for Windows
- MANIFEST.txt: This file

Flashing Instructions:
---------------------
1. Connect your ESP32-P4 board to your computer
2. Put the board in flash mode (hold BOOT button, press RESET)
3. Run the appropriate flashing script:
   - Unix/Linux/macOS: ./flash-$BOARD.sh
   - Windows: flash-$BOARD.bat
4. The board will automatically restart and boot the graphical bootloader

Web Flashing:
-------------
Visit https://georgik.github.io/esp32-p4-graphical-bootloader/ for web-based flashing

Technical Details:
-----------------
- Factory-first bootloader design
- RTC-based partition switching
- Dynamic partition mapping (up to 16 OTA partitions)
- JSON configuration management with SPIFFS storage
- Touch-enabled GUI with Raylib graphics

For more information, visit: https://github.com/georgik/esp32-p4-graphical-bootloader
EOF

# Create archive
echo "Creating archive..."
tar -czf "bootloader-$VERSION-$BOARD.tar.gz" -C "$OUTPUT_DIR" .

# Show package information
echo ""
echo "=== Package Information ==="
echo "Package: bootloader-$VERSION-$BOARD.tar.gz"
echo "Size: $(stat -c%s "bootloader-$VERSION-$BOARD.tar.gz" 2>/dev/null || stat -f%z "bootloader-$VERSION-$BOARD.tar.gz" 2>/dev/null) bytes"
echo "Contents:"
ls -la "$OUTPUT_DIR/"

echo ""
echo "📦 Package created successfully!"
echo ""
echo "Files created:"
echo "  - dist/bootloader-$VERSION-$BOARD.tar.gz (archive)"
echo "  - dist/bootloader-$VERSION-$BOARD-bootloader.bin (custom bootloader)"
echo "  - dist/bootloader-$VERSION-$BOARD-partition-table.bin (partition table)"
echo "  - dist/bootloader-$VERSION-$BOARD-factory.bin (factory GUI app)"
echo "  - dist/bootloader-$VERSION-$BOARD-ota-data.bin (OTA metadata)"
echo "  - dist/flash-$BOARD.sh (Unix flashing script)"
echo "  - dist/flash-$BOARD.bat (Windows flashing script)"
echo "  - dist/MANIFEST.txt (package manifest)"
echo ""
echo "🚀 Ready for ESP-Launchpad distribution!"