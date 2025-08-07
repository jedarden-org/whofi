/**
 * @file ntp_sync.c
 * @brief NTP time synchronization implementation
 * 
 * Provides network time synchronization functionality for accurate timestamping
 * of CSI data. Includes multiple server support, drift compensation, and callbacks.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <inttypes.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_sntp.h>
#include <esp_attr.h>
#include <esp_timer.h>
#include <lwip/inet.h>

#include "ntp_sync.h"

static const char *TAG = "NTP_SYNC";

// Event bits for synchronization status
#define NTP_SYNC_BIT           BIT0
#define NTP_STOP_BIT           BIT1

// Default NTP servers
#define DEFAULT_NTP_SERVER1    "pool.ntp.org"
#define DEFAULT_NTP_SERVER2    "time.nist.gov"
#define DEFAULT_NTP_SERVER3    "time.google.com"

// Drift compensation parameters
#define DRIFT_COMPENSATION_SAMPLES  10
#define MAX_DRIFT_PPM              100.0f  // Maximum allowed drift in parts per million

// Internal state structure
typedef struct {
    ntp_config_t config;
    ntp_status_t status;
    SemaphoreHandle_t mutex;
    EventGroupHandle_t event_group;
    TaskHandle_t sync_task;
    ntp_sync_callback_t callback;
    void *callback_user_ctx;
    bool initialized;
    bool running;
    
    // Drift compensation data
    struct {
        int64_t system_times[DRIFT_COMPENSATION_SAMPLES];
        int64_t ntp_times[DRIFT_COMPENSATION_SAMPLES];
        int sample_count;
        int sample_index;
        float drift_ppm;
        int64_t last_correction_time;
    } drift_comp;
} ntp_sync_state_t;

static ntp_sync_state_t s_ntp_state = {0};

// Forward declarations
static void ntp_sync_task(void *pvParameters);
static void sntp_sync_time_callback(struct timeval *tv);
static esp_err_t ntp_force_sync_internal(void);
static void calculate_drift_compensation(void);
static int64_t get_system_time_us(void);
static int64_t apply_drift_correction(int64_t system_time);
static esp_err_t configure_sntp_servers(void);

/**
 * @brief Initialize NTP synchronization
 */
