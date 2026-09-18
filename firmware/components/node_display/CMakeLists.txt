if("${IDF_TARGET}" STREQUAL "linux")
    idf_component_register(
        SRCS "common/display_layout.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log
    )
else()
    idf_component_register(
        SRCS "common/display_layout.c" "src/ssd1306_backend.c" "src/font8x8.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES node_log esp_driver_gpio esp_driver_i2c esp_lcd
    )
endif()
