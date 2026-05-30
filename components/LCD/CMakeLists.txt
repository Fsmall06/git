set(src_dirs
            .
            Driver
            Font
            )

set(include_dirs
            .
            Driver
            UI
            Font
            )

set(requires
            esp_driver_spi
            esp_driver_gpio
            freertos
            heap
            log
            )

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
