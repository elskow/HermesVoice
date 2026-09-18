if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_audio_sim.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log
    )
else()
    idf_component_register(
        SRCS "src/node_audio.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log esp_driver_i2s esp_timer
    )
endif()
