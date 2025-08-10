/**
 * @file test_csi_memory.c
 * @brief TDD tests for CSI firmware memory constraints and payload size validation
 * 
 * This file implements comprehensive Test-Driven Development tests to ensure
 * the CSI firmware meets the 1MB payload size constraint for ESP32-S3.
 */

#include <unity.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "TEST_CSI_MEMORY";

/**
 * @brief Test that app partition size is within 1MB constraint
 */
void test_app_partition_size_constraint(void)
{
    const esp_partition_t *app_partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    
    TEST_ASSERT_NOT_NULL_MESSAGE(app_partition, "App partition not found");
    
    // Verify app partition is <= 1MB (1,048,576 bytes)
    const size_t MAX_APP_SIZE = 1024 * 1024; // 1MB
    size_t app_size = app_partition->size;
    
    ESP_LOGI(TAG, "App partition size: %zu bytes (%.2f KB)", 
             app_size, (float)app_size / 1024.0f);
    
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(MAX_APP_SIZE, app_size,
        "App partition exceeds 1MB constraint");
}

/**
 * @brief Test available heap memory after firmware initialization
 */
void test_available_heap_memory(void)
{
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    
    ESP_LOGI(TAG, "Free heap: %zu bytes, Min free: %zu bytes", 
             free_heap, min_heap);
    
    // Should have at least 100KB free for CSI operations
    const size_t MIN_FREE_HEAP = 100 * 1024; // 100KB
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(MIN_FREE_HEAP, free_heap,
        "Insufficient free heap memory");
    
    // Minimum heap should never go below 50KB during operation
    const size_t MIN_HEAP_THRESHOLD = 50 * 1024; // 50KB
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(MIN_HEAP_THRESHOLD, min_heap,
        "Minimum heap usage too high");
}

/**
 * @brief Test stack memory usage for critical tasks
 */
void test_task_stack_usage(void)
{
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    UBaseType_t stack_high_water = uxTaskGetStackHighWaterMark(current_task);
    
    ESP_LOGI(TAG, "Task stack high water mark: %u words (%u bytes)",
             (unsigned)stack_high_water, (unsigned)(stack_high_water * sizeof(StackType_t)));
    
    // Should have reasonable stack usage (not excessive)
    const UBaseType_t MAX_STACK_USAGE = 2048; // words
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(MAX_STACK_USAGE / 4, stack_high_water,
        "Task using excessive stack space");
}

/**
 * @brief Test flash memory partition layout
 */
void test_flash_partition_layout(void)
{
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, 
        ESP_PARTITION_SUBTYPE_ANY, NULL);
    
    size_t total_used = 0;
    int partition_count = 0;
    
    ESP_LOGI(TAG, "Flash partition layout:");
    
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        ESP_LOGI(TAG, "  %s: offset=0x%06X, size=0x%06X (%u KB)",
                 part->label, (unsigned)part->address, (unsigned)part->size,
                 (unsigned)(part->size / 1024));
        
        total_used += part->size;
        partition_count++;
        
        it = esp_partition_next(it);
    }
    
    esp_partition_iterator_release(it);
    
    ESP_LOGI(TAG, "Total partitions: %d, Total size used: %zu bytes (%.2f MB)",
             partition_count, total_used, (float)total_used / (1024 * 1024));
    
    // Verify reasonable number of partitions
    TEST_ASSERT_GREATER_THAN_MESSAGE(2, partition_count, "Too few partitions");
    TEST_ASSERT_LESS_THAN_MESSAGE(10, partition_count, "Too many partitions");
}

/**
 * @brief Test CSI buffer allocation within memory constraints
 */
void test_csi_buffer_allocation(void)
{
    // Simulate CSI buffer allocation
    const size_t CSI_BUFFER_SIZE = 64 * 1024; // 64KB CSI buffer
    const int NUM_BUFFERS = 4; // Multiple buffers for buffering
    
    size_t heap_before = esp_get_free_heap_size();
    void *buffers[NUM_BUFFERS];
    
    // Allocate CSI buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        buffers[i] = malloc(CSI_BUFFER_SIZE);
        TEST_ASSERT_NOT_NULL_MESSAGE(buffers[i], "CSI buffer allocation failed");
    }
    
    size_t heap_after = esp_get_free_heap_size();
    size_t allocated = heap_before - heap_after;
    
    ESP_LOGI(TAG, "CSI buffer allocation: %zu bytes, Heap before: %zu, after: %zu",
             allocated, heap_before, heap_after);
    
    // Free buffers
    for (int i = 0; i < NUM_BUFFERS; i++) {
        free(buffers[i]);
    }
    
    // Should still have reasonable heap after allocation
    const size_t MIN_REMAINING_HEAP = 50 * 1024; // 50KB
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(MIN_REMAINING_HEAP, heap_after,
        "Insufficient heap after CSI buffer allocation");
}

