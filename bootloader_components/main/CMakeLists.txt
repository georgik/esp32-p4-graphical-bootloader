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

# Use default linker scripts
idf_build_get_property(scripts BOOTLOADER_LINKER_SCRIPT)
target_linker_script(${COMPONENT_LIB} INTERFACE "${scripts}")