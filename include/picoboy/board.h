/*
 * board.h — PicoBoy 板级引脚与硬件常量（RP2040 + ST7789，240x240 / 240x320）
 *
 * 面板用 PB_SCREEN_MODE 选择：0=240x240（默认），1=240x320。
 *
 * 所有引脚都可在编译时用 -D 覆盖，例如：
 *   cmake .. -DPB_PIN_LCD_SCK=6
 * 也可直接在此改默认值。
 */
#ifndef PIC0BOY_B0ARD_H
#define PIC0BOY_B0ARD_H

/* ---- BSP 版本（版本号变更时只改这里） ---- */
#define PICOBOY_BSP_VERSION_MAJOR 2
#define PICOBOY_BSP_VERSION_MINOR 0
#define PICOBOY_BSP_VERSION_STRING "2.0"

/* ---- 系统主频（250MHz 出厂值；供电/散热一般可改 200000） ---- */
#ifndef PB_SYS_CLOCK_KHZ
#define PB_SYS_CLOCK_KHZ 250000
#endif

/* ---- ST7789 屏幕：4 线 SPI（SCK/MOSI/CS/DC + RST/BL） ---- */
#ifndef PB_PIN_LCD_SCK
#define PB_PIN_LCD_SCK   6
#endif
#ifndef PB_PIN_LCD_MOSI
#define PB_PIN_LCD_MOSI  7
#endif
#ifndef PB_PIN_LCD_CS
#define PB_PIN_LCD_CS    5
#endif
#ifndef PB_PIN_LCD_DC
#define PB_PIN_LCD_DC    9
#endif
#ifndef PB_PIN_LCD_RST
#define PB_PIN_LCD_RST   4     /* 没有 RST 时设 -1 */
#endif
#ifndef PB_PIN_LCD_BL
#define PB_PIN_LCD_BL    12    /* 背光，-1 表示没有 */
#endif
#ifndef PB_PIN_LCD_BL_ACTIVE_HIGH
#define PB_PIN_LCD_BL_ACTIVE_HIGH 1
#endif

/* SPI 位时钟：ST7789 安全上限 62.5MHz；飞线/长排线可降到 40MHz/31.25MHz */
#ifndef PB_LCD_SPI_HZ
#define PB_LCD_SPI_HZ    62500000
#endif
/* 命令相位位时钟（保守，不超过 8MHz） */
#ifndef PB_LCD_CMD_HZ
#define PB_LCD_CMD_HZ    8000000
#endif

/* ---- 面板几何配置 ----
 * PB_SCREEN_MODE 选择物理面板：
 *   0 = 240x240（1.3/1.54 寸 ST7789）
 *   1 = 240x320（1.69/2.0 寸 ST7789）
 */
#ifndef PB_SCREEN_MODE
#define PB_SCREEN_MODE 0
#endif

/* 物理 GRAM 尺寸 */
#if PB_SCREEN_MODE == 1
#define PB_PANEL_W 240
#define PB_PANEL_H 320
#else
#define PB_PANEL_W 240
#define PB_PANEL_H 240
#endif

/* LCD 旋转：0 = 竖屏/原生；1 = 横屏 90°（240x320 变成 320x240，可完整显示
 * NES 原生 256x240）。240x240 面板旋转后几何不变。 */
#ifndef PB_LCD_ROTATE
#define PB_LCD_ROTATE 0
#endif

/* 面板方向寄存器：竖屏默认 0x00，横屏默认 0x60（MY|MV，常见横屏）。
 * 可 -D 覆盖；lcd_set_window 会按 MADCTL 的 MX/MY 位自动匹配坐标，改方向不会错位。 */
#ifndef PB_LCD_MADCTL
#if PB_LCD_ROTATE
#define PB_LCD_MADCTL    0x60
#else
#define PB_LCD_MADCTL    0x00
#endif
#endif

