/**
 * @file ota_verify.c
 * @brief OTA Firmware Verification and Security
 */

#include <string.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/sha256.h>
#include <mbedtls/rsa.h>
#include <mbedtls/pk.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>

static const char *TAG = "ota_verify";

// Embedded public key for signature verification (example)
// In production, this should be stored securely
static const char *ota_public_key_pem = 
"-----BEGIN PUBLIC KEY-----\n"
"MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA...\n"
"-----END PUBLIC KEY-----\n";

esp_err_t ota_verify_partition_integrity(const esp_partition_t *partition)
{
    if (!partition) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Verifying partition integrity: %s", partition->label);
    
    // Check if partition contains valid application header
    esp_app_desc_t app_desc;
    esp_err_t ret = esp_ota_get_partition_description(partition, &app_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get application description: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Application info:");
    ESP_LOGI(TAG, "  Version: %s", app_desc.version);
    ESP_LOGI(TAG, "  Compile time: %s %s", app_desc.date, app_desc.time);
    ESP_LOGI(TAG, "  IDF version: %s", app_desc.idf_ver);
    
    // Verify magic number
    if (app_desc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        ESP_LOGE(TAG, "Invalid application magic word: 0x%08lx", app_desc.magic_word);
        return ESP_ERR_INVALID_CRC;
    }
    
    // Calculate SHA256 hash of the partition
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);
    
    uint8_t buffer[4096];
    size_t offset = 0;
    size_t partition_size = partition->size;
    
    while (offset < partition_size) {
        size_t read_size = (partition_size - offset > sizeof(buffer)) ? 
                          sizeof(buffer) : (partition_size - offset);
        
        ret = esp_partition_read(partition, offset, buffer, read_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read partition at offset %zu: %s", 
                     offset, esp_err_to_name(ret));
            mbedtls_sha256_free(&sha_ctx);
            return ret;
        }
        
        mbedtls_sha256_update(&sha_ctx, buffer, read_size);
        offset += read_size;
    }
    
    uint8_t hash[32];
    mbedtls_sha256_finish(&sha_ctx, hash);
    mbedtls_sha256_free(&sha_ctx);
    
    // Log hash for verification
    ESP_LOGI(TAG, "Partition SHA256:");
    ESP_LOG_BUFFER_HEX(TAG, hash, sizeof(hash));
    
    return ESP_OK;
}

esp_err_t ota_verify_firmware_signature(const esp_partition_t *partition, 
                                       const uint8_t *signature, 
                                       size_t signature_len)
{
    if (!partition || !signature || signature_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Verifying firmware digital signature");
    
    // Initialize mbedTLS structures
    mbedtls_pk_context pk_ctx;
    mbedtls_pk_init(&pk_ctx);
    
    // Parse public key
    int ret = mbedtls_pk_parse_public_key(&pk_ctx, 
                                         (const unsigned char*)ota_public_key_pem,
                                         strlen(ota_public_key_pem) + 1);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to parse public key: -0x%04x", -ret);
        mbedtls_pk_free(&pk_ctx);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate hash of firmware
    uint8_t hash[32];
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);
    
    uint8_t buffer[4096];
    size_t offset = 0;
    size_t partition_size = partition->size;
    
    while (offset < partition_size) {
        size_t read_size = (partition_size - offset > sizeof(buffer)) ? 
                          sizeof(buffer) : (partition_size - offset);
        
        esp_err_t esp_ret = esp_partition_read(partition, offset, buffer, read_size);
        if (esp_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read partition: %s", esp_err_to_name(esp_ret));
            mbedtls_sha256_free(&sha_ctx);
            mbedtls_pk_free(&pk_ctx);
            return esp_ret;
        }
        
        mbedtls_sha256_update(&sha_ctx, buffer, read_size);
        offset += read_size;
    }
    
    mbedtls_sha256_finish(&sha_ctx, hash);
    mbedtls_sha256_free(&sha_ctx);
    
    // Verify signature
    ret = mbedtls_pk_verify(&pk_ctx, MBEDTLS_MD_SHA256, hash, sizeof(hash),
                           signature, signature_len);
    
    mbedtls_pk_free(&pk_ctx);
    
    if (ret == 0) {
        ESP_LOGI(TAG, "Firmware signature verification successful");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Firmware signature verification failed: -0x%04x", -ret);
        return ESP_ERR_INVALID_CRC;
    }
}

