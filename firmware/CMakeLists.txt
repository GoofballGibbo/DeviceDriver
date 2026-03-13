cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD waveshare_rp2350_one)

include(pico_sdk_import.cmake)

project(uniproj C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(uniprojfw
    uniprojfw.c
)

pico_enable_stdio_usb(uniprojfw 1)
pico_enable_stdio_uart(uniprojfw 1)

pico_generate_pio_header(uniprojfw ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/generated)

pico_add_extra_outputs(uniprojfw)

target_link_libraries(uniprojfw pico_stdlib hardware_pio)