esp_err_t ntp_sync_init(const ntp_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid configuration");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ntp_state.initialized) {
        ESP_LOGW(TAG, "NTP sync already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing NTP synchronization");

    // Create synchronization objects
    s_ntp_state.mutex = xSemaphoreCreateMutex();
    if (!s_ntp_state.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    s_ntp_state.event_group = xEventGroupCreate();
    if (!s_ntp_state.event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(s_ntp_state.mutex);
        return ESP_ERR_NO_MEM;
    }

    // Copy configuration with defaults
    memcpy(&s_ntp_state.config, config, sizeof(ntp_config_t));
    
    // Set default servers if not specified
    if (strlen(s_ntp_state.config.server1) == 0) {
        strncpy(s_ntp_state.config.server1, DEFAULT_NTP_SERVER1, sizeof(s_ntp_state.config.server1) - 1);
    }
    if (strlen(s_ntp_state.config.server2) == 0) {
        strncpy(s_ntp_state.config.server2, DEFAULT_NTP_SERVER2, sizeof(s_ntp_state.config.server2) - 1);
    }
    if (strlen(s_ntp_state.config.server3) == 0) {
        strncpy(s_ntp_state.config.server3, DEFAULT_NTP_SERVER3, sizeof(s_ntp_state.config.server3) - 1);
    }

    // Set default sync interval if not specified
    if (s_ntp_state.config.sync_interval == 0) {
        s_ntp_state.config.sync_interval = 60; // 60 minutes
    }

    // Set default timeout if not specified
    if (s_ntp_state.config.timeout == 0) {
        s_ntp_state.config.timeout = 30; // 30 seconds
    }

    // Initialize status structure
    memset(&s_ntp_state.status, 0, sizeof(ntp_status_t));
    strncpy(s_ntp_state.status.active_server, s_ntp_state.config.server1, 
           sizeof(s_ntp_state.status.active_server) - 1);

    // Initialize drift compensation
    memset(&s_ntp_state.drift_comp, 0, sizeof(s_ntp_state.drift_comp));

    // Configure timezone if specified
    if (s_ntp_state.config.timezone_offset != 0) {
        char tz_str[32];
        int hours = s_ntp_state.config.timezone_offset / 60;
        int minutes = abs(s_ntp_state.config.timezone_offset % 60);
        
        if (minutes == 0) {
            snprintf(tz_str, sizeof(tz_str), "UTC%+d", -hours);
        } else {
            snprintf(tz_str, sizeof(tz_str), "UTC%+d:%02d", -hours, minutes);
        }
        
        setenv("TZ", tz_str, 1);
        tzset();
        ESP_LOGI(TAG, "Timezone set to: %s", tz_str);
    }

    s_ntp_state.initialized = true;
    s_ntp_state.running = false;

    ESP_LOGI(TAG, "NTP sync initialized successfully");
    ESP_LOGI(TAG, "NTP servers: %s, %s, %s", 
             s_ntp_state.config.server1,
             s_ntp_state.config.server2, 
             s_ntp_state.config.server3);

    return ESP_OK;
}

/**
 * @brief Start NTP synchronization
 */
esp_err_t ntp_sync_start(void)
{
    if (!s_ntp_state.initialized) {
        ESP_LOGE(TAG, "NTP sync not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ntp_state.running) {
        ESP_LOGW(TAG, "NTP sync already running");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting NTP synchronization");

    // Configure SNTP
    esp_err_t err = configure_sntp_servers();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure SNTP servers: %s", esp_err_to_name(err));
        return err;
    }

    // Set callback for time synchronization
    esp_sntp_set_time_sync_notification_cb(sntp_sync_time_callback);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);

    // Initialize and start SNTP
    esp_sntp_init();

    // Create synchronization task
    xTaskCreate(
        ntp_sync_task,
        "ntp_sync",
        4096,
        NULL,
        5,
        &s_ntp_state.sync_task
    );

    s_ntp_state.running = true;

    ESP_LOGI(TAG, "NTP synchronization started");
    return ESP_OK;
}

/**
 * @brief Stop NTP synchronization
 */
esp_err_t ntp_sync_stop(void)
{
    if (!s_ntp_state.initialized || !s_ntp_state.running) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping NTP synchronization");

    // Signal stop to sync task
    xEventGroupSetBits(s_ntp_state.event_group, NTP_STOP_BIT);

    // Wait for task to finish
    if (s_ntp_state.sync_task) {
        vTaskDelete(s_ntp_state.sync_task);
        s_ntp_state.sync_task = NULL;
    }

    // Stop SNTP
    esp_sntp_stop();

    s_ntp_state.running = false;
    s_ntp_state.status.synchronized = false;

    ESP_LOGI(TAG, "NTP synchronization stopped");
    return ESP_OK;
}

/**
 * @brief Deinitialize NTP synchronization
 */
esp_err_t ntp_sync_deinit(void)
{
    if (!s_ntp_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing NTP synchronization");

    // Stop synchronization first
    if (s_ntp_state.running) {
        ntp_sync_stop();
    }

    // Clean up resources
    if (s_ntp_state.mutex) {
        vSemaphoreDelete(s_ntp_state.mutex);
        s_ntp_state.mutex = NULL;
    }

    if (s_ntp_state.event_group) {
        vEventGroupDelete(s_ntp_state.event_group);
        s_ntp_state.event_group = NULL;
    }

    memset(&s_ntp_state, 0, sizeof(s_ntp_state));

    ESP_LOGI(TAG, "NTP synchronization deinitialized");
    return ESP_OK;
}

/**
 * @brief Check if time is synchronized
 */
bool ntp_sync_is_synchronized(void)
{
    return s_ntp_state.status.synchronized;
}

/**
 * @brief Force immediate synchronization
 */
esp_err_t ntp_sync_force_sync(void)
{
    if (!s_ntp_state.initialized || !s_ntp_state.running) {
        ESP_LOGE(TAG, "NTP sync not running");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Forcing NTP synchronization");
    return ntp_force_sync_internal();
}

/**
 * @brief Get current synchronized time with drift compensation
 */
esp_err_t ntp_sync_get_time(struct timeval *tv)
{
    if (!tv) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ntp_state.status.synchronized) {
        ESP_LOGW(TAG, "Time not synchronized, returning system time");
        return gettimeofday(tv, NULL);
    }

    // Get current system time
    gettimeofday(tv, NULL);
    
    // Apply drift compensation if available
    if (s_ntp_state.drift_comp.sample_count > 0) {
        int64_t system_time_us = tv->tv_sec * 1000000LL + tv->tv_usec;
        int64_t corrected_time_us = apply_drift_correction(system_time_us);
        
        tv->tv_sec = corrected_time_us / 1000000LL;
        tv->tv_usec = corrected_time_us % 1000000LL;
    }

    return ESP_OK;
}

/**
 * @brief Get NTP synchronization status
 */
esp_err_t ntp_sync_get_status(ntp_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ntp_state.mutex, portMAX_DELAY);
    memcpy(status, &s_ntp_state.status, sizeof(ntp_status_t));
    xSemaphoreGive(s_ntp_state.mutex);

    return ESP_OK;
}

/**
 * @brief Register sync callback
 */
esp_err_t ntp_sync_register_callback(ntp_sync_callback_t callback, void *user_ctx)
{
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ntp_state.mutex, portMAX_DELAY);
    s_ntp_state.callback = callback;
    s_ntp_state.callback_user_ctx = user_ctx;
    xSemaphoreGive(s_ntp_state.mutex);

    ESP_LOGI(TAG, "Sync callback registered");
    return ESP_OK;
}

