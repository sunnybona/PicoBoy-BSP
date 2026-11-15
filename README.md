# PicoBoy BSP — 拿来即用的板级驱动库（v2.0）

把已验证的 **ST7789 显示 / PWM 音频 / 按键 / ROM 菜单 / 设置 / flash ROM 扫描** 抽成独立库，
做新程序时直接链接即可。平台：RP2040 + ST7789，pico-sdk；支持 **240×240 与 240×320 两种面板**，
并可 **90° 横屏旋转**（240×320 → 320×240，完整显示 NES 256×240），编译期开关切换。

当前版本：**v2.0**（版本宏见 `include/picoboy/board.h`，项目版本见 `CMakeLists.txt`）。

## 面板模式（240×240 / 240×320）

用 `PB_SCREEN_MODE` 在编译期选择，宽度两种模式都是 240，只变高度：

| `PB_SCREEN_MODE` | 面板 | 帧缓冲 | GB 1.5x 顶边 | NES 顶边 |
|---|---|---|---|---|
| `0`（默认） | 240×240 | 115,200 B | 12 | 0 |
| `1` | 240×320 | 153,600 B | 52 | 40 |

CMake 构建时传开关（见下文“编译”）：

```bash
cmake -G Ninja -B build320 -S . -DPB_SCREEN_MODE=1
```

作为子目录引入时，在 `add_subdirectory` 前设置同名缓存变量即可。也可直接用 `-D`
覆盖，或在 `board.h` 改 `PB_SCREEN_MODE` 默认值。

- GB/GBC 1.5x 内容固定 240×216，NES 内容固定 240×240；在 320 面板上自动垂直居中，
  非缩放 160×144 同样居中。
- 若 240×240 面板在 240×320 GRAM 里需要行/列偏移，用 `PB_LCD_X_OFFSET` /
  `PB_LCD_Y_OFFSET`（默认 0，可 `-D` 覆盖）。
- 菜单/设置界面按 240 高排版，320 面板上顶部对齐、完整可见不溢出，底部留空。

## 横屏旋转（PB_LCD_ROTATE）

用 `PB_LCD_ROTATE=1` 把 240×320 面板旋转 90°，逻辑分辨率变为 **320×240 横屏**，可完整显示
NES 的 256×240（取 256×240 帧缓冲居中，左右各留 32px 黑边；RAM 装不下完整 320×240）。

```bash
cmake -G Ninja -B buildls -S . -DPB_SCREEN_MODE=1 -DPB_LCD_ROTATE=1
```

- 旋转在 `lcd_set_window` 内部按逻辑坐标换算，第三方核心直接用逻辑 (x,y) 即可，无需手动换轴。
- 帧缓冲宽高宏（`PB_FB_W/H`、逻辑宽高）随旋转自动切换，菜单/设置页在横屏上正常排版。
- 反色 `PB_LCD_INVERT`（默认 1，发 0x21）与旋转互不影响，可叠加。

## 目录结构

```
picoboy_bsp/
├── CMakeLists.txt          # 独立构建：库 + 三个示例（project VERSION 2.0）
├── pico_sdk_import.cmake
├── include/picoboy/
│   ├── board.h            # 引脚/硬件常量 + 版本宏（可 -D 覆盖）
│   ├── pb_display.h       # 显示 API（含 GB / RGB / NES 三条流式接口）
│   ├── pb_audio.h         # 音频 API
│   ├── pb_input.h         # 按键 API
│   ├── pb_rom_menu.h      # 多 ROM 选择菜单（GB / NES 共用）
│   ├── pb_settings.h      # 亮度/音量/反色设置 + flash 持久化
│   ├── pb_flash_rom.h     # flash 上 ustar tar 扫描（运行时读 ROM，XIP 直读）
│   └── font3x5.h          # 内置 3×5 点阵字体
├── src/
│   ├── st7789_lcd.pio     # 极简串行 PIO
│   ├── pb_display.cpp
│   ├── pb_audio.cpp
│   ├── pb_input.cpp
│   ├── pb_rom_menu.cpp
│   ├── pb_settings.cpp
│   └── pb_flash_rom.cpp
└── examples/
    ├── demo.cpp           # 单核三合一（画图 + 按键 + 声音）
    ├── demo_dual.cpp      # 双核（core1 推带 + 1.5x + 反色 + GB/RGB 两条流）
    └── settings_demo.cpp  # 亮度/音量设置 + flash 掉电保存
```

## 三步接入新工程

1. 把本目录复制进工程，在你的 `CMakeLists.txt` 里：

