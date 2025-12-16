# Custom bootloader component for ESP32-P4 Graphical Bootloader

idf_component_register(
    SRCS
        "main/bootloader_custom.c"
        "main/bootloader_custom_simple.c"
        "main/bootloader_start.c"
    INCLUDE_DIRS
        "main"
    REQUIRES
        nvs_flash
        esp_partition
        bootloader
        bootloader_support
        esp_common
)