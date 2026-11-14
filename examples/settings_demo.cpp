/*
 * settings_demo.cpp — pb_settings 演示：亮度 / 音量设置页 + flash 持久化
 *
 * 烧到板子上应该看到：
 *   - 开机显示当前的 BRIGHT / VOLUME 值（第一次是 9 / 8，之后是你上次存的值）
 *   - A：响一声（听音量）
 *   - B：切换背景色（深蓝 / 深绿 / 深紫，方便对比亮度差异）
 *   - Y：进设置页 → 上下换项，左右调档，A/B/Y 返回并自动存盘
 *   - 断电重开，值还在 → 说明 flash 那一扇区写成功了
 *
 * 用法就三步：
 *   pb_settings_init();                       // 读 flash + 应用到背光/音量
 *   ...
 *   pb_settings_menu(); pb_settings_save();   // 设置页 + 落盘
 */
#include "pico/stdlib.h"
#include <stdio.h>

#include "picoboy/pb_display.h"
#include "picoboy/pb_audio.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_settings.h"

static const uint16_t kBG[3] = {
    PB_RGB(0x0A, 0x12, 0x18),
    PB_RGB(0x08, 0x16, 0x10),
    PB_RGB(0x14, 0x0C, 0x1A)
};

/* 从设置页退回来时，同一个按键可能还按着，等它松开再进主循环 */
static void wait_keys_released(uint8_t mask)
{
    while(pb_input_read() & mask)
    {
        pb_input_poll();
        sleep_ms(5);
    }
}

/* 画一栏：名字 + 大字数值 + 归零到 10 的小方块（直观看出档位） */
static void draw_gauge(int y, const char *name, uint8_t val, int maxv,
                       uint16_t fg, uint16_t bg)
{
    pb_text(16, y + 8, name, PB_RGB(0xC8, 0xC8, 0xD8), bg);

    char num[8];
    snprintf(num, sizeof num, "%d", (int)val);
    pb_text_scaled(PB_SCREEN_W - 16 - pb_text_width(num, 3), y, num, 3, fg, bg);

    int bw = 12, gap = 3;
    for(int i = 0; i < maxv; i++)
    {
        uint16_t c = (i < val) ? fg : PB_RGB(30, 30, 44);
        pb_fill_rect(16 + i * (bw + gap), y + 20, bw, 7, c);
    }
}

int main(void)
{
    stdio_init_all();

    pb_display_init();
    pb_audio_init();
    pb_input_init();

    /* 一行顶三行：读 flash → 没存过用默认 → 立刻应用 */
    pb_settings_init();

    int bg_sel = 0;

    while(true)
    {
        pb_input_poll();

        if(pb_input_just_pressed(PB_BTN_A))
            pb_beep(880, 80);

        if(pb_input_just_pressed(PB_BTN_B))
            bg_sel = (bg_sel + 1) % 3;

        if(pb_input_just_pressed(PB_BTN_Y))
        {
            int changed = pb_settings_menu();

            if(changed)
            {
                /* 这里还是单核（没起推屏 worker），擦 flash 是安全的 */
                pb_settings_save();
                pb_beep(1500, 30);
            }

            wait_keys_released(PB_BTN_A | PB_BTN_B | PB_BTN_Y);
        }

        /* ---- 画面 ---- */
        uint16_t bg = kBG[bg_sel];
        const pb_settings_t *st = pb_settings_get();

        pb_clear(bg);

        pb_text_center(10, "PICOBOY SETTINGS", 2, PB_YELLOW, bg);
        pb_fill_rect(8, 26, PB_SCREEN_W - 16, 1, PB_RGB(0x38, 0x50, 0x60));
        pb_text_center(32, "PB_SETTINGS DEMO", 1, PB_CYAN, bg);

        draw_gauge(56,  "BRIGHT", st->brightness, PB_SETTINGS_BRIGHT_MAX,
                   PB_YELLOW, bg);
        draw_gauge(112, "VOLUME", st->volume, PB_SETTINGS_VOL_MAX,
                   PB_GREEN, bg);

        pb_fill_rect(8, 168, PB_SCREEN_W - 16, 1, PB_RGB(0x38, 0x50, 0x60));
        pb_text(12, 176, "A BEEP   B COLOR", PB_RGB(0xA0, 0xA0, 0xB0), bg);
        pb_text(12, 188, "Y OPEN SETTINGS", PB_RGB(0xA0, 0xA0, 0xB0), bg);
        pb_text(12, 204, "VALUES STAY IN FLASH", PB_RGB(0x70, 0x70, 0x88), bg);

        pb_display_flush();
        sleep_ms(16);
    }
}
