idf_component_register(
    SRCS
        "main.c"
        "lcd_ili9341.c"
        "xpt2046.c"
        "sd_monitor.c"
        "ui.c"
        "wifi_mgr.c"
        "lv_port.c"
        "net_utils.c"
        "sudoku.c"
        "game2048.c"
        "flappy.c"
        "inventory.c"
        "file_xfer.c"
        "vocabulary.c"
        "font_cn16.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_spi esp_driver_sdspi esp_driver_uart fatfs sdmmc nvs_flash esp_timer
             esp_wifi esp_netif esp_event spi_flash
             lvgl__lvgl esp_http_client mbedtls json
)

# Keep source literals deterministic across Windows/Linux hosts.
target_compile_options(${COMPONENT_LIB} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
