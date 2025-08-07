/**
 * @file mqtt_client_wrapper.c
 * @brief MQTT client wrapper implementation
 * 
 * Provides MQTT connectivity for publishing CSI data and receiving configuration updates.
 * Includes SSL/TLS support, automatic reconnection, and comprehensive error handling.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_tls.h>
#include <mqtt_client.h>
#include <cJSON.h>

#include "mqtt_client_wrapper.h"

static const char *TAG = "MQTT_CLIENT";

// Event bits for MQTT connection status
#define MQTT_CONNECTED_BIT      BIT0
#define MQTT_DISCONNECTED_BIT   BIT1

// Maximum retry attempts for reconnection
#define MAX_RETRY_ATTEMPTS      10
#define RETRY_DELAY_MS         5000

// Internal state structure
typedef struct {
    esp_mqtt_client_handle_t client;
    mqtt_config_t config;
    mqtt_stats_t stats;
    EventGroupHandle_t event_group;
    SemaphoreHandle_t mutex;
    mqtt_message_callback_t message_callback;
    void *callback_user_ctx;
    bool initialized;
    bool connected;
    uint32_t retry_count;
    TaskHandle_t reconnect_task;
} mqtt_client_state_t;

static mqtt_client_state_t s_mqtt_state = {0};

// Forward declarations
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void mqtt_reconnect_task(void *pvParameters);
static esp_err_t mqtt_publish_internal(const char *topic, const char *data, int data_len, int qos, int retain);
static char* csi_data_to_json(const csi_data_t *csi_data);
static void update_connection_stats(bool connected);

/**
 * @brief Initialize MQTT client
 */
esp_err_t mqtt_client_init(const mqtt_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mqtt_state.initialized) {
        ESP_LOGW(TAG, "MQTT client already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing MQTT client");

    // Create synchronization objects
    s_mqtt_state.mutex = xSemaphoreCreateMutex();
    if (!s_mqtt_state.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    s_mqtt_state.event_group = xEventGroupCreate();
    if (!s_mqtt_state.event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(s_mqtt_state.mutex);
        return ESP_ERR_NO_MEM;
    }

    // Copy configuration
    memcpy(&s_mqtt_state.config, config, sizeof(mqtt_config_t));

    // Initialize statistics
    memset(&s_mqtt_state.stats, 0, sizeof(mqtt_stats_t));
    
    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.hostname = s_mqtt_state.config.broker_url,
            .address.port = s_mqtt_state.config.port,
            .verification.use_global_ca_store = s_mqtt_state.config.ssl_enabled,
            .verification.skip_cert_common_name_check = !s_mqtt_state.config.ssl_enabled,
        },
        .credentials = {
            .username = s_mqtt_state.config.username,
            .authentication.password = s_mqtt_state.config.password,
            .client_id = s_mqtt_state.config.client_id,
        },
        .session = {
            .keepalive = s_mqtt_state.config.keepalive,
            .disable_clean_session = false,
        },
        .network = {
            .disable_auto_reconnect = true, // We handle reconnection manually
            .timeout_ms = 10000,
            .refresh_connection_after_ms = 0,
        }
    };

    // Configure SSL/TLS if enabled
    if (s_mqtt_state.config.ssl_enabled) {
        if (s_mqtt_state.config.port == 0) {
            mqtt_cfg.broker.address.port = 8883; // Default MQTT SSL port
        }
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    } else {
        if (s_mqtt_state.config.port == 0) {
            mqtt_cfg.broker.address.port = 1883; // Default MQTT port
        }
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    }

    // Create MQTT client
    s_mqtt_state.client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_mqtt_state.client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        vSemaphoreDelete(s_mqtt_state.mutex);
        vEventGroupDelete(s_mqtt_state.event_group);
        return ESP_FAIL;
    }

    // Register event handler
    esp_err_t err = esp_mqtt_client_register_event(s_mqtt_state.client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(s_mqtt_state.client);
        vSemaphoreDelete(s_mqtt_state.mutex);
        vEventGroupDelete(s_mqtt_state.event_group);
        return err;
    }

    s_mqtt_state.initialized = true;
    s_mqtt_state.connected = false;
    s_mqtt_state.retry_count = 0;

    ESP_LOGI(TAG, "MQTT client initialized successfully");
    return ESP_OK;
}

/**
 * @brief Start MQTT client
 */
