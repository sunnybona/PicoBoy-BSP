cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(picoboy_bsp C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ---- 可复用板级库 ----
add_library(picoboy STATIC
    src/pb_display.cpp
    src/pb_audio.cpp
    src/pb_input.cpp
    src/pb_menu.cpp
    src/pb_settings.cpp
)

target_include_directories(picoboy PUBLIC include)

# 生成 PIO 头（st7789_lcd.pio.h），挂到 picoboy 目标
pico_generate_pio_header(picoboy ${CMAKE_CURRENT_LIST_DIR}/src/st7789_lcd.pio)

target_link_libraries(picoboy PUBLIC
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_pwm
    hardware_dma
    hardware_clocks
)

# ---- 演示程序 ----
add_executable(pb_demo examples/demo.cpp)
target_link_libraries(pb_demo PRIVATE picoboy)

pico_enable_stdio_usb(pb_demo 1)
pico_enable_stdio_uart(pb_demo 0)

pico_add_extra_outputs(pb_demo)

# ---- 双核演示程序（core1 推带 + invert + GB 流式接口） ----
add_executable(pb_demo_dual examples/demo_dual.cpp)
target_link_libraries(pb_demo_dual PRIVATE picoboy)

pico_enable_stdio_usb(pb_demo_dual 1)
pico_enable_stdio_uart(pb_demo_dual 0)

pico_add_extra_outputs(pb_demo_dual)

# ---- 菜单组件演示 ----
add_executable(pb_demo_menu examples/demo_menu.cpp)
target_link_libraries(pb_demo_menu PRIVATE picoboy)

pico_enable_stdio_usb(pb_demo_menu 1)
pico_enable_stdio_uart(pb_demo_menu 0)

pico_add_extra_outputs(pb_demo_menu)

# ---- 亮度/音量设置演示 ----
add_executable(pb_demo_settings examples/demo_settings.cpp)
target_link_libraries(pb_demo_settings PRIVATE picoboy)

pico_enable_stdio_usb(pb_demo_settings 1)
pico_enable_stdio_uart(pb_demo_settings 0)

pico_add_extra_outputs(pb_demo_settings)
