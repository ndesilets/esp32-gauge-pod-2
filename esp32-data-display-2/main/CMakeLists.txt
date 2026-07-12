file(GLOB_RECURSE MAIN_SRCS *.c *.cpp)

idf_component_register(
    SRCS ${MAIN_SRCS}
    REQUIRES
        esp32-shared
        bsp_extra
        esp_driver_jpeg
        espressif__esp_codec_dev
        nvs_flash
        waveshare__esp32_p4_wifi6_touch_lcd_4b
    INCLUDE_DIRS . fonts)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
