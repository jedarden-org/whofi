/**
 * @file web_server.c
 * @brief Web server implementation for CSI device configuration
 */

#include "web_server.h"
#include "csi_collector.h"
#include <string.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <cJSON.h>

static const char *TAG = "WEB_SERVER";

/**
 * @brief Web server context structure
 */
typedef struct {
    httpd_handle_t server;
    web_server_config_t config;
    web_server_stats_t stats;
    SemaphoreHandle_t mutex;
    uint64_t start_time;
    bool initialized;
    bool running;
} web_server_ctx_t;

static web_server_ctx_t s_ctx = {0};

// HTML templates
static const char index_html[] = {
#include "index_html.h"
};

static const char config_html[] = {
#include "config_html.h"
};

static const char status_html[] = {
#include "status_html.h"
};

// Forward declarations
static esp_err_t index_handler(httpd_req_t *req);
static esp_err_t config_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t api_status_handler(httpd_req_t *req);
static esp_err_t api_config_handler(httpd_req_t *req);
static esp_err_t api_csi_data_handler(httpd_req_t *req);
static esp_err_t api_stats_handler(httpd_req_t *req);
// WebSocket not supported in ESP-IDF v5.1.2
// static esp_err_t websocket_handler(httpd_req_t *req);
static bool authenticate_request(httpd_req_t *req);
static void update_stats(size_t bytes_sent, size_t bytes_received);

esp_err_t web_server_start(const web_server_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ctx.running) {
        ESP_LOGW(TAG, "Web server already running");
        return ESP_OK;
    }

    // Initialize context
    memset(&s_ctx, 0, sizeof(s_ctx));
    memcpy(&s_ctx.config, config, sizeof(web_server_config_t));
    
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (!s_ctx.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // HTTP server configuration
    httpd_config_t server_config = HTTPD_DEFAULT_CONFIG();
    server_config.server_port = config->port;
    server_config.max_open_sockets = config->max_sessions;
    server_config.stack_size = 8192;
    server_config.task_priority = 5;
    server_config.lru_purge_enable = true;

    // Start the HTTP server
    esp_err_t err = httpd_start(&s_ctx.server, &server_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_ctx.mutex);
        return err;
    }

    // Register URI handlers
    httpd_uri_t uri_handlers[] = {
        {
            .uri = "/",
            .method = HTTP_GET,
            .handler = index_handler,
            .user_ctx = NULL
        },
        {
            .uri = "/config",
            .method = HTTP_GET,
            .handler = config_handler,
            .user_ctx = NULL
        },
        {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL
        },
        {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = api_status_handler,
            .user_ctx = NULL
        },
        {
            .uri = "/api/config",
            .method = HTTP_GET,
            .handler = api_config_handler,
            .user_ctx = NULL
        },
        {
            .uri = "/api/config",
            .method = HTTP_POST,
            .handler = api_config_handler,
            .user_ctx = NULL
        },
        {
            .uri = "/api/csi-data",
            .method = HTTP_GET,
            .handler = api_csi_data_handler,
            .user_ctx = NULL
        },
        {
            .uri = "/api/stats",
            .method = HTTP_GET,
            .handler = api_stats_handler,
            .user_ctx = NULL
        }
        // WebSocket not supported in ESP-IDF v5.1.2
        /*{
            .uri = "/ws",
            .method = HTTP_GET,
            .handler = websocket_handler,
            .user_ctx = NULL,
            .is_websocket = true
        }*/
    };

    for (int i = 0; i < sizeof(uri_handlers) / sizeof(httpd_uri_t); i++) {
        err = httpd_register_uri_handler(s_ctx.server, &uri_handlers[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register URI handler %s: %s", 
                     uri_handlers[i].uri, esp_err_to_name(err));
        }
    }

    s_ctx.start_time = esp_timer_get_time();
    s_ctx.running = true;
    s_ctx.initialized = true;

    ESP_LOGI(TAG, "Web server started on port %d", config->port);
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (!s_ctx.running) {
        ESP_LOGW(TAG, "Web server not running");
        return ESP_OK;
    }

    if (s_ctx.server) {
        httpd_stop(s_ctx.server);
        s_ctx.server = NULL;
    }

    if (s_ctx.mutex) {
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
    }

    s_ctx.running = false;
    s_ctx.initialized = false;

    ESP_LOGI(TAG, "Web server stopped");
    return ESP_OK;
}

