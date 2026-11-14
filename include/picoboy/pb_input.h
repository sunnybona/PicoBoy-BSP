/*
 * pb_input.h — 按键 API（8 键直连 GPIO，高电平有效）
 *
 * 位序（与 pb_input_read 返回值一致）：
 *   bit0 A    bit1 B    bit2 Y(Select)  bit3 X(Start)
 *   bit4 Right bit5 Left bit6 Up        bit7 Down
 *
 * 用法：
 *   pb_input_init();
 *   pb_input_poll();                       // 每帧调一次，更新边沿
 *   if (pb_input_held(PB_BTN_A)) { ... }
 *   if (pb_input_just_pressed(PB_BTN_UP)) { ... }
 */
#ifndef PB_INPUT_H
#define PB_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 按键位掩码 */
#define PB_BTN_A       0x01
#define PB_BTN_B       0x02
#define PB_BTN_Y       0x04
#define PB_BTN_X       0x08
#define PB_BTN_RIGHT   0x10
#define PB_BTN_LEFT    0x20
#define PB_BTN_UP      0x40
#define PB_BTN_DOWN    0x80

/* 初始化 8 个按键 GPIO（内部下拉） */
void pb_input_init(void);

/* 读当前按键位掩码（1 = 按下） */
uint8_t pb_input_read(void);

/* 每帧调一次：刷新内部“当前/上一次”状态用于边沿判断 */
void pb_input_poll(void);

/* 当前是否按住 */
int pb_input_held(uint8_t btn);

/* 本次 poll 是否“刚按下”（单次触发） */
int pb_input_just_pressed(uint8_t btn);

/* 本次 poll 是否“刚松开” */
int pb_input_just_released(uint8_t btn);

#ifdef __cplusplus
}
#endif

#endif /* PB_INPUT_H */
