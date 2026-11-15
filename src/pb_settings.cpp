/*
 * pb_settings.cpp — 亮度/音量/反色设置：flash 持久化 + 设置页
 *
 * 存储布局（放在 flash 最后一扇区）：
 *   [0]   magic  'PBST'
 *   [4]   brightness 1..10
 *   [5]   volume 0..10
 *   [6]   invert 0/1
 *   [7]   保留
 *   [8]   check = magic ^ (brightness << 16) ^ volume ^ (invert << 8) ^ SALT
 *
 * 为什么用"整扇区 + 校验"而不是读写 EEPROM 那种字节级模拟：
 * RP2040 的 flash 只能整扇区（4KB）擦、按页（256B）写，擦一次就是 4KB，
 * 所以干脆把 12 字节结构体放一个扇区开头，靠 magic+check 判断有效性。
 * 值没变就不擦——擦写寿命是有限资源，调一次亮度也擦一次会白白消耗。
 */
#include "picoboy/pb_settings.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "picoboy/pb_audio.h"
#include "picoboy/pb_input.h"

#define SETTINGS_MAGIC  0x50425354u          /* 'PBST' */
#define SETTINGS_SALT   0xA5C3E1F0u

typedef struct
{
    uint32_t magic;
    uint8_t  brightness;
    uint8_t  volume;
    uint8_t  invert;
    uint8_t  rsv;
    uint32_t check;
} settings_blob_t;

/* flash 里的那一份（XIP 映射，只读访问） */
static const settings_blob_t *flash_blob(void)
{
    return (const settings_blob_t *)(XIP_BASE + PB_SETTINGS_FLASH_OFFSET);
}

static uint32_t calc_check(const settings_blob_t *b)
{
    return b->magic ^ ((uint32_t)b->brightness << 16) ^
           (uint32_t)b->volume ^ ((uint32_t)b->invert << 8) ^ SETTINGS_SALT;
}

/* 内存里的当前值（和 flash 里的那份分开，改完才落盘）。
 * invert 出厂默认取编译期 PB_LCD_INVERT（board.h，可 -D 覆盖）。 */
static pb_settings_t s_cur =
{
    PB_SETTINGS_BRIGHT_DEFAULT,
    PB_SETTINGS_VOL_DEFAULT,
    PB_LCD_INVERT
};

/* ---------------- 取值 / 生效 ---------------- */

void pb_settings_load(void)
{
    const settings_blob_t *b = flash_blob();

    /* magic、校验、范围三道关；任一不过就当没存过，用默认值（不擦 flash） */
    if(b->magic == SETTINGS_MAGIC && b->check == calc_check(b) &&
       b->brightness >= PB_SETTINGS_BRIGHT_MIN &&
       b->brightness <= PB_SETTINGS_BRIGHT_MAX &&
       b->volume >= PB_SETTINGS_VOL_MIN &&
       b->volume <= PB_SETTINGS_VOL_MAX &&
       b->invert <= 1)
    {
        s_cur.brightness = b->brightness;
        s_cur.volume     = b->volume;
        s_cur.invert     = b->invert;
    }
    else
    {
        s_cur.brightness = PB_SETTINGS_BRIGHT_DEFAULT;
        s_cur.volume     = PB_SETTINGS_VOL_DEFAULT;
        s_cur.invert     = PB_LCD_INVERT;
    }
}

void pb_settings_apply(void)
{
    /* 1..10 档 -> 25..255 的 PWM 占空比（1 档仍然是可见的微光，不会黑屏） */
    uint8_t duty = (uint8_t)((uint16_t)s_cur.brightness * 255u / PB_SETTINGS_BRIGHT_MAX);
    if(duty == 0) duty = 1;

    pb_backlight(duty);
    pb_audio_set_volume(s_cur.volume);
    pb_display_set_invert(s_cur.invert ? true : false);
}

void pb_settings_init(void)
{
    pb_settings_load();
    pb_settings_apply();
}

const pb_settings_t *pb_settings_get(void)
{
    return &s_cur;
}

void pb_settings_set_brightness(uint8_t level)
{
    if(level < PB_SETTINGS_BRIGHT_MIN) level = PB_SETTINGS_BRIGHT_MIN;
    if(level > PB_SETTINGS_BRIGHT_MAX) level = PB_SETTINGS_BRIGHT_MAX;

    s_cur.brightness = level;
    pb_settings_apply();
}

