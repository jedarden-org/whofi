# CMakeLists.txt for CSI Positioning System Firmware
cmake_minimum_required(VERSION 3.16)

# Set the project name and version
set(PROJECT_NAME "csi_positioning_firmware")
set(PROJECT_VER "1.0.0")

# Add custom components path
set(EXTRA_COMPONENT_DIRS "components")

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set C/C++ standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Project configuration
project(${PROJECT_NAME})

# Configure build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()