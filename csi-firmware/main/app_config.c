/**
 * @file app_config.c
 * @brief Application configuration implementation
 */

#include "app_config.h"
#include <string.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "APP_CONFIG";
static const char *NVS_NAMESPACE = "csi_config";

// Configuration keys
#define KEY_DEVICE_NAME         "device_name"
#define KEY_FIRMWARE_VERSION    "fw_version"
#define KEY_WIFI_SSID          "wifi_ssid"
#define KEY_WIFI_PASSWORD      "wifi_pass"
#define KEY_WIFI_CHANNEL       "wifi_chan"
#define KEY_WIFI_STA_MODE      "wifi_sta"
#define KEY_WIFI_AP_MODE       "wifi_ap"
#define KEY_WIFI_AP_SSID       "ap_ssid"
#define KEY_WIFI_AP_PASSWORD   "ap_pass"
#define KEY_CSI_ENABLED        "csi_enabled"
#define KEY_CSI_SAMPLE_RATE    "csi_rate"
#define KEY_CSI_BUFFER_SIZE    "csi_buffer"
#define KEY_CSI_FILTER_ENABLED "csi_filter"
#define KEY_CSI_FILTER_THRESH  "csi_thresh"
#define KEY_WEB_ENABLED        "web_enabled"
#define KEY_WEB_PORT           "web_port"
#define KEY_WEB_AUTH_ENABLED   "web_auth"
#define KEY_WEB_USERNAME       "web_user"
#define KEY_WEB_PASSWORD       "web_pass"
#define KEY_MQTT_ENABLED       "mqtt_enabled"
#define KEY_MQTT_BROKER_URL    "mqtt_broker"
#define KEY_MQTT_PORT          "mqtt_port"
#define KEY_MQTT_USERNAME      "mqtt_user"
#define KEY_MQTT_PASSWORD      "mqtt_pass"
#define KEY_MQTT_CLIENT_ID     "mqtt_client"
#define KEY_MQTT_TOPIC_PREFIX  "mqtt_topic"
#define KEY_MQTT_SSL_ENABLED   "mqtt_ssl"
#define KEY_MQTT_KEEPALIVE     "mqtt_keep"
#define KEY_NTP_ENABLED        "ntp_enabled"
#define KEY_NTP_SERVER1        "ntp_srv1"
#define KEY_NTP_SERVER2        "ntp_srv2"
#define KEY_NTP_SERVER3        "ntp_srv3"
#define KEY_NTP_TIMEZONE       "ntp_tz"
#define KEY_NTP_SYNC_INTERVAL  "ntp_sync"
#define KEY_OTA_ENABLED        "ota_enabled"
#define KEY_OTA_UPDATE_URL     "ota_url"
#define KEY_OTA_AUTO_UPDATE    "ota_auto"
#define KEY_OTA_CHECK_INTERVAL "ota_check"
#define KEY_OTA_VERIFY_SIG     "ota_verify"

