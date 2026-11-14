/*
 * pb_rom_menu.h — 多 ROM 选择菜单（GB / NES 共用）
 *
 * 外观完全按 PicoBoy GBC 菜单定版：标题栏 34px 色块、标题 2 倍字形、
 * 列表 7 行 × 22px 行距、每项 2 倍字形带序号、左侧 3px 强调条、
 * 上下滚动三角、底部信息行与提示行各 1 倍字形。
 * 两个模拟器都调这一份，所以菜单风格、字号、按键手感天然一致。
 *
 * 输入被抽象成「标题 + 副标题 + 每项 name/info」，不绑定任何 ROM 数据结构，
 * 因此 GB 的 pb_rom_entry_t 与 NES 的 pb_nes_rom_t 各自转换后用同一套渲染。
 *
 * 只碰 BSP 的帧缓冲绘制（pb_clear / pb_fill_rect / pb_text_scaled）和按键 API，
 * 不依赖 PIO/DMA/时钟，所以同一份代码也能在 PC 上用 tools/menu_preview.cpp
 * 渲染成图片做布局自检（那边定义 PB_ROM_MENU_HOST）。
 */
#ifndef PICOBoy_PB_ROM_MENU_H
#define PICOBoy_PB_ROM_MENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 一个列表项 */
typedef struct {
    const char *name;   /* 列表文本（0-9 / A-Z / 少量符号，字体画不出的字符会变空格） */
    const char *info;   /* 选中时底部信息行；NULL 表示不画 */
} pb_menu_item_t;

/* 菜单文案与配色（版式/字号由本模块固定，这里只让标题和提示可变） */
typedef struct {
    const char *title;      /* 标题栏主标题，2 倍字形 */
    const char *subtitle;   /* 标题栏副标题，1 倍字形 */
    const char *hint;       /* 底部提示行，1 倍字形 */
    uint16_t    info_color; /* 信息行颜色（RGB565） */
} pb_menu_style_t;

/* Y 键回调：进设置页用。传 NULL 则 Y 无效。 */
void pb_rom_menu_set_setup_cb(void (*cb)(void));

/* 显示菜单并等确认，返回选中下标；count==1 直接返回 0，count==0 返回 -1 */
int pb_rom_menu_select(const pb_menu_style_t *style, const pb_menu_item_t *items,
                       int count);

/* 只画一帧到帧缓冲（不 flush、不读键），供 PC 预览与自检使用 */
void pb_rom_menu_render(const pb_menu_style_t *style, const pb_menu_item_t *items,
                        int count, int sel, int top);

/* 选完到进游戏之间的过场：LOADING + 游戏名，按住 160ms */
void pb_rom_menu_splash(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PICOBoy_PB_ROM_MENU_H */
