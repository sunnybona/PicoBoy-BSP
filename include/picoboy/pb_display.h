/*
 * pb_display.h — ST7789 240x240 显示 API（PIO + DMA，RGB565）
 *
 * 简单单核用法（整帧缓冲）：
 *   pb_display_init();
 *   pb_clear(PB_RGB(0,0,0));
 *   pb_set_pixel(10, 20, PB_RGB(255,0,0));
 *   pb_text(4, 4, "HELLO", PB_RGB(255,255,255), PB_RGB(0,0,0));
 *   pb_display_flush();
 *
 * 双核 GB 流式用法（1.5 倍放大，core1 推屏）：
 *   pb_display_init();
 *   pb_display_set_invert(true);
 *   pb_display_start_pusher();
 *   pb_display_begin_frame();
 *   ... 每来一条 GB 扫描线调 pb_push_gb_line(pixels, line);
 *   pb_display_end_frame();
 */
#ifndef PB_DISPLAY_H
#define PB_DISPLAY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 8 位 RGB 转 16 位 RGB565 */
#define PB_RGB(r,g,b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

/* 常用颜色（RGB565） */
#define PB_BLACK   0x0000
#define PB_WHITE   0xFFFF
#define PB_RED     0xF800
#define PB_GREEN   0x07E0
#define PB_BLUE    0x001F
#define PB_CYAN    0x07FF
#define PB_MAGENTA 0xF81F
#define PB_YELLOW  0xFFE0

/* 初始化 PIO/DMA/ST7789，并准备整帧缓冲 */
void pb_display_init(void);

/* 整帧缓冲（240*240 个 RGB565，直接读写也行；双核 GB 流也复用这块内存） */
extern uint16_t pb_fb[PB_SCREEN_W * PB_SCREEN_H];

/* ---- 面板反色与 GB 调色板 ---- */
/* 面板反色：发 ST7789 的 0x21(开)/0x20(关)，用于绿色液晶 pattern */
void pb_display_set_invert(bool on);
/* 设置 GB 四档调色板（0 最亮 -> 3 最暗），默认 DMG 经典绿 */
void pb_display_set_gb_palette(const uint16_t pal4[4]);

/* ---- 简单单核：画到帧缓冲（不会立刻上屏，需 flush） ---- */
void pb_clear(uint16_t color);
void pb_set_pixel(int x, int y, uint16_t color);
void pb_fill_rect(int x, int y, int w, int h, uint16_t color);

/* 内置 3x5 点阵文本。字符 3x5、步进 4px；只有 0-9 / A-Z / 少量符号
 * （. : - / _ ? ! + 空格），小写按大写画，其他字符按空格画。 */
void pb_text(int x, int y, const char *s, uint16_t fg, uint16_t bg);

/* 可缩放版：scale=1 等价 pb_text，scale=2 每个点画成 2x2（标题/菜单用） */
void pb_text_scaled(int x, int y, const char *s, uint8_t scale,
                    uint16_t fg, uint16_t bg);

/* 字符串按 scale 缩放后的像素宽度（末字符不含字距），排版/画框用 */
int  pb_text_width(const char *s, uint8_t scale);

/* 在整屏宽度内水平居中绘制*/
void pb_text_center(int y, const char *s, uint8_t scale, uint16_t fg, uint16_t bg);

/* 背光亮度 0..255（无背光脚时无效） */
void pb_backlight(uint8_t level);

/* 把整帧缓冲通过 DMA 推到屏幕（阻塞到推完） */
void pb_display_flush(void);

/* 只把指定矩形区域推到屏幕（局部刷新，省时间）；坐标会被钳制到屏幕内 */
void pb_display_flush_region(int x, int y, int w, int h);

/* ---- 直接写屏（不经帧缓冲，省内存的流式用法） ---- */
void pb_fill_screen(uint16_t color);

/* ---- 双核 GB 流式：core1 推带，core0 填带 ---- */
/* 启动 core1 推屏 worker（在此之前先 pb_display_init） */
void pb_display_start_pusher(void);
/* 等 core1 把已交付的带全部推完（core0 直接写屏前用） */
void pb_display_wait_idle(void);
/* 一帧的开始/结束（内部做半幅乒乓交付） */
void pb_display_begin_frame(void);
void pb_display_end_frame(void);
/* 喂一条 GB 源扫描线（160 像素，低 2 位色号）；按 PB_DISPLAY_SCALE15 做 1.5 倍放大 */
void pb_push_gb_line(const uint8_t *pixels, uint8_t line);

/* 喂一条已上色（RGB565）的 GB 源扫描线：160 个颜色值，直接 1.5 倍放大上屏。
 * GBC 逐像素真实色（Walnut-CGB 的 fixPalette 查表结果）走这条路径。 */
void pb_push_rgb_line(const uint16_t *colors, uint8_t line);

/* ---- 双核 NES 流式：240x240 全屏 1:1，4 条带 × 60 行 ---- */
/* 用法（先用 pb_display_init + pb_display_start_pusher）：
 *   pb_display_nes_begin_frame();
 *   ... 每渲染完一条 NES 行，调 pb_push_nes_line(colors240, line);  line = 0..239
 *   pb_display_nes_end_frame();      // 在 InfoNES_LoadFrame() 里调
 * 带池把 pb_fb 的 115200B 全部用掉（4 槽 × 60 行 × 120 像素对），
 * 所以这种方式和 pb_fb 帧缓冲/文本绘制互斥，菜单画完再启动推屏 worker。 */
void pb_display_nes_begin_frame(void);
void pb_display_nes_end_frame(void);
/* 喂一条 240 像素的 RGB565 行（NES 256 列里居中裁剪出的 240 列），1:1 上屏 */
void pb_push_nes_line(const uint16_t *colors, uint8_t line);

#ifdef __cplusplus
}
#endif

#endif /* PB_DISPLAY_H */