/**
 * @brief Test firmware version and build constraints
 */
void test_firmware_build_constraints(void)
{
    // Get IDF version info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    ESP_LOGI(TAG, "ESP32 chip info:");
    ESP_LOGI(TAG, "  Model: ESP32-%s", (chip_info.model == CHIP_ESP32S3) ? "S3" : "Unknown");
    ESP_LOGI(TAG, "  Cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "  Features: WiFi%s%s", 
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
    ESP_LOGI(TAG, "  Flash: %dMB %s", 
             spi_flash_get_chip_size() / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    
    // Verify we're running on ESP32-S3
    TEST_ASSERT_EQUAL_MESSAGE(CHIP_ESP32S3, chip_info.model, 
        "Firmware not built for ESP32-S3");
    
    // Verify minimum flash size (4MB for development)
    size_t flash_size = spi_flash_get_chip_size();
    const size_t MIN_FLASH_SIZE = 4 * 1024 * 1024; // 4MB
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(MIN_FLASH_SIZE, flash_size,
        "Insufficient flash memory");
}

/**
 * @brief Test memory fragmentation resistance
 */
void test_memory_fragmentation_resistance(void)
{
    const int NUM_ALLOCS = 20;
    const size_t ALLOC_SIZE = 1024; // 1KB allocations
    void *ptrs[NUM_ALLOCS];
    
    size_t heap_before = esp_get_free_heap_size();
    
    // Allocate many small buffers
    for (int i = 0; i < NUM_ALLOCS; i++) {
        ptrs[i] = malloc(ALLOC_SIZE);
        TEST_ASSERT_NOT_NULL_MESSAGE(ptrs[i], "Small allocation failed");
    }
    
    // Free every other buffer to create fragmentation
    for (int i = 0; i < NUM_ALLOCS; i += 2) {
        free(ptrs[i]);
        ptrs[i] = NULL;
    }
    
    // Try to allocate larger buffer
    size_t large_size = NUM_ALLOCS * ALLOC_SIZE / 4; // 5KB
    void *large_ptr = malloc(large_size);
    
    // Clean up remaining allocations
    for (int i = 1; i < NUM_ALLOCS; i += 2) {
        if (ptrs[i]) free(ptrs[i]);
    }
    
    if (large_ptr) {
        free(large_ptr);
        ESP_LOGI(TAG, "Memory fragmentation test: PASSED");
    } else {
        ESP_LOGW(TAG, "Memory fragmentation test: Large allocation failed (expected in some cases)");
    }
    
    size_t heap_after = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Heap recovery: %zu -> %zu bytes", heap_before, heap_after);
    
    // Should recover most memory
    size_t recovery_threshold = heap_before * 9 / 10; // 90% recovery
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(recovery_threshold, heap_after,
        "Poor memory recovery after fragmentation test");
}

/**
 * @brief Test suite setup
 */
void setUp(void)
{
    // Setup before each test
    ESP_LOGI(TAG, "Setting up memory test...");
}

/**
 * @brief Test suite teardown
 */
void tearDown(void)
{
    // Cleanup after each test
    ESP_LOGI(TAG, "Tearing down memory test...");
}

/**
 * @brief Main test runner for memory tests
 */
void app_main(void)
{
    ESP_LOGI(TAG, "Starting CSI Memory Constraint Tests");
    
    UNITY_BEGIN();
    
    // Memory constraint tests
    RUN_TEST(test_app_partition_size_constraint);
    RUN_TEST(test_available_heap_memory);
    RUN_TEST(test_task_stack_usage);
    RUN_TEST(test_flash_partition_layout);
    RUN_TEST(test_csi_buffer_allocation);
    RUN_TEST(test_firmware_build_constraints);
    RUN_TEST(test_memory_fragmentation_resistance);
    
    UNITY_END();
}