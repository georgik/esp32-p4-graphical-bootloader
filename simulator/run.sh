#!/bin/bash
set -e

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Build first if needed
if [ ! -f "$SCRIPT_DIR/build/simulator" ]; then
    echo "Simulator not built, building first..."
    cd "$SCRIPT_DIR"
    ./build.sh
fi

echo "Starting ESP32-P4 Bootloader Simulator..."
echo ""
echo "Working directory: $SCRIPT_DIR"
echo "Firmware directory: $SCRIPT_DIR/sdcard/firmwares/"
echo ""

# Run simulator from script directory (so it can find sdcard/)
cd "$SCRIPT_DIR"
./build/simulator "$@"