esp_err_t app_config_load(app_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize NVS if not already done
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        app_config_set_defaults(config);
        return ESP_ERR_NOT_FOUND;
    }

    // Load device configuration
    size_t required_size;
    
    required_size = sizeof(config->device_name);
    err = nvs_get_str(nvs_handle, KEY_DEVICE_NAME, config->device_name, &required_size);
    if (err != ESP_OK) {
        strcpy(config->device_name, "CSI-Device-001");
    }

    required_size = sizeof(config->firmware_version);
    err = nvs_get_str(nvs_handle, KEY_FIRMWARE_VERSION, config->firmware_version, &required_size);
    if (err != ESP_OK) {
        strcpy(config->firmware_version, "1.0.0");
    }

    // Load WiFi configuration
    required_size = sizeof(config->wifi.ssid);
    nvs_get_str(nvs_handle, KEY_WIFI_SSID, config->wifi.ssid, &required_size);
    
    required_size = sizeof(config->wifi.password);
    nvs_get_str(nvs_handle, KEY_WIFI_PASSWORD, config->wifi.password, &required_size);
    
    nvs_get_u8(nvs_handle, KEY_WIFI_CHANNEL, &config->wifi.channel);
    nvs_get_u8(nvs_handle, KEY_WIFI_STA_MODE, (uint8_t*)&config->wifi.sta_mode);
    nvs_get_u8(nvs_handle, KEY_WIFI_AP_MODE, (uint8_t*)&config->wifi.ap_mode);
    
    required_size = sizeof(config->wifi.ap_ssid);
    nvs_get_str(nvs_handle, KEY_WIFI_AP_SSID, config->wifi.ap_ssid, &required_size);
    
    required_size = sizeof(config->wifi.ap_password);
    nvs_get_str(nvs_handle, KEY_WIFI_AP_PASSWORD, config->wifi.ap_password, &required_size);

    // Load CSI configuration
    nvs_get_u8(nvs_handle, KEY_CSI_ENABLED, (uint8_t*)&config->csi.enabled);
    nvs_get_u8(nvs_handle, KEY_CSI_SAMPLE_RATE, &config->csi.sample_rate);
    nvs_get_u16(nvs_handle, KEY_CSI_BUFFER_SIZE, &config->csi.buffer_size);
    nvs_get_u8(nvs_handle, KEY_CSI_FILTER_ENABLED, (uint8_t*)&config->csi.filter_enabled);
    
    // Load filter threshold as blob since it's a float
    required_size = sizeof(config->csi.filter_threshold);
    nvs_get_blob(nvs_handle, KEY_CSI_FILTER_THRESH, &config->csi.filter_threshold, &required_size);

    // Load web server configuration
    nvs_get_u8(nvs_handle, KEY_WEB_ENABLED, (uint8_t*)&config->web_server.enabled);
    nvs_get_u16(nvs_handle, KEY_WEB_PORT, &config->web_server.port);
    nvs_get_u8(nvs_handle, KEY_WEB_AUTH_ENABLED, (uint8_t*)&config->web_server.auth_enabled);
    
    required_size = sizeof(config->web_server.username);
    nvs_get_str(nvs_handle, KEY_WEB_USERNAME, config->web_server.username, &required_size);
    
    required_size = sizeof(config->web_server.password);
    nvs_get_str(nvs_handle, KEY_WEB_PASSWORD, config->web_server.password, &required_size);

    // Load MQTT configuration
    nvs_get_u8(nvs_handle, KEY_MQTT_ENABLED, (uint8_t*)&config->mqtt.enabled);
    
    required_size = sizeof(config->mqtt.broker_url);
    nvs_get_str(nvs_handle, KEY_MQTT_BROKER_URL, config->mqtt.broker_url, &required_size);
    
    nvs_get_u16(nvs_handle, KEY_MQTT_PORT, &config->mqtt.port);
    
    required_size = sizeof(config->mqtt.username);
    nvs_get_str(nvs_handle, KEY_MQTT_USERNAME, config->mqtt.username, &required_size);
    
    required_size = sizeof(config->mqtt.password);
    nvs_get_str(nvs_handle, KEY_MQTT_PASSWORD, config->mqtt.password, &required_size);
    
    required_size = sizeof(config->mqtt.client_id);
    nvs_get_str(nvs_handle, KEY_MQTT_CLIENT_ID, config->mqtt.client_id, &required_size);
    
    required_size = sizeof(config->mqtt.topic_prefix);
    nvs_get_str(nvs_handle, KEY_MQTT_TOPIC_PREFIX, config->mqtt.topic_prefix, &required_size);
    
    nvs_get_u8(nvs_handle, KEY_MQTT_SSL_ENABLED, (uint8_t*)&config->mqtt.ssl_enabled);
    nvs_get_u16(nvs_handle, KEY_MQTT_KEEPALIVE, &config->mqtt.keepalive);

    // Load NTP configuration
    nvs_get_u8(nvs_handle, KEY_NTP_ENABLED, (uint8_t*)&config->ntp.enabled);
    
    required_size = sizeof(config->ntp.server1);
    nvs_get_str(nvs_handle, KEY_NTP_SERVER1, config->ntp.server1, &required_size);
    
    required_size = sizeof(config->ntp.server2);
    nvs_get_str(nvs_handle, KEY_NTP_SERVER2, config->ntp.server2, &required_size);
    
    required_size = sizeof(config->ntp.server3);
    nvs_get_str(nvs_handle, KEY_NTP_SERVER3, config->ntp.server3, &required_size);
    
    nvs_get_i16(nvs_handle, KEY_NTP_TIMEZONE, &config->ntp.timezone_offset);
    nvs_get_u16(nvs_handle, KEY_NTP_SYNC_INTERVAL, &config->ntp.sync_interval);

    // Load OTA configuration
    nvs_get_u8(nvs_handle, KEY_OTA_ENABLED, (uint8_t*)&config->ota.enabled);
    
    required_size = sizeof(config->ota.update_url);
    nvs_get_str(nvs_handle, KEY_OTA_UPDATE_URL, config->ota.update_url, &required_size);
    
    nvs_get_u8(nvs_handle, KEY_OTA_AUTO_UPDATE, (uint8_t*)&config->ota.auto_update);
    nvs_get_u16(nvs_handle, KEY_OTA_CHECK_INTERVAL, &config->ota.check_interval);
    nvs_get_u8(nvs_handle, KEY_OTA_VERIFY_SIG, (uint8_t*)&config->ota.verify_signature);

    nvs_close(nvs_handle);
    
    ESP_LOGI(TAG, "Configuration loaded successfully");
    return ESP_OK;
}

