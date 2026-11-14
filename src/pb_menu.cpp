/*
 * pb_menu.cpp — 通用列表菜单实现（大字号，pb_fb + pb_display_flush）
 */
#include "picoboy/pb_menu.h"
#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"

#include "pico/stdlib.h"

#include <cstdlib>

/* 布局常量（3 倍大字号） */
#define TITLE_BAR_H   36
#define TITLE_SCALE   3
#define ITEM_SCALE    3
#define ITEM_TOP      56
#define ITEM_PITCH    38
#define ITEM_BAR_H    28
#define ARROW_X       12
#define ITEM_X        40

/* 画一个右指三角箭头，高度 h，垂直从 y 起 */
static void draw_arrow(int x, int y, int h, uint16_t color)
{
    for(int r = 0; r < h; r++)
    {
        int w = (h / 2) - abs(r - (h - 1) / 2);
        for(int dx = 0; dx <= w; dx++)
            pb_set_pixel(x + dx, y + r, color);
    }
}

void pb_menu_init(pb_menu_t *m, const char *title,
                  const char *const *items, int count)
{
    m->title = title;
    m->items = items;
    m->count = count;
    m->selection = 0;
}

static void menu_render(const pb_menu_t *m)
{
    /* 若双核推屏还在跑，先等它收手，避免 core0 与 core1 同时用 SPI/DMA */
    pb_display_wait_idle();

    pb_clear(PB_BLACK);

    /* 标题栏（标题水平居中） */
    pb_fill_rect(0, 0, PB_SCREEN_W, TITLE_BAR_H, PB_BLUE);
    int tw = pb_text_scaled_width(m->title, TITLE_SCALE);
    int ty = (TITLE_BAR_H - 5 * TITLE_SCALE) / 2;
    pb_text_scaled((PB_SCREEN_W - tw) / 2, ty,
                   m->title, PB_WHITE, PB_BLUE, TITLE_SCALE);

    /* 列表项 */
    for(int i = 0; i < m->count; i++)
    {
        int y = ITEM_TOP + i * ITEM_PITCH;
        int text_y = y + (ITEM_BAR_H - 5 * ITEM_SCALE) / 2;

        if(i == m->selection)
        {
            pb_fill_rect(4, y, PB_SCREEN_W - 8, ITEM_BAR_H, PB_GREEN);
            draw_arrow(ARROW_X, text_y, 5 * ITEM_SCALE, PB_BLACK);
            pb_text_scaled(ITEM_X, text_y, m->items[i],
                           PB_BLACK, PB_GREEN, ITEM_SCALE);
        }
        else
        {
            pb_text_scaled(ITEM_X, text_y, m->items[i],
                           PB_WHITE, PB_BLACK, ITEM_SCALE);
        }
    }

    pb_display_flush();
}

int pb_menu_run(pb_menu_t *m)
{
    menu_render(m);

    while(true)
    {
        pb_input_poll();

        int moved = 0;
        if(pb_input_just_pressed(PB_BTN_UP))
        {
            m->selection--;
            moved = 1;
        }
        if(pb_input_just_pressed(PB_BTN_DOWN))
        {
            m->selection++;
            moved = 1;
        }
        if(m->selection < 0) m->selection = m->count - 1;
        if(m->selection >= m->count) m->selection = 0;
        if(moved) menu_render(m);

        if(pb_input_just_pressed(PB_BTN_A))
            return m->selection;

        sleep_ms(8);
    }
}
