/*
 * pb_rom_menu.cpp — 共用 ROM 菜单实现
 *
 * 版式与配色取自 PicoBoy GBC 定版菜单（picoboy_gbc/rom_menu.cpp）：
 *   标题栏  y=0..33 实心色块，标题 scale 2 @ y=7，副标题 scale 1 @ y=23
 *   列表    7 行、行距 22、首行 y=42、文字 x=14，scale 2，带 "序号 名字" 前缀
 *   高亮    6,y-6 起 228x20 色块 + 左侧 3px 强调条
 *   箭头    右侧 x=228，上 40 / 下 190，三行矩形拼三角
 *   信息行  y=201 scale 1，提示行 y=216 scale 1
 * 字号一律继承 GB：标题 2 倍、列表项 2 倍、其余 1 倍，不再有 3 倍标题。
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "picoboy/pb_display.h"
#include "picoboy/pb_input.h"
#include "picoboy/pb_rom_menu.h"

#ifdef PB_ROM_MENU_HOST
/* PC 预览：不引入 pico-sdk，也不真的延时 —— 渲染函数根本不进按键循环 */
static uint64_t now_us(void) { return 0; }
static void wait_ms(uint32_t ms) { (void)ms; }
#else
#include "pico/stdlib.h"
static uint64_t now_us(void) { return time_us_64(); }
static void wait_ms(uint32_t ms) { sleep_ms(ms); }
#endif

/* ---------------- 外观常量（与 GB 菜单逐像素一致） ---------------- */
#define MENU_BG    PB_RGB(0x08, 0x10, 0x08)
#define MENU_HEAD  PB_RGB(0x0C, 0x22, 0x10)
#define MENU_INK   PB_RGB(0xC8, 0xD4, 0xC0)
#define MENU_DIM   PB_RGB(0x68, 0x80, 0x68)
#define MENU_HL    PB_RGB(0x24, 0x56, 0x24)
#define MENU_ACC   PB_YELLOW

#define MENU_ROWS     7
#define MENU_ROWH     22
#define MENU_LIST_Y   42
#define MENU_ITEM_X   14

/* 2 倍字形从 x=14 起、每字符 8px，最多 27 个字符仍在 240 宽屏内（含序号前缀） */
#define MENU_CHARS_MAX 27

static void (*s_setup_cb)(void);

void pb_rom_menu_set_setup_cb(void (*cb)(void))
{
    s_setup_cb = cb;
}

/* 滚动指示的小三角：三行矩形，逐行收窄 */
static void draw_arrow(int x, int y, bool up, uint16_t col)
{
    for(int i = 0; i < 3; i++)
        pb_fill_rect(x + (up ? i : 2 - i) * 2, y + i * 3, 6 - i * 2, 3, col);
}

/* "序号 名字"，超宽就截断并用 ".." 收尾 */
static void make_line(char *dst, int cap, int idx, const char *name)
{
    int n = snprintf(dst, cap, "%d ", idx + 1);
    const char *p = name;
    while(*p && n < MENU_CHARS_MAX && n < cap - 1)
        dst[n++] = *p++;
    if(*p && n >= 2)
    {
        dst[n - 2] = '.';
        dst[n - 1] = '.';
    }
    dst[n] = 0;
}

