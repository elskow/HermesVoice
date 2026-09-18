if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "sim/node_led_sim.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log
    )
else()
    idf_component_register(
        SRCS "src/node_led.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log esp_driver_gpio esp_timer
    )
endif()
