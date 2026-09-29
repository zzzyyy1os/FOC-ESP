idf_component_register(SRCS "oled.c" "uart_test.c" "hello_world_main.c"
                       PRIV_REQUIRES spi_flash driver
                       INCLUDE_DIRS "")
