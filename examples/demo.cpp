/*
 * demo.cpp — PicoBoy BSP 三合一演示：画图 + 按键 + 声音
 *
 *  - 方向键移动方块
 *  - A 键：短音；B 键：长音
 *  - 屏幕显示坐标与按键状态
 * 把它当作 API 用法速查，改改就能做自己的程序。
 */
#include "pico/stdlib.h"
#include "picoboy/pb_display.h"
#include "picoboy/pb_audio.h"
#include "picoboy/pb_input.h"

int main(void)
{
    stdio_init_all();

    pb_display_init();
    pb_audio_init();
    pb_input_init();
    pb_audio_set_volume(8);
    pb_backlight(200);

    int x = 110, y = 110;
    const int S = 20;

    pb_beep(660, 120);

    while(true)
    {
        pb_input_poll();

        /* 方向键移动（按住连续生效） */
        if(pb_input_held(PB_BTN_LEFT))  x -= 4;
        if(pb_input_held(PB_BTN_RIGHT)) x += 4;
        if(pb_input_held(PB_BTN_UP))    y -= 4;
        if(pb_input_held(PB_BTN_DOWN))  y += 4;

        /* 边界 */
        if(x < 0) x = 0;
        if(y < 0) y = 0;
        if(x > PB_SCREEN_W - S) x = PB_SCREEN_W - S;
        if(y > PB_SCREEN_H - S) y = PB_SCREEN_H - S;

        /* 按键音效（单次触发） */
        if(pb_input_just_pressed(PB_BTN_A)) pb_beep(880, 80);
        if(pb_input_just_pressed(PB_BTN_B)) pb_beep(440, 250);

        /* 画到帧缓冲 */
        pb_clear(PB_BLACK);
        pb_text(6, 6, "PICOBOY BSP", PB_GREEN, PB_BLACK);
        pb_fill_rect(x, y, S, S, PB_RED);
        pb_fill_rect(0, 232, 240, 8, PB_BLUE);

        /* 整帧上屏 */
        pb_display_flush();

        sleep_ms(16);
    }
}
