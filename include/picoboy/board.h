/*
 * board.h — PicoBoy 板级引脚与硬件常量（RP2040 + ST7789 240x240）
 *
 * 所有引脚都可在编译时用 -D 覆盖，例如：
 *   cmake .. -DPB_PIN_LCD_SCK=6
 * 也可直接在此改默认值。
 */
#ifndef PIC0BOY_B0ARD_H
#define PIC0BOY_B0ARD_H

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

/* 面板方向：0x00 正常；0x60 横屏；0xC0 上下翻；0xA0 左右翻 */
#ifndef PB_LCD_MADCTL
#define PB_LCD_MADCTL    0x00
#endif
/* 反色：颜色发反的面板设 1 */
#ifndef PB_LCD_INVERT
#define PB_LCD_INVERT    1
#endif
/* GB 画面缩放：1 = 1.5 倍最近邻铺满宽度（240x216，需双核）；0 = 160x144 居中 */
#ifndef PB_DISPLAY_SCALE15
#define PB_DISPLAY_SCALE15 1
#endif

#define PB_SCREEN_W 240
#define PB_SCREEN_H 240

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
