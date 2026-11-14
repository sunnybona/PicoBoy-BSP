/*
 * demo.cpp — PicoBoy BSP 三合一演示：画图 + 按键 + 声音
 *
 *  - 方向键移动方块
 *  - A 键：短音；B 键：长音
 *  - 屏幕显示坐标与当前按下的键
 *
 * 文本接口速查（这里的标题/副标题就用了三种写法）：
 *   pb_text(8, 210, "X 110", fg, bg);            // 1 倍，原始 3x5
 *   pb_text_scaled(x, y, "BIG", 2, fg, bg);      // 2 倍，每点画成 2x2
 *   pb_text_center(y, "CENTER", 2, fg, bg);      // 整屏水平居中
 *   pb_text_width("CENTER", 2)                   // 算宽度，画框/下划线用
 */
#include "pico/stdlib.h"
#include <stdio.h>

#include "picoboy/pb_display.h"
#include "picoboy/pb_audio.h"
#include "picoboy/pb_input.h"

#define DEMO_BG PB_RGB(0x0A, 0x12, 0x18)

int main(void)
{
    stdio_init_all();

    pb_display_init();
    pb_audio_init();
    pb_input_init();
    pb_audio_set_volume(8);
    pb_backlight(200);

    int x = 110, y = 120;          /* 方块位置 */
    const int S = 20;              /* 方块边长 */

    pb_beep(660, 120);

    while(true)
    {
        pb_input_poll();

        /* 方向键移动（按住连续生效） */
        if(pb_input_held(PB_BTN_LEFT))  x -= 4;
        if(pb_input_held(PB_BTN_RIGHT)) x += 4;
        if(pb_input_held(PB_BTN_UP))    y -= 4;
        if(pb_input_held(PB_BTN_DOWN))  y += 4;

        /* 边界（上方留出标题区） */
        if(x < 0) x = 0;
        if(y < 44) y = 44;
        if(x > PB_SCREEN_W - S) x = PB_SCREEN_W - S;
        if(y > 200 - S) y = 200 - S;

        /* 按键音效（单次触发） */
        if(pb_input_just_pressed(PB_BTN_A)) pb_beep(880, 80);
        if(pb_input_just_pressed(PB_BTN_B)) pb_beep(440, 250);

        /* ---- 画面 ---- */
        pb_clear(DEMO_BG);

        /* 标题：2 倍居中，下面一条和标题等宽的分隔线（示范 pb_text_width） */
        pb_text_center(10, "PICOBOY BSP", 2, PB_YELLOW, DEMO_BG);
        int tw = pb_text_width("PICOBOY BSP", 2);
        pb_fill_rect((PB_SCREEN_W - tw) / 2, 24, tw, 2, PB_CYAN);
        pb_text_center(30, "DISPLAY AUDIO INPUT", 1, PB_CYAN, DEMO_BG);

        /* 方块：按住 A 变红 */
        pb_fill_rect(x, y, S, S, pb_input_held(PB_BTN_A) ? PB_RED : PB_GREEN);
        pb_text(x + 2, y + 7, "BOX", PB_WHITE, pb_input_held(PB_BTN_A) ? PB_RED : PB_GREEN);

        /* 坐标（1 倍文本） */
        char buf[32];
        snprintf(buf, sizeof buf, "X%4d Y%4d", x, y);
        pb_text(8, 206, buf, PB_WHITE, DEMO_BG);

        /* 当前按下的键（用底层位掩码，顺便演示 pb_input_read） */
        static const struct { uint8_t bit; const char *name; } NM[] = {
            { PB_BTN_A, "A" }, { PB_BTN_B, "B" }, { PB_BTN_X, "X" }, { PB_BTN_Y, "Y" },
            { PB_BTN_UP, "UP" }, { PB_BTN_DOWN, "DN" },
            { PB_BTN_LEFT, "LT" }, { PB_BTN_RIGHT, "RT" },
        };
        uint8_t pad = pb_input_read();
        char keys[40] = "KEYS";
        int used = 4;
        for(unsigned i = 0; i < sizeof(NM) / sizeof(NM[0]) && used < 30; i++)
            if(pad & NM[i].bit)
            {
                keys[used++] = ' ';
                for(const char *p = NM[i].name; *p; p++)
                    keys[used++] = *p;
            }
        if(used == 4) { keys[used++] = ' '; keys[used++] = '-'; }
        keys[used] = 0;
        pb_text(8, 220, keys, PB_GREEN, DEMO_BG);

        /* 整帧上屏 */
        pb_display_flush();

        sleep_ms(16);
    }
}
