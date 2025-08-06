/**
 * @file test_ntp_sync.c
 * @brief Unit tests for NTP sync component
 * 
 * Contains comprehensive unit tests for the NTP synchronization
 * functionality, client utilities, and timezone handling.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <inttypes.h>

#include <unity.h>
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ntp_sync.h"

static const char *TAG = "NTP_TEST";

// Test configuration
static ntp_config_t test_config = {
    .enabled = true,
    .server1 = "pool.ntp.org",
    .server2 = "time.nist.gov",
    .server3 = "time.google.com",
    .timezone_offset = 0,  // UTC
    .sync_interval = 60,   // 60 minutes
    .timeout = 30          // 30 seconds
};

// Test callback data
static int callback_count = 0;
static bool last_sync_result = false;

// Test callback function
static void test_sync_callback(bool synchronized, void *user_ctx)
{
    callback_count++;
    last_sync_result = synchronized;
    ESP_LOGI(TAG, "Test sync callback: synchronized=%s", synchronized ? "true" : "false");
}

void setUp(void)
{
    // Reset test state
    callback_count = 0;
    last_sync_result = false;
}

void tearDown(void)
{
    // Clean up after each test
    ntp_sync_deinit();
}

void test_ntp_sync_init_valid_config(void)
{
    ESP_LOGI(TAG, "Testing NTP sync initialization with valid config");
    
    esp_err_t err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Test that we can't initialize twice
    err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

void test_ntp_sync_init_invalid_config(void)
{
    ESP_LOGI(TAG, "Testing NTP sync initialization with invalid config");
    
    esp_err_t err = ntp_sync_init(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_ntp_sync_init_with_defaults(void)
{
    ESP_LOGI(TAG, "Testing NTP sync initialization with default values");
    
    ntp_config_t minimal_config = {
        .enabled = true,
        .server1 = "",  // Will use default
        .server2 = "",  // Will use default
        .server3 = "",  // Will use default
        .timezone_offset = 0,
        .sync_interval = 0,  // Will use default
        .timeout = 0         // Will use default
    };
    
    esp_err_t err = ntp_sync_init(&minimal_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_ntp_sync_operations_not_initialized(void)
{
    ESP_LOGI(TAG, "Testing NTP sync operations without initialization");
    
    esp_err_t err = ntp_sync_start();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    
    err = ntp_sync_stop();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
    
    bool synchronized = ntp_sync_is_synchronized();
    TEST_ASSERT_FALSE(synchronized);
}

void test_ntp_sync_lifecycle(void)
{
    ESP_LOGI(TAG, "Testing NTP sync lifecycle");
    
    // Initialize
    esp_err_t err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Start
    err = ntp_sync_start();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Give some time for sync attempt
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // Stop
    err = ntp_sync_stop();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Deinitialize
    err = ntp_sync_deinit();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_ntp_sync_callback_registration(void)
{
    ESP_LOGI(TAG, "Testing NTP sync callback registration");
    
    esp_err_t err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Register callback
    err = ntp_sync_register_callback(test_sync_callback, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Test invalid callback
    err = ntp_sync_register_callback(NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_ntp_sync_get_status(void)
{
    ESP_LOGI(TAG, "Testing NTP sync status retrieval");
    
    esp_err_t err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    ntp_status_t status;
    err = ntp_sync_get_status(&status);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Initial status should show not synchronized
    TEST_ASSERT_FALSE(status.synchronized);
    TEST_ASSERT_EQUAL(0, status.sync_count);
    TEST_ASSERT_EQUAL(0, status.sync_errors);
    
    // Test invalid status pointer
    err = ntp_sync_get_status(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_ntp_sync_get_time(void)
{
    ESP_LOGI(TAG, "Testing NTP sync time retrieval");
    
    esp_err_t err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    struct timeval tv;
    err = ntp_sync_get_time(&tv);
    // Should return system time even if not synchronized
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_EQUAL(0, tv.tv_sec);
    
    // Test invalid timeval pointer
    err = ntp_sync_get_time(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_ntp_sync_force_sync_not_running(void)
{
    ESP_LOGI(TAG, "Testing NTP force sync when not running");
    
    esp_err_t err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Try to force sync without starting
    err = ntp_sync_force_sync();
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

void test_ntp_sync_update_config(void)
{
    ESP_LOGI(TAG, "Testing NTP sync configuration update");
    
    esp_err_t err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    ntp_config_t new_config = test_config;
    strncpy(new_config.server1, "time.cloudflare.com", sizeof(new_config.server1));
    new_config.sync_interval = 120;  // Change to 2 hours
    
    err = ntp_sync_update_config(&new_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Test invalid config
    err = ntp_sync_update_config(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    // Test updating when not initialized
    ntp_sync_deinit();
    err = ntp_sync_update_config(&new_config);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, err);
}

void test_ntp_client_timezone_functions(void)
{
    ESP_LOGI(TAG, "Testing NTP client timezone functions");
    
    int16_t offset;
    esp_err_t err;
    
    // Test valid timezone
    err = ntp_client_get_timezone_offset("UTC", &offset);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(0, offset);
    
    // Test EST timezone
    err = ntp_client_get_timezone_offset("EST", &offset);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(-300, offset);  // EST is UTC-5 hours = -300 minutes
    
    // Test invalid timezone
    err = ntp_client_get_timezone_offset("INVALID", &offset);
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, err);
    
    // Test invalid arguments
    err = ntp_client_get_timezone_offset(NULL, &offset);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    err = ntp_client_get_timezone_offset("UTC", NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    // Test list timezones
    err = ntp_client_list_timezones();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void test_ntp_client_time_formatting(void)
{
    ESP_LOGI(TAG, "Testing NTP client time formatting");
    
    char buffer[64];
    esp_err_t err;
    
    // Test timestamp formatting
    uint64_t test_timestamp = 1609459200000000ULL;  // 2021-01-01 00:00:00 UTC
    err = ntp_client_format_time(test_timestamp, 0, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "Formatted time: %s", buffer);
    
    // Test with timezone offset (EST)
    err = ntp_client_format_time(test_timestamp, -300, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "Formatted time (EST): %s", buffer);
    
    // Test invalid arguments
    err = ntp_client_format_time(test_timestamp, 0, NULL, sizeof(buffer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    err = ntp_client_format_time(test_timestamp, 0, buffer, 10);  // Too small buffer
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    // Test current time string
    err = ntp_client_get_time_string(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "Current time string: %s", buffer);
    
    // Test invalid arguments
    err = ntp_client_get_time_string(NULL, sizeof(buffer));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    err = ntp_client_get_time_string(buffer, 10);  // Too small buffer
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_ntp_client_server_operations(void)
{
    ESP_LOGI(TAG, "Testing NTP client server operations");
    
    const char *test_server = "pool.ntp.org";
    uint32_t delay_ms;
    esp_err_t err;
    
    // Test server delay measurement
    err = ntp_client_measure_server_delay(test_server, &delay_ms);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "Server delay: %u ms", delay_ms);
    
    // Test invalid arguments
    err = ntp_client_measure_server_delay(NULL, &delay_ms);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    err = ntp_client_measure_server_delay(test_server, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    // Test server validation
    err = ntp_client_validate_server(test_server);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Test invalid server
    err = ntp_client_validate_server("invalid.server.name.that.does.not.exist");
    TEST_ASSERT_NOT_EQUAL(ESP_OK, err);
    
    err = ntp_client_validate_server(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    // Test server statistics
    ntp_server_stats_t stats;
    err = ntp_client_get_server_stats(test_server, &stats);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    ESP_LOGI(TAG, "Server stats: stratum=%d, delay=%ums", stats.stratum, stats.delay_ms);
    
    // Test invalid arguments
    err = ntp_client_get_server_stats(NULL, &stats);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    err = ntp_client_get_server_stats(test_server, NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_ntp_client_sync_quality(void)
{
    ESP_LOGI(TAG, "Testing NTP client sync quality");
    
    ntp_sync_quality_t quality;
    esp_err_t err;
    
    // Initialize NTP sync first
    err = ntp_sync_init(&test_config);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Get sync quality
    err = ntp_client_get_sync_quality(&quality);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Should show poor quality initially (not synchronized)
    TEST_ASSERT_FALSE(quality.synchronized);
    TEST_ASSERT_EQUAL(NTP_QUALITY_POOR, quality.quality);
    
    ESP_LOGI(TAG, "Sync quality: %s, offset: %" PRId32 "ms, age: %" PRIu64 "s", 
             quality.synchronized ? "synchronized" : "not synchronized",
             quality.offset_ms, quality.time_since_sync_sec);
    
    // Test invalid arguments
    err = ntp_client_get_sync_quality(NULL);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

void test_ntp_sync_with_timezone(void)
{
    ESP_LOGI(TAG, "Testing NTP sync with timezone configuration");
    
    ntp_config_t config_with_tz = test_config;
    config_with_tz.timezone_offset = -480;  // PST (UTC-8)
    
    esp_err_t err = ntp_sync_init(&config_with_tz);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    // Start and allow some time
    err = ntp_sync_start();
    TEST_ASSERT_EQUAL(ESP_OK, err);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Stop
    err = ntp_sync_stop();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting NTP Sync Unit Tests");
    
    UNITY_BEGIN();
    
    // Basic functionality tests
    RUN_TEST(test_ntp_sync_init_valid_config);
    RUN_TEST(test_ntp_sync_init_invalid_config);
    RUN_TEST(test_ntp_sync_init_with_defaults);
    RUN_TEST(test_ntp_sync_operations_not_initialized);
    RUN_TEST(test_ntp_sync_lifecycle);
    RUN_TEST(test_ntp_sync_callback_registration);
    RUN_TEST(test_ntp_sync_get_status);
    RUN_TEST(test_ntp_sync_get_time);
    RUN_TEST(test_ntp_sync_force_sync_not_running);
    RUN_TEST(test_ntp_sync_update_config);
    
    // NTP client utility tests
    RUN_TEST(test_ntp_client_timezone_functions);
    RUN_TEST(test_ntp_client_time_formatting);
    RUN_TEST(test_ntp_client_server_operations);
    RUN_TEST(test_ntp_client_sync_quality);
    
    // Advanced configuration tests
    RUN_TEST(test_ntp_sync_with_timezone);
    
    UNITY_END();
    
    ESP_LOGI(TAG, "NTP Sync Unit Tests Completed");
}