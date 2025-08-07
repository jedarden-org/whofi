/**
 * @file main.c
 * @brief Main entry point for CSI Positioning System firmware
 * 
 * This file contains the main application logic that coordinates all system components
 * including CSI data collection, web server, MQTT client, NTP sync, and OTA updates.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>

#include "app_config.h"
#include "system_init.h"
#include "csi_collector.h"
#include "web_server.h"
#include "mqtt_client_wrapper.h"
#include "ntp_sync.h"
#include "ota_updater.h"

static const char *TAG = "MAIN";

/**
 * @brief Main application task that coordinates all system components
 * @param pvParameters Task parameters (unused)
 */
static void app_main_task(void *pvParameters)
{
    // Initialize system components
    if (system_init() != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed");
        vTaskDelete(NULL);
        return;
    }
    
    // Load application configuration
    app_config_t config;
    if (app_config_load(&config) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config, using defaults");
        app_config_set_defaults(&config);
    }
    
    ESP_LOGI(TAG, "Starting CSI Positioning System v%s", config.firmware_version);
    
    // Start web configuration server
    web_server_config_t web_config = {
        .enabled = config.web_server.enabled,
        .port = config.web_server.port,
        .auth_enabled = config.web_server.auth_enabled,
        .max_sessions = 10,  // Default value
        .session_timeout = 30  // Default value
    };
    strncpy(web_config.username, config.web_server.username, sizeof(web_config.username) - 1);
    strncpy(web_config.password, config.web_server.password, sizeof(web_config.password) - 1);
    
    if (web_server_start(&web_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
    }
    
    // Initialize and start CSI collector
    if (config.csi.enabled) {
        csi_collector_config_t csi_config = {
            .sample_rate = config.csi.sample_rate,
            .buffer_size = config.csi.buffer_size,
            .filter_enabled = config.csi.filter_enabled,
            .filter_threshold = config.csi.filter_threshold,
            .enable_rssi = config.csi.enable_rssi,
            .enable_phase = config.csi.enable_phase,
            .enable_amplitude = config.csi.enable_amplitude
        };
        
        if (csi_collector_init(&csi_config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize CSI collector");
        } else if (csi_collector_start() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start CSI collector");
        }
    } else {
        ESP_LOGI(TAG, "CSI collector disabled in configuration");
    }
    
    // Start NTP synchronization first (required for accurate timestamps)
    if (config.ntp.enabled) {
        ntp_config_t ntp_config = {
            .enabled = config.ntp.enabled,
            .sync_interval = config.ntp.sync_interval * 60,  // Convert minutes to seconds
            .timezone = "UTC"  // Default timezone
        };
        strncpy(ntp_config.server1, config.ntp.server1, sizeof(ntp_config.server1) - 1);
        strncpy(ntp_config.server2, config.ntp.server2, sizeof(ntp_config.server2) - 1);
        strncpy(ntp_config.server3, config.ntp.server3, sizeof(ntp_config.server3) - 1);
        
        if (ntp_sync_init(&ntp_config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize NTP sync");
        } else {
            ESP_LOGI(TAG, "NTP sync initialized successfully");
            if (ntp_sync_start() != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start NTP sync");
            } else {
                ESP_LOGI(TAG, "NTP sync started, waiting for initial synchronization...");
                // Wait up to 30 seconds for initial sync
                for (int i = 0; i < 30 && !ntp_sync_is_synchronized(); i++) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
                if (ntp_sync_is_synchronized()) {
                    ESP_LOGI(TAG, "NTP time synchronized successfully");
                    
                    // Display current synchronized time
                    char time_str[64];
                    if (ntp_client_get_time_string(time_str, sizeof(time_str)) == ESP_OK) {
                        ESP_LOGI(TAG, "Current time: %s", time_str);
                    }
                } else {
                    ESP_LOGW(TAG, "NTP synchronization timeout, continuing with system time");
                }
            }
        }
    } else {
        ESP_LOGI(TAG, "NTP sync disabled in configuration");
    }
    
    // Start MQTT client if configured
    if (config.mqtt.enabled) {
        mqtt_config_t mqtt_config = {
            .enabled = config.mqtt.enabled,
            .port = config.mqtt.port,
            .ssl_enabled = config.mqtt.ssl_enabled,
            .keepalive = config.mqtt.keepalive,
            .qos = 1,  // Default QoS
            .retain = false  // Default retain
        };
        strncpy(mqtt_config.broker_url, config.mqtt.broker_url, sizeof(mqtt_config.broker_url) - 1);
        strncpy(mqtt_config.username, config.mqtt.username, sizeof(mqtt_config.username) - 1);
        strncpy(mqtt_config.password, config.mqtt.password, sizeof(mqtt_config.password) - 1);
        strncpy(mqtt_config.client_id, config.mqtt.client_id, sizeof(mqtt_config.client_id) - 1);
        strncpy(mqtt_config.topic_prefix, config.mqtt.topic_prefix, sizeof(mqtt_config.topic_prefix) - 1);
        
        if (mqtt_client_init(&mqtt_config) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize MQTT client");
        } else if (mqtt_client_start() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start MQTT client");
        } else {
            ESP_LOGI(TAG, "MQTT client started successfully");
            
            // Register default message callback for remote control
            mqtt_client_register_callback(mqtt_subscriber_default_callback, NULL);
            
            // Subscribe to device control topics
            if (mqtt_client_is_connected()) {
                mqtt_subscriber_subscribe_device_topics(config.device_name);
            }
            
            // Publish device startup status
            mqtt_publish_device_status(
                config.device_name,
                config.firmware_version,
                esp_timer_get_time() / 1000000ULL,
                -50,  // TODO: Get actual WiFi RSSI
                esp_get_free_heap_size()
            );
        }
    } else {
        ESP_LOGI(TAG, "MQTT client disabled in configuration");
    }
    
    // Initialize OTA updater
    ota_config_t ota_config = {
        .enabled = config.ota.enabled,
        .auto_check = config.ota.auto_update,
        .check_interval = config.ota.check_interval * 60,  // Convert minutes to seconds
        .server_port = 443,  // Default HTTPS port
        .use_secure_connection = true  // Default to secure
    };
    strncpy(ota_config.server_url, config.ota.update_url, sizeof(ota_config.server_url) - 1);
    
    if (ota_updater_init(&ota_config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize OTA updater");
    }
    
    ESP_LOGI(TAG, "All systems initialized successfully");
    
    // Initialize counters and timers for monitoring
    uint32_t loop_counter = 0;
    uint32_t csi_data_count = 0;
    uint32_t mqtt_publish_count = 0;
    uint32_t mqtt_publish_errors = 0;
    TickType_t last_stats_time = xTaskGetTickCount();
    TickType_t last_ota_check = xTaskGetTickCount();
    TickType_t last_system_metrics = xTaskGetTickCount();
    
    // Main application loop
    while (1) {
        loop_counter++;
        
        // Process CSI data if collector is running
        if (csi_collector_is_running()) {
            csi_data_t csi_data;
            if (csi_collector_get_data(&csi_data, pdMS_TO_TICKS(100)) == ESP_OK) {
                csi_data_count++;
                
                // Ensure CSI data has proper timestamp
                if (ntp_sync_is_synchronized()) {
                    struct timeval tv;
                    if (ntp_sync_get_time(&tv) == ESP_OK) {
                        csi_data.timestamp = tv.tv_sec * 1000000ULL + tv.tv_usec;
                    }
                }
                
                ESP_LOGD(TAG, "CSI data received: %d bytes, RSSI: %d dBm, MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
                        csi_data.len, csi_data.rssi,
                        csi_data.mac[0], csi_data.mac[1], csi_data.mac[2],
                        csi_data.mac[3], csi_data.mac[4], csi_data.mac[5]);
                
                // Send to MQTT if connected
                if (mqtt_client_is_connected()) {
                    esp_err_t err = mqtt_client_publish_csi_data(&csi_data);
                    if (err == ESP_OK) {
                        mqtt_publish_count++;
                    } else {
                        mqtt_publish_errors++;
                        ESP_LOGW(TAG, "Failed to publish CSI data to MQTT: %s", esp_err_to_name(err));
                    }
                }
                
                // Free CSI data resources
                csi_collector_free_data(&csi_data);
            }
        }
        
        // Periodic statistics and monitoring (every 30 seconds)
        TickType_t current_time = xTaskGetTickCount();
        if ((current_time - last_stats_time) >= pdMS_TO_TICKS(30000)) {
            last_stats_time = current_time;
            
            ESP_LOGI(TAG, "=== System Status ===");
            ESP_LOGI(TAG, "Loop cycles: %" PRIu32 ", CSI data processed: %" PRIu32, loop_counter, csi_data_count);
            ESP_LOGI(TAG, "MQTT publishes: %" PRIu32 " (errors: %" PRIu32 ")", mqtt_publish_count, mqtt_publish_errors);
            ESP_LOGI(TAG, "Free heap: %" PRIu32 " bytes", esp_get_free_heap_size());
            ESP_LOGI(TAG, "Min free heap: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
            
            // NTP sync status
            if (ntp_sync_is_synchronized()) {
                ntp_sync_quality_t quality;
                if (ntp_client_get_sync_quality(&quality) == ESP_OK) {
                    const char *quality_str[] = {"POOR", "FAIR", "GOOD", "EXCELLENT"};
                    ESP_LOGI(TAG, "NTP quality: %s (offset: %" PRId32 "ms, age: %" PRIu64 "s)", 
                             quality_str[quality.quality], quality.offset_ms, quality.time_since_sync_sec);
                }
            } else {
                ESP_LOGW(TAG, "NTP not synchronized");
            }
            
            // MQTT connection status
            if (config.mqtt.enabled) {
                if (mqtt_client_is_connected()) {
                    mqtt_stats_t mqtt_stats;
                    if (mqtt_client_get_stats(&mqtt_stats) == ESP_OK) {
                        ESP_LOGI(TAG, "MQTT: connected, sent: %" PRIu32 ", received: %" PRIu32 ", errors: %" PRIu32, 
                                mqtt_stats.messages_sent, mqtt_stats.messages_received, 
                                mqtt_stats.connection_errors);
                    }
                } else {
                    ESP_LOGW(TAG, "MQTT: disconnected");
                }
            }
        }
        
        // Publish system metrics to MQTT (every 5 minutes)
        if (config.mqtt.enabled && mqtt_client_is_connected() && 
            (current_time - last_system_metrics) >= pdMS_TO_TICKS(300000)) {
            last_system_metrics = current_time;
            
            mqtt_publish_system_metrics(
                config.device_name,
                0.0f,  // TODO: Calculate actual CPU usage
                esp_get_free_heap_size(),
                esp_get_minimum_free_heap_size(),
                uxTaskGetNumberOfTasks()
            );
            
            ESP_LOGI(TAG, "Published system metrics to MQTT");
        }
        
        // Check for OTA updates periodically (every 5 minutes)
        if (config.ota.enabled && config.ota.auto_update &&
            (current_time - last_ota_check) >= pdMS_TO_TICKS(300000)) {
            last_ota_check = current_time;
            
            ESP_LOGI(TAG, "Checking for OTA updates...");
            esp_err_t ota_err = ota_updater_check_for_updates();
            if (ota_err != ESP_OK) {
                ESP_LOGW(TAG, "OTA update check failed: %s", esp_err_to_name(ota_err));
            }
        }
        
        // Monitor system health and restart if necessary
        if (esp_get_free_heap_size() < 10000) {  // Less than 10KB free heap
            ESP_LOGE(TAG, "Critical low memory condition detected!");
            mqtt_publish_alert(config.device_name, "ERROR", "SYSTEM", "Critical low memory");
            
            // Emergency restart after 5 seconds
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay for responsive operation
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief Application entry point
 */
void app_main(void)
{
    ESP_LOGI(TAG, "CSI Positioning System starting...");
    
    // Create main application task
    xTaskCreate(
        app_main_task,
        "app_main",
        8192,  // Stack size
        NULL,  // Parameters
        5,     // Priority
        NULL   // Task handle
    );
}