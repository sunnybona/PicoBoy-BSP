cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(picoboy_bsp VERSION 2.0.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ---- 可复用板级库 ----
add_library(picoboy STATIC
    src/pb_display.cpp
    src/pb_audio.cpp
    src/pb_input.cpp
    src/pb_settings.cpp
    src/pb_rom_menu.cpp
)

target_include_directories(picoboy PUBLIC include)

# PicoBoy 板载 flash 是 16MB，SDK 默认按 2MB 算。pb_settings 要把设置放最后
# 一扇区，必须知道真实容量；顺带让 flash.c 的范围断言正确。
# 可在 configure 时 -DPB_FLASH_SIZE_BYTES=... 覆盖（例如换回 2MB 板）。
if(NOT DEFINED PB_FLASH_SIZE_BYTES)
    set(PB_FLASH_SIZE_BYTES 16777216)
endif()
target_compile_definitions(picoboy PUBLIC PICO_FLASH_SIZE_BYTES=${PB_FLASH_SIZE_BYTES})

# 生成 PIO 头（st7789_lcd.pio.h），挂到 picoboy 目标
pico_generate_pio_header(picoboy ${CMAKE_CURRENT_LIST_DIR}/src/st7789_lcd.pio)

target_link_libraries(picoboy PUBLIC
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_pwm
    hardware_dma
    hardware_clocks
    hardware_flash
)

# ---- 演示程序：三合一（画图 + 按键 + 声音） ----
add_executable(pb_demo examples/demo.cpp)
target_link_libraries(pb_demo PRIVATE picoboy)
pico_enable_stdio_usb(pb_demo 1)
pico_enable_stdio_uart(pb_demo 0)
pico_add_extra_outputs(pb_demo)

# ---- 双核演示（core1 推带 + 1.5x 放大 + 反色 + GB/RGB 两条流式接口） ----
add_executable(pb_demo_dual examples/demo_dual.cpp)
target_link_libraries(pb_demo_dual PRIVATE picoboy)
pico_enable_stdio_usb(pb_demo_dual 1)
pico_enable_stdio_uart(pb_demo_dual 0)
pico_add_extra_outputs(pb_demo_dual)

# ---- 亮度/音量设置演示（flash 持久化） ----
add_executable(pb_settings_demo examples/settings_demo.cpp)
target_link_libraries(pb_settings_demo PRIVATE picoboy)
pico_enable_stdio_usb(pb_settings_demo 1)
pico_enable_stdio_uart(pb_settings_demo 0)
pico_add_extra_outputs(pb_settings_demo)