```cmake
add_subdirectory(picoboy_bsp)
target_link_libraries(你的程序 PRIVATE picoboy)
```

2. 按需包含头文件：

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
pb_clear(PB_BLACK);                        // 或 PB_RGB(r,g,b)
pb_set_pixel(10, 20, PB_RED);
pb_fill_rect(0, 0, 100, 40, PB_BLUE);
pb_text(4, 4, "HELLO", PB_WHITE, PB_BLACK);
pb_text_scaled(4, 40, "BIG", 2, PB_WHITE, PB_BLACK);  // 2 倍字形
pb_text_center(70, "CENTER", 2, PB_YELLOW, PB_BLACK); // 水平居中
pb_backlight(200);                         // 0..255
pb_display_flush();                        // DMA 整帧上屏
```

帧缓冲本体 `pb_fb`（240×240 或 240×320，随面板模式）可直接读写；省内存的流式用法：
`pb_fill_screen(color)`。

### 显示：双核流式（core1 推带，core0 填带）

GB / GBC（160×144 → 1.5x 最近邻放大到 240×216，在面板里垂直居中）：

```cpp
pb_display_set_invert(true);               // 面板反色 0x21（绿色液晶 pattern）
pb_display_start_pusher();                 // 启动 core1 推屏
pb_display_begin_frame();
for (int line = 0; line < 144; line++) {
    pb_push_gb_line(gb_palette_line, line);   // 160 个色号 0..3，BSP 查四档绿
    // 或 GBC 逐像素真实色：
    pb_push_rgb_line(rgb565_line, line);      // 160 个 RGB565，直接放大上屏
}
pb_display_end_frame();
```

NES（240×240 内容 1:1，4 条带 × 60 行，在 320 面板上垂直居中）：

```cpp
pb_display_start_pusher();
pb_display_nes_begin_frame();
for (int line = 0; line < 240; line++)
    pb_push_nes_line(rgb240_line, line);     // 240 个 RGB565
pb_display_nes_end_frame();
```

注意：流式模式下 `pb_fb` 被当作带池复用，不能再与 `pb_clear / pb_text / pb_display_flush`
混用。完整示例见 `examples/demo_dual.cpp`。

### 音频（PWM 当 DAC，DMA 环形缓冲）

```cpp
pb_audio_init();
pb_audio_set_volume(8);                     // 0..10
pb_audio_submit(mono_pcm, n);               // 非阻塞：连续游戏音频，每帧写环
pb_beep(440, 200);                          // 阻塞：440Hz 响 200ms
pb_tone_start(880); /* ... */ pb_tone_stop();   // 非阻塞后台音
pb_audio_play_sample(pcm, n, 22050);        // 阻塞：短 PCM 音效
```

### 按键（每帧先 poll）

```cpp
pb_input_init();
pb_input_poll();
if (pb_input_held(PB_BTN_A)) { ... }
if (pb_input_just_pressed(PB_BTN_UP)) { ... }
if (pb_input_just_released(PB_BTN_B)) { ... }
```

按键位：`PB_BTN_A/B/X/Y/UP/DOWN/LEFT/RIGHT`，直连 GPIO、高有效。

### ROM 菜单（pb_rom_menu，GB / NES 共用）

```cpp
static pb_menu_item_t items[] = {
    { "GAME ONE", "INFO LINE" },
    { "GAME TWO", NULL },
};
pb_menu_style_t style = {
    "PICOBOY", "SELECT ROM", "U/D MOVE  A OK", PB_WHITE
};
pb_rom_menu_set_setup_cb(setup_cb);   // 可选：Y 键进设置；NULL 则 Y 无效
int choice = pb_rom_menu_select(&style, items, 2);  // 返回选中下标
pb_rom_menu_splash(items[choice].name);             // 进游戏过场
```

版式（标题栏、2 倍字形、序号、强调条、滚动三角、底部提示）由模块固定，因此两个模拟器
菜单风格天然一致。

### 设置（pb_settings，flash 持久化）

```cpp
pb_settings_init();                 // 读 flash + 立即应用（无有效数据用默认值，不写 flash）
/* ... 菜单阶段按 Y： */
int changed = pb_settings_menu();   // 全屏设置页，上下换项、左右调档/切换，A/B/Y 返回
if (changed) pb_settings_save();    // 落盘：值没变自动跳过
const pb_settings_t *st = pb_settings_get();   // 只读 brightness / volume / invert
```

亮度 1..10 档（0 档会关死背光，故不给），音量 0..10 档，反色 INVERT ON/OFF；值存在 flash
最后一扇区。反色出厂默认取编译期 `PB_LCD_INVERT`，菜单内可随时改，立即调 `pb_display_set_invert`。
**限制**：`pb_settings_save()` 擦写时 XIP 停止，只能在 core1 推屏 worker 未启动时调用。

### flash ROM 扫描（pb_flash_rom，运行时读 ROM）

ROM 不再编译期内嵌。`rom_uploader.py` 把多个游戏打成一个**未压缩 ustar tar** 写到固定 flash
ROM 区，本组件把 tar 头扫成条目表，数据指针指向 XIP，**一个字节都不进 RAM**。

```cpp
static pb_flash_rom_entry_t roms[64];
int n = pb_flash_rom_scan(roms, 64, my_validator);  // 校验回调可传 NULL 全保留
for (int i = 0; i < n; i++)
    load(roms[i].data, roms[i].size);               // 直接 XIP 读取