bool web_server_is_running(void)
{
    return s_ctx.running;
}

esp_err_t web_server_get_stats(web_server_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    
    memcpy(stats, &s_ctx.stats, sizeof(web_server_stats_t));
    stats->uptime = (esp_timer_get_time() - s_ctx.start_time) / 1000000; // Convert to seconds
    
    xSemaphoreGive(s_ctx.mutex);

    return ESP_OK;
}

esp_err_t web_server_reset_stats(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    memset(&s_ctx.stats, 0, sizeof(web_server_stats_t));
    s_ctx.start_time = esp_timer_get_time();
    xSemaphoreGive(s_ctx.mutex);

    ESP_LOGI(TAG, "Web server statistics reset");
    return ESP_OK;
}

esp_err_t web_server_update_config(const web_server_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    
    // Update configuration (some changes might require restart)
    memcpy(&s_ctx.config, config, sizeof(web_server_config_t));
    
    xSemaphoreGive(s_ctx.mutex);

    ESP_LOGI(TAG, "Web server configuration updated");
    return ESP_OK;
}

// HTTP handlers implementation

static esp_err_t index_handler(httpd_req_t *req)
{
    if (s_ctx.config.auth_enabled && !authenticate_request(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"CSI Device\"");
        httpd_resp_send(req, "Authentication required", HTTPD_RESP_USE_STRLEN);
        update_stats(0, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    update_stats(strlen(index_html), req->content_len);
    return ESP_OK;
}

static esp_err_t config_handler(httpd_req_t *req)
{
    if (s_ctx.config.auth_enabled && !authenticate_request(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"CSI Device\"");
        httpd_resp_send(req, "Authentication required", HTTPD_RESP_USE_STRLEN);
        update_stats(0, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, config_html, HTTPD_RESP_USE_STRLEN);
    update_stats(strlen(config_html), req->content_len);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (s_ctx.config.auth_enabled && !authenticate_request(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"CSI Device\"");
        httpd_resp_send(req, "Authentication required", HTTPD_RESP_USE_STRLEN);
        update_stats(0, 0);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, status_html, HTTPD_RESP_USE_STRLEN);
    update_stats(strlen(status_html), req->content_len);
    return ESP_OK;
}

static esp_err_t api_status_handler(httpd_req_t *req)
{
    if (s_ctx.config.auth_enabled && !authenticate_request(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"Authentication required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Create JSON status response
    cJSON *json = cJSON_CreateObject();
    cJSON *system = cJSON_CreateObject();
    cJSON *csi = cJSON_CreateObject();
    cJSON *wifi = cJSON_CreateObject();

    // System info
    cJSON_AddStringToObject(system, "version", "1.0.0");
    cJSON_AddNumberToObject(system, "uptime", (esp_timer_get_time() - s_ctx.start_time) / 1000000);
    cJSON_AddNumberToObject(system, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(system, "min_free_heap", esp_get_minimum_free_heap_size());

    // CSI status
    cJSON_AddBoolToObject(csi, "running", csi_collector_is_running());
    
    if (csi_collector_is_running()) {
        csi_collector_stats_t csi_stats;
        if (csi_collector_get_stats(&csi_stats) == ESP_OK) {
            cJSON_AddNumberToObject(csi, "packets_received", csi_stats.packets_received);
            cJSON_AddNumberToObject(csi, "packets_processed", csi_stats.packets_processed);
            cJSON_AddNumberToObject(csi, "packets_dropped", csi_stats.packets_dropped);
            cJSON_AddNumberToObject(csi, "average_rssi", csi_stats.average_rssi);
            cJSON_AddNumberToObject(csi, "last_packet_time", csi_stats.last_packet_time);
        }
    }

    // Wi-Fi info
    wifi_ap_record_t ap_info;
    esp_err_t wifi_err = esp_wifi_sta_get_ap_info(&ap_info);
    if (wifi_err == ESP_OK) {
        cJSON_AddStringToObject(wifi, "ssid", (char*)ap_info.ssid);
        cJSON_AddNumberToObject(wifi, "rssi", ap_info.rssi);
        cJSON_AddNumberToObject(wifi, "channel", ap_info.primary);
    } else {
        cJSON_AddStringToObject(wifi, "status", "disconnected");
    }

    // Add sub-objects to main JSON
    cJSON_AddItemToObject(json, "system", system);
    cJSON_AddItemToObject(json, "csi", csi);
    cJSON_AddItemToObject(json, "wifi", wifi);

    // Send response
    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

    update_stats(strlen(json_string), req->content_len);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

static esp_err_t api_config_handler(httpd_req_t *req)
{
    if (s_ctx.config.auth_enabled && !authenticate_request(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"Authentication required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (req->method == HTTP_GET) {
        // Return current configuration
        cJSON *json = cJSON_CreateObject();
        
        csi_collector_config_t csi_config;
        if (csi_collector_get_config(&csi_config) == ESP_OK) {
            cJSON *csi = cJSON_CreateObject();
            cJSON_AddNumberToObject(csi, "sample_rate", csi_config.sample_rate);
            cJSON_AddNumberToObject(csi, "buffer_size", csi_config.buffer_size);
            cJSON_AddBoolToObject(csi, "filter_enabled", csi_config.filter_enabled);
            cJSON_AddNumberToObject(csi, "filter_threshold", csi_config.filter_threshold);
            cJSON_AddBoolToObject(csi, "enable_rssi", csi_config.enable_rssi);
            cJSON_AddBoolToObject(csi, "enable_phase", csi_config.enable_phase);
            cJSON_AddBoolToObject(csi, "enable_amplitude", csi_config.enable_amplitude);
            cJSON_AddItemToObject(json, "csi", csi);
        }

        char *json_string = cJSON_Print(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

        update_stats(strlen(json_string), req->content_len);
        
        free(json_string);
        cJSON_Delete(json);
        
    } else if (req->method == HTTP_POST) {
        // Update configuration
        char *content = malloc(req->content_len + 1);
        if (!content) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, "{\"error\":\"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        int ret = httpd_req_recv(req, content, req->content_len);
        if (ret <= 0) {
            free(content);
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        content[req->content_len] = '\0';

        cJSON *json = cJSON_Parse(content);
        if (!json) {
            free(content);
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "{\"error\":\"Invalid JSON\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }

        // Process configuration update
        bool success = true;
        cJSON *csi = cJSON_GetObjectItem(json, "csi");
        if (csi) {
            csi_collector_config_t csi_config = {0};
            csi_collector_get_config(&csi_config);

            cJSON *item = cJSON_GetObjectItem(csi, "sample_rate");
            if (item && cJSON_IsNumber(item)) {
                csi_config.sample_rate = (uint8_t)item->valueint;
            }

            item = cJSON_GetObjectItem(csi, "buffer_size");
            if (item && cJSON_IsNumber(item)) {
                csi_config.buffer_size = (uint16_t)item->valueint;
            }

            item = cJSON_GetObjectItem(csi, "filter_enabled");
            if (item && cJSON_IsBool(item)) {
                csi_config.filter_enabled = cJSON_IsTrue(item);
            }

            item = cJSON_GetObjectItem(csi, "filter_threshold");
            if (item && cJSON_IsNumber(item)) {
                csi_config.filter_threshold = (float)item->valuedouble;
            }

            // Update CSI configuration
            if (csi_collector_update_config(&csi_config) != ESP_OK) {
                success = false;
            }
        }

        // Send response
        if (success) {
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
        } else {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_send(req, "{\"error\":\"Configuration update failed\"}", HTTPD_RESP_USE_STRLEN);
        }

        update_stats(0, req->content_len);
        
        free(content);
        cJSON_Delete(json);
    }

    return ESP_OK;
}

static esp_err_t api_csi_data_handler(httpd_req_t *req)
{
    if (s_ctx.config.auth_enabled && !authenticate_request(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"Authentication required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (!csi_collector_is_running()) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"CSI collector not running\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    // Get latest CSI data
    csi_data_t csi_data;
    esp_err_t err = csi_collector_get_data(&csi_data, pdMS_TO_TICKS(100));
    
    if (err == ESP_OK) {
        cJSON *json = cJSON_CreateObject();
        
        cJSON_AddNumberToObject(json, "timestamp", csi_data.timestamp);
        
        // MAC address as hex string
        char mac_str[18];
        sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x",
                csi_data.mac[0], csi_data.mac[1], csi_data.mac[2],
                csi_data.mac[3], csi_data.mac[4], csi_data.mac[5]);
        cJSON_AddStringToObject(json, "mac", mac_str);
        
        cJSON_AddNumberToObject(json, "rssi", csi_data.rssi);
        cJSON_AddNumberToObject(json, "channel", csi_data.channel);
        cJSON_AddNumberToObject(json, "subcarrier_count", csi_data.subcarrier_count);
        
        // Add amplitude data if available
        if (csi_data.amplitude) {
            cJSON *amplitude_array = cJSON_CreateArray();
            for (int i = 0; i < csi_data.subcarrier_count; i++) {
                cJSON_AddItemToArray(amplitude_array, cJSON_CreateNumber(csi_data.amplitude[i]));
            }
            cJSON_AddItemToObject(json, "amplitude", amplitude_array);
        }
        
        // Add phase data if available
        if (csi_data.phase) {
            cJSON *phase_array = cJSON_CreateArray();
            for (int i = 0; i < csi_data.subcarrier_count; i++) {
                cJSON_AddItemToArray(phase_array, cJSON_CreateNumber(csi_data.phase[i]));
            }
            cJSON_AddItemToObject(json, "phase", phase_array);
        }

        char *json_string = cJSON_Print(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

        update_stats(strlen(json_string), req->content_len);
        
        // Clean up CSI data
        csi_collector_free_data(&csi_data);
        
        free(json_string);
        cJSON_Delete(json);
        
    } else {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"No CSI data available\"}", HTTPD_RESP_USE_STRLEN);
    }

    return ESP_OK;
}

static esp_err_t api_stats_handler(httpd_req_t *req)
{
    if (s_ctx.config.auth_enabled && !authenticate_request(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"Authentication required\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    web_server_stats_t stats;
    web_server_get_stats(&stats);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "total_requests", stats.total_requests);
    cJSON_AddNumberToObject(json, "active_sessions", stats.active_sessions);
    cJSON_AddNumberToObject(json, "failed_auth", stats.failed_auth);
    cJSON_AddNumberToObject(json, "bytes_sent", stats.bytes_sent);
    cJSON_AddNumberToObject(json, "bytes_received", stats.bytes_received);
    cJSON_AddNumberToObject(json, "uptime", stats.uptime);

    char *json_string = cJSON_Print(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, json_string, HTTPD_RESP_USE_STRLEN);

    update_stats(strlen(json_string), req->content_len);
    
    free(json_string);
    cJSON_Delete(json);
    return ESP_OK;
}

// WebSocket not supported in ESP-IDF v5.1.2
/*static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "WebSocket handshake");
        return ESP_OK;
    }

    // Handle WebSocket frames
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    
    // Receive the frame
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %s", esp_err_to_name(ret));
            free(buf);
            return ret;
        }
    }

    // Process WebSocket message
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ESP_LOGI(TAG, "Received packet with message: %s", ws_pkt.payload);
        
        // Echo response for now
        httpd_ws_frame_t ws_resp = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t*)"ACK",
            .len = 3
        };
        httpd_ws_send_frame(req, &ws_resp);
    }

    if (buf) {
        free(buf);
    }
    
    return ESP_OK;
}*/

static bool authenticate_request(httpd_req_t *req)
{
    if (!s_ctx.config.auth_enabled) {
        return true;
    }

    char *auth_header = NULL;
    size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
    
    if (auth_len == 0) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        s_ctx.stats.failed_auth++;
        xSemaphoreGive(s_ctx.mutex);
        return false;
    }

    auth_header = malloc(auth_len + 1);
    if (!auth_header) {
        return false;
    }

    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, auth_len + 1) != ESP_OK) {
        free(auth_header);
        return false;
    }

    // Simple Basic Auth check (in production, use proper base64 decoding)
    bool authenticated = false;
    if (strstr(auth_header, "Basic ") == auth_header) {
        // For now, just check if credentials are present
        // In production, decode base64 and compare with configured credentials
        authenticated = true;
    }

    free(auth_header);
    
    if (!authenticated) {
        xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
        s_ctx.stats.failed_auth++;
        xSemaphoreGive(s_ctx.mutex);
    }
    
    return authenticated;
}

static void update_stats(size_t bytes_sent, size_t bytes_received)
{
    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);
    s_ctx.stats.total_requests++;
    s_ctx.stats.bytes_sent += bytes_sent;
    s_ctx.stats.bytes_received += bytes_received;
    xSemaphoreGive(s_ctx.mutex);
}