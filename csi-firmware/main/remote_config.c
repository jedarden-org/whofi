/**
 * @file remote_config.c
 * @brief Remote configuration handler for ESP32 nodes
 */

#include <string.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <cJSON.h>
#include "app_config.h"
#include "mqtt_client_wrapper.h"
#include "csi_collector.h"
#include "web_server.h"
#include "ntp_sync.h"

static const char *TAG = "remote_config";

// Forward declaration
static void publish_detailed_status(void);

/**
 * @brief Handle CSI configuration update
 */
static esp_err_t handle_csi_config(cJSON *config)
{
    csi_collector_config_t csi_config;
    
    // Get current configuration
    esp_err_t err = csi_collector_get_config(&csi_config);
    if (err != ESP_OK) {
        return err;
    }
    
    // Update configuration from JSON
    cJSON *item = cJSON_GetObjectItem(config, "sample_rate");
    if (item && cJSON_IsNumber(item)) {
        csi_config.sample_rate = item->valueint;
    }
    
    item = cJSON_GetObjectItem(config, "buffer_size");
    if (item && cJSON_IsNumber(item)) {
        csi_config.buffer_size = item->valueint;
    }
    
    item = cJSON_GetObjectItem(config, "filter_enabled");
    if (item && cJSON_IsBool(item)) {
        csi_config.filter_enabled = cJSON_IsTrue(item);
    }
    
    item = cJSON_GetObjectItem(config, "filter_threshold");
    if (item && cJSON_IsNumber(item)) {
        csi_config.filter_threshold = item->valuedouble;
    }
    
    // Apply configuration
    return csi_collector_update_config(&csi_config);
}

/**
 * @brief Handle MQTT configuration update
 */
static esp_err_t handle_mqtt_config(cJSON *config)
{
    // Load current configuration
    app_config_t app_cfg;
    esp_err_t err = app_config_load(&app_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config: %s", esp_err_to_name(err));
        return err;
    }
    
    // Update MQTT settings
    cJSON *item = cJSON_GetObjectItem(config, "broker_url");
    if (item && cJSON_IsString(item)) {
        strncpy(app_cfg.mqtt.broker_url, item->valuestring, sizeof(app_cfg.mqtt.broker_url) - 1);
    }
    
    item = cJSON_GetObjectItem(config, "port");
    if (item && cJSON_IsNumber(item)) {
        app_cfg.mqtt.port = item->valueint;
    }
    
    item = cJSON_GetObjectItem(config, "topic_prefix");
    if (item && cJSON_IsString(item)) {
        strncpy(app_cfg.mqtt.topic_prefix, item->valuestring, sizeof(app_cfg.mqtt.topic_prefix) - 1);
    }
    
    // Save configuration
    err = app_config_save(&app_cfg);
    if (err == ESP_OK) {
        // Restart MQTT client with new settings
        mqtt_client_stop();
        vTaskDelay(pdMS_TO_TICKS(1000));
        mqtt_client_init(&app_cfg.mqtt);
        mqtt_client_start();
    }
    
    return err;
}

/**
 * @brief Handle node settings update
 */
static esp_err_t handle_node_settings(cJSON *config)
{
    // Load current configuration
    app_config_t app_cfg;
    esp_err_t err = app_config_load(&app_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load config: %s", esp_err_to_name(err));
        return err;
    }
    
    bool restart_required = false;
    
    // Note: Position fields don't exist in app_config_t structure
    // If needed, they should be added to the structure definition
    // For now, we'll skip position updates
    
    // Update node name
    cJSON *name = cJSON_GetObjectItem(config, "node_name");
    if (name && cJSON_IsString(name)) {
        strncpy(app_cfg.device_name, name->valuestring, sizeof(app_cfg.device_name) - 1);
    }
    
    // Update WiFi settings (requires restart)
    cJSON *wifi = cJSON_GetObjectItem(config, "wifi");
    if (wifi) {
        cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
        cJSON *password = cJSON_GetObjectItem(wifi, "password");
        
        if (ssid && cJSON_IsString(ssid)) {
            strncpy(app_cfg.wifi.ssid, ssid->valuestring, sizeof(app_cfg.wifi.ssid) - 1);
            restart_required = true;
        }
        if (password && cJSON_IsString(password)) {
            strncpy(app_cfg.wifi.password, password->valuestring, sizeof(app_cfg.wifi.password) - 1);
            restart_required = true;
        }
    }
    
    // Save configuration
    err = app_config_save(&app_cfg);
    
    if (err == ESP_OK && restart_required) {
        ESP_LOGW(TAG, "WiFi settings changed, restart required in 5 seconds");
        vTaskDelay(pdMS_TO_TICKS(5000));
        esp_restart();
    }
    
    return err;
}

/**
 * @brief Main configuration update handler
 */
