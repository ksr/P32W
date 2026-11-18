# The debug card's firmware for its Raspberry Pi Pico 2 (D135): hardware/debug-card/firmware/README.md.
#
#   export PICO_SDK_PATH=~/pico-sdk          (the Raspberry Pi Pico SDK, 2.x)
#   cmake -S . -B build && cmake --build build      ->  build/dbgcard.uf2
cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico2)
include(pico_sdk_import.cmake)
project(dbgcard C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(dbgcard dbgcard/pico.c dbgcard/core.c)
target_link_libraries(dbgcard pico_stdlib)
pico_enable_stdio_usb(dbgcard 1)
pico_enable_stdio_uart(dbgcard 0)
pico_add_extra_outputs(dbgcard)
