idf_component_register(SRCS "wifi_TCP.c" "pca9685.c" "main.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES driver esp_rom freertos esp_wifi esp_event nvs_flash lwip)