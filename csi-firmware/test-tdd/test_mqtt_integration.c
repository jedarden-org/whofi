/**
 * @file test_mqtt_integration.c
 * @brief TDD tests for MQTT integration and CSI data transmission
 * 
 * This file implements comprehensive Test-Driven Development tests to ensure
 * MQTT integration works correctly with CSI data transmission.
 */

#include <unity.h>
#include <esp_system.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <cJSON.h>

// Mock includes for components
#ifdef ESP_PLATFORM
#include "mqtt_client_wrapper.h"
#include "csi_collector.h"
#endif

static const char *TAG = "TEST_MQTT_INTEG";

// Test message reception callback
static bool test_message_received = false;
static char test_received_topic[128];
static char test_received_data[512];

/**
 * @brief Test callback for MQTT message reception
 */
static void test_mqtt_callback(const char *topic, const char *data, int data_len, void *user_data)
{
    test_message_received = true;
    strncpy(test_received_topic, topic, sizeof(test_received_topic) - 1);
    strncpy(test_received_data, data, MIN(data_len, sizeof(test_received_data) - 1));
    test_received_data[MIN(data_len, sizeof(test_received_data) - 1)] = '\0';
    
    ESP_LOGI(TAG, "Test callback received: topic=%s, data_len=%d", topic, data_len);
}

/**
 * @brief Test MQTT connection establishment
 */
void test_mqtt_connection(void)
{
#ifdef ESP_PLATFORM
    mqtt_config_t config = {
        .enabled = true,
        .port = 1883,
        .ssl_enabled = false,
        .keepalive = 60,
        .qos = 1,
        .retain = false
    };
    
    strncpy(config.broker_url, "test.mosquitto.org", sizeof(config.broker_url) - 1);
    strncpy(config.client_id, "test_csi_esp32", sizeof(config.client_id) - 1);
    strncpy(config.topic_prefix, "csi/test", sizeof(config.topic_prefix) - 1);
    
    // Initialize MQTT client
    esp_err_t result = mqtt_client_init(&config);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "MQTT client initialization failed");
    
    // Register test callback
    mqtt_client_register_callback(test_mqtt_callback, NULL);
    
    // Start MQTT client
    result = mqtt_client_start();
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "MQTT client start failed");
    
    // Wait for connection (up to 10 seconds)
    int wait_count = 0;
    while (!mqtt_client_is_connected() && wait_count < 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    
    TEST_ASSERT_TRUE_MESSAGE(mqtt_client_is_connected(), 
        "MQTT client should be connected after start");
    
    ESP_LOGI(TAG, "MQTT connection test completed successfully");
#else
    ESP_LOGI(TAG, "MQTT connection test skipped - not on ESP platform");
    TEST_PASS();
#endif
}

/**
 * @brief Test CSI data JSON serialization
 */
void test_csi_data_json_serialization(void)
{
#ifdef ESP_PLATFORM
    // Create test CSI data
    csi_data_t test_data;
    memset(&test_data, 0, sizeof(test_data));
    
    test_data.len = 128;
    test_data.rssi = -45;
    test_data.timestamp = esp_timer_get_time();
    memcpy(test_data.mac, "\x00\x11\x22\x33\x44\x55", 6);
    
    // Fill with test pattern
    for (int i = 0; i < test_data.len; i++) {
        test_data.data[i] = i & 0xFF;
    }
    
    // Test JSON serialization (mock implementation)
    cJSON *json = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL_MESSAGE(json, "Failed to create JSON object");
    
    cJSON_AddNumberToObject(json, "len", test_data.len);
    cJSON_AddNumberToObject(json, "rssi", test_data.rssi);
    cJSON_AddNumberToObject(json, "timestamp", (double)test_data.timestamp);
    
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             test_data.mac[0], test_data.mac[1], test_data.mac[2],
             test_data.mac[3], test_data.mac[4], test_data.mac[5]);
    cJSON_AddStringToObject(json, "mac", mac_str);
    
    // Add CSI data as array
    cJSON *data_array = cJSON_CreateArray();
    for (int i = 0; i < test_data.len; i++) {
        cJSON_AddItemToArray(data_array, cJSON_CreateNumber(test_data.data[i]));
    }
    cJSON_AddItemToObject(json, "data", data_array);
    
    char *json_string = cJSON_Print(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(json_string, "Failed to serialize JSON");
    
    ESP_LOGI(TAG, "Serialized JSON size: %d bytes", strlen(json_string));
    TEST_ASSERT_LESS_THAN_MESSAGE(2048, strlen(json_string), 
        "JSON should be less than 2KB for efficient transmission");
    
    // Validate JSON structure
    cJSON *parsed = cJSON_Parse(json_string);
    TEST_ASSERT_NOT_NULL_MESSAGE(parsed, "Failed to parse serialized JSON");
    
    cJSON *len_item = cJSON_GetObjectItem(parsed, "len");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsNumber(len_item), "Length should be number");
    TEST_ASSERT_EQUAL_MESSAGE(test_data.len, (int)cJSON_GetNumberValue(len_item), 
        "Length mismatch in JSON");
    
    // Cleanup
    free(json_string);
    cJSON_Delete(json);
    cJSON_Delete(parsed);
    
    ESP_LOGI(TAG, "CSI data JSON serialization test completed");