/**
 * @brief Update NTP configuration
 */
esp_err_t ntp_sync_update_config(const ntp_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ntp_state.initialized) {
        ESP_LOGE(TAG, "NTP sync not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Updating NTP configuration");

    xSemaphoreTake(s_ntp_state.mutex, portMAX_DELAY);
    
    bool servers_changed = (strcmp(s_ntp_state.config.server1, config->server1) != 0) ||
                          (strcmp(s_ntp_state.config.server2, config->server2) != 0) ||
                          (strcmp(s_ntp_state.config.server3, config->server3) != 0);
    
    memcpy(&s_ntp_state.config, config, sizeof(ntp_config_t));
    
    xSemaphoreGive(s_ntp_state.mutex);

    // Reconfigure servers if they changed
    if (servers_changed && s_ntp_state.running) {
        esp_sntp_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        esp_err_t err = configure_sntp_servers();
        if (err == ESP_OK) {
            esp_sntp_init();
            ESP_LOGI(TAG, "NTP servers reconfigured");
        } else {
            ESP_LOGE(TAG, "Failed to reconfigure NTP servers: %s", esp_err_to_name(err));
            return err;
        }
    }

    ESP_LOGI(TAG, "NTP configuration updated successfully");
    return ESP_OK;
}

// ===== INTERNAL FUNCTIONS =====

/**
 * @brief NTP synchronization task
 */
