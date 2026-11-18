# The Raspberry Pi Pico SDK, from PICO_SDK_PATH (the environment's or cmake's -D). A shortened form of the
# SDK's own external/pico_sdk_import.cmake: no fetching from git.
if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()
if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR "Set PICO_SDK_PATH to the Raspberry Pi Pico SDK (2.x)")
endif ()
get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_SDK_PATH}/pico_sdk_init.cmake)
    message(FATAL_ERROR "${PICO_SDK_PATH} is not the Pico SDK (no pico_sdk_init.cmake)")
endif ()
set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "the Raspberry Pi Pico SDK" FORCE)
include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
