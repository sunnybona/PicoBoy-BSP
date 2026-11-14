/*
 * pb_settings.h — 通用数值设置界面（亮度/音量等可调节项）
 *
 * 每个可调节项有标签、范围、步进、当前值和一个应用回调；
 * 上/下选择，左/右调节并立即应用，按 B 退出。
 */
#ifndef PB_SETTINGS_H
#define PB_SETTINGS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *label;       /* 名称 */
    int min;                 /* 最小 */
    int max;                 /* 最大 */
    int step;                /* 步进 */
    int value;               /* 当前值 */
    void (*apply)(int value);/* 值改变时立即调用（接硬件） */
} pb_setting_item_t;

typedef struct {
    const char *title;
    pb_setting_item_t *items;
    int count;
    int selection;
} pb_settings_t;

void pb_settings_init(pb_settings_t *s, const char *title,
                      pb_setting_item_t *items, int count);

/* 运行设置界面，直到按 B 退出；返回最后选中的下标 */
int pb_settings_run(pb_settings_t *s);

#ifdef __cplusplus
}
#endif

#endif /* PB_SETTINGS_H */
