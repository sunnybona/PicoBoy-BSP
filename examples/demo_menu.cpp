/*
 * demo_menu.cpp — PicoBoy BSP 菜单组件演示
 *
 * 弹出一个列表菜单，选中后显示结果，按 A 再回到菜单。
 * 当作 pb_menu 的用法速查，改改条目就能做自己的设置/选择界面。
 */
#include "pico/stdlib.h"

#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_menu.h"

int main(void)
{
    stdio_init_all();

    pb_display_init();
    pb_input_init();
    pb_backlight(200);

    static const char *items[] = {
        "Start Game", "Settings", "About", "Back",
    };

    while(true)
    {
        pb_menu_t menu;
        pb_menu_init(&menu, "MAIN MENU", items, 4);
        int choice = pb_menu_run(&menu);

        /* 结果页（大字号，垂直居中） */
        pb_display_wait_idle();
        pb_clear(PB_BLACK);
        pb_fill_rect(0, 92, PB_SCREEN_W, 56, PB_GREEN);
        pb_text_scaled(20, 100, "PICKED", PB_BLACK, PB_GREEN, 3);
        pb_text_scaled(20, 124, items[choice], PB_BLACK, PB_GREEN, 3);
        pb_text_scaled(20, 172, "PRESS A", PB_WHITE, PB_BLACK, 3);
        pb_display_flush();

        while(true)
        {
            pb_input_poll();
            if(pb_input_just_pressed(PB_BTN_A))
                break;
            sleep_ms(8);
        }
    }
}
