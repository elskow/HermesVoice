if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_voice_sim.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config node_audio
    )
else()
    idf_component_register(
        SRCS "src/node_voice.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log node_config node_audio esp_http_client esp_timer
    )
endif()
