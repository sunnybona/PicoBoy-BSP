cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(picoboy_bsp VERSION 2.0.0 LANGUAGES C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ---- 可复用板级库 ----
add_library(picoboy STATIC
    src/pb_display.cpp
    src/pb_input.cpp
    src/pb_settings.cpp
    src/pb_rom_menu.cpp
    src/pb_flash_rom.cpp
)

# 音频后端：0=单 GPIO PWM 当 DAC（默认），1=PIO I2S（外部 I2S DAC）。
set(PB_AUDIO_I2S 0 CACHE STRING "Audio backend: 0=PWM DAC, 1=PIO I2S")
set_property(CACHE PB_AUDIO_I2S PROPERTY STRINGS 0 1)
if(PB_AUDIO_I2S)
    target_sources(picoboy PRIVATE src/pb_audio_i2s.cpp)
    target_compile_definitions(picoboy PUBLIC PB_AUDIO_I2S=1)
    pico_generate_pio_header(picoboy ${CMAKE_CURRENT_LIST_DIR}/src/pb_i2s.pio)
else()
    target_sources(picoboy PRIVATE src/pb_audio.cpp)
    target_compile_definitions(picoboy PUBLIC PB_AUDIO_I2S=0)
endif()

target_include_directories(picoboy PUBLIC include)

# 面板模式：0=240x240，1=240x320。PUBLIC 传出，所有编译单元看到同一分辨率。
set(PB_SCREEN_MODE 0 CACHE STRING "LCD profile: 0=240x240, 1=240x320")
set_property(CACHE PB_SCREEN_MODE PROPERTY STRINGS 0 1)
target_compile_definitions(picoboy PUBLIC PB_SCREEN_MODE=${PB_SCREEN_MODE})

# 面板旋转：0=竖屏/原生，1=横屏 90°（240x320 -> 320x240，完整显示 NES 256x240）。
set(PB_LCD_ROTATE 0 CACHE STRING "LCD rotation: 0=portrait, 1=landscape 90")
set_property(CACHE PB_LCD_ROTATE PROPERTY STRINGS 0 1)
target_compile_definitions(picoboy PUBLIC PB_LCD_ROTATE=${PB_LCD_ROTATE})

# 面板反色：1=发 ST7789 0x21（默认，多数面板需要），0=发 0x20。颜色发反时翻转。
set(PB_LCD_INVERT 1 CACHE STRING "LCD invert: 1=0x21 on, 0=0x20 off")
set_property(CACHE PB_LCD_INVERT PROPERTY STRINGS 0 1)
target_compile_definitions(picoboy PUBLIC PB_LCD_INVERT=${PB_LCD_INVERT})

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
