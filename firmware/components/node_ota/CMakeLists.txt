if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_ota_sim.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config
    )
else()
    idf_component_register(
        SRCS "src/node_ota.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config app_update esp_http_client mbedtls
    )
endif()
