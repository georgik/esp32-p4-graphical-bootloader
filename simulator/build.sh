#!/bin/bash
set -e

echo "=================================="
echo "ESP32-P4 Bootloader Simulator"
echo "Building for macOS"
echo "=================================="
echo ""

# Check dependencies
echo "Checking dependencies..."

if ! command -v cmake &> /dev/null; then
    echo "❌ CMake not found. Install: brew install cmake"
    exit 1
fi

if ! pkg-config --exists sdl2; then
    echo "❌ SDL2 not found. Install: brew install sdl2"
    exit 1
fi

if ! pkg-config --exists json-c; then
    echo "❌ JSON-C not found. Install: brew install json-c"
    exit 1
fi

echo "✅ All dependencies found"
echo ""

# Create build directory
mkdir -p build
cd build

# Clean previous build
echo "Cleaning previous build..."
make clean 2>/dev/null || true
rm -rf CMakeCache.txt CMakeFiles

# Configure
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Build
echo ""
echo "Building..."
make -j$(sysctl -n hw.ncpu)

echo ""
echo "=================================="
echo "✅ Build complete!"
echo ""
echo "Run simulator:"
echo "  cd build"
echo "  ./simulator"
echo ""
echo "Or use: ./simulator/run.sh"
echo "=================================="
