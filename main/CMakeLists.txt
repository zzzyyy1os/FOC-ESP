idf_component_register(SRCS "oled.c" "uart_test.c" "wifi_udp.c" "hello_world_main.c"
                       PRIV_REQUIRES spi_flash driver nvs_flash esp_wifi esp_netif
                       INCLUDE_DIRS "")
