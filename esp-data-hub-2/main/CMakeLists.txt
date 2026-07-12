file(GLOB_RECURSE MAIN_SRCS *.c)

idf_component_register(SRCS ${MAIN_SRCS}
                       PRIV_REQUIRES spi_flash
                       REQUIRES esp32-shared esp_driver_uart esp_driver_gpio esp_driver_twai esp_driver_i2c esp_timer
                       INCLUDE_DIRS "." "data_analog" "data_canbus" "../managed_components")
