/*
 * pb_flash_rom.h — flash ROM 区（rom_uploader 写入的未压缩 ustar tar）
 *
 * rom_uploader 把多个 .gb/.gbc/.nes 打成一个未压缩 ustar tar，写到固定 ROM 区，
 * 固件 XIP 直读、一个字节都不进 RAM。本组件把 tar 头扫成 ROM 条目表。
 *
 * 地址与 rom_uploader.py 的 ROM_AREA_ADDR 严格对齐；末尾 64KB 是存档/设置保留区，
 * 由 uploader 避让，扫描到 tar 自己的零块结束符即停。
 */
#ifndef PB_FLASH_ROM_H
#define PB_FLASH_ROM_H

#include <stdint.h>
#include <stdbool.h>

#include "board.h"

/* ROM 区基址（XIP 地址，与 rom_uploader.py ROM_AREA_ADDR 对齐），可 -D 覆盖 */
#ifndef PB_ROM_AREA_ADDR
#define PB_ROM_AREA_ADDR 0x10120000u
#endif

/* 单条 ROM 描述：指针都指 flash，不做 RAM 拷贝 */
typedef struct
{
    const char    *name;    /* tar 内文件名（原始，含扩展名） */
    const uint8_t *data;    /* ROM 数据起点（XIP） */
    uint32_t       size;
} pb_flash_rom_entry_t;

/* 校验回调：返回 true 才保留该条目；NULL 表示全部保留。
 * data 指向 ROM 起点，size 为 tar 记录的大小。 */
typedef bool (*pb_rom_validator_fn)(const uint8_t *data, uint32_t size);

/* ROM 区是否为 tar（首块 ustar 魔数） */
bool pb_flash_rom_is_tar(void);

/* 扫描 tar，把通过校验的条目填入 out（最多 cap 条），返回条目数。
 * 非 tar 或无有效条目返回 0。 */
int pb_flash_rom_scan(pb_flash_rom_entry_t *out, int cap,
                      pb_rom_validator_fn validator);

#endif /* PB_FLASH_ROM_H */
