/*
 * pb_settings.h — 亮度 / 音量设置：flash 持久化 + 可复用的设置页
 *
 * 能力：
 *   - 亮度 1..10 档（0 档会把背光关死、屏幕看不见，所以不给），映射到 pb_backlight(1..255)
 *   - 音量 0..10 档，直接给 pb_audio_set_volume()
 *   - 值存在 flash 最后一扇区，上电自动读回；值没变不擦写
 *   - 一个全屏设置页（上下换项、左右调档、A/B/Y 返回）
 *
 * 典型用法（单核阶段，例如开机菜单里）：
 *   pb_display_init();
 *   pb_audio_init();
 *   pb_input_init();
 *   pb_settings_init();          // = load + apply，一次到位
 *   ...
 *   if (按键 == Y) {
 *       pb_settings_menu();      // 画设置页，阻塞到用户返回
 *       pb_settings_save();      // 存 flash（值没变会自动跳过）
 *   }
 *
 * 重要限制：pb_settings_save() 会擦写 flash，期间 XIP 停止。
 *   - 只能在 **core1 推屏 worker 未启动** 时调用（pb_display_start_pusher() 之前）。
 *   - 如果已经起了双核推屏，就别再保存；要保存得先停下游戏、回到单核菜单。
 *   - 擦写用 save_and_disable_interrupts() 包住，单核下是安全的。
 */
#ifndef PB_SETTINGS_H
#define PB_SETTINGS_H

#include <stdint.h>

#include "board.h"
#include "pb_display.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 档位范围与默认值（默认值对齐原 PicoBoy GB 工程：背光 220/255、音量 8） */
#define PB_SETTINGS_BRIGHT_MIN      1
#define PB_SETTINGS_BRIGHT_MAX      10
#define PB_SETTINGS_BRIGHT_DEFAULT  9
#define PB_SETTINGS_VOL_MIN         0
#define PB_SETTINGS_VOL_MAX         10
#define PB_SETTINGS_VOL_DEFAULT     8

/*
 * 设置区所在的 flash 偏移，默认用最后一扇区。
 * 16MB 的板子建议在 CMake 里加 -DPICO_FLASH_SIZE_BYTES=16777216，
 * 否则这里只按 2MB 算（设置会落在 0x1FF000，功能正常但浪费空间，
 * 而且以后固件长过 2MB 就会冲突）。
 */
#ifndef PB_SETTINGS_FLASH_OFFSET
#  ifdef PICO_FLASH_SIZE_BYTES
#    define PB_SETTINGS_FLASH_OFFSET  (PICO_FLASH_SIZE_BYTES - 4096)
#  else
#    define PB_SETTINGS_FLASH_OFFSET  0x1FF000
#  endif
#endif

typedef struct
{
    uint8_t brightness;      /* 1..10 */
    uint8_t volume;          /* 0..10 */
} pb_settings_t;

/* 读 flash 里的设置并立即应用到背光/音量；没有有效数据就用默认值（不写 flash） */
void pb_settings_init(void);

/* 只读：把 flash 里的值取进内存（不碰硬件） */
void pb_settings_load(void);

/* 把内存里的值写到背光 PWM 和音频音量 */
void pb_settings_apply(void);

/* 存 flash：值没变就什么都不做；返回 1 = 真写了，0 = 跳过，-1 = 无有效 flash 区 */
int  pb_settings_save(void);

/* 取当前值（只读用；要改请用下面的 set 函数） */
const pb_settings_t *pb_settings_get(void);

/* 改单个值：自动钳位到合法范围，并且立即生效 */
void pb_settings_set_brightness(uint8_t level);
void pb_settings_set_volume(uint8_t level);

/*
 * 全屏设置页。阻塞到用户按 A / B / Y 返回。
 * 返回 1 = 期间改动过（调用者应自行 pb_settings_save()），0 = 没改。
 * 注意：它只管画和调，不负责存盘。
 */
int  pb_settings_menu(void);

#ifdef __cplusplus
}
#endif

#endif /* PB_SETTINGS_H */
