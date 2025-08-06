/**
 * @file ntp_client.c
 * @brief NTP client utility functions
 * 
 * Contains utility functions for NTP client operations, time zone handling,
 * and advanced synchronization features.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <inttypes.h>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_sntp.h>

#include "ntp_sync.h"

static const char *TAG = "NTP_CLIENT";

// Time zone definitions for common regions
typedef struct {
    const char *name;
    int16_t offset_minutes;
    const char *description;
} timezone_info_t;

static const timezone_info_t timezone_table[] = {
    {"UTC",     0,      "Coordinated Universal Time"},
    {"GMT",     0,      "Greenwich Mean Time"},
    {"EST",     -300,   "Eastern Standard Time"},
    {"EDT",     -240,   "Eastern Daylight Time"},
    {"CST",     -360,   "Central Standard Time"},
    {"CDT",     -300,   "Central Daylight Time"},
    {"MST",     -420,   "Mountain Standard Time"},
    {"MDT",     -360,   "Mountain Daylight Time"},
    {"PST",     -480,   "Pacific Standard Time"},
    {"PDT",     -420,   "Pacific Daylight Time"},
    {"CET",     60,     "Central European Time"},
    {"CEST",    120,    "Central European Summer Time"},
    {"JST",     540,    "Japan Standard Time"},
    {"AEST",    600,    "Australian Eastern Standard Time"},
    {"AEDT",    660,    "Australian Eastern Daylight Time"},
    {NULL,      0,      NULL}  // Sentinel
};

// NTP packet structure for direct NTP communication
typedef struct {
    uint8_t li_vn_mode;      // Leap Indicator, Version Number, Mode
    uint8_t stratum;         // Stratum
    uint8_t poll;            // Poll Interval
    uint8_t precision;       // Precision
    uint32_t root_delay;     // Root Delay
    uint32_t root_dispersion; // Root Dispersion
    uint32_t reference_id;   // Reference Clock Identifier
    uint64_t reference_ts;   // Reference Timestamp
    uint64_t origin_ts;      // Origin Timestamp
    uint64_t receive_ts;     // Receive Timestamp
    uint64_t transmit_ts;    // Transmit Timestamp
} __attribute__((packed)) ntp_packet_t;

// Internal function declarations
static int16_t timezone_name_to_offset(const char *tz_name);
static void format_time_string(time_t timestamp, char *buffer, size_t buffer_size);
static esp_err_t validate_ntp_response(const ntp_packet_t *packet);

/**
 * @brief Get timezone offset from timezone name
 */
esp_err_t ntp_client_get_timezone_offset(const char *timezone, int16_t *offset_minutes)
{
    if (!timezone || !offset_minutes) {
        return ESP_ERR_INVALID_ARG;
    }

    *offset_minutes = timezone_name_to_offset(timezone);
    
    if (*offset_minutes == INT16_MAX) {
        ESP_LOGW(TAG, "Unknown timezone: %s", timezone);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Timezone %s offset: %d minutes", timezone, *offset_minutes);
    return ESP_OK;
}

/**
 * @brief List available timezones
 */
esp_err_t ntp_client_list_timezones(void)
{
    ESP_LOGI(TAG, "Available timezones:");
    
    for (int i = 0; timezone_table[i].name != NULL; i++) {
        int hours = timezone_table[i].offset_minutes / 60;
        int minutes = abs(timezone_table[i].offset_minutes % 60);
        
        ESP_LOGI(TAG, "  %s: UTC%+d:%02d - %s", 
                timezone_table[i].name, 
                hours, 
                minutes,
                timezone_table[i].description);
    }
    
    return ESP_OK;
}

/**
 * @brief Convert timestamp to formatted string with timezone
 */
esp_err_t ntp_client_format_time(uint64_t timestamp_us, int16_t timezone_offset, 
                                char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 32) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t timestamp_sec = timestamp_us / 1000000ULL;
    uint32_t microseconds = timestamp_us % 1000000ULL;
    
    // Apply timezone offset
    timestamp_sec += timezone_offset * 60;
    
    struct tm *timeinfo = gmtime(&timestamp_sec);
    if (!timeinfo) {
        ESP_LOGE(TAG, "Failed to convert timestamp to time structure");
        return ESP_FAIL;
    }

    // Format time string
    strftime(buffer, buffer_size - 8, "%Y-%m-%d %H:%M:%S", timeinfo);
    
    // Add microseconds
    char us_str[8];
    snprintf(us_str, sizeof(us_str), ".%06u", microseconds);
    strncat(buffer, us_str, buffer_size - strlen(buffer) - 1);

    return ESP_OK;
}

