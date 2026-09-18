if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_provision_sim.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config
    )
else()
    idf_component_register(
        SRCS "src/node_provision.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config network_provisioning protocomm esp_wifi esp_netif esp_event nvs_flash
    )
endif()
