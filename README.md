# ESP32-P4 Graphical Bootloader with RTC Boot

A 2nd + 3rd stage bootloader with touch-enabled GUI framework selection for ESP32-P4 Function EV Board.
The bootloader allows graphical selection of partition to boot using RTC mechanism.
After the next HW reboot it returns to orifinal state.
This approach does not require modification of flashed applications.

If you're lookign for simpler bootloader which utilizes OTA partition switching, check out article:
[How to switch between multiple ESP32 firmware binaries stored in the flash memory
](https://developer.espressif.com/blog/switch-between-firmware-binaries/), that approach requires modification
of each of flashed applications.


## Architecture Overview

This project implements a three-stage boot architecture with **RTC-based boot requests**:

1. **Stage 1**: ESP-IDF bootloader (embedded in ROM)
2. **Stage 2**: Custom bootloader with RTC register-based partition selection
3. **Stage 3**: Factory application with GUI framework selector

### Factory-First Boot Behavior

The system boots into the factory application (Stage 3) by default, which presents a touch-enabled interface for selecting different GUI frameworks. When a user selects a framework:

1. **Application writes** boot request to RTC register (`LP_SYSTEM_REG_LP_STORE0_REG`)
2. **System restarts** using `esp_restart()`
3. **Custom bootloader reads** RTC register for boot request
4. **Custom bootloader boots** the requested OTA partition (one-time only)
5. **Bootloader clears** the RTC register
6. **Next reboot defaults back to factory** (factory-first behavior)

## Project Structure

```
├── bootloader_components/
│   └── main/
│       ├── bootloader_start.c      # Custom bootloader entry point
│       ├── bootloader_custom.c     # RTC-based boot request handling
│       ├── bootloader_custom.h     # Bootloader API and data structures
│       └── CMakeLists.txt          # Bootloader component configuration
├── main/
│   ├── graphical_bootloader.c      # Factory app with Raylib GUI
│   ├── board_init.c                # Display and BSP initialization
│   ├── CMakeLists.txt              # Main application configuration
│   └── idf_component.yml           # Component dependencies
├── boot-knowledge.txt              # Technical documentation and storage options
├── partitions.csv                  # Custom partition table
├── sdkconfig.defaults              # Default project configuration
└── CMakeLists.txt                  # Top-level project configuration
```

## Core Files

### Bootloader Components

#### `bootloader_start.c`
Main bootloader entry point that:
- Initializes hardware and ESP-IDF bootloader subsystem
- Reads boot requests from RTC register
- Implements partition selection logic
- Handles request clearing for factory-first behavior

#### `bootloader_custom.c`
**RTC-based boot request implementation**:
- `bootloader_read_boot_request()`: Reads partition selection from RTC register
- `bootloader_clear_boot_request()`: Clears processed requests from RTC
- `bootloader_get_boot_partition()`: Maps partition types to actual partitions
- **Uses ESP32-P4 reserved RTC register** (`LP_SYSTEM_REG_LP_STORE0_REG`)

#### `bootloader_custom.h`
Defines the bootloader data structures:
- `boot_request_t`: Structure for boot requests (magic, version, partition type)
- Partition type constants (FACTORY, OTA_0, OTA_1)
- RTC register and magic number definitions

### Factory Application

#### `main/graphica_bootloader.c`
Primary factory application with:
- Raylib-based touch interface for framework selection
- **Direct RTC register access** for boot requests
- Visual feedback during boot transitions
- Error handling and restart functionality

## Boot Flow

### Normal Boot (No Request)
```
ESP-IDF ROM Bootloader → Custom Bootloader → Factory Application (GUI)
```

### OTA Selection Boot
```
1. User selects framework in GUI
2. Factory app writes to RTC register: magic | (partition_type << 24)
3. System restarts via esp_restart()
4. Custom bootloader reads RTC register
5. Custom bootloader extracts magic and partition type
6. Custom bootloader clears RTC register (writes 0)
7. Custom bootloader boots selected OTA partition
```

### Subsequent Boot (Returns to Factory)
```
ESP-IDF ROM Bootloader → Custom Bootloader → Factory Application (GUI)
```

## RTC Register Protocol

### Boot Request Format
```c
// RTC register value: 0xPPTTTTTT
// PP = Partition type (0=Factory, 1=OTA_0, 2=OTA_1)
// TTTTTT = Magic number (0x00544551 = "BOOT" in ASCII)
#define BOOT_REQUEST_MAGIC_RTC   0x00544551
#define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG

// Example: Boot OTA_0 partition
uint32_t rtc_value = BOOT_REQUEST_MAGIC_RTC | (1 << 24);  // 0x01544551
REG_WRITE(BOOT_REQUEST_RTC_REG, rtc_value);
```

### RTC Register Advantages
- **Available in bootloader context** (no component dependencies)
- **Survives across reboots** (RTC memory)
- **No flash wear** (register access)
- **Simple 32-bit interface** (easy to encode/decode)
- **Factory-first by default** (cleared after use)

## Failed Approaches (For Reference)

### NVS Storage (Multiple Attempts)
**Reason**: NVS APIs are **not available in bootloader context**
- `nvs_flash_init()`, `nvs_open()`, `nvs_get_blob()` cause linker errors
- Bootloader has limited component dependencies
- Multiple attempts with different approaches all failed

### Standard ESP-IDF OTA (`esp_ota_set_boot_partition()`)
**Reason**: `app_update` component requires `esp_system` which is **not available in bootloader**
- `esp_ota_set_boot_partition()` only works in application context
- Bootloader cannot depend on `app_update` component
- Would require complex bootloader rebuild configuration

## Partition Layout

The custom partition table supports factory-first boot behavior:

```
Offset      Size    Type      Purpose
0x00000     64KB    bootloader Custom bootloader
0x10000     4KB     partition Partition table
0x20000     1MB     factory  Factory app (GUI selector) - DEFAULT
0x120000    1MB     ota_0    OTA slot 0 (one-time boot)
0x220000    1MB     ota_1    OTA slot 1 (one-time boot)
0x320000    32KB    nvs      Non-volatile storage
0x328000    8KB     otadata  OTA metadata (unused by our system)
0x32a000    1MB     spiffs   File system
```

## Build Configuration

### Key Settings (`sdkconfig.defaults`)
- `CONFIG_BOOTLOADER_SIZE_IN_KB=32`: Increased bootloader size for custom logic
- `CONFIG_PARTITION_TABLE_OFFSET=0x10000`: Adjusted for 32KB bootloader
- `CONFIG_PARTITION_TABLE_CUSTOM=y`: Custom partition table usage
- ESP32-P4 target configuration (`CONFIG_IDF_TARGET=esp32p4`)

### Bootloader Component Dependencies
```cmake
idf_component_register(
    SRCS
        "bootloader_start.c"
        "bootloader_custom.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        bootloader
        bootloader_support
        esp_partition
)
```

### Application Component Dependencies
```cmake
idf_component_register(
    SRCS "graphical_bootloader.c"
    REQUIRES
        esp_lcd_touch
        esp_timer
        esp_system
        nvs_flash
    PRIV_REQUIRES
        esp_raylib_port
)
```

## Usage

### Building
```bash
idf.py build
```

### Flashing
```bash
idf.py flash
```

### Monitoring
```bash
idf.py monitor
```

## Boot Request API

### Direct RTC Register Access
Applications can request specific boot partitions directly:

```c
#include "soc/lp_system_reg.h"

// RTC register constants
#define BOOT_REQUEST_RTC_REG     LP_SYSTEM_REG_LP_STORE0_REG
#define BOOT_REQUEST_MAGIC_RTC   0x00544551  // 'BOOT' magic

// Request boot from OTA_0 on next restart
uint32_t partition_type = 1;  // 0=Factory, 1=OTA_0, 2=OTA_1
uint32_t rtc_value = BOOT_REQUEST_MAGIC_RTC | (partition_type << 24);
REG_WRITE(BOOT_REQUEST_RTC_REG, rtc_value);

// Restart to trigger bootloader
esp_restart();
```

### Partition Type Mapping
```c
// Partition types for RTC register encoding
#define BOOT_PARTITION_FACTORY  0
#define BOOT_PARTITION_OTA_0    1
#define BOOT_PARTITION_OTA_1    2
```

## Error Handling

### Bootloader Error Recovery
- **Invalid RTC magic value** → Boots factory partition
- **Invalid partition type** → Defaults to factory partition
- **No RTC request present** → Boots factory partition
- **Corrupted partition data** → Falls back to factory partition

### Application Error Handling
- **RTC register access failures** → System logs error, continues
- **Invalid partition indices** → Shows error state, doesn't restart
- **Hardware failures** → Graceful degradation to factory boot

## Debugging

### Bootloader Logs
Monitor for these key messages:
```
I (xxx) bootloader_custom: === Custom Bootloader Active (RTC-based) ===
I (xxx) bootloader_custom: RTC store register value: 0x01544551
I (xxx) bootloader_custom: RTC boot request found: magic=0x00544551, partition_type=1
I (xxx) bootloader_custom: Boot request cleared - clearing RTC register
```

### Application Logs
Monitor for boot request creation:
```
I (xxx) RaylibDemo: Writing boot request to RTC register: magic=0x00544551, partition_type=1
I (xxx) RaylibDemo: RTC register updated successfully, value: 0x01544551
I (xxx) RaylibDemo: Restarting now for bootloader to handle the boot request...
```

### Common Issues
1. **No boot request detected**: Check RTC register write format
2. **Wrong partition boots**: Verify partition type mapping
3. **Factory doesn't load**: Check partition table integrity
4. **Boot loop**: RTC register not being cleared properly

## Technical Documentation

See `boot-knowledge.txt` for detailed technical information about:
- Available storage options in bootloader context
- RTC register specifications for ESP32-P4
- Bootloader flash API alternatives
- Implementation decisions and trade-offs

## Integration Notes

### Adding New OTA Applications
1. Flash application to appropriate OTA partition (`ota_0` or `ota_1`)
2. Update GUI framework mapping in `graphical_bootloader.c` if needed
3. Partition type mapping is handled in RTC bootloader logic
4. No changes needed to bootloader for new OTA apps

### Extending Boot Request System
The RTC register protocol can be extended:
- Use additional reserved RTC registers for more data
- Modify encoding format for complex boot requests
- Add additional validation magic numbers
- Implement request queuing if needed