esp_err_t app_config_save(const app_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Validate configuration first
    esp_err_t err = app_config_validate(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configuration validation failed");
        return err;
    }

    nvs_handle_t nvs_handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    // Save device configuration
    nvs_set_str(nvs_handle, KEY_DEVICE_NAME, config->device_name);
    nvs_set_str(nvs_handle, KEY_FIRMWARE_VERSION, config->firmware_version);

    // Save WiFi configuration
    nvs_set_str(nvs_handle, KEY_WIFI_SSID, config->wifi.ssid);
    nvs_set_str(nvs_handle, KEY_WIFI_PASSWORD, config->wifi.password);
    nvs_set_u8(nvs_handle, KEY_WIFI_CHANNEL, config->wifi.channel);
    nvs_set_u8(nvs_handle, KEY_WIFI_STA_MODE, config->wifi.sta_mode);
    nvs_set_u8(nvs_handle, KEY_WIFI_AP_MODE, config->wifi.ap_mode);
    nvs_set_str(nvs_handle, KEY_WIFI_AP_SSID, config->wifi.ap_ssid);
    nvs_set_str(nvs_handle, KEY_WIFI_AP_PASSWORD, config->wifi.ap_password);

    // Save CSI configuration
    nvs_set_u8(nvs_handle, KEY_CSI_ENABLED, config->csi.enabled);
    nvs_set_u8(nvs_handle, KEY_CSI_SAMPLE_RATE, config->csi.sample_rate);
    nvs_set_u16(nvs_handle, KEY_CSI_BUFFER_SIZE, config->csi.buffer_size);
    nvs_set_u8(nvs_handle, KEY_CSI_FILTER_ENABLED, config->csi.filter_enabled);
    nvs_set_blob(nvs_handle, KEY_CSI_FILTER_THRESH, &config->csi.filter_threshold, sizeof(float));

    // Save web server configuration
    nvs_set_u8(nvs_handle, KEY_WEB_ENABLED, config->web_server.enabled);
    nvs_set_u16(nvs_handle, KEY_WEB_PORT, config->web_server.port);
    nvs_set_u8(nvs_handle, KEY_WEB_AUTH_ENABLED, config->web_server.auth_enabled);
    nvs_set_str(nvs_handle, KEY_WEB_USERNAME, config->web_server.username);
    nvs_set_str(nvs_handle, KEY_WEB_PASSWORD, config->web_server.password);

    // Save MQTT configuration
    nvs_set_u8(nvs_handle, KEY_MQTT_ENABLED, config->mqtt.enabled);
    nvs_set_str(nvs_handle, KEY_MQTT_BROKER_URL, config->mqtt.broker_url);
    nvs_set_u16(nvs_handle, KEY_MQTT_PORT, config->mqtt.port);
    nvs_set_str(nvs_handle, KEY_MQTT_USERNAME, config->mqtt.username);
    nvs_set_str(nvs_handle, KEY_MQTT_PASSWORD, config->mqtt.password);
    nvs_set_str(nvs_handle, KEY_MQTT_CLIENT_ID, config->mqtt.client_id);
    nvs_set_str(nvs_handle, KEY_MQTT_TOPIC_PREFIX, config->mqtt.topic_prefix);
    nvs_set_u8(nvs_handle, KEY_MQTT_SSL_ENABLED, config->mqtt.ssl_enabled);
    nvs_set_u16(nvs_handle, KEY_MQTT_KEEPALIVE, config->mqtt.keepalive);

    // Save NTP configuration
    nvs_set_u8(nvs_handle, KEY_NTP_ENABLED, config->ntp.enabled);
    nvs_set_str(nvs_handle, KEY_NTP_SERVER1, config->ntp.server1);
    nvs_set_str(nvs_handle, KEY_NTP_SERVER2, config->ntp.server2);
    nvs_set_str(nvs_handle, KEY_NTP_SERVER3, config->ntp.server3);
    nvs_set_i16(nvs_handle, KEY_NTP_TIMEZONE, config->ntp.timezone_offset);
    nvs_set_u16(nvs_handle, KEY_NTP_SYNC_INTERVAL, config->ntp.sync_interval);

    // Save OTA configuration
    nvs_set_u8(nvs_handle, KEY_OTA_ENABLED, config->ota.enabled);
    nvs_set_str(nvs_handle, KEY_OTA_UPDATE_URL, config->ota.update_url);
    nvs_set_u8(nvs_handle, KEY_OTA_AUTO_UPDATE, config->ota.auto_update);
    nvs_set_u16(nvs_handle, KEY_OTA_CHECK_INTERVAL, config->ota.check_interval);
    nvs_set_u8(nvs_handle, KEY_OTA_VERIFY_SIG, config->ota.verify_signature);

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Configuration saved successfully");
    }
    
    return err;
}

