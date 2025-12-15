idf_component_register(
    SRCS
        "hello.c"
        "board_init.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        georgik__raylib
        espressif__esp_lcd_touch
        espressif__esp32_p4_function_ev_board_noglib
        app_update
        esp_timer
        # esp_raylib_port is pulled in via georgik__raylib dependency
)
