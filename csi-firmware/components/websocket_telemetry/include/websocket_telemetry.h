/**
 * @file websocket_telemetry.h
 * @brief WebSocket streaming telemetry for real-time CSI positioning data
 * 
 * Combines HTTP for commands/config with WebSocket for high-frequency streaming
 * Ideal for real-time CSI data transmission to Docker server
 */

#pragma once

#include <esp_err.h>
#include <esp_websocket_client.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WebSocket telemetry configuration
 */
typedef struct {
    char server_url[256];           ///< WebSocket server URL (e.g., "ws://192.168.1.100:3000/ws")
    char device_id[64];             ///< Unique device identifier
    char auth_token[128];           ///< Authentication token
    uint32_t reconnect_timeout_ms;  ///< Reconnection timeout
    uint32_t keepalive_interval_sec; ///< WebSocket ping interval
    bool auto_reconnect;            ///< Enable automatic reconnection
    uint32_t buffer_size;           ///< WebSocket buffer size
} websocket_telemetry_config_t;

/**
 * @brief Streaming CSI data packet for WebSocket
 * Optimized for high-frequency transmission
 */
typedef struct {
    uint64_t timestamp;             ///< Microsecond timestamp
    uint8_t mac_address[6];         ///< Target device MAC
    int8_t rssi;                    ///< Signal strength (dBm)
    uint16_t channel;               ///< WiFi channel
    uint16_t data_length;           ///< CSI data length
    float *amplitude_data;          ///< CSI amplitude array
    float *phase_data;              ///< CSI phase array (optional)
} websocket_csi_packet_t;

/**
 * @brief Streaming system metrics for WebSocket
 */
typedef struct {
    uint64_t timestamp;             ///< Current timestamp
    uint32_t free_heap_bytes;       ///< Available heap memory
    uint8_t cpu_usage_percent;      ///< CPU usage percentage
    int8_t wifi_rssi;               ///< WiFi connection RSSI
    uint16_t csi_packets_sent;      ///< CSI packets in this interval
    uint16_t websocket_latency_ms;  ///< WebSocket round-trip latency
} websocket_metrics_t;

/**
 * @brief WebSocket message types
 */
typedef enum {
    WS_MSG_CSI_DATA = 1,            ///< Real-time CSI positioning data
    WS_MSG_SYSTEM_METRICS = 2,      ///< System performance metrics
    WS_MSG_HEARTBEAT = 3,           ///< Device heartbeat/keepalive
    WS_MSG_ALERT = 4,               ///< System alerts and warnings
    WS_MSG_BATCH_CSI = 5,           ///< Batched CSI data for efficiency
    WS_MSG_PING = 6,                ///< Connection health check
    WS_MSG_PONG = 7                 ///< Ping response
} websocket_msg_type_t;

/**
 * @brief WebSocket message header
 */
typedef struct {
    uint8_t msg_type;               ///< Message type (websocket_msg_type_t)
    uint8_t device_id_len;          ///< Device ID length
    uint16_t payload_len;           ///< Payload length in bytes
    uint32_t sequence_num;          ///< Sequence number for ordering
} __attribute__((packed)) websocket_msg_header_t;

/**
 * @brief WebSocket client event callback
 * 
 * @param event WebSocket client event
 * @param user_data User data pointer
 */
typedef void (*websocket_event_callback_t)(esp_websocket_event_data_t *event, void *user_data);

/**
 * @brief Initialize WebSocket telemetry client
 * 
 * @param config WebSocket configuration
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_init(const websocket_telemetry_config_t *config);

/**
 * @brief Start WebSocket telemetry client
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_start(void);

/**
 * @brief Stop WebSocket telemetry client
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_stop(void);

/**
 * @brief Check if WebSocket client is connected
 * 
 * @return true if connected, false otherwise
 */
bool websocket_telemetry_is_connected(void);

/**
 * @brief Send CSI data via WebSocket (real-time streaming)
 * 
 * @param csi_packet CSI data packet to stream
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_send_csi_data(const websocket_csi_packet_t *csi_packet);

/**
 * @brief Send batched CSI data via WebSocket (efficient bulk transmission)
 * 
 * @param csi_packets Array of CSI data packets
 * @param packet_count Number of packets in array
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_send_csi_batch(const websocket_csi_packet_t *csi_packets, 
                                             uint16_t packet_count);

/**
 * @brief Send system metrics via WebSocket
 * 
 * @param metrics System metrics to stream
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_send_metrics(const websocket_metrics_t *metrics);

/**
 * @brief Send heartbeat via WebSocket
 * 
 * @param status Device status string
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_send_heartbeat(const char *status);

/**
 * @brief Send alert via WebSocket
 * 
 * @param alert_level Alert level: "info", "warning", "error", "critical"
 * @param component Component name
 * @param message Alert message
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_send_alert(const char *alert_level, const char *component, 
                                         const char *message);

/**
 * @brief Register event callback for WebSocket events
 * 
 * @param callback Event callback function
 * @param user_data User data pointer passed to callback
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_register_callback(websocket_event_callback_t callback, void *user_data);

/**
 * @brief Get WebSocket connection statistics
 * 
 * @param messages_sent Total messages sent
 * @param messages_received Total messages received
 * @param bytes_sent Total bytes transmitted
 * @param connection_errors Connection error count
 * @param avg_latency_ms Average round-trip latency
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_get_stats(uint32_t *messages_sent, uint32_t *messages_received,
                                       uint32_t *bytes_sent, uint32_t *connection_errors,
                                       uint32_t *avg_latency_ms);

/**
 * @brief Send ping to measure connection latency
 * 
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_ping(void);

/**
 * @brief Enable/disable data compression for WebSocket messages
 * 
 * @param enable true to enable compression, false to disable
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_set_compression(bool enable);

/**
 * @brief Set streaming mode for continuous CSI data transmission
 * 
 * @param enable true to enable streaming mode, false for on-demand
 * @param stream_rate_hz Streaming rate in Hz (1-1000)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t websocket_telemetry_set_streaming_mode(bool enable, uint16_t stream_rate_hz);

/**
 * @brief WebSocket telemetry endpoints
 */
#define WS_ENDPOINT_TELEMETRY       "/ws/telemetry"
#define WS_ENDPOINT_CSI_STREAM      "/ws/csi-stream"
#define WS_ENDPOINT_METRICS         "/ws/metrics"

/**
 * @brief Default configuration values
 */
#define WS_DEFAULT_KEEPALIVE_SEC    30
#define WS_DEFAULT_RECONNECT_MS     5000
#define WS_DEFAULT_BUFFER_SIZE      1024
#define WS_MAX_CSI_BATCH_SIZE       50

#ifdef __cplusplus
}
#endif