/* GRAM 原点偏移（物理列/行方向）：240x240 是 240x320 GRAM 的视口，部分面板要加；
 * 本 BSP 两种模式都从 0 起，需要时可 -D 覆盖。 */
#ifndef PB_LCD_X_OFFSET
#define PB_LCD_X_OFFSET   0
#endif
#ifndef PB_LCD_Y_OFFSET
#define PB_LCD_Y_OFFSET   0
#endif

/* 反色：颜色发反的面板设 1 */
#ifndef PB_LCD_INVERT
#define PB_LCD_INVERT    1
#endif
/* GB 画面缩放：1 = 1.5 倍最近邻铺满宽度（240x216，需双核）；0 = 160x144 居中 */
#ifndef PB_DISPLAY_SCALE15
#define PB_DISPLAY_SCALE15 1
#endif

/* 逻辑面板尺寸（面板完整范围；旋转时宽高交换，像素总数不变）。
 * 直接写屏(pb_fill_screen)与面板级信息用这两个。 */
#if PB_LCD_ROTATE
#define PB_SCREEN_W PB_PANEL_H
#define PB_SCREEN_H PB_PANEL_W
#else
#define PB_SCREEN_W PB_PANEL_W
#define PB_SCREEN_H PB_PANEL_H
#endif

/* 帧缓冲几何：画图/菜单/带池都在这块里。
 * 竖屏铺满面板；横屏时 RP2040 264KB RAM 装不下完整 320x240，故只取 NES 原生
 * 256x240 并水平居中（左右各留 32px 黑边），比完整 320 宽省 12800B。
 * 三个值都可 -D 覆盖。 */
#ifndef PB_FB_W
#if PB_LCD_ROTATE
#define PB_FB_W 256
#else
#define PB_FB_W PB_SCREEN_W
#endif
#endif
#ifndef PB_FB_H
#define PB_FB_H PB_SCREEN_H
#endif
#ifndef PB_FB_X
#define PB_FB_X ((PB_SCREEN_W - PB_FB_W) / 2)
#endif
#ifndef PB_FB_Y
#define PB_FB_Y ((PB_SCREEN_H - PB_FB_H) / 2)
#endif

/* ---- 音频：单 GPIO + PWM 当 DAC ---- */
#ifndef PB_PIN_AUDIO
#define PB_PIN_AUDIO     11
#endif
#ifndef PB_AUDIO_PWM_BITS
#define PB_AUDIO_PWM_BITS 12
#endif
/* 音频环形缓冲（样本数，必须是 2 的幂）。2048 ≈ 33ms。 */
#ifndef PB_AUDIO_RING
#define PB_AUDIO_RING 2048
#endif
/* 采样率 = PWM 载波：250MHz / 2^12 ≈ 61035；APU 也用同一速率。 */
#ifndef PB_AUDIO_SAMPLE_RATE
#define PB_AUDIO_SAMPLE_RATE 61035
#endif

/* ---- 按键：直连 GPIO，高电平有效（外部下拉，按下接 VCC） ---- */
#ifndef PB_PIN_BTN_A
#define PB_PIN_BTN_A      18
#endif
#ifndef PB_PIN_BTN_B
#define PB_PIN_BTN_B      19
#endif
#ifndef PB_PIN_BTN_X
#define PB_PIN_BTN_X      17
#endif
#ifndef PB_PIN_BTN_Y
#define PB_PIN_BTN_Y      16
#endif
#ifndef PB_PIN_BTN_UP
#define PB_PIN_BTN_UP     23
#endif
#ifndef PB_PIN_BTN_DOWN
#define PB_PIN_BTN_DOWN   20
#endif
#ifndef PB_PIN_BTN_LEFT
#define PB_PIN_BTN_LEFT   22
#endif
#ifndef PB_PIN_BTN_RIGHT
#define PB_PIN_BTN_RIGHT  21
#endif

#endif /* PIC0BOY_B0ARD_H */