void app_config_set_defaults(app_config_t *config)
{
    if (!config) {
        return;
    }

    // Clear the entire structure
    memset(config, 0, sizeof(app_config_t));

    // Set device defaults
    strcpy(config->device_name, "CSI-Device-001");
    strcpy(config->firmware_version, "1.0.0");

    // Set WiFi defaults
    strcpy(config->wifi.ssid, "");
    strcpy(config->wifi.password, "");
    config->wifi.channel = 0; // Auto
    config->wifi.sta_mode = true;
    config->wifi.ap_mode = false;
    strcpy(config->wifi.ap_ssid, "CSI-Device-AP");
    strcpy(config->wifi.ap_password, "");

    // Set CSI defaults
    config->csi.enabled = true;
    config->csi.sample_rate = 10;
    config->csi.buffer_size = 1024;
    config->csi.filter_enabled = true;
    config->csi.filter_threshold = 0.3f;
    config->csi.enable_rssi = true;
    config->csi.enable_phase = true;
    config->csi.enable_amplitude = true;

    // Set web server defaults
    config->web_server.enabled = true;
    config->web_server.port = 80;
    config->web_server.auth_enabled = false;
    strcpy(config->web_server.username, "admin");
    strcpy(config->web_server.password, "");

    // Set MQTT defaults
    config->mqtt.enabled = false;
    strcpy(config->mqtt.broker_url, "");
    config->mqtt.port = 1883;
    strcpy(config->mqtt.username, "");
    strcpy(config->mqtt.password, "");
    strcpy(config->mqtt.client_id, "csi-device");
    strcpy(config->mqtt.topic_prefix, "csi-device");
    config->mqtt.ssl_enabled = false;
    config->mqtt.keepalive = 60;

    // Set NTP defaults
    config->ntp.enabled = true;
    strcpy(config->ntp.server1, "pool.ntp.org");
    strcpy(config->ntp.server2, "time.nist.gov");
    strcpy(config->ntp.server3, "time.google.com");
    config->ntp.timezone_offset = 0;
    config->ntp.sync_interval = 60;

    // Set OTA defaults
    config->ota.enabled = true;
    strcpy(config->ota.update_url, "");
    config->ota.auto_update = false;
    config->ota.check_interval = 360; // 6 hours
    config->ota.verify_signature = true;

    ESP_LOGI(TAG, "Default configuration set");
}

