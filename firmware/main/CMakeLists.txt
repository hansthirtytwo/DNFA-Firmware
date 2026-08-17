idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES bt
                    PRIV_REQUIRES nvs_flash esp_driver_gpio)