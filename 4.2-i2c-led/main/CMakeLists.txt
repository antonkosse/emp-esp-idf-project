idf_component_register(SRCS "oled_debug.c" "main.c" "oled_cmds.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio)