esp_err_t remote_config_update_handler(const cJSON *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t err = ESP_OK;
    
    // Handle CSI configuration
    cJSON *csi = cJSON_GetObjectItem(config, "csi");
    if (csi) {
        err = handle_csi_config(csi);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update CSI config: %s", esp_err_to_name(err));
        }
    }
    
    // Handle MQTT configuration
    cJSON *mqtt = cJSON_GetObjectItem(config, "mqtt");
    if (mqtt) {
        err = handle_mqtt_config(mqtt);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update MQTT config: %s", esp_err_to_name(err));
        }
    }
    
    // Handle node settings
    cJSON *node = cJSON_GetObjectItem(config, "node");
    if (node) {
        err = handle_node_settings(node);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update node settings: %s", esp_err_to_name(err));
        }
    }
    
    // Load config to get device name for ack topic
    app_config_t app_cfg;
    if (app_config_load(&app_cfg) == ESP_OK) {
        // Send acknowledgment
        char ack_topic[128];
        snprintf(ack_topic, sizeof(ack_topic), "devices/%s/config/ack", app_cfg.device_name);
        
        cJSON *ack = cJSON_CreateObject();
        cJSON_AddStringToObject(ack, "status", err == ESP_OK ? "success" : "failed");
        cJSON_AddNumberToObject(ack, "timestamp", (double)esp_timer_get_time() / 1000000.0);
        
        char *ack_str = cJSON_PrintUnformatted(ack);
        if (ack_str) {
            mqtt_client_publish(ack_topic, ack_str, strlen(ack_str), 1, false);
            free(ack_str);
        }
        cJSON_Delete(ack);
    }
    
    return err;
}

/**
 * @brief Remote command handler
 */
esp_err_t remote_command_handler(const cJSON *params)
{
    if (!params) {
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *cmd = cJSON_GetObjectItem(params, "command");
    if (!cmd || !cJSON_IsString(cmd)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const char *command = cmd->valuestring;
    ESP_LOGI(TAG, "Executing remote command: %s", command);
    
    if (strcmp(command, "restart") == 0) {
        ESP_LOGW(TAG, "Restart requested, restarting in 2 seconds");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
    else if (strcmp(command, "start_csi") == 0) {
        return csi_collector_start();
    }
    else if (strcmp(command, "stop_csi") == 0) {
        return csi_collector_stop();
    }
    else if (strcmp(command, "calibrate") == 0) {
        // Trigger calibration mode
        ESP_LOGI(TAG, "Starting calibration mode");
        // Implementation specific to your calibration needs
    }
    else if (strcmp(command, "factory_reset") == 0) {
        ESP_LOGW(TAG, "Factory reset requested");
        nvs_flash_erase();
        esp_restart();
    }
    else if (strcmp(command, "get_status") == 0) {
        // Publish detailed status
        publish_detailed_status();
    }
    else {
        ESP_LOGW(TAG, "Unknown command: %s", command);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    return ESP_OK;
}

/**
 * @brief Publish detailed node status
 */
static void publish_detailed_status(void)
{
    // Load current configuration
    app_config_t cfg;
    if (app_config_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load configuration for status");
        return;
    }
    
    cJSON *status = cJSON_CreateObject();
    cJSON_AddStringToObject(status, "device_name", cfg.device_name);
    cJSON_AddStringToObject(status, "version", cfg.firmware_version);
    cJSON_AddNumberToObject(status, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(status, "free_heap", esp_get_free_heap_size());
    
    // Add CSI status
    cJSON *csi_status = cJSON_CreateObject();
    cJSON_AddBoolToObject(csi_status, "running", csi_collector_is_running());
    
    csi_collector_stats_t csi_stats;
    if (csi_collector_get_stats(&csi_stats) == ESP_OK) {
        cJSON_AddNumberToObject(csi_status, "packets_received", csi_stats.packets_received);
        cJSON_AddNumberToObject(csi_status, "packets_processed", csi_stats.packets_processed);
        cJSON_AddNumberToObject(csi_status, "packets_dropped", csi_stats.packets_dropped);
    }
    cJSON_AddItemToObject(status, "csi", csi_status);
    
    // Note: Position fields don't exist in app_config_t structure
    // If needed, they should be added to the structure definition
    
    // Publish status
    char topic[128];
    snprintf(topic, sizeof(topic), "devices/%s/status/detailed", cfg.device_name);
    
    char *status_str = cJSON_PrintUnformatted(status);
    if (status_str) {
        mqtt_client_publish(topic, status_str, strlen(status_str), 1, false);
        free(status_str);
    }
    cJSON_Delete(status);
}

/**
 * @brief Initialize remote configuration
 */
esp_err_t remote_config_init(void)
{
    // Register configuration update handler
    esp_err_t err = mqtt_subscriber_register_config_handler(remote_config_update_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register config handler: %s", esp_err_to_name(err));
        return err;
    }
    
    // Register command handler
    err = mqtt_subscriber_register_command_handler(remote_command_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register command handler: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Remote configuration initialized");
    return ESP_OK;
}