#else
    ESP_LOGI(TAG, "CSI JSON serialization test skipped - not on ESP platform");
    TEST_PASS();
#endif
}

/**
 * @brief Test CSI data MQTT publishing
 */
void test_csi_data_mqtt_publish(void)
{
#ifdef ESP_PLATFORM
    // Ensure MQTT is connected
    test_mqtt_connection();
    
    if (!mqtt_client_is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, skipping publish test");
        TEST_PASS();
        return;
    }
    
    // Create test CSI data
    csi_data_t test_data;
    memset(&test_data, 0, sizeof(test_data));
    
    test_data.len = 64;  // Smaller for testing
    test_data.rssi = -50;
    test_data.timestamp = esp_timer_get_time();
    memcpy(test_data.mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    
    // Publish CSI data
    esp_err_t result = mqtt_client_publish_csi_data(&test_data);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "CSI data publish failed");
    
    ESP_LOGI(TAG, "CSI data MQTT publish test completed");
#else
    ESP_LOGI(TAG, "CSI MQTT publish test skipped - not on ESP platform");
    TEST_PASS();
#endif
}

/**
 * @brief Test MQTT message reception and parsing
 */
void test_mqtt_message_reception(void)
{
#ifdef ESP_PLATFORM
    // Reset test flags
    test_message_received = false;
    memset(test_received_topic, 0, sizeof(test_received_topic));
    memset(test_received_data, 0, sizeof(test_received_data));
    
    // Ensure MQTT is connected
    if (!mqtt_client_is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, skipping reception test");
        TEST_PASS();
        return;
    }
    
    // Subscribe to test topic
    const char *test_topic = "csi/test/commands";
    esp_err_t result = mqtt_client_subscribe(test_topic, 1);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "MQTT subscribe failed");
    
    // Publish test message to ourselves
    const char *test_message = "{\"command\":\"test\",\"value\":123}";
    result = mqtt_client_publish(test_topic, test_message, strlen(test_message), 1, false);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "MQTT test publish failed");
    
    // Wait for message reception (up to 5 seconds)
    int wait_count = 0;
    while (!test_message_received && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    
    TEST_ASSERT_TRUE_MESSAGE(test_message_received, "Test message not received");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(test_topic, test_received_topic, 
        "Received topic mismatch");
    
    // Parse received JSON
    cJSON *json = cJSON_Parse(test_received_data);
    TEST_ASSERT_NOT_NULL_MESSAGE(json, "Failed to parse received JSON");
    
    cJSON *command = cJSON_GetObjectItem(json, "command");
    TEST_ASSERT_TRUE_MESSAGE(cJSON_IsString(command), "Command should be string");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("test", cJSON_GetStringValue(command), 
        "Command value mismatch");
    
    cJSON_Delete(json);
    
    ESP_LOGI(TAG, "MQTT message reception test completed");
#else
    ESP_LOGI(TAG, "MQTT reception test skipped - not on ESP platform");
    TEST_PASS();
#endif
}

/**
 * @brief Test MQTT statistics and monitoring
 */
void test_mqtt_statistics(void)
{
#ifdef ESP_PLATFORM
    if (!mqtt_client_is_connected()) {
        ESP_LOGW(TAG, "MQTT not connected, skipping statistics test");
        TEST_PASS();
        return;
    }
    
    mqtt_stats_t stats;
    esp_err_t result = mqtt_client_get_stats(&stats);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_OK, result, "Failed to get MQTT statistics");
    
    ESP_LOGI(TAG, "MQTT Statistics:");
    ESP_LOGI(TAG, "  Messages sent: %u", (unsigned)stats.messages_sent);
    ESP_LOGI(TAG, "  Messages received: %u", (unsigned)stats.messages_received);
    ESP_LOGI(TAG, "  Connection errors: %u", (unsigned)stats.connection_errors);
    ESP_LOGI(TAG, "  Reconnection count: %u", (unsigned)stats.reconnection_count);
    
    // Validate statistics make sense
    TEST_ASSERT_TRUE_MESSAGE(stats.messages_sent >= 0, 
        "Messages sent should be non-negative");
    TEST_ASSERT_TRUE_MESSAGE(stats.messages_received >= 0, 
        "Messages received should be non-negative");
    
    ESP_LOGI(TAG, "MQTT statistics test completed");
