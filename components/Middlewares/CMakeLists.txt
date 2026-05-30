set(src_dirs
            ENV
            )

set(include_dirs
           ENV
           )

set(requires
            BSP
            esp_timer
            )


idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})
