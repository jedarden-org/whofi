/**
 * @file csi_collector.c
 * @brief CSI collector implementation
 */

#include "csi_collector.h"
#include "csi_filter.h"
#include "csi_buffer.h"
#include <string.h>
#include <math.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static const char *TAG = "CSI_COLLECTOR";

/**
 * @brief CSI collector context structure
 */
typedef struct {
    csi_collector_config_t config;     ///< Configuration
    csi_collector_stats_t stats;       ///< Statistics
    QueueHandle_t data_queue;          ///< Data queue
    TaskHandle_t process_task;         ///< Processing task handle
    SemaphoreHandle_t mutex;           ///< Mutex for thread safety
    csi_data_callback_t callback;     ///< Data callback function
    void *callback_ctx;                ///< Callback context
    bool running;                      ///< Running state
    bool initialized;                  ///< Initialization state
    csi_buffer_handle_t buffer_handle; ///< Buffer handle
    csi_filter_handle_t filter_handle; ///< Filter handle
} csi_collector_ctx_t;

static csi_collector_ctx_t s_ctx = {0};

/**
 * @brief CSI data processing task
 * @param pvParameters Task parameters
 */
static void csi_process_task(void *pvParameters);

/**
 * @brief Wi-Fi CSI receive callback
 * @param ctx Context pointer
 * @param data CSI data from Wi-Fi driver
 */
static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *data);

/**
 * @brief Process raw CSI data
 * @param raw_data Raw CSI data from Wi-Fi
 * @param processed_data Processed CSI data output
 * @return ESP_OK on success, error code on failure
 */
static esp_err_t process_csi_data(const wifi_csi_info_t *raw_data, csi_data_t *processed_data);