```

- ROM 区基址 `PB_ROM_AREA_ADDR`（默认 0x10120000，与 uploader 对齐，可 `-D` 覆盖）。
- `pb_flash_rom_is_tar()` 判首块 ustar 魔数；扫描到 tar 零块结束符即停，末尾 64KB 保留区不踩。
- 条目含 `name / data / size`；校验回调签名 `bool(const uint8_t*, uint32_t)`，返回 false 丢弃。

## 编译

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
# 默认 240×240
cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Release
# 240×320 面板
cmake -G Ninja -B build320 -S . -DCMAKE_BUILD_TYPE=Release -DPB_SCREEN_MODE=1
# 320×240 横屏（在 240×320 面板上旋转 90°）
cmake -G Ninja -B buildls -S . -DCMAKE_BUILD_TYPE=Release -DPB_SCREEN_MODE=1 -DPB_LCD_ROTATE=1
# 反色默认 PB_LCD_INVERT=1，需要关掉可加 -DPB_LCD_INVERT=0
cmake --build build
```

产物 `pb_demo.uf2`、`pb_demo_dual.uf2`、`pb_settings_demo.uf2` 可直接烧录。

## v1.0 → v2.0 变更

- **菜单重构**：通用 `pb_menu` 升级为定版外观的 `pb_rom_menu`（GB/NES 共用），支持
  标题/副标题/每项信息、Y 键设置回调、进游戏 splash 与 PC 预览渲染。
- **设置持久化**：`pb_settings` 改为单例接口，亮度/音量存 flash 最后一扇区，上电自动
  读回，值未变不擦写。
- **新增流式接口**：`pb_push_rgb_line`（GBC 逐像素 RGB565）、`pb_push_nes_line` 与
  NES 带帧（240×240 1:1）。
- **flash 容量**：默认按 16MB 板定义 `PICO_FLASH_SIZE_BYTES`，可 `-DPB_FLASH_SIZE_BYTES=`
  覆盖；链接 `hardware_flash`。
- **版本号**：`board.h` 新增 `PICOBOY_BSP_VERSION_*` 宏，CMake project 标记 2.0.0。

## 本次更新：运行时 flash ROM、横屏旋转、反色菜单

- **运行时 flash ROM**：新增 `pb_flash_rom` 组件，扫描 `rom_uploader.py` 写入的未压缩 ustar tar，
  替代编译期内嵌 ROM；XIP 直读不占 RAM，GB/GBC 与 NES 共用同一套 BSP。
- **横屏旋转**：新增 `PB_LCD_ROTATE`，240×320 旋转为 320×240，完整显示 NES 256×240；
  `lcd_set_window` 直接吃逻辑坐标，新增显示 `pb_display_push_rect / pb_display_set_madctl`。
- **反色菜单化**：设置页新增第三项 INVERT（ON/OFF），`pb_settings_t` 加 `invert` 字段并参与校验，
  出厂默认取 `PB_LCD_INVERT`；新增 `pb_settings_set_invert()`。
- **死机修复**：改 invert 后按 A/B/Y 退出会死机，根因是 flash 整页写用栈顶缓冲、256B 读取越过
  SRAM 末端触发总线错误；改用静态 RAM 页缓冲（先填 0xFF 再拷贝），两几何均验证通过。

## 资源占用与备注

- 整帧缓冲：240×240×2 = 115,200 字节，240×320×2 = 153,600 字节。240 模式示例固件
  text 约 31–42KB、bss 约 122–126KB；320 模式 bss 增加 38,400 字节（约 160–164KB），
  RP2040 264KB RAM 仍有余量。
- 内存紧张时改用 `pb_fill_screen` / 流式接口，不持有整帧缓冲。
- 引脚默认值见 `board.h`；换板只改这一个文件或用 `-D` 覆盖，无需动驱动源码。
