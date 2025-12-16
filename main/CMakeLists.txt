# Restore original graphical bootloader application
idf_component_register(
    SRCS
        "graphical_bootloader.c"
        "board_init.c"
        "../bootloader_api.c"
    INCLUDE_DIRS
        "."
        ".."
    PRIV_REQUIRES
        georgik__raylib
        espressif__esp_lcd_touch
        espressif__esp32_p4_function_ev_board_noglib
        app_update
        esp_timer
        nvs_flash
        # esp_raylib_port is pulled in via georgik__raylib dependency
)