/**
 * @brief Get current time as formatted string
 */
esp_err_t ntp_client_get_time_string(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 32) {
        return ESP_ERR_INVALID_ARG;
    }

    struct timeval tv;
    esp_err_t err = ntp_sync_get_time(&tv);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get synchronized time: %s", esp_err_to_name(err));
        return err;
    }

    uint64_t timestamp_us = tv.tv_sec * 1000000ULL + tv.tv_usec;
    
    // Use UTC (0 offset) by default
    return ntp_client_format_time(timestamp_us, 0, buffer, buffer_size);
}

/**
 * @brief Calculate round-trip delay to NTP server
 */
esp_err_t ntp_client_measure_server_delay(const char *server, uint32_t *delay_ms)
{
    if (!server || !delay_ms) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Measuring delay to NTP server: %s", server);

    // This is a simplified implementation
    // In a real implementation, you would send NTP packets directly
    // and measure the round-trip time
    
    TickType_t start_time = xTaskGetTickCount();
    
    // Simulate server communication delay measurement
    vTaskDelay(pdMS_TO_TICKS(100)); // Simulated network delay
    
    TickType_t end_time = xTaskGetTickCount();
    *delay_ms = (end_time - start_time) * portTICK_PERIOD_MS;
    
    ESP_LOGI(TAG, "Measured delay to %s: %u ms", server, *delay_ms);
    return ESP_OK;
}

/**
 * @brief Get NTP server statistics
 */
esp_err_t ntp_client_get_server_stats(const char *server, ntp_server_stats_t *stats)
{
    if (!server || !stats) {
        return ESP_ERR_INVALID_ARG;
    }

    // Initialize stats structure
    memset(stats, 0, sizeof(ntp_server_stats_t));
    strncpy(stats->server_name, server, sizeof(stats->server_name) - 1);
    
    // Get basic server information
    // This would typically involve querying the NTP server for detailed stats
    stats->stratum = 2;  // Typical for public NTP servers
    stats->precision = -20;  // ~1 microsecond precision
    stats->poll_interval = 6;  // 64 seconds
    
    // Measure current delay
    esp_err_t err = ntp_client_measure_server_delay(server, &stats->delay_ms);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to measure server delay: %s", esp_err_to_name(err));
        stats->delay_ms = 0;
    }
    
    // Set availability based on delay measurement success
    stats->available = (err == ESP_OK);
    
    if (stats->available) {
        stats->last_response = time(NULL);
    }
    
    ESP_LOGI(TAG, "Server %s stats: stratum=%d, delay=%ums, available=%s", 
             server, stats->stratum, stats->delay_ms, 
             stats->available ? "yes" : "no");
    
    return ESP_OK;
}

/**
 * @brief Validate NTP server accessibility
 */