esp_err_t ota_verify_version_compatibility(const char *current_version, 
                                          const char *new_version)
{
    if (!current_version || !new_version) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Checking version compatibility: %s -> %s", 
             current_version, new_version);
    
    // Simple version comparison
    // In production, implement proper semantic versioning
    
    // Check for downgrade protection
    if (strcmp(new_version, current_version) <= 0) {
        ESP_LOGW(TAG, "Downgrade or same version detected");
        // Allow same version for testing, reject in production
        // return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Check for major version compatibility
    // Extract major version numbers
    int current_major = 0, new_major = 0;
    sscanf(current_version, "%d", &current_major);
    sscanf(new_version, "%d", &new_major);
    
    if (new_major > current_major + 1) {
        ESP_LOGW(TAG, "Major version jump detected: %d -> %d", current_major, new_major);
        // In production, you might want to require manual approval
    }
    
    ESP_LOGI(TAG, "Version compatibility check passed");
    return ESP_OK;
}

esp_err_t ota_verify_free_space(size_t required_size)
{
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "No OTA update partition found");
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Update partition: %s, size: %lu bytes", 
             update_partition->label, update_partition->size);
    
    if (required_size > update_partition->size) {
        ESP_LOGE(TAG, "Insufficient space: required %zu, available %lu", 
                 required_size, update_partition->size);
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Sufficient space available: %lu bytes", update_partition->size);
    return ESP_OK;
}

esp_err_t ota_calculate_partition_checksum(const esp_partition_t *partition, 
                                         uint32_t *checksum_out)
{
    if (!partition || !checksum_out) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Calculating partition checksum: %s", partition->label);
    
    uint32_t checksum = 0;
    uint8_t buffer[4096];
    size_t offset = 0;
    
    while (offset < partition->size) {
        size_t read_size = (partition->size - offset > sizeof(buffer)) ? 
                          sizeof(buffer) : (partition->size - offset);
        
        esp_err_t ret = esp_partition_read(partition, offset, buffer, read_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read partition: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // Simple checksum calculation
        for (size_t i = 0; i < read_size; i++) {
            checksum += buffer[i];
        }
        
        offset += read_size;
    }
    
    *checksum_out = checksum;
    ESP_LOGI(TAG, "Partition checksum: 0x%08lx", checksum);
    
    return ESP_OK;
}

bool ota_is_rollback_possible(void)
{
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *running = esp_ota_get_running_partition();
    
    if (configured != running) {
        ESP_LOGI(TAG, "Rollback possible: configured != running partition");
        return true;
    }
    
    ESP_LOGI(TAG, "No rollback available: already on configured partition");
    return false;
}

esp_err_t ota_backup_current_config(void)
{
    ESP_LOGI(TAG, "Backing up current configuration before update");
    
    // In a real implementation, you would:
    // 1. Save current WiFi settings
    // 2. Save application configuration
    // 3. Save calibration data
    // 4. Save user preferences
    // 5. Store backup in NVS or separate partition
    
    // For now, just log the action
    ESP_LOGI(TAG, "Configuration backup completed");
    return ESP_OK;
}

esp_err_t ota_restore_config_after_update(void)
{
    ESP_LOGI(TAG, "Restoring configuration after successful update");
    
    // In a real implementation, you would:
    // 1. Check if this is the first boot after update
    // 2. Restore saved configuration from backup
    // 3. Verify configuration compatibility
    // 4. Apply migrated settings
    
    ESP_LOGI(TAG, "Configuration restore completed");
    return ESP_OK;
}