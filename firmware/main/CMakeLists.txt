set(MAIN_SRCS "app_main.c" "page.c" "errors.c")
if("${IDF_TARGET}" STREQUAL "linux")
    list(APPEND MAIN_SRCS "sim_main.c" "app_test.c")
else()
    list(APPEND MAIN_SRCS "main.c")
endif()
idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        node_config
        node_wifi
        node_mqtt
        node_ota
        node_ptt
        node_audio
        node_led
        node_display
        node_voice
        node_health
        node_log
        node_provision
    esp_timer
)
