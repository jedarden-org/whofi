/**
 * @file http_telemetry.h
 * @brief HTTP-only telemetry client for CSI positioning system
 * 
 * Replaces MQTT client for simplified Docker deployment architecture
 * All CSI data and system metrics transmitted via HTTP POST
 */

#pragma once

#include <esp_err.h>
#include <esp_http_client.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP telemetry configuration
 */
typedef struct {
    char server_url[256];           ///< Docker server URL (e.g., "http://192.168.1.100:3000")
    char device_id[64];             ///< Unique device identifier
    char auth_token[128];           ///< Authentication token (optional)
    uint32_t timeout_ms;            ///< HTTP request timeout
    uint32_t retry_count;           ///< Number of retries on failure
    bool compress_data;             ///< Enable gzip compression for payloads
} http_telemetry_config_t;

/**
 * @brief CSI data structure for HTTP transmission
 */
typedef struct {
    char device_id[64];             ///< Device identifier
    uint64_t timestamp;             ///< Microsecond timestamp
    uint8_t mac_address[6];         ///< Target device MAC
    int8_t rssi;                    ///< Signal strength (dBm)
    uint16_t channel;               ///< WiFi channel
    uint32_t data_length;           ///< CSI data length
    float *amplitude_data;          ///< CSI amplitude array
    float *phase_data;              ///< CSI phase array (optional)
    float position_x;               ///< Calculated X position (optional)
    float position_y;               ///< Calculated Y position (optional)
    float confidence;               ///< Position confidence (0-1)
} http_csi_payload_t;

/**
 * @brief System metrics structure for HTTP transmission
 */
typedef struct {
    char device_id[64];             ///< Device identifier
    uint64_t timestamp;             ///< Current timestamp
    uint32_t uptime_sec;            ///< System uptime in seconds
    uint32_t free_heap_bytes;       ///< Available heap memory
    uint32_t min_free_heap_bytes;   ///< Minimum heap ever available
    uint8_t cpu_usage_percent;      ///< CPU usage percentage
    int8_t wifi_rssi;               ///< WiFi connection RSSI
    uint8_t task_count;             ///< Number of running tasks
    uint32_t csi_packets_processed; ///< Total CSI packets processed
    uint32_t http_requests_sent;    ///< Total HTTP requests sent
    uint32_t http_errors;           ///< HTTP transmission errors
    char firmware_version[32];      ///< Current firmware version
} http_system_metrics_t;

/**
 * @brief Device status structure for heartbeat
 */
typedef struct {
    char device_id[64];             ///< Device identifier
    uint64_t timestamp;             ///< Current timestamp
    char status[32];                ///< Status: "online", "offline", "error"
    uint32_t uptime_sec;            ///< System uptime
    char ip_address[16];            ///< Current IP address
    int8_t wifi_rssi;               ///< WiFi signal strength
    char error_message[128];        ///< Last error message (if any)
} http_heartbeat_t;

/**
 * @brief Initialize HTTP telemetry client
 * 
 * @param config Telemetry configuration
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_telemetry_init(const http_telemetry_config_t *config);

/**
 * @brief Start HTTP telemetry client
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_telemetry_start(void);

/**
 * @brief Stop HTTP telemetry client
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_telemetry_stop(void);

/**
 * @brief Check if telemetry client is running
 * 
 * @return true if running, false otherwise
 */
bool http_telemetry_is_running(void);

/**
 * @brief Send CSI data via HTTP POST
 * 
 * @param csi_data CSI payload to transmit
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_telemetry_send_csi_data(const http_csi_payload_t *csi_data);

/**
 * @brief Send system metrics via HTTP POST
 * 
 * @param metrics System metrics to transmit
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_telemetry_send_system_metrics(const http_system_metrics_t *metrics);

/**
 * @brief Send device heartbeat via HTTP POST
 * 
 * @param heartbeat Heartbeat data to transmit
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_telemetry_send_heartbeat(const http_heartbeat_t *heartbeat);

/**
 * @brief Send device alert via HTTP POST
 * 
 * @param device_id Device identifier
 * @param alert_level Alert level: "info", "warning", "error", "critical"
 * @param component Component name (e.g., "csi", "memory", "wifi")
 * @param message Alert message
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_telemetry_send_alert(const char *device_id, const char *alert_level, 
                                   const char *component, const char *message);

/**
 * @brief Get HTTP telemetry statistics
 * 
 * @param requests_sent Total HTTP requests sent
 * @param requests_failed Total failed HTTP requests
 * @param bytes_sent Total bytes transmitted
 * @return ESP_OK on success, error code on failure
 */
esp_err_t http_telemetry_get_stats(uint32_t *requests_sent, uint32_t *requests_failed, 
                                  uint32_t *bytes_sent);

/**
 * @brief Configure HTTP endpoints for different data types
 * 
 * Default endpoints:
 * - CSI data: POST /api/csi/data
 * - System metrics: POST /api/system/metrics  
 * - Heartbeat: POST /api/device/heartbeat
 * - Alerts: POST /api/device/alert
 */
#define HTTP_ENDPOINT_CSI_DATA      "/api/csi/data"
#define HTTP_ENDPOINT_SYSTEM_METRICS "/api/system/metrics"
#define HTTP_ENDPOINT_HEARTBEAT     "/api/device/heartbeat"  
#define HTTP_ENDPOINT_ALERT         "/api/device/alert"

#ifdef __cplusplus
}
#endif