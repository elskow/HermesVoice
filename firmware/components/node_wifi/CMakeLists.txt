if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_wifi_sim.c" "sim/support.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config esp_event esp_netif
    )
else()
    idf_component_register(
        SRCS "src/node_wifi.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config esp_wifi esp_netif esp_event nvs_flash esp_eth
    )
endif()
