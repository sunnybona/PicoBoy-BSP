/*
 * demo_dual.cpp — PicoBoy BSP 双核 + 逐行流式演示
 *
 * 分工：
 *   core0：合成 160x144 的源画面，逐行喂给 BSP
 *   core1：由 pb_display_start_pusher 启动，负责把 1.5 倍放大后的带推给面板
 *
 * 画面分两段，一个文件演示两条推线接口：
 *   源线 0..95   -> pb_push_gb_line()  传 160 个「调色板色号」(0..3)，BSP 查四档绿
 *   源线 96..143 -> pb_push_rgb_line() 传 160 个 RGB565，逐像素真实色
 *
 * 下半屏的彩条是**色彩自检图**：8 条色带 + 从上到下由暗到亮。
 * 如果红蓝互换、整体反相、或某条色带不对，就是面板色序/MADCTL/PB_LCD_INVERT 的问题。
 *
 * 操作：
 *   方向键：推一下弹跳方块
 *   A 键  ：切换面板反色（0x21 / 0x20）
 *
 * 注意：流式模式下 pb_fb 被当作「带池」复用，所以这一路不能再调 pb_text /
 * pb_clear / pb_display_flush（那是整帧缓冲那条路，两者不能混用）。
 */
#include "pico/stdlib.h"

#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"

#define PAL_ROWS   96     /* 调色板路径占的源线数，剩下 48 行给 RGB565 */

/* 把 RGB565 各通道按百分比压暗 */
static uint16_t dim565(uint16_t c, int pct)
{
    int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = r * pct / 100;
    g = g * pct / 100;
    b = b * pct / 100;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

int main(void)
{
    stdio_init_all();

    pb_display_init();
    pb_input_init();
    pb_backlight(200);

    pb_display_set_invert(true);     /* 绿色液晶 pattern，换成彩色内容时也保持一致 */
    pb_display_start_pusher();       /* 启动 core1 推屏 */

    const int S = 36;                /* 方块边长（GB 源坐标） */
    int bx = 20, by = 20;
    int vx = 2, vy = 2;
    bool inv = true;

    /* 色彩自检的 8 条色带 */
    static const uint16_t BARS[8] = {
        PB_WHITE, PB_YELLOW, PB_CYAN, PB_GREEN,
        PB_MAGENTA, PB_RED, PB_BLUE, PB_RGB(0x80, 0x80, 0x80),
    };

    while(true)
    {
        pb_input_poll();

        /* 方向键给方块加一点位移 */
        if(pb_input_held(PB_BTN_LEFT))  bx -= 3;
        if(pb_input_held(PB_BTN_RIGHT)) bx += 3;
        if(pb_input_held(PB_BTN_UP))    by -= 3;
        if(pb_input_held(PB_BTN_DOWN))  by += 3;

        /* A 键切换反色（在 begin_frame 之前调用，内部会先等 core1 空闲） */
        if(pb_input_just_pressed(PB_BTN_A))
        {
            inv = !inv;
            pb_display_set_invert(inv);
        }

        /* 自动弹跳（限制在调色板上半屏内） */
        bx += vx;
        by += vy;
        if(bx < 0 || bx > 160 - S) { bx -= vx; vx = -vx; }
        if(by < 0 || by > PAL_ROWS - S) { by -= vy; vy = -vy; }
        if(bx < 0) bx = 0;  if(bx > 160 - S) bx = 160 - S;
        if(by < 0) by = 0;  if(by > PAL_ROWS - S) by = PAL_ROWS - S;

        pb_display_begin_frame();

        /* 上半屏：GB 调色板路径（每像素 0..3） */
        for(int y = 0; y < PAL_ROWS; y++)
        {
            uint8_t line[160];
            for(int x = 0; x < 160; x++)
            {
                if(x >= bx && x < bx + S && y >= by && y < by + S)
                    line[x] = 3;                                   /* 最深绿方块 */
                else
                    line[x] = ((x / 12) + (y / 12)) & 1 ? 1 : 0;    /* 浅绿棋盘 */
            }
            pb_push_gb_line(line, (uint8_t)y);
        }

        /* 下半屏：RGB565 真实色路径，8 条色带 + 由暗到亮 */
        for(int y = PAL_ROWS; y < 144; y++)
        {
            uint16_t line[160];
            int pct = 35 + (y - PAL_ROWS) * 65 / (143 - PAL_ROWS);  /* 35% -> 100% */
            for(int x = 0; x < 160; x++)
                line[x] = dim565(BARS[x / 20], pct);
            pb_push_rgb_line(line, (uint8_t)y);
        }

        pb_display_end_frame();
    }
}