void pb_rom_menu_render(const pb_menu_style_t *style, const pb_menu_item_t *items,
                        int count, int sel, int top)
{
    char line[MENU_CHARS_MAX + 8];

    pb_clear(MENU_BG);

    /* 标题栏 */
    pb_fill_rect(0, 0, PB_SCREEN_W, 34, MENU_HEAD);
    pb_text_center(7, style->title, 2, MENU_ACC, MENU_HEAD);
    if(style->subtitle)
        pb_text_center(23, style->subtitle, 1, PB_CYAN, MENU_HEAD);

    /* 列表 */
    for(int r = 0; r < MENU_ROWS && top + r < count; r++)
    {
        int i = top + r;
        int y = MENU_LIST_Y + r * MENU_ROWH;

        make_line(line, sizeof line, i, items[i].name);

        if(i == sel)
        {
            pb_fill_rect(6, y - 6, PB_SCREEN_W - 12, 20, MENU_HL);
            pb_fill_rect(6, y - 6, 3, 20, MENU_ACC);      /* 左侧高亮条 */
            pb_text_scaled(MENU_ITEM_X, y, line, 2, PB_WHITE, MENU_HL);
        }
        else
        {
            pb_text_scaled(MENU_ITEM_X, y, line, 2, MENU_INK, MENU_BG);
        }
    }

    if(top > 0)
        draw_arrow(PB_SCREEN_W - 12, MENU_LIST_Y - 2, true, MENU_DIM);
    if(top + MENU_ROWS < count)
        draw_arrow(PB_SCREEN_W - 12, 190, false, MENU_DIM);

    /* 选中项信息 + 操作提示 */
    if(count > 0 && items[sel].info)
        pb_text_center(201, items[sel].info, 1, style->info_color, MENU_BG);
    if(style->hint)
        pb_text_center(216, style->hint, 1, MENU_DIM, MENU_BG);
}

/* 等某个键松开：菜单 ↔ 设置页来回切时，同一个按键不能被下一层立刻再判一次 */
static void wait_released(uint8_t mask)
{
    if(!mask)
        return;
    for(;;)
    {
        pb_input_poll();
        if(!(pb_input_read() & mask))
            return;
        wait_ms(5);
    }
}

int pb_rom_menu_select(const pb_menu_style_t *style, const pb_menu_item_t *items,
                       int count)
{
    if(count <= 0)
        return -1;
    if(count == 1)
        return 0;

    int sel = 0, top = 0;
    bool dirty = true;
    uint32_t next_up = 0, next_down = 0;

    while(true)
    {
        if(dirty)
        {
            pb_rom_menu_render(style, items, count, sel, top);
            pb_display_flush();
            dirty = false;
        }

        pb_input_poll();
        uint64_t now = now_us();

        /* 按下立刻走一格；按住 0.4s 后以 90ms 连发 */
        int step = 0;
        if(pb_input_just_pressed(PB_BTN_DOWN))
        {
            step = 1;
            next_down = (uint32_t)now + 400000u;
        }
        else if(pb_input_just_pressed(PB_BTN_UP))
        {
            step = -1;
            next_up = (uint32_t)now + 400000u;
        }
        else if(pb_input_held(PB_BTN_DOWN) && (uint32_t)now >= next_down)
        {
            step = 1;
            next_down = (uint32_t)now + 90000u;
        }
        else if(pb_input_held(PB_BTN_UP) && (uint32_t)now >= next_up)
        {
            step = -1;
            next_up = (uint32_t)now + 90000u;
        }

        if(step)
        {
            sel = (sel + step + count) % count;
            if(sel < top)
                top = sel;
            else if(sel >= top + MENU_ROWS)
                top = sel - MENU_ROWS + 1;
            dirty = true;
            /* 移动光标不出声：连按上下时“哒哒”声很吵，只在确认时给一声 */
        }

        /* Y：进设置页（只有调用方装了回调才响应） */
        if(s_setup_cb && pb_input_just_pressed(PB_BTN_Y))
        {
            wait_released(PB_BTN_Y);
            s_setup_cb();
            wait_released(PB_BTN_A | PB_BTN_B | PB_BTN_Y);
            dirty = true;
            continue;
        }

        /* A 或 X（Start）确认：菜单全程静音，进出都不响 */
        if(pb_input_just_pressed(PB_BTN_A) || pb_input_just_pressed(PB_BTN_X))
            return sel;

        wait_ms(8);
    }
}

void pb_rom_menu_splash(const char *name)
{
    pb_clear(MENU_BG);
    pb_text_center(96, "LOADING", 2, MENU_ACC, MENU_BG);
    if(name)
        pb_text_center(120, name, 1, MENU_INK, MENU_BG);
    pb_display_flush();
    wait_ms(160);
}
