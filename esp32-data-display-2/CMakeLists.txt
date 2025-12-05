# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS
    ./components/bsp_extra
    )

add_compile_options(-Wno-format)
project(lvgl_demo_v9)

# Build LittleFS image from assets and flash it with the project
include(${CMAKE_CURRENT_LIST_DIR}/managed_components/joltwallet__littlefs/project_include.cmake)
littlefs_create_partition_image(storage ${CMAKE_CURRENT_LIST_DIR}/assets)