esp_err_t mqtt_client_start(void)
{
    if (!s_mqtt_state.initialized) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting MQTT client");

    esp_err_t err = esp_mqtt_client_start(s_mqtt_state.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    // Create reconnection task
    xTaskCreate(
        mqtt_reconnect_task,
        "mqtt_reconnect",
        4096,
        NULL,
        5,
        &s_mqtt_state.reconnect_task
    );

    ESP_LOGI(TAG, "MQTT client started successfully");
    return ESP_OK;
}

/**
 * @brief Stop MQTT client
 */
esp_err_t mqtt_client_stop(void)
{
    if (!s_mqtt_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping MQTT client");

    // Stop reconnection task
    if (s_mqtt_state.reconnect_task) {
        vTaskDelete(s_mqtt_state.reconnect_task);
        s_mqtt_state.reconnect_task = NULL;
    }

    esp_err_t err = esp_mqtt_client_stop(s_mqtt_state.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    s_mqtt_state.connected = false;
    xEventGroupClearBits(s_mqtt_state.event_group, MQTT_CONNECTED_BIT);
    xEventGroupSetBits(s_mqtt_state.event_group, MQTT_DISCONNECTED_BIT);

    ESP_LOGI(TAG, "MQTT client stopped");
    return ESP_OK;
}

/**
 * @brief Deinitialize MQTT client
 */
esp_err_t mqtt_client_deinit(void)
{
    if (!s_mqtt_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing MQTT client");

    // Stop client first
    mqtt_client_stop();

    // Destroy client
    esp_err_t err = esp_mqtt_client_destroy(s_mqtt_state.client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to destroy MQTT client: %s", esp_err_to_name(err));
    }

    // Clean up resources
    if (s_mqtt_state.mutex) {
        vSemaphoreDelete(s_mqtt_state.mutex);
        s_mqtt_state.mutex = NULL;
    }

    if (s_mqtt_state.event_group) {
        vEventGroupDelete(s_mqtt_state.event_group);
        s_mqtt_state.event_group = NULL;
    }

    memset(&s_mqtt_state, 0, sizeof(s_mqtt_state));

    ESP_LOGI(TAG, "MQTT client deinitialized");
    return ESP_OK;
}

/**
 * @brief Check if MQTT client is connected
 */
bool mqtt_client_is_connected(void)
{
    return s_mqtt_state.connected;
}

/**
 * @brief Publish CSI data
 */
esp_err_t mqtt_client_publish_csi_data(const csi_data_t *csi_data)
{
    if (!csi_data || !csi_data->valid) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mqtt_state.connected) {
        ESP_LOGD(TAG, "MQTT not connected, skipping CSI data publish");
        return ESP_ERR_INVALID_STATE;
    }

    // Convert CSI data to JSON
    char *json_data = csi_data_to_json(csi_data);
    if (!json_data) {
        ESP_LOGE(TAG, "Failed to serialize CSI data to JSON");
        s_mqtt_state.stats.publish_errors++;
        return ESP_ERR_NO_MEM;
    }

    // Create topic
    char topic[128];
    snprintf(topic, sizeof(topic), "%s/csi_data", s_mqtt_state.config.topic_prefix);

    // Publish data
    esp_err_t err = mqtt_publish_internal(topic, json_data, strlen(json_data), 
                                         s_mqtt_state.config.qos, s_mqtt_state.config.retain);
    
    free(json_data);
    
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "CSI data published successfully");
    } else {
        ESP_LOGE(TAG, "Failed to publish CSI data: %s", esp_err_to_name(err));
        s_mqtt_state.stats.publish_errors++;
    }

    return err;
}

/**
 * @brief Publish generic message
 */
esp_err_t mqtt_client_publish(const char *topic, const char *data, int data_len, int qos, int retain)
{
    if (!topic || !data || data_len < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mqtt_state.connected) {
        ESP_LOGD(TAG, "MQTT not connected, skipping publish");
        return ESP_ERR_INVALID_STATE;
    }

    return mqtt_publish_internal(topic, data, data_len, qos, retain);
}

/**
 * @brief Subscribe to topic
 */
esp_err_t mqtt_client_subscribe(const char *topic, int qos)
{
    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mqtt_state.connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot subscribe to: %s", topic);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Subscribing to topic: %s (QoS: %d)", topic, qos);

    int msg_id = esp_mqtt_client_subscribe(s_mqtt_state.client, (char *)topic, qos);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to subscribe to topic: %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully subscribed to topic: %s (msg_id: %d)", topic, msg_id);
    return ESP_OK;
}

/**
 * @brief Unsubscribe from topic
 */
