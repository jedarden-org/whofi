/**
 * @file csi_buffer.c
 * @brief CSI data buffering implementation
 */

#include "csi_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static const char *TAG = "CSI_BUFFER";

/**
 * @brief CSI buffer context structure
 */
typedef struct csi_buffer_ctx {
    QueueHandle_t queue;
    SemaphoreHandle_t mutex;
    uint16_t size;
    uint32_t total_items;
    uint32_t dropped_items;
    bool overwrite_enabled;
    bool initialized;
} csi_buffer_ctx_t;

esp_err_t csi_buffer_init(csi_buffer_handle_t *handle, uint16_t size)
{
    if (!handle || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    csi_buffer_ctx_t *ctx = malloc(sizeof(csi_buffer_ctx_t));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }

    ctx->queue = xQueueCreate(size / 64, sizeof(csi_data_t)); // Approximate queue size
    if (!ctx->queue) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    ctx->mutex = xSemaphoreCreateMutex();
    if (!ctx->mutex) {
        vQueueDelete(ctx->queue);
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    ctx->size = size;
    ctx->total_items = 0;
    ctx->dropped_items = 0;
    ctx->overwrite_enabled = false;
    ctx->initialized = true;

    *handle = ctx;
    return ESP_OK;
}

esp_err_t csi_buffer_put_data(csi_buffer_handle_t handle, const csi_data_t *data)
{
    if (!handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    csi_buffer_ctx_t *ctx = (csi_buffer_ctx_t *)handle;
    if (!ctx->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    
    if (xQueueSend(ctx->queue, data, 0) != pdTRUE) {
        if (ctx->overwrite_enabled && uxQueueSpacesAvailable(ctx->queue) == 0) {
            // Remove oldest item and try again
            csi_data_t dummy;
            xQueueReceive(ctx->queue, &dummy, 0);
            if (xQueueSend(ctx->queue, data, 0) == pdTRUE) {
                ctx->total_items++;
                xSemaphoreGive(ctx->mutex);
                return ESP_OK;
            }
        }
        ctx->dropped_items++;
        xSemaphoreGive(ctx->mutex);
        return ESP_ERR_NO_MEM;
    }

    ctx->total_items++;
    xSemaphoreGive(ctx->mutex);
    return ESP_OK;
}

esp_err_t csi_buffer_get_data(csi_buffer_handle_t handle, csi_data_t *data, TickType_t timeout_ticks)
{
    if (!handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    csi_buffer_ctx_t *ctx = (csi_buffer_ctx_t *)handle;
    if (!ctx->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueReceive(ctx->queue, data, timeout_ticks) == pdTRUE) {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t csi_buffer_deinit(csi_buffer_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    csi_buffer_ctx_t *ctx = (csi_buffer_ctx_t *)handle;
    
    if (ctx->initialized) {
        if (ctx->queue) {
            vQueueDelete(ctx->queue);
        }
        if (ctx->mutex) {
            vSemaphoreDelete(ctx->mutex);
        }
    }
    
    free(ctx);
    return ESP_OK;
}

esp_err_t csi_buffer_get_stats(csi_buffer_handle_t handle, uint32_t *total_items, 
                              uint32_t *dropped_items, uint32_t *queue_size)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    csi_buffer_ctx_t *ctx = (csi_buffer_ctx_t *)handle;
    if (!ctx->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    
    if (total_items) {
        *total_items = ctx->total_items;
    }
    if (dropped_items) {
        *dropped_items = ctx->dropped_items;
    }
    if (queue_size) {
        *queue_size = (uint32_t)uxQueueMessagesWaiting(ctx->queue);
    }
    
    xSemaphoreGive(ctx->mutex);
    return ESP_OK;
}

esp_err_t csi_buffer_set_overwrite(csi_buffer_handle_t handle, bool enable)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    csi_buffer_ctx_t *ctx = (csi_buffer_ctx_t *)handle;
    if (!ctx->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->mutex, portMAX_DELAY);
    ctx->overwrite_enabled = enable;
    xSemaphoreGive(ctx->mutex);
    
    ESP_LOGI(TAG, "Buffer overwrite mode %s", enable ? "enabled" : "disabled");
    return ESP_OK;
}