/*
 * pb_menu.h — PicoBoy BSP 通用列表菜单
 *
 * 最小用法：
 *   static const char *items[] = { "ONE", "TWO", "THREE" };
 *   pb_menu_t m;
 *   pb_menu_init(&m, "PICK ONE", items, 3);
 *   int choice = pb_menu_run(&m);   // 上/下选择，A 确认
 *
 * 菜单渲染在 pb_fb 上并用 pb_display_flush 上屏；若双核推屏正在运行，
 * 内部会先等 core1 空闲，不会和它抢 SPI。
 */
#ifndef PB_MENU_H
#define PB_MENU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *title;
    const char *const *items;
    int count;
    int selection;
} pb_menu_t;

/* 初始化菜单（默认选中第 0 项） */
void pb_menu_init(pb_menu_t *m, const char *title,
                  const char *const *items, int count);

/* 运行菜单：上/下移动（循环），A 确认。返回选中项下标。 */
int pb_menu_run(pb_menu_t *m);

#ifdef __cplusplus
}
#endif

#endif /* PB_MENU_H */
