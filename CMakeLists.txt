cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico_w)

include(C:/pico/pico-sdk/external/pico_sdk_import.cmake)

project(pico_mqtt C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(pico_mqtt
    src/main.c
)

target_include_directories(pico_mqtt PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(pico_mqtt
    pico_stdlib
    hardware_adc
    pico_cyw43_arch_lwip_poll
    pico_lwip_mqtt
)

pico_enable_stdio_usb(pico_mqtt 1)
pico_enable_stdio_uart(pico_mqtt 0)

pico_add_extra_outputs(pico_mqtt)