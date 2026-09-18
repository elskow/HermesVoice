cmake_minimum_required(VERSION 3.16)

set(SUPPORTED_TARGETS esp32 linux)

# Top-level project file. Keep thin: toolchain + project() only.
# Per-component build logic lives in main/CMakeLists.txt and
# components/*/CMakeLists.txt. Do NOT add sources here.
# Linux sim needs COMPONENTS=main: stops IDF auto-registering every
# chip-only component (analog comparators etc.) that has no linux port.
if("${IDF_TARGET}" STREQUAL "linux" OR "$ENV{IDF_TARGET}" STREQUAL "linux")
    set(COMPONENTS main)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Firmware version: CI tags override via -DPROJECT_VER, else fallback.
if(NOT PROJECT_VER)
    execute_process(
        COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/tools/gen_version.py
        OUTPUT_VARIABLE PROJECT_VER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

project(voice-node VERSION ${PROJECT_VER})
