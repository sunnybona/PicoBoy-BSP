/*
 * pb_flash_rom.cpp — 未压缩 ustar tar 扫描（XIP 只读，不占 RAM）
 *
 * tar 布局（每块 512B）：
 *   头块  name@0(100)、size@124(12,八进制)、magic@257("ustar")
 *   数据块 紧跟头块，长度按 512 向上取整
 *   结束  全零块
 */
#include "picoboy/pb_flash_rom.h"

#include <string.h>

/* tar 头块内的偏移 */
#define TAR_OFF_NAME   0
#define TAR_OFF_SIZE   124
#define TAR_OFF_MAGIC  257
#define TAR_BLOCK      512

/* 解析八进制字段（NUL 或空格结束），失败返回 false */
static bool parse_oct(const char *p, int count, uint32_t *out)
{
    uint32_t v = 0;
    while(count--)
    {
        char ch = *p++;
        if(ch == 0 || ch == 0x20)
        {
            *out = v;
            return true;
        }
        if(ch < '0' || ch > '7')
            return false;
        v = (v << 3) | (uint32_t)(ch - '0');
    }
    *out = v;
    return true;
}

static bool is_all_zero(const char *p, uint32_t len)
{
    while(len--)
    {
        if(*p++)
            return false;
    }
    return true;
}

bool pb_flash_rom_is_tar(void)
{
    return strncmp(reinterpret_cast<const char *>(PB_ROM_AREA_ADDR) + TAR_OFF_MAGIC,
                   "ustar", 5) == 0;
}

int pb_flash_rom_scan(pb_flash_rom_entry_t *out, int cap,
                      pb_rom_validator_fn validator)
{
    if(cap <= 0)
        return 0;

    const char *p = reinterpret_cast<const char *>(PB_ROM_AREA_ADDR);
    int count = 0;

    for(;;)
    {
        /* 全零头块 = 归档结束 */
        if(is_all_zero(p, TAR_BLOCK))
            break;

        if(strncmp(p + TAR_OFF_MAGIC, "ustar", 5) != 0)
        {
            /* 非 ustar：停止，避免越界乱扫 */
            break;
        }

        uint32_t size;
        if(!parse_oct(p + TAR_OFF_SIZE, 12, &size))
            break;

        const uint8_t *data = reinterpret_cast<const uint8_t *>(p + TAR_BLOCK);

        if(!validator || validator(data, size))
        {
            if(count < cap)
            {
                out[count].name = p + TAR_OFF_NAME;
                out[count].data = data;
                out[count].size = size;
            }
            count++;
        }

        /* 前进到下一头块：512 头 + 数据按 512 取整 */
        uint32_t padded = (size + TAR_BLOCK - 1u) & ~(uint32_t)(TAR_BLOCK - 1u);
        p += TAR_BLOCK + padded;
    }

    /* 超出容量时按实际能装下的数量返回 */
    return count < cap ? count : cap;
}
