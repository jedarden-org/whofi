idf_component_register(SRCS "src/web_server.c"
                       INCLUDE_DIRS "include" "src"
                       REQUIRES esp_http_server esp_wifi json nvs_flash
                       PRIV_REQUIRES "unity" "csi_collector")