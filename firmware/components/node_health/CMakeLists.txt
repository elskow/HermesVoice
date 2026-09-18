if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_health_sim.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config node_mqtt node_wifi
    )
else()
    idf_component_register(
        SRCS "src/node_health.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config node_mqtt node_wifi node_ota esp_timer
    )
endif()
