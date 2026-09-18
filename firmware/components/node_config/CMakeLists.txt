if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_config_sim.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log
    )
else()
    idf_component_register(
        SRCS "src/node_config.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log nvs_flash
    )
endif()
