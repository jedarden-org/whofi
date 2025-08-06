/**
 * @file system_init.c
 * @brief System initialization implementation
 */

#include "system_init.h"
#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_spiffs.h>
#include <esp_vfs_fat.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>

static const char *TAG = "SYSTEM_INIT";

esp_err_t system_init(void)
{
    esp_err_t err;

    ESP_LOGI(TAG, "Initializing system components...");

    // Initialize event loop first
    err = event_loop_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Event loop initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize NVS
    err = nvs_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize file systems
    err = filesystem_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Filesystem initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize Wi-Fi
    err = wifi_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "System initialization complete");
    return ESP_OK;
}

esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t wifi_init(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi initialized successfully");
        
        // Set Wi-Fi mode to STA+AP for initial setup
        err = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(err));
            return err;
        }
    } else {
        ESP_LOGE(TAG, "Failed to initialize Wi-Fi: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t event_loop_init(void)
{
    esp_err_t err = esp_event_loop_create_default();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Event loop initialized successfully");
    } else if (err == ESP_ERR_INVALID_STATE) {
        // Event loop already created
        ESP_LOGI(TAG, "Event loop already initialized");
        err = ESP_OK;
    } else {
        ESP_LOGE(TAG, "Failed to initialize event loop: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t filesystem_init(void)
{
    esp_err_t err = ESP_OK;

    // Initialize SPIFFS for web server files
    ESP_LOGI(TAG, "Initializing SPIFFS...");
    esp_vfs_spiffs_conf_t spiffs_conf = {
        .base_path = "/spiffs",
        .partition_label = "www",
        .max_files = 10,
        .format_if_mount_failed = true
    };

    err = esp_vfs_spiffs_register(&spiffs_conf);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format SPIFFS filesystem");
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS: %s", esp_err_to_name(err));
        }
        return err;
    }

    size_t total = 0, used = 0;
    err = esp_spiffs_info("www", &total, &used);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %d KB total, %d KB used", total / 1024, used / 1024);
    } else {
        ESP_LOGW(TAG, "Failed to get SPIFFS partition information");
    }

    // Initialize FAT filesystem for CSI data storage
    ESP_LOGI(TAG, "Initializing FAT filesystem...");
    
    const esp_vfs_fat_mount_config_t mount_config = {
        .max_files = 4,
        .format_if_mount_failed = true,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE
    };

    wl_handle_t wl_handle;
    err = esp_vfs_fat_spiflash_mount_rw_wl("/data", "csi_data", &mount_config, &wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount FAT filesystem: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Filesystems initialized successfully");
    return ESP_OK;
}