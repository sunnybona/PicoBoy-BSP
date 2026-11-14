# PicoBoy BSP — 拿来即用的板级驱动库

把已验证的 **ST7789 显示 / PWM 音频 / 按键 / 菜单** 抽成独立库，做新程序时直接链接即可。
平台：RP2040 + ST7789 240×240，pico-sdk。

## 目录结构

```
picoboy_bsp/
├── CMakeLists.txt
├── pico_sdk_import.cmake
├── include/picoboy/
│   ├── board.h          # 引脚与硬件常量（可 -D 覆盖）
│   ├── pb_display.h     # 显示 API
│   ├── pb_audio.h       # 音频 API
│   ├── pb_input.h       # 按键 API
│   ├── pb_menu.h        # 通用列表菜单 API
│   ├── pb_settings.h    # 数值设置界面 API（亮度/音量等）
│   └── font3x5.h        # 内置 3×5 点阵字体
├── src/
│   ├── st7789_lcd.pio   # 极简串行 PIO
│   ├── pb_display.cpp
│   ├── pb_audio.cpp
│   ├── pb_input.cpp
│   ├── pb_menu.cpp
│   └── pb_settings.cpp
└── examples/
    ├── demo.cpp        # 单核三合一演示（画图+按键+声音）
    ├── demo_dual.cpp   # 双核演示（core1 推带 + 1.5 倍放大 + 反色）
    ├── demo_menu.cpp   # 菜单组件演示
    └── demo_settings.cpp # 亮度/音量设置演示
```

## 三步接入新工程

1. 把本目录复制进你的工程，在你的 `CMakeLists.txt` 里：

```cmake
add_subdirectory(picoboy_bsp)
target_link_libraries(你的程序 PRIVATE picoboy)
```

2. 包含头文件（一个总入口或按需）：

```cpp
#include "picoboy/pb_display.h"
#include "picoboy/pb_audio.h"
#include "picoboy/pb_input.h"
```

3. 初始化后即可使用。

## API 速览

### 显示（RGB565，先画帧缓冲再 flush）

```cpp
pb_display_init();
pb_clear(PB_BLACK);                       // 或 PB_RGB(r,g,b)
pb_set_pixel(10, 20, PB_RED);
pb_fill_rect(0, 0, 100, 40, PB_BLUE);
pb_text(4, 4, "HELLO", PB_WHITE, PB_BLACK);
pb_backlight(200);                        // 0..255
pb_display_flush();                       // DMA 整帧上屏
```

帧缓冲本体 `pb_fb[240*240]` 可直接读写。省内存的流式用法：`pb_fill_screen(color)`。

### 显示：反色 + 双核 GB 流式（1.5 倍放大）

```cpp
pb_display_set_gb_palette(pal4);          // 可选：四档调色板，默认 DMG 绿
pb_display_set_invert(true);              // 面板反色 0x21（绿色液晶 pattern）
pb_display_start_pusher();                // 启动 core1 推屏
pb_display_begin_frame();
for (int line = 0; line < 144; line++)
    pb_push_gb_line(gb_source_line, line);   // 喂 160 像素源线，内部 1.5 倍放大
pb_display_end_frame();
```

`pb_push_gb_line` 用最近邻把 160×144 放大到 240×216，core0 填带、core1 推带，
推屏与计算并行。完整示例见 `examples/demo_dual.cpp`。

### 音频

```cpp
pb_audio_init();
pb_audio_set_volume(8);                   // 0..10
pb_beep(440, 200);                        // 阻塞：440Hz 响 200ms
pb_tone_start(880);  /* ... */  pb_tone_stop();   // 非阻塞后台音
pb_audio_play_sample(pcm, n, 22050);      // 阻塞播放短 PCM 音效
```

### 按键（每帧先 poll）

```cpp
pb_input_init();
pb_input_poll();
if (pb_input_held(PB_BTN_A)) { ... }
if (pb_input_just_pressed(PB_BTN_UP)) { ... }
if (pb_input_just_released(PB_BTN_B)) { ... }
```

按键位：`PB_BTN_A/B/X/Y/UP/DOWN/LEFT/RIGHT`。

### 菜单（pb_menu，上/下选择，A 确认）

```cpp
static const char *items[] = { "Start", "Settings", "About" };
pb_menu_t menu;
pb_menu_init(&menu, "MAIN MENU", items, 3);
int choice = pb_menu_run(&menu);    // 返回选中下标
```

菜单渲染在 `pb_fb` 上并用 `pb_display_flush` 上屏；若双核推屏在跑，内部会先等
core1 空闲。完整示例见 `examples/demo_menu.cpp`。

### 数值设置（pb_settings，上/下选择，左/右调节，B 退出）

```cpp
static void apply_brightness(int v) { pb_backlight((uint8_t)v); }

pb_setting_item_t items[1] = {
    { "BRIGHTNESS", 10, 255, 5, 220, apply_brightness },  // 标签,最小,最大,步进,初值,回调
};
pb_settings_t st;
pb_settings_init(&st, "SETTINGS", items, 1);
pb_settings_run(&st);
```

每项带名称、数值与进度条，调节时立即通过回调应用（背光/音量等）。
完整示例见 `examples/demo_settings.cpp`。

## 编译

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -G Ninja -B build -S .
cmake --build build
```

产物 `pb_demo.uf2`、`pb_demo_dual.uf2`、`pb_demo_menu.uf2`、
`pb_demo_settings.uf2` 可直接烧录。

## 资源占用与备注

- 整帧缓冲 240×240×2 = 115,200 字节；演示固件总 RAM 约 120KB，RP2040 264KB 仍有余量。
- 若后续做大型模拟器（内存紧张），改用 `pb_fill_screen` 等流式接口，不持有整帧缓冲。
- 引脚默认值见 `board.h`；换板只改这一个文件或用 `-D` 覆盖，无需动驱动源码。
- 按键默认直连 GPIO、高有效；I2C 手柄模式可在此基础上扩展。
