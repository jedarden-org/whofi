cmake_minimum_required(VERSION 3.16)

# TDD Test Suite for CSI Firmware
project(csi_firmware_tdd_tests)

# Test configuration for ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set test-specific components
set(EXTRA_COMPONENT_DIRS 
    "${CMAKE_CURRENT_SOURCE_DIR}/../components"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Test main component
idf_component_register(
    SRCS 
        "test_csi_memory.c"
        "test_csi_functionality.c"
        "test_mqtt_integration.c"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        unity
        esp_system
        esp_partition
        freertos
        esp_wifi
        mqtt
        nvs_flash
        csi_collector
        mqtt_client
        ntp_sync
        web_server
        ota_updater
)