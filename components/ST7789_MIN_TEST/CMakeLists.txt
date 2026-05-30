idf_component_register(SRCS "st7789_min_test.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_gpio esp_driver_spi freertos heap log)
