file(GLOB_RECURSE MAIN_SRCS *.c *.cpp)

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS . fonts "../../esp32-shared/include")

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(
    ${LVGL_LIB} 
    PRIVATE
        -DLV_LVGL_H_INCLUDE_SIMPLE
)
