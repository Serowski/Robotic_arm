idf_component_register(SRCS "pca9685.c" "main.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES driver esp_rom freertos)