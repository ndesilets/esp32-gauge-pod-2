cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(data_display_2)

# Build LittleFS image from assets and flash it with the project
include(${CMAKE_CURRENT_LIST_DIR}/managed_components/joltwallet__littlefs/project_include.cmake)
littlefs_create_partition_image(storage ${CMAKE_CURRENT_LIST_DIR}/assets)
