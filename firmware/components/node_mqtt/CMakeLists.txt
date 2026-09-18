if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_mqtt_sim.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config
    )
else()
    idf_component_register(
        SRCS "src/node_mqtt.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config mqtt
    )
endif()