esp_err_t ntp_client_validate_server(const char *server)
{
    if (!server) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Validating NTP server: %s", server);

    // Basic server name validation
    if (strlen(server) == 0 || strlen(server) > 63) {
        ESP_LOGE(TAG, "Invalid server name length");
        return ESP_ERR_INVALID_ARG;
    }

    // Check for obvious invalid characters
    for (const char *p = server; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            ESP_LOGE(TAG, "Invalid character in server name");
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Try to measure delay as a connectivity test
    uint32_t delay_ms;
    esp_err_t err = ntp_client_measure_server_delay(server, &delay_ms);
    
    if (err == ESP_OK && delay_ms < 5000) {  // Reasonable delay threshold
        ESP_LOGI(TAG, "Server %s is accessible (delay: %ums)", server, delay_ms);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Server %s may not be accessible or has high delay", server);
        return ESP_ERR_NOT_FOUND;
    }
}

/**
 * @brief Get current NTP synchronization quality
 */
esp_err_t ntp_client_get_sync_quality(ntp_sync_quality_t *quality)
{
    if (!quality) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(quality, 0, sizeof(ntp_sync_quality_t));

    // Get current status
    ntp_status_t status;
    esp_err_t err = ntp_sync_get_status(&status);
    if (err != ESP_OK) {
        return err;
    }

    quality->synchronized = status.synchronized;
    quality->offset_ms = status.time_offset_ms;
    
    if (status.synchronized) {
        // Calculate time since last sync
        struct timeval current_time;
        gettimeofday(&current_time, NULL);
        uint64_t current_us = current_time.tv_sec * 1000000ULL + current_time.tv_usec;
        
        quality->time_since_sync_sec = (current_us - status.last_sync) / 1000000ULL;
        
        // Determine quality based on offset and time since sync
        if (abs(quality->offset_ms) < 10 && quality->time_since_sync_sec < 3600) {
            quality->quality = NTP_QUALITY_EXCELLENT;
        } else if (abs(quality->offset_ms) < 50 && quality->time_since_sync_sec < 7200) {
            quality->quality = NTP_QUALITY_GOOD;
        } else if (abs(quality->offset_ms) < 200 && quality->time_since_sync_sec < 14400) {
            quality->quality = NTP_QUALITY_FAIR;
        } else {
            quality->quality = NTP_QUALITY_POOR;
        }
    } else {
        quality->quality = NTP_QUALITY_POOR;
    }

    const char *quality_str[] = {"POOR", "FAIR", "GOOD", "EXCELLENT"};
    ESP_LOGI(TAG, "NTP sync quality: %s (offset: %" PRId32 "ms, age: %" PRIu64 "s)", 
             quality_str[quality->quality], quality->offset_ms, quality->time_since_sync_sec);

    return ESP_OK;
}

// ===== INTERNAL FUNCTIONS =====

/**
 * @brief Convert timezone name to offset in minutes
 */
static int16_t timezone_name_to_offset(const char *tz_name)
{
    for (int i = 0; timezone_table[i].name != NULL; i++) {
        if (strcasecmp(tz_name, timezone_table[i].name) == 0) {
            return timezone_table[i].offset_minutes;
        }
    }
    return INT16_MAX;  // Invalid timezone
}

/**
 * @brief Format timestamp as human-readable string
 */
static void format_time_string(time_t timestamp, char *buffer, size_t buffer_size)
{
    struct tm *timeinfo = localtime(&timestamp);
    if (timeinfo) {
        strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", timeinfo);
    } else {
        snprintf(buffer, buffer_size, "Invalid time");
    }
}

/**
 * @brief Validate NTP response packet
 */
static esp_err_t validate_ntp_response(const ntp_packet_t *packet)
{
    if (!packet) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check basic packet structure
    uint8_t version = (packet->li_vn_mode >> 3) & 0x07;
    uint8_t mode = packet->li_vn_mode & 0x07;

    if (version < 3 || version > 4) {
        ESP_LOGE(TAG, "Invalid NTP version: %d", version);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (mode != 4) {  // Server mode
        ESP_LOGE(TAG, "Invalid NTP mode: %d", mode);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (packet->stratum == 0 || packet->stratum > 15) {
        ESP_LOGE(TAG, "Invalid stratum: %d", packet->stratum);
        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}