static void ntp_sync_task(void *pvParameters)
{
    ESP_LOGI(TAG, "NTP sync task started");

    TickType_t sync_interval_ticks = pdMS_TO_TICKS(s_ntp_state.config.sync_interval * 60 * 1000);
    TickType_t last_sync_time = 0;

    while (1) {
        // Check for stop signal
        EventBits_t bits = xEventGroupWaitBits(s_ntp_state.event_group, NTP_STOP_BIT, 
                                              pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
        if (bits & NTP_STOP_BIT) {
            ESP_LOGI(TAG, "NTP sync task stop requested");
            break;
        }

        // Check if it's time for periodic sync
        TickType_t current_time = xTaskGetTickCount();
        if (!s_ntp_state.status.synchronized || 
            (current_time - last_sync_time) >= sync_interval_ticks) {
            
            ESP_LOGI(TAG, "Performing periodic NTP sync");
            
            esp_err_t err = ntp_force_sync_internal();
            if (err == ESP_OK) {
                last_sync_time = current_time;
                ESP_LOGI(TAG, "Periodic NTP sync completed successfully");
            } else {
                ESP_LOGW(TAG, "Periodic NTP sync failed: %s", esp_err_to_name(err));
            }
        }

        // Monitor synchronization status
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            if (!s_ntp_state.status.synchronized) {
                ESP_LOGI(TAG, "NTP synchronization achieved");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }

    ESP_LOGI(TAG, "NTP sync task stopped");
    vTaskDelete(NULL);
}

/**
 * @brief SNTP time sync callback
 */
static void sntp_sync_time_callback(struct timeval *tv)
{
    if (!tv) {
        ESP_LOGE(TAG, "Invalid time received from SNTP");
        return;
    }

    ESP_LOGI(TAG, "Time synchronized via SNTP: %lld.%06ld", (long long)tv->tv_sec, tv->tv_usec);

    xSemaphoreTake(s_ntp_state.mutex, portMAX_DELAY);
    
    // Update status
    s_ntp_state.status.synchronized = true;
    s_ntp_state.status.last_sync = get_system_time_us();
    s_ntp_state.status.sync_count++;
    
    // Calculate time offset for drift compensation
    struct timeval system_tv;
    gettimeofday(&system_tv, NULL);
    
    int64_t ntp_time = tv->tv_sec * 1000000LL + tv->tv_usec;
    int64_t system_time = system_tv.tv_sec * 1000000LL + system_tv.tv_usec;
    s_ntp_state.status.time_offset_ms = (ntp_time - system_time) / 1000;
    
    // Store data for drift compensation
    int idx = s_ntp_state.drift_comp.sample_index;
    s_ntp_state.drift_comp.system_times[idx] = system_time;
    s_ntp_state.drift_comp.ntp_times[idx] = ntp_time;
    
    s_ntp_state.drift_comp.sample_index = (idx + 1) % DRIFT_COMPENSATION_SAMPLES;
    if (s_ntp_state.drift_comp.sample_count < DRIFT_COMPENSATION_SAMPLES) {
        s_ntp_state.drift_comp.sample_count++;
    }
    
    // Calculate drift compensation
    if (s_ntp_state.drift_comp.sample_count >= 2) {
        calculate_drift_compensation();
    }
    
    xSemaphoreGive(s_ntp_state.mutex);

    // Notify callback if registered
    if (s_ntp_state.callback) {
        s_ntp_state.callback(true, s_ntp_state.callback_user_ctx);
    }

    ESP_LOGI(TAG, "NTP sync callback processed successfully");
}

/**
 * @brief Force internal synchronization
 */
static esp_err_t ntp_force_sync_internal(void)
{
    ESP_LOGI(TAG, "Requesting immediate SNTP sync");
    
    // Force a sync by restarting SNTP
    esp_sntp_stop();
    esp_sntp_init();
    
    // Wait for synchronization with timeout
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(s_ntp_state.config.timeout * 1000);
    
    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        if (esp_sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
            ESP_LOGI(TAG, "SNTP sync completed successfully");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGW(TAG, "SNTP sync timeout");
    xSemaphoreTake(s_ntp_state.mutex, portMAX_DELAY);
    s_ntp_state.status.sync_errors++;
    xSemaphoreGive(s_ntp_state.mutex);
    
    return ESP_ERR_TIMEOUT;
}

/**
 * @brief Calculate drift compensation parameters
 */
static void calculate_drift_compensation(void)
{
    if (s_ntp_state.drift_comp.sample_count < 2) {
        return;
    }

    // Use linear regression to calculate drift
    int n = s_ntp_state.drift_comp.sample_count;
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    
    for (int i = 0; i < n; i++) {
        double x = (double)s_ntp_state.drift_comp.system_times[i];
        double y = (double)s_ntp_state.drift_comp.ntp_times[i];
        
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }
    
    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
    
    // Convert slope to drift in PPM
    s_ntp_state.drift_comp.drift_ppm = (float)((slope - 1.0) * 1000000.0);
    
    // Limit drift to reasonable range
    if (fabs(s_ntp_state.drift_comp.drift_ppm) > MAX_DRIFT_PPM) {
        s_ntp_state.drift_comp.drift_ppm = (s_ntp_state.drift_comp.drift_ppm > 0) ? 
                                          MAX_DRIFT_PPM : -MAX_DRIFT_PPM;
    }
    
    s_ntp_state.drift_comp.last_correction_time = get_system_time_us();
    
    ESP_LOGD(TAG, "Calculated drift: %.3f PPM", s_ntp_state.drift_comp.drift_ppm);
}

/**
 * @brief Get system time in microseconds
 */
static int64_t get_system_time_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000LL + tv.tv_usec;
}

/**
 * @brief Apply drift correction to system time
 */
static int64_t apply_drift_correction(int64_t system_time)
{
    if (s_ntp_state.drift_comp.sample_count < 2) {
        return system_time;
    }

    int64_t time_since_correction = system_time - s_ntp_state.drift_comp.last_correction_time;
    double correction = (double)time_since_correction * s_ntp_state.drift_comp.drift_ppm / 1000000.0;
    
    return system_time + (int64_t)correction;
}

/**
 * @brief Configure SNTP servers
 */
static esp_err_t configure_sntp_servers(void)
{
    ESP_LOGI(TAG, "Configuring SNTP servers");

    if (strlen(s_ntp_state.config.server1) > 0) {
        esp_sntp_setservername(0, s_ntp_state.config.server1);
        ESP_LOGI(TAG, "SNTP server 0: %s", s_ntp_state.config.server1);
    }
    
    if (strlen(s_ntp_state.config.server2) > 0) {
        esp_sntp_setservername(1, s_ntp_state.config.server2);
        ESP_LOGI(TAG, "SNTP server 1: %s", s_ntp_state.config.server2);
    }
    
    if (strlen(s_ntp_state.config.server3) > 0) {
        esp_sntp_setservername(2, s_ntp_state.config.server3);
        ESP_LOGI(TAG, "SNTP server 2: %s", s_ntp_state.config.server3);
    }

    return ESP_OK;
}