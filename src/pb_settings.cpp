/*
 * pb_settings.cpp — 通用数值设置界面实现（pb_fb + pb_display_flush）
 */
#include "picoboy/pb_settings.h"
#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"

#include "pico/stdlib.h"

#include <cstdlib>

/* 布局常量 */
#define TITLE_BAR_H  32
#define TITLE_SCALE  3
#define ROW_SCALE    2
#define ROW_TOP      48
#define ROW_PITCH    72
#define ROW_X        16
#define BAR_W        208
#define BAR_H        14
#define BAR_GAP      24

static void int_to_str(int v, char *out)
{
    char tmp[8];
    int t = 0, n = 0;
    bool neg = v < 0;
    if(neg) v = -v;
    if(v == 0) tmp[t++] = '0';
    while(v) { tmp[t++] = '0' + v % 10; v /= 10; }
    if(neg) out[n++] = '-';
    while(t) out[n++] = tmp[--t];
    out[n] = 0;
}

/* 右指三角（同菜单） */
static void draw_arrow(int x, int y, int h, uint16_t color)
{
    for(int r = 0; r < h; r++)
    {
        int w = (h / 2) - abs(r - (h - 1) / 2);
        for(int dx = 0; dx <= w; dx++)
            pb_set_pixel(x + dx, y + r, color);
    }
}

static void settings_render(const pb_settings_t *s)
{
    pb_display_wait_idle();
    pb_clear(PB_BLACK);

    /* 标题栏 */
    pb_fill_rect(0, 0, PB_SCREEN_W, TITLE_BAR_H, PB_BLUE);
    int tw = pb_text_scaled_width(s->title, TITLE_SCALE);
    int ty = (TITLE_BAR_H - 5 * TITLE_SCALE) / 2;
    pb_text_scaled((PB_SCREEN_W - tw) / 2, ty,
                   s->title, PB_WHITE, PB_BLUE, TITLE_SCALE);

    for(int i = 0; i < s->count; i++)
    {
        const pb_setting_item_t *it = &s->items[i];
        int y = ROW_TOP + i * ROW_PITCH;
        bool sel = (i == s->selection);
        uint16_t label_col = sel ? PB_GREEN : PB_WHITE;

        if(sel) draw_arrow(ROW_X - 10, y, 5 * ROW_SCALE, PB_GREEN);

        /* 名称 + 数值（数值右对齐） */
        pb_text_scaled(ROW_X, y, it->label, label_col, PB_BLACK, ROW_SCALE);

        char num[8];
        int_to_str(it->value, num);
        int nw = pb_text_scaled_width(num, ROW_SCALE);
        pb_text_scaled(PB_SCREEN_W - ROW_X - nw, y,
                       num, label_col, PB_BLACK, ROW_SCALE);

        /* 数值条 */
        int bar_y = y + BAR_GAP;
        uint16_t bar_bg = PB_RGB(40, 40, 40);
        pb_fill_rect(ROW_X, bar_y, BAR_W, BAR_H, bar_bg);

        int span = it->max - it->min;
        int frac = it->value - it->min;
        int inner_w = BAR_W - 4;
        int fill_w = span > 0 ? (inner_w * frac) / span : 0;
        if(fill_w > 0)
            pb_fill_rect(ROW_X + 2, bar_y + 2, fill_w, BAR_H - 4, PB_GREEN);
    }

    /* 操作提示 */
    pb_text_scaled(ROW_X, 198, "L/R ADJUST", PB_WHITE, PB_BLACK, ROW_SCALE);
    pb_text_scaled(150, 198, "B BACK", PB_WHITE, PB_BLACK, ROW_SCALE);

    pb_display_flush();
}

void pb_settings_init(pb_settings_t *s, const char *title,
                      pb_setting_item_t *items, int count)
{
    s->title = title;
    s->items = items;
    s->count = count;
    s->selection = 0;
}

int pb_settings_run(pb_settings_t *s)
{
    settings_render(s);

    while(true)
    {
        pb_input_poll();

        bool changed = false;
        if(pb_input_just_pressed(PB_BTN_UP))
        {
            s->selection--;
            changed = true;
        }
        if(pb_input_just_pressed(PB_BTN_DOWN))
        {
            s->selection++;
            changed = true;
        }
        if(s->selection < 0) s->selection = s->count - 1;
        if(s->selection >= s->count) s->selection = 0;

        pb_setting_item_t *it = &s->items[s->selection];
        if(pb_input_just_pressed(PB_BTN_LEFT))
        {
            it->value -= it->step;
            if(it->value < it->min) it->value = it->min;
            it->apply(it->value);
            changed = true;
        }
        if(pb_input_just_pressed(PB_BTN_RIGHT))
        {
            it->value += it->step;
            if(it->value > it->max) it->value = it->max;
            it->apply(it->value);
            changed = true;
        }

        if(pb_input_just_pressed(PB_BTN_B))
            return s->selection;

        if(changed) settings_render(s);
        sleep_ms(8);
    }
}
