# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mini_ft8)
# Build SPIFFS image, but do NOT include it in default flash target (preserve on-device files)
spiffs_create_partition_image(spiffs ${CMAKE_SOURCE_DIR}/spiffs_image)
partition_table_get_partition_info(spiffs_offset "--partition-name spiffs" "offset")
add_dependencies(app spiffs_spiffs_bin)

# Automatically generate a single merged binary file (like in the release directory) after every build.
# Include SPIFFS in the merged release image without adding it to the default flash target.
add_custom_command(TARGET app POST_BUILD
    COMMAND ${PYTHON} -m esptool --chip esp32s3 merge_bin -o "MiniFT8_Merged_Auto.bin" @flash_args ${spiffs_offset} spiffs.bin
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Auto-generating merged binary MiniFT8_Merged_Auto.bin for release..."
)
