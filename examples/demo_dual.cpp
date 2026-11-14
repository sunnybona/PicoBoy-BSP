/*
 * demo_dual.cpp — PicoBoy BSP 双核演示
 *
 * 分工：
 *   core0：合成一条 160x144 的 GB 画面（低 2 位色号），逐行喂给 BSP
 *   core1：由 pb_display_start_pusher 启动，负责把 1.5 倍放大后的带推给面板
 *
 * 操作：
 *   方向键：推一下弹跳方块
 *   A 键：切换面板反色（绿色液晶 pattern / 正常）
 *
 * 这个文件就是“双核 + 放大 + invert”的最小用法速查。
 */
#include "pico/stdlib.h"
#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"

int main(void)
{
    stdio_init_all();

    pb_display_init();
    pb_input_init();
    pb_backlight(200);

    pb_display_set_invert(true);     /* 绿色液晶 pattern */
    pb_display_start_pusher();       /* 启动 core1 推屏 */

    const int S = 36;                /* 方块边长（GB 源坐标） */
    int bx = 20, by = 20;
    int vx = 2, vy = 2;
    bool inv = true;

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

        /* 自动弹跳 */
        bx += vx;
        by += vy;
        if(bx < 0 || bx > 160 - S) { bx -= vx; vx = -vx; }
        if(by < 0 || by > 144 - S){ by -= vy; vy = -vy; }
        if(bx < 0) bx = 0;  if(bx > 160 - S) bx = 160 - S;
        if(by < 0) by = 0;  if(by > 144 - S) by = 144 - S;

        /* 一帧：逐行喂 GB 源线，BSP 内部做 1.5 倍最近邻 + 半幅乒乓 */
        pb_display_begin_frame();
        for(int y = 0; y < 144; y++)
        {
            uint8_t line[160];
            for(int x = 0; x < 160; x++)
            {
                if(x >= bx && x < bx + S && y >= by && y < by + S)
                    line[x] = 3;                          /* 最深绿方块 */
                else
                    line[x] = ((x / 12) + (y / 12)) & 1 ? 1 : 0;  /* 浅绿棋盘 */
            }
            pb_push_gb_line(line, (uint8_t)y);
        }
        pb_display_end_frame();
    }
}