esp_err_t mqtt_client_unsubscribe(const char *topic)
{
    if (!topic) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mqtt_state.connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot unsubscribe from: %s", topic);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Unsubscribing from topic: %s", topic);

    int msg_id = esp_mqtt_client_unsubscribe(s_mqtt_state.client, topic);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to unsubscribe from topic: %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Successfully unsubscribed from topic: %s (msg_id: %d)", topic, msg_id);
    return ESP_OK;
}

/**
 * @brief Register message callback
 */
esp_err_t mqtt_client_register_callback(mqtt_message_callback_t callback, void *user_ctx)
{
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mqtt_state.mutex, portMAX_DELAY);
    s_mqtt_state.message_callback = callback;
    s_mqtt_state.callback_user_ctx = user_ctx;
    xSemaphoreGive(s_mqtt_state.mutex);

    ESP_LOGI(TAG, "Message callback registered");
    return ESP_OK;
}

/**
 * @brief Get MQTT client statistics
 */
esp_err_t mqtt_client_get_stats(mqtt_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mqtt_state.mutex, portMAX_DELAY);
    memcpy(stats, &s_mqtt_state.stats, sizeof(mqtt_stats_t));
    xSemaphoreGive(s_mqtt_state.mutex);

    return ESP_OK;
}

/**
 * @brief Reset MQTT client statistics
 */
esp_err_t mqtt_client_reset_stats(void)
{
    xSemaphoreTake(s_mqtt_state.mutex, portMAX_DELAY);
    memset(&s_mqtt_state.stats, 0, sizeof(mqtt_stats_t));
    s_mqtt_state.stats.connected = s_mqtt_state.connected;
    xSemaphoreGive(s_mqtt_state.mutex);

    ESP_LOGI(TAG, "Statistics reset");
    return ESP_OK;
}

// ===== INTERNAL FUNCTIONS =====

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            s_mqtt_state.connected = true;
            s_mqtt_state.retry_count = 0;
            xEventGroupClearBits(s_mqtt_state.event_group, MQTT_DISCONNECTED_BIT);
            xEventGroupSetBits(s_mqtt_state.event_group, MQTT_CONNECTED_BIT);
            update_connection_stats(true);
            
            // Subscribe to configuration topic
            char config_topic[128];
            snprintf(config_topic, sizeof(config_topic), "%s/config", s_mqtt_state.config.topic_prefix);
            mqtt_client_subscribe(config_topic, 1);
            
            // Publish device status
            char status_topic[128];
            snprintf(status_topic, sizeof(status_topic), "%s/status", s_mqtt_state.config.topic_prefix);
            mqtt_publish_internal(status_topic, "online", 6, 1, true);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            s_mqtt_state.connected = false;
            xEventGroupClearBits(s_mqtt_state.event_group, MQTT_CONNECTED_BIT);
            xEventGroupSetBits(s_mqtt_state.event_group, MQTT_DISCONNECTED_BIT);
            update_connection_stats(false);
            s_mqtt_state.stats.connection_errors++;
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscribed (msg_id: %d)", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT unsubscribed (msg_id: %d)", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT published (msg_id: %d)", event->msg_id);
            s_mqtt_state.stats.messages_sent++;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT data received (topic: %.*s)", event->topic_len, event->topic);
            s_mqtt_state.stats.messages_received++;
            
            // Call user callback if registered
            xSemaphoreTake(s_mqtt_state.mutex, portMAX_DELAY);
            if (s_mqtt_state.message_callback) {
                char topic_buffer[256];
                int topic_len = event->topic_len < sizeof(topic_buffer) - 1 ? 
                               event->topic_len : sizeof(topic_buffer) - 1;
                memcpy(topic_buffer, event->topic, topic_len);
                topic_buffer[topic_len] = '\0';
                
                s_mqtt_state.message_callback(topic_buffer, event->data, event->data_len, 
                                            s_mqtt_state.callback_user_ctx);
            }
            xSemaphoreGive(s_mqtt_state.mutex);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error occurred");
            s_mqtt_state.stats.connection_errors++;
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "Last error code reported from esp-tls: 0x%x", 
                        event->error_handle->esp_tls_last_esp_err);
                ESP_LOGE(TAG, "Last tls stack error number: 0x%x", 
                        event->error_handle->esp_tls_stack_err);
                ESP_LOGE(TAG, "Last captured errno : %d (%s)", 
                        event->error_handle->esp_transport_sock_errno,
                        strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGE(TAG, "Connection refused error: 0x%x", 
                        event->error_handle->connect_return_code);
            }
            break;

        default:
            ESP_LOGD(TAG, "Other MQTT event id: %d", event->event_id);
            break;
    }
}