esp_err_t app_config_validate(const app_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Validate device name
    if (strlen(config->device_name) == 0 || strlen(config->device_name) >= sizeof(config->device_name)) {
        ESP_LOGE(TAG, "Invalid device name");
        return ESP_ERR_INVALID_ARG;
    }

    // Validate CSI configuration
    if (config->csi.sample_rate == 0 || config->csi.sample_rate > 100) {
        ESP_LOGE(TAG, "Invalid CSI sample rate: %d", config->csi.sample_rate);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->csi.buffer_size < 256 || config->csi.buffer_size > 4096) {
        ESP_LOGE(TAG, "Invalid CSI buffer size: %d", config->csi.buffer_size);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->csi.filter_threshold < 0.0f || config->csi.filter_threshold > 1.0f) {
        ESP_LOGE(TAG, "Invalid CSI filter threshold: %.2f", config->csi.filter_threshold);
        return ESP_ERR_INVALID_ARG;
    }

    // Validate web server configuration
    if (config->web_server.port == 0) {
        ESP_LOGE(TAG, "Invalid web server port: %d", config->web_server.port);
        return ESP_ERR_INVALID_ARG;
    }

    // Validate MQTT configuration
    if (config->mqtt.enabled) {
        if (strlen(config->mqtt.broker_url) == 0) {
            ESP_LOGE(TAG, "MQTT enabled but broker URL is empty");
            return ESP_ERR_INVALID_ARG;
        }

        if (config->mqtt.port == 0) {
            ESP_LOGE(TAG, "Invalid MQTT port: %d", config->mqtt.port);
            return ESP_ERR_INVALID_ARG;
        }

        if (config->mqtt.keepalive == 0 || config->mqtt.keepalive > 3600) {
            ESP_LOGE(TAG, "Invalid MQTT keepalive: %d", config->mqtt.keepalive);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Validate NTP configuration
    if (config->ntp.enabled) {
        if (strlen(config->ntp.server1) == 0) {
            ESP_LOGE(TAG, "NTP enabled but server1 is empty");
            return ESP_ERR_INVALID_ARG;
        }

        if (config->ntp.timezone_offset < -720 || config->ntp.timezone_offset > 720) {
            ESP_LOGE(TAG, "Invalid timezone offset: %d", config->ntp.timezone_offset);
            return ESP_ERR_INVALID_ARG;
        }

        if (config->ntp.sync_interval == 0 || config->ntp.sync_interval > 1440) {
            ESP_LOGE(TAG, "Invalid NTP sync interval: %d", config->ntp.sync_interval);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Validate OTA configuration
    if (config->ota.check_interval == 0 || config->ota.check_interval > 1440) {
        ESP_LOGE(TAG, "Invalid OTA check interval: %d", config->ota.check_interval);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Configuration validation passed");
    return ESP_OK;
}