# ESP32-P4 Graphical Bootloader with Multi-Firmware Support

A comprehensive 3-stage bootloader with LVGL-based GUI for ESP32-P4 Function EV Board. Features firmware flashing from SD card, RTC-based one-time boot, and multi-framework selection without modifying original applications.

Note: Configured for ESP32-P4 1.5 (ECO5). For the older revisions please use tag: v0.2.0

## Key Features

- **LVGL-based Touch GUI**: Modern touch-enabled interface for firmware selection
- **SD Card Firmware Flashing**: Load and flash `.bin` files directly from SD card
- **Firmware Storage Partition**: Custom partition (FWST magic) stores firmware metadata
- **RTC Boot Mechanism**: One-time boot via `LP_SYSTEM_REG_LP_STORE0_REG` (doesn't modify factory app)
- **Factory-First Behavior**: Always returns to GUI after reboot (no persistent OTA switching)
- **Multiple OTA Support**: Support for OTA_0, OTA_1, OTA_2, OTA_3 partitions
- **Simulator Support**: Full macOS/Linux simulator for testing without hardware

## Architecture

### Three-Stage Boot Process

1. **Stage 1**: ESP-IDF ROM Bootloader (embedded in chip)
2. **Stage 2**: ESP-IDF 2nd Stage Bootloader (reads RTC register, selects partition)
3. **Stage 3**: Graphical Bootloader with LVGL GUI (firmware selection interface)

### Boot Flow

**Default Boot (No RTC Request):**
```
ROM Bootloader → 2nd Stage Bootloader → 3rd Stage Graphical Bootloader (LVGL GUI)
```

**Firmware Selection Boot (One-Time):**
```
1. User taps firmware button in LVGL GUI
2. 3rd stage bootloader writes to RTC register: 0x00544551 | (partition_type << 24)
3. 3rd stage bootloader calls esp_restart()
4. 2nd stage bootloader reads RTC register
5. 2nd stage bootloader boots selected OTA partition
6. RTC register is cleared
7. Next reboot returns to 3rd stage graphical bootloader
```

**After Subsequent Reboot:**
```
ROM Bootloader → 2nd Stage Bootloader → 3rd Stage Graphical Bootloader (LVGL GUI)
```

## Project Structure

```
├── main/                           # 3rd stage bootloader (stored in factory_app partition)
│   ├── lvgl_bootloader.c          # LVGL GUI and boot logic
│   ├── firmware_flasher.c         # Firmware flashing engine
│   ├── firmware_selector.c         # SD card firmware selection UI
│   ├── firmware_validator.c        # Firmware integrity checking
│   ├── firmware_storage.c         # Firmware metadata storage (FWST partition)
│   ├── firmware_storage.h
│   ├── firmware_storage_config.h  # Firmware storage configuration
│   ├── partition_manager.c        # Partition table management
│   ├── board_init.c               # Display and BSP initialization
│   └── CMakeLists.txt
├── bootloader_components/          # 2nd stage bootloader components (optional)
│   └── main/
│       ├── bootloader_custom.c     # RTC-based boot request handling
│       └── bootloader_custom.h
├── simulator/                      # Simulator for desktop testing
│   ├── main.c                     # Simulator entry point
│   ├── mocks/                     # Mock implementations for ESP-IDF APIs
│   ├── platform/                  # Platform-specific code
│   └── build.sh                   # Simulator build script
├── partitions.csv                  # Custom partition table
├── sdkconfig.defaults              # Default configuration
└── README.md
```

## Partition Table

| Name | Type | SubType | Offset | Size | Description |
|------|------|---------|--------|------|-------------|
| bootloader | data | - | 0x0 | 0x20000 (128KB) | ESP-IDF bootloader |
| partition_table | data | - | 0x10000 | 0x1000 (4KB) | Partition table |
| otadata | data | - | 0x11000 | 0x2000 (8KB) | OTA data |
| bootloader_config | data | - | 0x12B000 | 0x11000 (68KB) | Bootloader config |
| **firmware_storage** | data | - | 0x13C000 | 0x4000 (16KB) | **Firmware metadata (FWST)** |
| factory_app | app | factory | 0x20000 | 0x100000 (1MB) | Factory app with GUI |
| ota_0 | app | ota_0 | 0x120000 | 0x200000 (2MB) | OTA slot 0 |
| ota_1 | app | ota_1 | 0x320000 | 0x200000 (2MB) | OTA slot 1 |
| ota_2 | app | ota_2 | 0x520000 | 0x200000 (2MB) | OTA slot 2 |
| ota_3 | app | ota_3 | 0x720000 | 0x200000 (2MB) | OTA slot 3 |
| storage | data | spi_flash | 0x920000 | 0x600000 (6MB) | Internal flash storage |
| sdcard | data | fat | 0xF20000 | 0x600000 (6MB) | SD card filesystem |

## Firmware Storage (FWST)

The firmware_storage partition at `0x13C000` stores firmware metadata using a custom format:

### Header Format
```c
typedef struct {
    char magic[4];              // 'FWST' magic
    uint32_t version;           // Version (currently 1)
    uint32_t header_size;       // Size of header
    uint32_t count;             // Number of firmware entries
} firmware_storage_header_t;
```

### Entry Format
```c
typedef struct {
    char name[64];             // Firmware display name
    uint32_t offset;           // Offset from firmware_storage base
    uint32_t size;             // Firmware size in bytes
    uint32_t crc32;            // CRC32 checksum
    uint32_t flags;            // Flags (reserved)
    uint32_t next_offset;      // Next entry offset (reserved)
} firmware_storage_entry_t;
```

## Building

### Hardware (ESP32-P4)

```bash
# Setup ESP-IDF environment
export IDF_PATH=/path/to/esp-idf
source $IDF_PATH/export.sh

# Build the project
idf.py build

# Flash to device
idf.py flash

# Monitor output
idf.py monitor
```

### Simulator (macOS/Linux)

```bash
cd simulator
./build.sh

# Run simulator with flash image
./run.sh --load-image flash-ew-qt-opentyrian-combined.bin

# Or run without flash image
./build/simulator
```

## Usage

### Flashing Firmware from SD Card

1. Copy `.bin` firmware files to `/sdcard/firmwares/` on SD card
2. Insert SD card into ESP32-P4 board
3. Tap "Load from SD Card" button in LVGL GUI
4. Select firmware(s) to flash
5. Tap "Flash" button to start flashing
6. Progress will be displayed during flashing
7. After flashing, firmware button will appear on main screen

### Booting Firmware

1. Tap firmware button on main screen
2. System writes RTC register and restarts
3. Custom bootloader reads RTC register
4. Selected firmware boots (one-time only)
5. After next reboot, returns to GUI

## RTC Boot Protocol

### Register Definition
```c
#define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG
#define BOOT_REQUEST_MAGIC_RTC   0x00544551  // 'BOOT' in ASCII
```

### Encoding Format
```
Bit 31-24: Partition type (0=Factory, 1=OTA_0, 2=OTA_1, 3=OTA_2, 4=OTA_3)
Bit 23-0:  Magic number (0x00544551)
```

### Example Usage
```c
// Boot OTA_0 partition
uint32_t partition_type = 1;
uint32_t rtc_value = BOOT_REQUEST_MAGIC_RTC | (partition_type << 24);
REG_WRITE(BOOT_REQUEST_RTC_REG, rtc_value);

// Wait and restart
vTaskDelay(pdMS_TO_TICKS(1000));
esp_restart();
```

### Partition Type Mapping
| Partition Type | Value | Partition Subtype |
|----------------|-------|-------------------|
| FACTORY | 0 | ESP_PARTITION_SUBTYPE_APP_FACTORY |
| OTA_0 | 1 | ESP_PARTITION_SUBTYPE_APP_OTA_0 |
| OTA_1 | 2 | ESP_PARTITION_SUBTYPE_APP_OTA_1 |
| OTA_2 | 3 | ESP_PARTITION_SUBTYPE_APP_OTA_2 |
| OTA_3 | 4 | ESP_PARTITION_SUBTYPE_APP_OTA_3 |

## Key Features

### 1. Firmware Storage Partition
- Custom partition format with 'FWST' magic
- Stores firmware name, offset, size, and CRC32
- Supports up to 10 firmware entries (MAX_FIRMWARE_ENTRIES)
- Flash erase before initialization (hardware requirement)

### 2. SD Card Integration
- VFFAT filesystem support
- Automatic scanning of `/sdcard/firmwares/`
- Progress tracking during flashing
- Error handling and recovery

### 3. LVGL Interface
- Touch-enabled buttons
- Progress bars for flash operations
- Status messages and error handling
- Support for 1024x600 MIPI DSI display

### 4. Watchdog-Friendly
- Task yielding during flash operations (prevents WDT timeout)
- LVGL updates with proper delays
- Safe long-running operations

## Simulator

The simulator provides a complete development environment:

### Features
- **Flash Emulator**: Memory-mapped flash simulation
- **VFS Translation**: `/sdcard` → `./sdcard/` directory mapping
- **Mock APIs**: ESP-IDF API implementations for desktop
- **LVGL Desktop**: Native SDL2-based rendering
- **CLI Tools**: Flash inspection and firmware management

### Build Requirements
- CMake 3.16+
- SDL2 development libraries
- pthread and standard C libraries

### Simulator-Specific Files
```
simulator/
├── mocks/                    # ESP-IDF API mocks
│   ├── esp_flash_mock.h
│   ├── nvs_mock.c           # NVS emulation
│   ├── firmware_storage_mock.c  # Firmware storage emulation
│   └── vfs_mock.c           # VFS path translation
├── platform/                 # Platform-specific code
│   ├── flash_emulator.c     # Flash memory emulation
│   └── display_sdl2.c       # SDL2 display driver
└── build.sh                 # Build script
```

## Troubleshooting

### Common Issues

**1. "No firmware applications found"**
- Flash firmware from SD card first
- Check SD card is mounted: Look for "SD card firmware directory found" log
- Verify firmware_storage partition: Look for "Found X firmware(s)" log

**2. Watchdog timeout during flashing**
- Check for `vTaskDelay()` calls in firmware_flasher.c
- Ensure flash operations yield every 64KB
- Look for "Flash progress: X%" logs

**3. RTC boot not working**
- Verify RTC register write: Look for "RTC register updated: 0x..." log
- Check partition_type calculation
- Ensure custom bootloader is reading RTC register

**4. Partition iterator infinite loop**
- Check for proper iterator usage: `esp_partition_next(it)`
- Verify iterator is released: `esp_partition_iterator_release(it)`
- Look for "Partition iteration completed" log

**5. Firmware storage not found**
- Check flash erase on first write: "Erasing firmware storage region"
- Verify magic number: 'FWST' (0x46545354)
- Check offset: Should be 0x13C000

### Debug Logs

**Successful Firmware Flash:**
```
I (xxx) firmware_storage: Adding firmware entry to storage: firmware.bin @ offset 0x1F4000
I (xxx) firmware_storage: Initializing new firmware storage
I (xxx) firmware_storage: Erasing firmware storage region at 0x13C000
I (xxx) firmware_storage: Writing entry 0 at offset 0x13C03C: firmware.bin (1399952 bytes, CRC32: 0xEBB6C042)
I (xxx) firmware_storage: ✓ Firmware entry added: firmware.bin (total: 1 entries)
```

**Successful Boot Button Press:**
```
I (xxx) lvgl_bootloader: Booting firmware 0: firmware.bin @ address 0x330000
I (xxx) lvgl_bootloader: Searching for partition containing address 0x330000...
I (xxx) lvgl_bootloader: Partition iterator created: 0x...
I (xxx) lvgl_bootloader: [1] Checking partition: factory_app (0x20000 - 0x120000)
I (xxx) lvgl_bootloader: [2] Checking partition: ota_0 (0x120000 - 0x320000)
I (xxx) lvgl_bootloader: Found matching partition: ota_0
I (xxx) lvgl_bootloader: Partition found successfully, continuing with RTC boot...
I (xxx) lvgl_bootloader: Booting from partition: ota_0 (subtype: 1)
I (xxx) lvgl_bootloader: RTC register updated: 0x01544551 for partition type 1 (ota_0)
I (xxx) lvgl_bootloader: System will boot from ota_0 after restart (one-time boot via RTC)
```