/**
 * @brief MQTT reconnection task
 */
static void mqtt_reconnect_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT reconnection task started");

    while (1) {
        // Wait for disconnection event
        xEventGroupWaitBits(s_mqtt_state.event_group, MQTT_DISCONNECTED_BIT, 
                           pdFALSE, pdFALSE, portMAX_DELAY);

        if (!s_mqtt_state.initialized) {
            ESP_LOGI(TAG, "MQTT client deinitialized, stopping reconnection task");
            break;
        }

        if (s_mqtt_state.retry_count >= MAX_RETRY_ATTEMPTS) {
            ESP_LOGE(TAG, "Max retry attempts reached, stopping reconnection attempts");
            vTaskDelay(pdMS_TO_TICKS(60000)); // Wait 1 minute before resetting counter
            s_mqtt_state.retry_count = 0;
            continue;
        }

        // Wait before retry
        ESP_LOGI(TAG, "Attempting MQTT reconnection (attempt %d/%d)", 
                (int)(s_mqtt_state.retry_count + 1), (int)MAX_RETRY_ATTEMPTS);
        
        vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));

        if (!s_mqtt_state.connected) {
            esp_err_t err = esp_mqtt_client_reconnect(s_mqtt_state.client);
            s_mqtt_state.retry_count++;
            
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "MQTT reconnection failed: %s", esp_err_to_name(err));
                s_mqtt_state.stats.connection_errors++;
            }
        }
    }

    ESP_LOGI(TAG, "MQTT reconnection task stopped");
    vTaskDelete(NULL);
}

/**
 * @brief Internal publish function with error handling
 */
static esp_err_t mqtt_publish_internal(const char *topic, const char *data, int data_len, int qos, int retain)
{
    if (!s_mqtt_state.connected) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(s_mqtt_state.client, topic, data, data_len, qos, retain);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "Failed to publish to topic: %s", topic);
        s_mqtt_state.stats.publish_errors++;
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Published to topic: %s (msg_id: %d, len: %d)", topic, msg_id, data_len);
    return ESP_OK;
}

/**
 * @brief Convert CSI data to JSON string
 */
static char* csi_data_to_json(const csi_data_t *csi_data)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) {
        return NULL;
    }

    // Add timestamp
    cJSON_AddNumberToObject(json, "timestamp", csi_data->timestamp);
    
    // Add MAC address
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             csi_data->mac[0], csi_data->mac[1], csi_data->mac[2],
             csi_data->mac[3], csi_data->mac[4], csi_data->mac[5]);
    cJSON_AddStringToObject(json, "mac", mac_str);
    
    // Add basic information
    cJSON_AddNumberToObject(json, "rssi", csi_data->rssi);
    cJSON_AddNumberToObject(json, "channel", csi_data->channel);
    cJSON_AddNumberToObject(json, "secondary_channel", csi_data->secondary_channel);
    cJSON_AddNumberToObject(json, "subcarrier_count", csi_data->subcarrier_count);
    
    // Add amplitude data if available
    if (csi_data->amplitude && csi_data->subcarrier_count > 0) {
        cJSON *amplitude_array = cJSON_CreateArray();
        for (int i = 0; i < csi_data->subcarrier_count; i++) {
            cJSON_AddItemToArray(amplitude_array, cJSON_CreateNumber(csi_data->amplitude[i]));
        }
        cJSON_AddItemToObject(json, "amplitude", amplitude_array);
    }
    
    // Add phase data if available
    if (csi_data->phase && csi_data->subcarrier_count > 0) {
        cJSON *phase_array = cJSON_CreateArray();
        for (int i = 0; i < csi_data->subcarrier_count; i++) {
            cJSON_AddItemToArray(phase_array, cJSON_CreateNumber(csi_data->phase[i]));
        }
        cJSON_AddItemToObject(json, "phase", phase_array);
    }

    char *json_string = cJSON_Print(json);
    cJSON_Delete(json);
    
    return json_string;
}

/**
 * @brief Update connection statistics
 */
static void update_connection_stats(bool connected)
{
    xSemaphoreTake(s_mqtt_state.mutex, portMAX_DELAY);
    s_mqtt_state.stats.connected = connected;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    s_mqtt_state.stats.last_activity = tv.tv_sec * 1000000ULL + tv.tv_usec;
    
    xSemaphoreGive(s_mqtt_state.mutex);
}