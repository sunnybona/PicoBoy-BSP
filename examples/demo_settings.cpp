/*
 * demo_settings.cpp — PicoBoy BSP 亮度/音量设置演示
 *
 * 进入设置界面：上/下选择，左/右调节并立即生效，按 B 退出后再进入。
 * 当作 pb_settings 的用法速查，改改条目就能做自己的设置页。
 */
#include "pico/stdlib.h"

#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_settings.h"
#include "picoboy/pb_audio.h"

static void apply_brightness(int v) { pb_backlight((uint8_t)v); }
static void apply_volume(int v)     { pb_audio_set_volume((uint8_t)v); }

int main(void)
{
    stdio_init_all();

    pb_display_init();
    pb_audio_init();
    pb_input_init();
    pb_backlight(220);
    pb_audio_set_volume(8);

    pb_setting_item_t items[2];
    items[0].label = "BRIGHTNESS";
    items[0].min = 10;
    items[0].max = 255;
    items[0].step = 5;
    items[0].value = 220;
    items[0].apply = apply_brightness;

    items[1].label = "VOLUME";
    items[1].min = 0;
    items[1].max = 10;
    items[1].step = 1;
    items[1].value = 8;
    items[1].apply = apply_volume;

    while(true)
    {
        pb_settings_t settings;
        pb_settings_init(&settings, "SETTINGS", items, 2);
        pb_settings_run(&settings);

        /* 退出后短暂停留，再重新进入 */
        sleep_ms(200);
    }
}
