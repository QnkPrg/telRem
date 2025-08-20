# ESP32 Audio Communication Project
# Main CMakeLists.txt for ESP-IDF build system
#

cmake_minimum_required(VERSION 3.5)

# Set the config directory to use the existing config BEFORE including ESP-IDF
set(SDKCONFIG "${CMAKE_CURRENT_SOURCE_DIR}/config/sdkconfig")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/config/sdkconfig.defaults.esp32s3")

# Include ESP-ADF and ESP-IDF build systems
include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set component directories for organized structure
set(EXTRA_COMPONENT_DIRS 
    "managed_components"
    "$ENV{ADF_PATH}/components"
)

# Project name and build
project(tfg_project)