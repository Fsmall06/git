set(src_dirs
            IIC
            BME690
            )

set(include_dirs
           IIC
           BME690
            )

set(requires
            esp_driver_i2c
            driver
            )

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
