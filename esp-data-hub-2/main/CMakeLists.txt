file(GLOB_RECURSE MAIN_SRCS *.c)

idf_component_register(SRCS ${MAIN_SRCS}
                       PRIV_REQUIRES spi_flash
                       REQUIRES cbor esp_driver_uart esp_driver_gpio esp_driver_twai esp_driver_i2c
                       INCLUDE_DIRS "." "../managed_components" "../../esp32-shared/include")