void pb_settings_set_volume(uint8_t level)
{
    if(level > PB_SETTINGS_VOL_MAX) level = PB_SETTINGS_VOL_MAX;

    s_cur.volume = level;
    pb_settings_apply();
}

void pb_settings_set_invert(uint8_t on)
{
    s_cur.invert = on ? 1u : 0u;
    pb_settings_apply();
}

/* ---------------- 存盘 ---------------- */

int pb_settings_save(void)
{
    settings_blob_t blob;
    const settings_blob_t *old = flash_blob();

    blob.magic      = SETTINGS_MAGIC;
    blob.brightness = s_cur.brightness;
    blob.volume     = s_cur.volume;
    blob.invert     = s_cur.invert;
    blob.rsv        = 0;
    blob.check      = calc_check(&blob);

    /* 已经一模一样就别擦——擦一次就是 4KB 的寿命 */
    if(old->magic == blob.magic && old->check == blob.check &&
       old->brightness == blob.brightness && old->volume == blob.volume &&
       old->invert == blob.invert)
        return 0;

    /* 关键：program 一次要读满一整页（256B），而这里的栈顶 __StackTop 就是
     * 整块 SRAM 的末端（0x20042000）。若直接把 12B 的栈 blob 当源，flash_range_program
     * 会从 blob 向高地址读 256B，越过 SRAM 末端进入未映射区，触发总线错误死机。
     * 用静态页缓冲（落在主 RAM 中段），先填 0xFF 再把 blob 拷到开头，保证读取不越界。 */
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof page);
    memcpy(page, &blob, sizeof blob);

    /* 擦写期间 XIP 停摆：关中断 + 走 bootrom（SDK 的 flash_range_* 在 RAM 里跑）。
     * 前提是 core1 还没在跑推屏 worker，否则 core1 会在这几毫秒里取指崩溃。 */
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(PB_SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(PB_SETTINGS_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    return 1;
}

/* ---------------- 设置页 ---------------- */

#define UI_BG        PB_RGB(8, 8, 16)
#define UI_BG_HOT    PB_RGB(40, 40, 64)
#define UI_FG        PB_RGB(190, 190, 200)
#define UI_FG_HOT    PB_RGB(0xF8, 0xC8, 0x38)
#define UI_DIM       PB_RGB(110, 110, 130)
#define UI_LINE      PB_RGB(90, 90, 120)
#define UI_BAR_BG    PB_RGB(24, 24, 40)
#define UI_BAR_FG    PB_RGB(150, 150, 170)
#define UI_TICK      PB_RGB(70, 70, 96)

/* 刻度条：底 + 填充 + 每档一根 1px 分隔线 */
static void draw_bar(int x, int y, int w, int h, int val, int maxv,
                     uint16_t fg, uint16_t bg)
{
    pb_fill_rect(x, y, w, h, bg);

    int filled = (w * val) / maxv;
    if(filled > 0)
        pb_fill_rect(x, y, filled, h, fg);

    for(int i = 1; i < maxv; i++)
        pb_fill_rect(x + (w * i) / maxv, y, 1, h, UI_TICK);
}

/* 一行设置：名字 + 数值 + 刻度条；hot 时整行高亮 */
static void draw_row(int y, const char *label, uint8_t val, uint8_t hot)
{
    uint16_t bg = hot ? UI_BG_HOT : UI_BG;
    uint16_t fg = hot ? UI_FG_HOT : UI_FG;

    if(hot)
        pb_fill_rect(6, y - 6, PB_FB_W - 12, 44, bg);

    pb_text_scaled(14, y, label, 2, fg, bg);

    char num[8];
    snprintf(num, sizeof num, "%d", (int)val);
    pb_text_scaled(PB_FB_W - 14 - pb_text_width(num, 2), y, num, 2, fg, bg);

    draw_bar(14, y + 16, PB_FB_W - 28, 10, val, PB_SETTINGS_BRIGHT_MAX,
             hot ? UI_FG_HOT : UI_BAR_FG, UI_BAR_BG);
}

/* 开关行：名字 + ON/OFF + 右侧拨动开关；hot 时整行高亮 */
static void draw_toggle_row(int y, const char *label, uint8_t on, uint8_t hot)
{
    uint16_t bg = hot ? UI_BG_HOT : UI_BG;
    uint16_t fg = hot ? UI_FG_HOT : UI_FG;

    if(hot)
        pb_fill_rect(6, y - 6, PB_FB_W - 12, 44, bg);

    pb_text_scaled(14, y, label, 2, fg, bg);

    const char *st = on ? "ON" : "OFF";
    uint16_t stc = on ? PB_GREEN : UI_DIM;
    pb_text_scaled(PB_FB_W - 14 - pb_text_width(st, 2), y, st, 2, stc, bg);

    /* 拨动开关：轨道 + 滑块（ON 滑块靠右、OFF 靠左） */
    const int tw = 64, th = 14;
    int tx = PB_FB_W - 14 - tw;
    int ty = y + 14;
    pb_fill_rect(tx, ty, tw, th, UI_BAR_BG);
    int knob = th - 4;
    if(on)
        pb_fill_rect(tx + tw - knob - 2, ty + 2, knob, knob, PB_GREEN);
    else
        pb_fill_rect(tx + 2, ty + 2, knob, knob, UI_BAR_FG);
}

int pb_settings_menu(void)
{
    uint8_t sel = 0;              /* 0 = 亮度, 1 = 音量, 2 = 反色 */
    int changed = 0;

    /* 进入这一页的那个按键可能还按着（例如从菜单按 Y 进来）：
     * 不等它松开，下一轮 just_pressed 会立刻把同一按键判成"返回"。 */
    while(pb_input_read() & (PB_BTN_A | PB_BTN_B | PB_BTN_Y))
    {
        pb_input_poll();
        sleep_ms(5);
    }

    for(;;)
    {
        pb_input_poll();

        if(pb_input_just_pressed(PB_BTN_UP) ||
           pb_input_just_pressed(PB_BTN_DOWN))
        {
            /* 三项循环切换 */
            sel = (uint8_t)((sel + 1) % 3);
        }

        bool go_right = pb_input_just_pressed(PB_BTN_RIGHT);
        bool go_left  = pb_input_just_pressed(PB_BTN_LEFT);

        if(go_right || go_left)
        {
            if(sel == 2)
            {
                /* 反色：LEFT = OFF，RIGHT = ON */
                uint8_t nv = go_right ? 1u : 0u;
                if(nv != s_cur.invert)
                {
                    s_cur.invert = nv;
                    pb_settings_apply();
                    changed = 1;
                }
            }
            else
            {
                int delta = go_right ? +1 : -1;
                int lo = (sel == 0) ? PB_SETTINGS_BRIGHT_MIN : PB_SETTINGS_VOL_MIN;
                int hi = (sel == 0) ? PB_SETTINGS_BRIGHT_MAX : PB_SETTINGS_VOL_MAX;
                uint8_t *cur = (sel == 0) ? &s_cur.brightness : &s_cur.volume;

                int nv = (int)*cur + delta;
                if(nv < lo) nv = lo;
                if(nv > hi) nv = hi;

                if((uint8_t)nv != *cur)
                {
                    *cur = (uint8_t)nv;
                    pb_settings_apply();
                    changed = 1;

                    /* 调音量时响一声，当场听出音量有多大 */
                    if(sel == 1 && nv > 0)
                        pb_beep(1200, 40);
                }
            }
        }

        if(pb_input_just_pressed(PB_BTN_A) ||
           pb_input_just_pressed(PB_BTN_B) ||
           pb_input_just_pressed(PB_BTN_Y))
            return changed;

        /* ---- 画面 ---- */
        pb_clear(UI_BG);

        pb_text_center(14, "SETTINGS", 3, UI_FG_HOT, UI_BG);
        pb_fill_rect(6, 38, PB_FB_W - 12, 1, UI_LINE);

        draw_row(50,  "BRIGHT", s_cur.brightness, (uint8_t)(sel == 0));
        draw_row(102, "VOLUME", s_cur.volume,     (uint8_t)(sel == 1));
        draw_toggle_row(154, "INVERT", s_cur.invert, (uint8_t)(sel == 2));

        pb_text(8, 200, "UP DOWN   ITEM",    UI_DIM, UI_BG);
        pb_text(8, 212, "LEFT RIGHT ADJUST",  UI_DIM, UI_BG);
        pb_text(8, 224, "A SAVE AND BACK",    UI_DIM, UI_BG);

        pb_display_flush();
        sleep_ms(16);
    }
}