esp_err_t csi_collector_init(const csi_collector_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // Validate configuration
    if (config->sample_rate == 0 || config->sample_rate > 100) {
        ESP_LOGE(TAG, "Invalid sample rate: %d", config->sample_rate);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->buffer_size < 256 || config->buffer_size > 4096) {
        ESP_LOGE(TAG, "Invalid buffer size: %d", config->buffer_size);
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize context
    memset(&s_ctx, 0, sizeof(s_ctx));
    memcpy(&s_ctx.config, config, sizeof(csi_collector_config_t));

    // Create mutex
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_ctx.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Create data queue
    s_ctx.data_queue = xQueueCreate(10, sizeof(csi_data_t));
    if (!s_ctx.data_queue) {
        ESP_LOGE(TAG, "Failed to create data queue");
        vSemaphoreDelete(s_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    // Initialize buffer
    esp_err_t err = csi_buffer_init(&s_ctx.buffer_handle, config->buffer_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize buffer: %s", esp_err_to_name(err));
        vQueueDelete(s_ctx.data_queue);
        vSemaphoreDelete(s_ctx.mutex);
        return err;
    }

    // Initialize filter if enabled
    if (config->filter_enabled) {
        csi_filter_config_t filter_config = {
            .threshold = config->filter_threshold,
            .enable_amplitude_filter = config->enable_amplitude,
            .enable_phase_filter = config->enable_phase
        };
        
        err = csi_filter_init(&s_ctx.filter_handle, &filter_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize filter: %s", esp_err_to_name(err));
            csi_buffer_deinit(s_ctx.buffer_handle);
            vQueueDelete(s_ctx.data_queue);
            vSemaphoreDelete(s_ctx.mutex);
            return err;
        }
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "CSI collector initialized successfully");
    
    return ESP_OK;
}

esp_err_t csi_collector_start(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ctx.running) {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    // Create processing task
    BaseType_t ret = xTaskCreate(
        csi_process_task,
        "csi_process",
        4096,
        NULL,
        5,
        &s_ctx.process_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create processing task");
        return ESP_ERR_NO_MEM;
    }

    // Register Wi-Fi CSI callback
    esp_err_t err = esp_wifi_set_csi_rx_cb(wifi_csi_rx_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register CSI callback: %s", esp_err_to_name(err));
        vTaskDelete(s_ctx.process_task);
        s_ctx.process_task = NULL;
        return err;
    }

    // Configure and enable CSI
    wifi_csi_config_t csi_config = {
        .lltf_en = 1,
        .htltf_en = 1,
        .stbc_htltf2_en = 0,
        .ltf_merge_en = 1,
        .channel_filter_en = 0,
        .manu_scale = 1,
        .shift = 0
    };

    err = esp_wifi_set_csi_config(&csi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CSI: %s", esp_err_to_name(err));
        esp_wifi_set_csi_rx_cb(NULL, NULL);
        vTaskDelete(s_ctx.process_task);
        s_ctx.process_task = NULL;
        return err;
    }

    err = esp_wifi_set_csi(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable CSI: %s", esp_err_to_name(err));
        esp_wifi_set_csi_rx_cb(NULL, NULL);
        vTaskDelete(s_ctx.process_task);
        s_ctx.process_task = NULL;
        return err;
    }

    s_ctx.running = true;
    ESP_LOGI(TAG, "CSI collector started");
    
    return ESP_OK;
}

esp_err_t csi_collector_stop(void)
{
    if (!s_ctx.running) {
        ESP_LOGW(TAG, "Not running");
        return ESP_OK;
    }

    // Disable CSI
    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(NULL, NULL);

    // Stop processing task
    if (s_ctx.process_task) {
        vTaskDelete(s_ctx.process_task);
        s_ctx.process_task = NULL;
    }

    s_ctx.running = false;
    ESP_LOGI(TAG, "CSI collector stopped");
    
    return ESP_OK;
}

esp_err_t csi_collector_deinit(void)
{
    if (s_ctx.running) {
        csi_collector_stop();
    }

    if (!s_ctx.initialized) {
        return ESP_OK;
    }

    // Clean up resources
    if (s_ctx.filter_handle) {
        csi_filter_deinit(s_ctx.filter_handle);
    }

    if (s_ctx.buffer_handle) {
        csi_buffer_deinit(s_ctx.buffer_handle);
    }

    if (s_ctx.data_queue) {
        vQueueDelete(s_ctx.data_queue);
    }

    if (s_ctx.mutex) {
        vSemaphoreDelete(s_ctx.mutex);
    }

    s_ctx.initialized = false;
    ESP_LOGI(TAG, "CSI collector deinitialized");
    
    return ESP_OK;
}

bool csi_collector_is_running(void)
{
    return s_ctx.running;
}

esp_err_t csi_collector_get_data(csi_data_t *csi_data, uint32_t timeout_ms)
{
    if (!csi_data) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.running) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    if (xQueueReceive(s_ctx.data_queue, csi_data, timeout_ticks) == pdTRUE) {
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

esp_err_t csi_collector_register_callback(csi_data_callback_t callback, void *user_ctx)
{
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.callback = callback;
    s_ctx.callback_ctx = user_ctx;
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

esp_err_t csi_collector_unregister_callback(void)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.callback = NULL;
    s_ctx.callback_ctx = NULL;
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

esp_err_t csi_collector_get_stats(csi_collector_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    memcpy(stats, &s_ctx.stats, sizeof(csi_collector_stats_t));
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

esp_err_t csi_collector_reset_stats(void)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    memset(&s_ctx.stats, 0, sizeof(csi_collector_stats_t));
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

esp_err_t csi_collector_update_config(const csi_collector_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Validate new configuration
    if (config->sample_rate == 0 || config->sample_rate > 100) {
        ESP_LOGE(TAG, "Invalid sample rate: %d", config->sample_rate);
        return ESP_ERR_INVALID_ARG;
    }

    if (config->buffer_size < 256 || config->buffer_size > 4096) {
        ESP_LOGE(TAG, "Invalid buffer size: %d", config->buffer_size);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    // Update configuration
    memcpy(&s_ctx.config, config, sizeof(csi_collector_config_t));

    // Update filter if configuration changed
    if (s_ctx.filter_handle && config->filter_enabled) {
        csi_filter_config_t filter_config = {
            .threshold = config->filter_threshold,
            .enable_amplitude_filter = config->enable_amplitude,
            .enable_phase_filter = config->enable_phase
        };
        
        // Reinitialize filter with new config
        csi_filter_deinit(s_ctx.filter_handle);
        esp_err_t err = csi_filter_init(&s_ctx.filter_handle, &filter_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update filter configuration");
            xSemaphoreGive(s_ctx.mutex);
            return err;
        }
    }

    xSemaphoreGive(s_ctx.mutex);

    ESP_LOGI(TAG, "Configuration updated successfully");
    return ESP_OK;
}

esp_err_t csi_collector_get_config(csi_collector_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Config pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    memcpy(config, &s_ctx.config, sizeof(csi_collector_config_t));
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

static void csi_process_task(void *pvParameters)
{
    csi_data_t csi_data;
    
    ESP_LOGI(TAG, "CSI processing task started");
    
    while (s_ctx.running) {
        // Process data from buffer
        if (csi_buffer_get_data(s_ctx.buffer_handle, &csi_data, pdMS_TO_TICKS(100)) == ESP_OK) {
            // Apply filtering if enabled
            if (s_ctx.config.filter_enabled && s_ctx.filter_handle) {
                if (csi_filter_process(s_ctx.filter_handle, &csi_data) != ESP_OK) {
                    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
                    s_ctx.stats.packets_dropped++;
                    xSemaphoreGive(s_ctx.mutex);
                    continue;
                }
                
                xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
                s_ctx.stats.filter_hits++;
                xSemaphoreGive(s_ctx.mutex);
            }
            
            // Update statistics
            xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
            s_ctx.stats.packets_processed++;
            s_ctx.stats.average_rssi = (s_ctx.stats.average_rssi * 0.9f) + (csi_data.rssi * 0.1f);
            s_ctx.stats.last_packet_time = csi_data.timestamp;
            xSemaphoreGive(s_ctx.mutex);
            
            // Send to queue
            if (xQueueSend(s_ctx.data_queue, &csi_data, 0) != pdTRUE) {
                xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
                s_ctx.stats.buffer_overruns++;
                xSemaphoreGive(s_ctx.mutex);
            }
            
            // Call callback if registered
            if (s_ctx.callback) {
                s_ctx.callback(&csi_data, s_ctx.callback_ctx);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000 / s_ctx.config.sample_rate));
    }
    
    ESP_LOGI(TAG, "CSI processing task ended");
    vTaskDelete(NULL);
}

static void wifi_csi_rx_cb(void *ctx, wifi_csi_info_t *data)
{
    if (!data || !s_ctx.running) {
        return;
    }

    csi_data_t processed_data;
    if (process_csi_data(data, &processed_data) == ESP_OK) {
        if (csi_buffer_put_data(s_ctx.buffer_handle, &processed_data) != ESP_OK) {
            xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
            s_ctx.stats.packets_dropped++;
            xSemaphoreGive(s_ctx.mutex);
        }
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.stats.packets_received++;
    xSemaphoreGive(s_ctx.mutex);
}

static esp_err_t process_csi_data(const wifi_csi_info_t *raw_data, csi_data_t *processed_data)
{
    if (!raw_data || !processed_data) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize processed data structure
    memset(processed_data, 0, sizeof(csi_data_t));
    
    // Copy basic information
    processed_data->timestamp = esp_timer_get_time();
    memcpy(processed_data->mac, raw_data->mac, 6);
    processed_data->rssi = raw_data->rx_ctrl.rssi;
    processed_data->channel = raw_data->rx_ctrl.channel;
    processed_data->secondary_channel = raw_data->rx_ctrl.secondary_channel;
    processed_data->len = raw_data->len;
    processed_data->valid = true;

    // Allocate memory for raw data
    processed_data->data = malloc(raw_data->len);
    if (!processed_data->data) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(processed_data->data, raw_data->buf, raw_data->len);

    // Calculate number of subcarriers
    processed_data->subcarrier_count = raw_data->len / 2; // Assuming complex data (I/Q)
    if (processed_data->subcarrier_count > CSI_MAX_SUBCARRIERS) {
        processed_data->subcarrier_count = CSI_MAX_SUBCARRIERS;
    }

    // Process amplitude and phase if requested
    if (s_ctx.config.enable_amplitude) {
        processed_data->amplitude = malloc(processed_data->subcarrier_count * sizeof(float));
        if (processed_data->amplitude) {
            for (int i = 0; i < processed_data->subcarrier_count; i++) {
                int8_t real = raw_data->buf[i * 2];
                int8_t imag = raw_data->buf[i * 2 + 1];
                processed_data->amplitude[i] = sqrtf(real * real + imag * imag);
            }
        }
    }

    if (s_ctx.config.enable_phase) {
        processed_data->phase = malloc(processed_data->subcarrier_count * sizeof(float));
        if (processed_data->phase) {
            for (int i = 0; i < processed_data->subcarrier_count; i++) {
                int8_t real = raw_data->buf[i * 2];
                int8_t imag = raw_data->buf[i * 2 + 1];
                processed_data->phase[i] = atan2f(imag, real);
            }
        }
    }

    return ESP_OK;
}

void csi_collector_free_data(csi_data_t *csi_data)
{
    if (csi_data) {
        if (csi_data->data) {
            free(csi_data->data);
            csi_data->data = NULL;
        }
        if (csi_data->amplitude) {
            free(csi_data->amplitude);
            csi_data->amplitude = NULL;
        }
        if (csi_data->phase) {
            free(csi_data->phase);
            csi_data->phase = NULL;
        }
    }
}