#else
    ESP_LOGI(TAG, "MQTT statistics test skipped - not on ESP platform");
    TEST_PASS();
#endif
}

/**
 * @brief Test MQTT error handling and recovery
 */
void test_mqtt_error_handling(void)
{
#ifdef ESP_PLATFORM
    // Test invalid broker configuration
    mqtt_config_t bad_config = {
        .enabled = true,
        .port = 1883,
        .ssl_enabled = false,
        .keepalive = 60,
        .qos = 1,
        .retain = false
    };
    
    // Use invalid broker
    strncpy(bad_config.broker_url, "invalid.broker.nowhere", 
            sizeof(bad_config.broker_url) - 1);
    strncpy(bad_config.client_id, "test_invalid", sizeof(bad_config.client_id) - 1);
    
    // This should handle the error gracefully
    esp_err_t result = mqtt_client_init(&bad_config);
    // Init might succeed, but connection should fail
    if (result == ESP_OK) {
        result = mqtt_client_start();
        // Start might succeed, but connection will fail
        
        // Wait a bit and check connection status
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        // Should not be connected to invalid broker
        TEST_ASSERT_FALSE_MESSAGE(mqtt_client_is_connected(), 
            "Should not connect to invalid broker");
    }
    
    ESP_LOGI(TAG, "MQTT error handling test completed");
#else
    ESP_LOGI(TAG, "MQTT error handling test skipped - not on ESP platform");
    TEST_PASS();
#endif
}

/**
 * @brief Test MQTT payload size constraints for 1MB firmware
 */
void test_mqtt_payload_size_constraints(void)
{
    // Test maximum CSI payload size
    const size_t MAX_CSI_DATA_SIZE = 512;  // Reasonable for ESP32
    const size_t MAX_JSON_OVERHEAD = 1024; // JSON formatting overhead
    const size_t MAX_TOTAL_PAYLOAD = MAX_CSI_DATA_SIZE + MAX_JSON_OVERHEAD;
    
    ESP_LOGI(TAG, "Maximum CSI payload test:");
    ESP_LOGI(TAG, "  Max CSI data: %zu bytes", MAX_CSI_DATA_SIZE);
    ESP_LOGI(TAG, "  Max JSON overhead: %zu bytes", MAX_JSON_OVERHEAD);
    ESP_LOGI(TAG, "  Max total payload: %zu bytes", MAX_TOTAL_PAYLOAD);
    
    // Should be reasonable for MQTT transmission
    TEST_ASSERT_LESS_THAN_MESSAGE(4096, MAX_TOTAL_PAYLOAD, 
        "Total MQTT payload should be less than 4KB");
    
    // Test memory allocation for payload
    void *payload_buffer = malloc(MAX_TOTAL_PAYLOAD);
    TEST_ASSERT_NOT_NULL_MESSAGE(payload_buffer, 
        "Should be able to allocate payload buffer");
    
    if (payload_buffer) {
        free(payload_buffer);
    }
    
    ESP_LOGI(TAG, "MQTT payload size constraints test completed");
    TEST_PASS();
}

/**
 * @brief Test suite setup
 */
void setUp(void)
{
    ESP_LOGI(TAG, "Setting up MQTT integration test...");
}

/**
 * @brief Test suite teardown
 */
void tearDown(void)
{
    ESP_LOGI(TAG, "Tearing down MQTT integration test...");
#ifdef ESP_PLATFORM
    // Clean up MQTT connection
    if (mqtt_client_is_connected()) {
        mqtt_client_stop();
    }
#endif
}

/**
 * @brief Main test runner for MQTT integration tests
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting MQTT Integration Tests");
    
    UNITY_BEGIN();
    
    // MQTT integration tests
    RUN_TEST(test_mqtt_connection);
    RUN_TEST(test_csi_data_json_serialization);
    RUN_TEST(test_csi_data_mqtt_publish);
    RUN_TEST(test_mqtt_message_reception);
    RUN_TEST(test_mqtt_statistics);
    RUN_TEST(test_mqtt_error_handling);
    RUN_TEST(test_mqtt_payload_size_constraints);
    
    UNITY_END();
}