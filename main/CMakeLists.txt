# Main graphical bootloader application
idf_component_register(
    SRCS
        "main.c"
        "lvgl_bootloader.c"
        "board_init.c"
        "sd_ota.c"
        "firmware_selector.c"
        "firmware_validator.c"
        "partition_manager.c"
        "firmware_flasher.c"
    INCLUDE_DIRS
        "."
    PRIV_REQUIRES
        #georgik__raylib
        espressif__esp_lcd_touch
        espressif__esp32_p4_function_ev_board
        esp_partition
        esp_timer
        esp_system
        spiffs                 # For configuration storage
        app_update             # For OTA operations
        esp_app_format         # For OTA operations
        fatfs                  # For SD card filesystem
        sdmmc                  # For SD card access
        # esp_raylib_port is pulled in via georgik__raylib dependency
)
