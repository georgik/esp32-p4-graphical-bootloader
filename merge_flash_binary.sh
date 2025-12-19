#!/bin/bash

# ESP32-P4 Flash Binary Merger using idf.py merge-bin
# Creates a complete flash binary using ESP-IDF's merge-bin command

set -e

# Configuration
OUTPUT_FILE="esp32-p4-$(date +%Y-%m-%d-%H).bin"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FLASH_SIZE="16MB"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}ESP32-P4 Flash Binary Merger${NC}"
echo -e "${BLUE}Using idf.py merge-bin${NC}"
echo -e "${BLUE}===========================${NC}"
echo

# Check if we're in the correct directory
if [ ! -f "$PROJECT_DIR/CMakeLists.txt" ]; then
    echo -e "${RED}Error: Please run this script from the ESP-IDF project directory${NC}"
    exit 1
fi

cd "$PROJECT_DIR"

echo -e "${GREEN}Step 1: Building the project...${NC}"
idf.py build

echo -e "${GREEN}Step 2: Creating merged binary...${NC}"
echo -e "${BLUE}Output file: $OUTPUT_FILE${NC}"
echo

# Create temporary directory for missing partitions
TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo -e "${BLUE}Preparing partition files...${NC}"

# Create zero-filled partitions that don't exist in build
# NVS partition (32K)
if [ ! -f "build/nvs.bin" ]; then
    dd if=/dev/zero of="$TEMP_DIR/nvs.bin" bs=1024 count=32 2>/dev/null
    echo -e "${YELLOW}Created zero-filled NVS partition (32K)${NC}"
else
    cp build/nvs.bin "$TEMP_DIR/nvs.bin"
fi

# Bootdata partition (12K) - readonly
if [ ! -f "build/bootdata.bin" ]; then
    dd if=/dev/zero of="$TEMP_DIR/bootdata.bin" bs=1024 count=12 2>/dev/null
    echo -e "${YELLOW}Created zero-filled bootdata partition (12K)${NC}"
else
    cp build/bootdata.bin "$TEMP_DIR/bootdata.bin"
fi

# Bootloader config partition (2M)
if [ ! -f "build/bootloader_config.bin" ]; then
    dd if=/dev/zero of="$TEMP_DIR/bootloader_config.bin" bs=1024 count=2048 2>/dev/null
    echo -e "${YELLOW}Created zero-filled bootloader_config partition (2M)${NC}"
else
    cp build/bootloader_config.bin "$TEMP_DIR/bootloader_config.bin"
fi

# OTA_0 partition (1280K) - this will be zeroed as requested
dd if=/dev/zero of="$TEMP_DIR/ota_0.bin" bs=1024 count=1280 2>/dev/null
echo -e "${YELLOW}Created zero-filled ota_0 partition (1280K) as requested${NC}"

echo -e "${BLUE}Running esptool merge_bin with all partitions...${NC}"

# Use esptool to create the complete flash binary with all partitions from partitions.csv
python -m esptool --chip esp32p4 merge_bin \
    -o "$OUTPUT_FILE" \
    -f raw \
    --flash_mode dio \
    --flash_freq 80m \
    --flash_size "$FLASH_SIZE" \
    0x2000 build/bootloader/bootloader.bin \
    0x10000 build/partition_table/partition-table.bin \
    0x20000 build/esp32_p4_graphical_bootloader.bin \
    0x120000 "$TEMP_DIR/nvs.bin" \
    0x128000 "$TEMP_DIR/bootdata.bin" \
    0x130000 "$TEMP_DIR/bootloader_config.bin" \
    0x330000 "$TEMP_DIR/ota_0.bin"

if [ $? -eq 0 ]; then
    # Check if the file was created
    if [ -f "$OUTPUT_FILE" ]; then
        FILE_SIZE=$(stat -c%s "$OUTPUT_FILE" 2>/dev/null || stat -f%z "$OUTPUT_FILE" 2>/dev/null)

        echo -e "${GREEN}✓ Merged binary created successfully!${NC}"
        echo -e "${GREEN}  File: $OUTPUT_FILE${NC}"
        echo -e "${GREEN}  Size: $FILE_SIZE bytes ($(($FILE_SIZE / 1024 / 1024)) MB)${NC}"

        # Create checksum
        if command -v sha256sum > /dev/null 2>&1; then
            sha256sum "$OUTPUT_FILE" > "$OUTPUT_FILE.sha256"
            echo -e "${GREEN}  Checksum: $OUTPUT_FILE.sha256${NC}"
        fi

        echo
        echo -e "${BLUE}To flash this binary from 0x0, use:${NC}"
        echo -e "${YELLOW}python -m esptool --chip esp32p4 write_flash 0x0 $OUTPUT_FILE${NC}"
        echo
        echo -e "${YELLOW}The merged binary contains:${NC}"
        echo -e "${YELLOW}• Bootloader (automatically included)${NC}"
        echo -e "${YELLOW}• Partition table (automatically included)${NC}"
        echo -e "${YELLOW}• Application firmware (automatically included)${NC}"
        echo -e "${YELLOW}• All other partitions from partition table${NC}"
        echo

    else
        echo -e "${RED}Error: Merged binary file was not created${NC}"
        exit 1
    fi
else
    echo -e "${RED}Error: Failed to create merged binary${NC}"
    exit 1
fi

echo -e "${GREEN}Done! 🎉${NC}"