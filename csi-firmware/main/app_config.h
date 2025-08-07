/**
 * @file app_config.h
 * @brief Application configuration structures and functions
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wi-Fi configuration structure
 */
typedef struct {
    char ssid[32];          ///< Wi-Fi SSID
    char password[64];      ///< Wi-Fi password
    uint8_t channel;        ///< Wi-Fi channel (0 for auto)
    bool sta_mode;          ///< Station mode enabled
    bool ap_mode;           ///< Access Point mode enabled
    char ap_ssid[32];       ///< AP SSID
    char ap_password[64];   ///< AP password
} app_wifi_config_t;

/**
 * @brief CSI collector configuration structure
 */
typedef struct {
    bool enabled;           ///< CSI collection enabled
    uint8_t sample_rate;    ///< Sampling rate in Hz
    uint16_t buffer_size;   ///< Buffer size for CSI data
    bool filter_enabled;    ///< Enable CSI data filtering
    float filter_threshold; ///< Filter threshold value
    bool enable_rssi;       ///< Include RSSI data
    bool enable_phase;      ///< Include phase information
    bool enable_amplitude;  ///< Include amplitude information
} app_csi_config_t;

/**
 * @brief Web server configuration structure
 */
typedef struct {
    bool enabled;           ///< Web server enabled
    uint16_t port;          ///< Server port
    bool auth_enabled;      ///< Authentication enabled
    char username[32];      ///< Admin username
    char password[64];      ///< Admin password
} app_web_server_config_t;

/**
 * @brief MQTT client configuration structure
 */
typedef struct {
    bool enabled;           ///< MQTT client enabled
    char broker_url[128];   ///< MQTT broker URL
    uint16_t port;          ///< MQTT broker port
    char username[32];      ///< MQTT username
    char password[64];      ///< MQTT password
    char client_id[32];     ///< MQTT client ID
    char topic_prefix[64];  ///< Topic prefix for published data
    bool ssl_enabled;       ///< SSL/TLS enabled
    uint16_t keepalive;     ///< Keepalive interval
} app_mqtt_config_t;

/**
 * @brief NTP synchronization configuration structure
 */
typedef struct {
    bool enabled;           ///< NTP sync enabled
    char server1[64];       ///< Primary NTP server
    char server2[64];       ///< Secondary NTP server
    char server3[64];       ///< Tertiary NTP server
    int16_t timezone_offset; ///< Timezone offset in minutes
    uint16_t sync_interval; ///< Sync interval in minutes
} app_ntp_config_t;

/**
 * @brief OTA update configuration structure
 */
typedef struct {
    bool enabled;           ///< OTA updates enabled
    char update_url[128];   ///< OTA update server URL
    bool auto_update;       ///< Automatic updates enabled
    uint16_t check_interval; ///< Update check interval in minutes
    bool verify_signature;  ///< Verify update signature
} app_ota_config_t;

/**
 * @brief Main application configuration structure
 */
typedef struct {
    char device_name[32];           ///< Device name/identifier
    char firmware_version[16];      ///< Firmware version string
    app_wifi_config_t wifi;             ///< Wi-Fi configuration
    app_csi_config_t csi;               ///< CSI collector configuration
    app_web_server_config_t web_server; ///< Web server configuration
    app_mqtt_config_t mqtt;             ///< MQTT client configuration
    app_ntp_config_t ntp;               ///< NTP synchronization configuration
    app_ota_config_t ota;               ///< OTA update configuration
} app_config_t;

/**
 * @brief Load application configuration from NVS
 * @param config Pointer to configuration structure to populate
 * @return ESP_OK on success, error code on failure
 */
esp_err_t app_config_load(app_config_t *config);

/**
 * @brief Save application configuration to NVS
 * @param config Pointer to configuration structure to save
 * @return ESP_OK on success, error code on failure
 */
esp_err_t app_config_save(const app_config_t *config);

/**
 * @brief Set default configuration values
 * @param config Pointer to configuration structure to initialize
 */
void app_config_set_defaults(app_config_t *config);

/**
 * @brief Validate configuration parameters
 * @param config Pointer to configuration structure to validate
 * @return ESP_OK if valid, error code on validation failure
 */
esp_err_t app_config_validate(const app_config_t *config);

#ifdef __cplusplus
}
#endif

#endif // APP_CONFIG_H