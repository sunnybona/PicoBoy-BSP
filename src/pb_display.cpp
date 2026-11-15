/*
 * pb_display.cpp — ST7789 显示驱动（PIO + DMA，RGB565）
 * 由 rp2040-gbc 已验证的 display.cpp 解耦而来。
 *
 * 两种用法共用同一块大内存 pb_fb（240x240=115200B，240x320=153600B）：
 *   - 简单单核：pb_fb 当 uint16 帧缓冲，pb_display_flush(_region) 推屏。
 *   - 双核 GB 流：把 pb_fb 前 103680B 当 uint32“像素对”带池，core1 推带。
 * 一个程序只用其中一种，不要混用。
 */
#include "picoboy/pb_display.h"
#include "picoboy/font3x5.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/multicore.h"

#include "st7789_lcd.pio.h"

/* 帧缓冲：竖屏铺满面板，横屏取 256x240 居中（见 board.h PB_FB_*）。
 * 4 字节对齐，便于双核模式当 uint32 带池 */
alignas(4) uint16_t pb_fb[PB_FB_W * PB_FB_H];

static PIO   s_pio = pio0;
static uint  s_sm;
static uint  s_prog_off;
static int   s_dma_chan = -1;
static int   s_pixel_mode;

/* GB 四档调色板（0 最亮 -> 3 最暗），默认 DMG 经典绿 */
static uint16_t s_pal565[4];

static inline uint32_t mk_pair(uint16_t c0, uint16_t c1)
{
    return ((uint32_t)c0 << 16) | (uint32_t)c1;
}

/* 由目标位时钟算 PIO 分频（整数 + 1/256 小数，取最接近的可表示值） */
static float lcd_div_for(unsigned int bit_hz)
{
    if(!bit_hz) bit_hz = 8000000u;
    float d = (float)clock_get_hz(clk_sys) / (2.0f * (float)bit_hz);
    if(d < 1.0f) d = 1.0f;
    if(d > 255.0f + 255.0f / 256.0f) d = 255.0f + 255.0f / 256.0f;

    float di = (float)(int)d;
    int frac = (int)((d - di) * 256.0f + 0.5f);
    if(frac > 255) { frac = 0; di += 1.0f; }
    return di + (float)frac / 256.0f;
}

static void pio_tx_idle(void)
{
    while(!pio_sm_is_tx_fifo_empty(s_pio, s_sm))
        tight_loop_contents();
    unsigned int hz = s_pixel_mode ? PB_LCD_SPI_HZ : PB_LCD_CMD_HZ;
    if(!hz) hz = 8000000u;
    busy_wait_us((uint32_t)(40000000ull / hz) + 1u);
}

static void lcd_pio_configure(int pixel)
{
    pio_sm_set_enabled(s_pio, s_sm, false);

    pio_sm_config c = st7789_lcd_program_get_default_config(s_prog_off);
    sm_config_set_sideset_pins(&c, PB_PIN_LCD_SCK);
    sm_config_set_out_pins(&c, PB_PIN_LCD_MOSI, 1);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    /* false=左移=MSB first；命令 8bit，像素 32bit */
    sm_config_set_out_shift(&c, false, true, pixel ? 32 : 8);
    sm_config_set_clkdiv(&c, pixel ? lcd_div_for(PB_LCD_SPI_HZ)
                                   : lcd_div_for(PB_LCD_CMD_HZ));

    pio_sm_init(s_pio, s_sm, s_prog_off, &c);
    pio_sm_set_enabled(s_pio, s_sm, true);
    s_pixel_mode = pixel;
}

static inline void pio_put_byte(uint8_t b)
{
    while(pio_sm_is_tx_fifo_full(s_pio, s_sm))
        tight_loop_contents();
    /* 窄写：字节复制到 32 位四个字节，左对齐 */
    *((volatile uint8_t *)&s_pio->txf[s_sm]) = b;
}

static void dma_init_once(void)
{
    if(s_dma_chan >= 0) return;
    s_dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, true));
    dma_channel_configure(s_dma_chan, &c,
                          &s_pio->txf[s_sm], NULL, 0, false);
}

static void push_words(const uint32_t *src, size_t nwords)
{
    if(nwords == 0) return;
    dma_init_once();
    dma_channel_set_read_addr(s_dma_chan, src, false);
    dma_channel_set_trans_count(s_dma_chan, (uint32_t)nwords, true);
    dma_channel_wait_for_finish_blocking(s_dma_chan);
}

/* ---- 命令/数据 ---- */
static void lcd_cmd(uint8_t cmd)
{
    pio_tx_idle();
    if(s_pixel_mode) lcd_pio_configure(0);
    gpio_put(PB_PIN_LCD_DC, 0);
    gpio_put(PB_PIN_LCD_CS, 0);
    pio_put_byte(cmd);
    pio_tx_idle();
    gpio_put(PB_PIN_LCD_CS, 1);
}

static void lcd_data(const uint8_t *data, size_t len)
{
    if(len == 0) return;
    pio_tx_idle();
    if(s_pixel_mode) lcd_pio_configure(0);
    gpio_put(PB_PIN_LCD_DC, 1);
    gpio_put(PB_PIN_LCD_CS, 0);
    for(size_t i = 0; i < len; i++)
        pio_put_byte(data[i]);
    pio_tx_idle();
    gpio_put(PB_PIN_LCD_CS, 1);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    lcd_cmd(cmd);
    lcd_data(data, len);
}

static void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    /* CASET(0x2A)/RASET(0x2B) 一律直接用逻辑坐标：竖屏逻辑即物理；横屏时
     * 控制器按 MADCTL 的 MV 位在内部交换行列，所以这里绝不能手动换轴，
     * 否则地址窗口与 0x2C 写入自增方向不一致，会出现 x 超 240 折回、画面紊乱。
     * 窗口恒按起点<=终点给，MX/MY 只决定控制器的显示方向，不在此手动反向。 */
    uint16_t col0 = (uint16_t)(x + PB_LCD_X_OFFSET);
    uint16_t col1 = (uint16_t)(x + w - 1 + PB_LCD_X_OFFSET);
    uint16_t row0 = (uint16_t)(y + PB_LCD_Y_OFFSET);
    uint16_t row1 = (uint16_t)(y + h - 1 + PB_LCD_Y_OFFSET);
    uint8_t caset[4] = { (uint8_t)(col0 >> 8), (uint8_t)col0,
                         (uint8_t)(col1 >> 8), (uint8_t)col1 };
    uint8_t raset[4] = { (uint8_t)(row0 >> 8), (uint8_t)row0,
                         (uint8_t)(row1 >> 8), (uint8_t)row1 };
    lcd_cmd_data(0x2A, caset, 4);
    lcd_cmd_data(0x2B, raset, 4);
}

static void lcd_begin_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    /* 调用方坐标都在帧缓冲空间，这里加上帧缓冲在面板里的原点 */
    lcd_set_window((uint16_t)(x + PB_FB_X), (uint16_t)(y + PB_FB_Y), w, h);
    lcd_cmd(0x2C);
    pio_tx_idle();
    lcd_pio_configure(1);
    gpio_put(PB_PIN_LCD_DC, 1);
    gpio_put(PB_PIN_LCD_CS, 0);
}

static void lcd_end_pixels(void)
{
    pio_tx_idle();
    gpio_put(PB_PIN_LCD_CS, 1);
}

/* 一行的像素对打包缓冲：按面板最宽处取（pb_fill_screen 整屏填色用） */
static uint32_t s_rowword[PB_SCREEN_W / 2];

/* ---- 简单单核：帧缓冲图元 ---- */
void pb_clear(uint16_t color)
{
    for(int i = 0; i < PB_FB_W * PB_FB_H; i++)
        pb_fb[i] = color;
}

void pb_set_pixel(int x, int y, uint16_t color)
{
    if(x < 0 || y < 0 || x >= PB_FB_W || y >= PB_FB_H) return;
    pb_fb[y * PB_FB_W + x] = color;
}

void pb_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > PB_FB_W ? PB_FB_W : x + w;
    int y1 = y + h > PB_FB_H ? PB_FB_H : y + h;
    for(int yy = y0; yy < y1; yy++)
        for(int xx = x0; xx < x1; xx++)
            pb_fb[yy * PB_FB_W + xx] = color;
}

/* 文本（3x5 点阵，字符 3x5、步进 4px）。scale 倍逐点放大：
 *   scale=1 -> 原始大小（等价于 pb_text）
 *   scale=2 -> 每个点画成 2x2（字符 6x10、步进 8px），标题/菜单用
 * 与 pb_text 一样连字形的底色也写（bg），所以画在彩色底上不会透出来。 */
void pb_text_scaled(int x, int y, const char *s, uint8_t scale,
                    uint16_t fg, uint16_t bg)
{
    if(scale < 1) scale = 1;

    int gx = 0;
    for(int n = 0; s[n]; n++)
    {
        const uint8_t *g = font3x5_glyph(s[n]);
        for(int r = 0; r < 5; r++)
        {
            for(int c = 0; c < 3; c++)
            {
                uint16_t col = ((g[r] >> (2 - c)) & 1) ? fg : bg;
                pb_fill_rect(x + gx + c * scale, y + r * scale,
                             scale, scale, col);
            }
        }
        gx += 4 * scale;   /* 3 列字形 + 1 列字距 */
    }
}

void pb_text(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    pb_text_scaled(x, y, s, 1, fg, bg);
}

int pb_text_width(const char *s, uint8_t scale)
{
    if(scale < 1) scale = 1;
    int n = 0;
    while(s[n]) n++;
    return n ? n * 4 * (int)scale - (int)scale : 0;   /* 末字符不带字距 */
}

void pb_text_center(int y, const char *s, uint8_t scale,
                    uint16_t fg, uint16_t bg)
{
    int w = pb_text_width(s, scale);
    pb_text_scaled((PB_FB_W - w) / 2, y, s, scale, fg, bg);
}

void pb_backlight(uint8_t level)
{
    if(PB_PIN_LCD_BL < 0) return;
    gpio_set_function(PB_PIN_LCD_BL, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PB_PIN_LCD_BL);
    pwm_set_wrap(slice, 255);
    pwm_set_enabled(slice, true);
    pwm_set_gpio_level(PB_PIN_LCD_BL, level);
}

/* 把整帧缓冲推到屏幕 */
void pb_display_flush(void)
{
    pb_display_flush_region(0, 0, PB_FB_W, PB_FB_H);
}

/* 只把指定矩形区域推到屏幕（区域内按整行成对推送，宽度需为偶数） */
void pb_display_flush_region(int x, int y, int w, int h)
{
    if(x < 0) { w += x; x = 0; }
    if(y < 0) { h += y; y = 0; }
    if(x + w > PB_FB_W) w = PB_FB_W - x;
    if(y + h > PB_FB_H) h = PB_FB_H - y;
    if(w <= 0 || h <= 0) return;
    if(w & 1) w--;                    /* 成对推送，宽度取偶数 */
    if(w <= 0) return;

    lcd_begin_pixels((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
    for(int yy = 0; yy < h; yy++)
    {
        const uint16_t *row = &pb_fb[(y + yy) * PB_FB_W + x];
        for(int xx = 0; xx < w / 2; xx++)
            s_rowword[xx] = mk_pair(row[2 * xx], row[2 * xx + 1]);
        push_words(s_rowword, w / 2);
    }
    lcd_end_pixels();
}

/* 不经帧缓冲直接整面板填色（含黑边，清屏用），覆盖完整面板而非帧缓冲 */
void pb_fill_screen(uint16_t color)
{
    lcd_set_window(0, 0, PB_SCREEN_W, PB_SCREEN_H);
    lcd_cmd(0x2C);
    pio_tx_idle();
    lcd_pio_configure(1);
    gpio_put(PB_PIN_LCD_DC, 1);
    gpio_put(PB_PIN_LCD_CS, 0);
    for(int x = 0; x < PB_SCREEN_W / 2; x++)
        s_rowword[x] = mk_pair(color, color);
    for(int y = 0; y < PB_SCREEN_H; y++)
        push_words(s_rowword, PB_SCREEN_W / 2);
    lcd_end_pixels();
}

/* ---- 任意矩形直推（第三方核心/模拟器流式上屏用） ---- */
/* 绝对面板坐标，不经帧缓冲：把 (x,y,w,h) 的 RGB565 像素按行优先直接发给面板。
 * 宽度必须是偶数（内部按像素对打包）。阻塞到 DMA 推完。 */
void pb_display_push_rect(int x, int y, int w, int h, const uint16_t *rgb565)
{
    if(rgb565 == NULL || w <= 0 || h <= 0) return;
    if(w & 1) return;
    if(w > PB_SCREEN_W) return;

    lcd_set_window((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
    lcd_cmd(0x2C);
    pio_tx_idle();
    lcd_pio_configure(1);
    gpio_put(PB_PIN_LCD_DC, 1);
    gpio_put(PB_PIN_LCD_CS, 0);
    for(int yy = 0; yy < h; yy++)
    {
        const uint16_t *row = &rgb565[(size_t)yy * w];
        for(int xx = 0; xx < w / 2; xx++)
            s_rowword[xx] = mk_pair(row[2 * xx], row[2 * xx + 1]);
        push_words(s_rowword, (size_t)(w / 2));
    }
    lcd_end_pixels();
}

/* 改 MADCTL（0x36）：MX=0x40 左右镜像，MY=0x80 上下镜像，两者同时=旋转 180°。
 * 供画面需要翻转的核心使用；窗口坐标含义不变，由面板按新方向解释。 */
void pb_display_set_madctl(uint8_t madctl)
{
    pio_tx_idle();
    uint8_t d[1] = { madctl };
    lcd_cmd_data(0x36, d, 1);
    lcd_cmd(0x2C);
}

/* ---- 面板反色与 GB 调色板 ---- */
void pb_display_set_invert(bool on)
{
    pb_display_wait_idle();
    lcd_cmd(on ? 0x21 : 0x20);
}

void pb_display_set_gb_palette(const uint16_t pal4[4])
{
    for(int i = 0; i < 4; i++)
        s_pal565[i] = pal4[i];
}

/* ================================================================== */
/* 双核 GB 流式：半幅乒乓 + core1 推带                                  */
/* ================================================================== */

/* GB 画面在帧缓冲里的几何（按帧缓冲 PB_FB_W/H 居中） */
#if PB_DISPLAY_SCALE15
#define GB_FRAME_W  240
#define GB_FRAME_H  216
#define GB_FRAME_X  ((PB_FB_W - GB_FRAME_W) / 2)
#define GB_FRAME_Y  ((PB_FB_H - GB_FRAME_H) / 2)   /* 240 高=12 */
#else
#define GB_FRAME_W  160
#define GB_FRAME_H  144
#define GB_FRAME_X  ((PB_FB_W - 160) / 2)
#define GB_FRAME_Y  ((PB_FB_H - 144) / 2)
#endif

#define GB_WORDS_PER_ROW (GB_FRAME_W / 2)
#define GB_BAND_ROWS     (GB_FRAME_H / 2)
#define GB_BAND_SLOTS    2

/* 带池复用 pb_fb 内存（1.5x 需 103680B < pb_fb 115200B） */
static uint32_t *s_band_pool_base(void)
{
    return reinterpret_cast<uint32_t *>(pb_fb);
}

static uint32_t *band_slot_ptr(int slot)
{
    return s_band_pool_base() +
           (size_t)slot * GB_BAND_ROWS * GB_WORDS_PER_ROW;
}

/* core0 侧：正在填的那条带 */
static int       s_band_id = -1;
static int       s_band_active;
static int       s_band_base_row;
static int       s_band_rows;
static uint32_t *s_band;

/* core0 -> core1 交付环（两项） */
typedef struct { int slot, row, rows; } band_order_t;
static volatile band_order_t s_ord[GB_BAND_SLOTS];
static volatile int s_head;
static volatile int s_tail;

static int s_gb_row;

/* 把 buf 的 rows 行推给面板（面板起始行 GB_FRAME_Y + row0），只在 core1 调 */
static void push_band(const uint32_t *buf, int row0, int rows)
{
    if(rows <= 0) return;
    lcd_begin_pixels((uint16_t)GB_FRAME_X, (uint16_t)(GB_FRAME_Y + row0),
                     (uint16_t)GB_FRAME_W, (uint16_t)rows);
    push_words(buf, (size_t)rows * GB_WORDS_PER_ROW);
    lcd_end_pixels();
}

/* 开一条新带：这块缓冲上一次装的是 id-2 那条带，等它推完才能覆盖 */
static void band_open(void)
{
    int id = s_band_id + 1;
    while(s_tail < id - GB_BAND_SLOTS + 1)
        __asm volatile ("nop");

    s_band_id   = id;
    s_band      = band_slot_ptr(id % GB_BAND_SLOTS);
    s_band_rows = 0;
}

/* 把当前这条带交给 core1 */
static void band_publish(void)
{
    if(!s_band_active || s_band_rows <= 0) return;

    int id = s_band_id;
    int slot = id % GB_BAND_SLOTS;
    s_ord[slot].slot = slot;
    s_ord[slot].row  = s_band_base_row;
    s_ord[slot].rows = s_band_rows;
    __dmb();
    s_head = id + 1;
    __sev();

    s_band_active = 0;
    s_band_rows   = 0;
}

/* 取“输出第 r 行”要写的那一行缓冲；写满一条带就自动交付、开下一条 */
static uint32_t *band_row_ptr(int r)
{
    if(!s_band_active || s_band_rows >= GB_BAND_ROWS ||
       r != s_band_base_row + s_band_rows)
    {
        if(s_band_active) band_publish();
        band_open();
        s_band_base_row = r;
        s_band_active   = 1;
    }
    s_band_rows++;
    return s_band + (size_t)(s_band_rows - 1) * GB_WORDS_PER_ROW;
}

void pb_display_begin_frame(void)
{
    if(s_band_active) band_publish();
    s_gb_row = 0;
}

void pb_display_end_frame(void)
{
    if(s_band_active) band_publish();
}

/* ================================================================== */
/* 双核 NES 流式：4 条带 × 60 行，带池复用 pb_fb                        */
/* 竖屏：240x240 裁剪 1:1（带池 115200B）                              */
/* 横屏：完整 256x240，填满 256 帧缓冲（带池 122880B），帧缓冲再居中    */
/* ================================================================== */

#if PB_LCD_ROTATE
#define NES_FRAME_W      256
#else
#define NES_FRAME_W      240
#endif
/* 内容在帧缓冲内：横屏填满 256 宽，竖屏 x=0；帧缓冲在面板上的位置由 PB_FB_X 处理 */
#define NES_FRAME_X      0
#define NES_FRAME_H      240
/* 帧缓冲高度即 240，垂直无偏移 */
#define NES_FRAME_Y      0
#define NES_WORDS_PER_ROW (NES_FRAME_W / 2)
#define NES_BAND_ROWS     (NES_FRAME_H / NES_BAND_SLOTS)
#define NES_BAND_SLOTS    4

static uint32_t *nes_band_slot_ptr(int slot)
{
    return reinterpret_cast<uint32_t *>(pb_fb) +
           (size_t)slot * NES_BAND_ROWS * NES_WORDS_PER_ROW;
}

/* core0 侧：正在填的那条带 */
static int       s_nes_band_id = -1;
static int       s_nes_band_active;
static int       s_nes_band_base_row;
static int       s_nes_band_rows;
static uint32_t *s_nes_band;

/* core0 -> core1 交付环（NES_BAND_SLOTS 项） */
typedef struct { int slot, row, rows; } nes_band_order_t;
static volatile nes_band_order_t s_nes_ord[NES_BAND_SLOTS];
static volatile int s_nes_head;
static volatile int s_nes_tail;

/* 把 buf 的 rows 行推给面板（逻辑起点 NES_FRAME_X, NES_FRAME_Y + row0），只在 core1 调 */
static void nes_push_band(const uint32_t *buf, int row0, int rows)
{
    if(rows <= 0) return;
    lcd_begin_pixels((uint16_t)NES_FRAME_X, (uint16_t)(NES_FRAME_Y + row0),
                     (uint16_t)NES_FRAME_W, (uint16_t)rows);
    push_words(buf, (size_t)rows * NES_WORDS_PER_ROW);
    lcd_end_pixels();
}

/* 开一条新带：这块缓冲上一次装的是 id-NES_BAND_SLOTS 那条带，等它推完才能覆盖 */
static void nes_band_open(void)
{
    int id = s_nes_band_id + 1;
    while(s_nes_tail < id - NES_BAND_SLOTS + 1)
        __asm volatile ("nop");

    s_nes_band_id     = id;
    s_nes_band        = nes_band_slot_ptr(id % NES_BAND_SLOTS);
    s_nes_band_rows   = 0;
    s_nes_band_active = 1;
}

/* 把当前这条带交给 core1 */
static void nes_band_publish(void)
{
    if(!s_nes_band_active || s_nes_band_rows <= 0) return;

    int id = s_nes_band_id;
    int slot = id % NES_BAND_SLOTS;
    s_nes_ord[slot].slot = slot;
    s_nes_ord[slot].row  = s_nes_band_base_row;
    s_nes_ord[slot].rows = s_nes_band_rows;
    __dmb();
    s_nes_head = id + 1;
    __sev();

    s_nes_band_active = 0;
    s_nes_band_rows   = 0;
}

/* 取“输出第 r 行”要写的那一行缓冲；写满一条带就自动交付、开下一条 */
static uint32_t *nes_band_row_ptr(int r)
{
    if(!s_nes_band_active || s_nes_band_rows >= NES_BAND_ROWS ||
       r != s_nes_band_base_row + s_nes_band_rows)
    {
        if(s_nes_band_active) nes_band_publish();
        nes_band_open();
        s_nes_band_base_row = r;
    }
    s_nes_band_rows++;
    return s_nes_band + (size_t)(s_nes_band_rows - 1) * NES_WORDS_PER_ROW;
}

void pb_display_nes_begin_frame(void)
{
    if(s_nes_band_active) nes_band_publish();
}

void pb_display_nes_end_frame(void)
{
    if(s_nes_band_active) nes_band_publish();
}

void pb_push_nes_line(const uint16_t *colors, uint8_t line)
{
    if(line >= NES_FRAME_H) return;
    uint32_t *dst = nes_band_row_ptr(line);
    for(int x = 0; x < NES_WORDS_PER_ROW; x++)
        dst[x] = mk_pair(colors[x * 2], colors[x * 2 + 1]);
}

/* core1 主循环：唯一职责是把 core0 交付的每条带推给面板 */
static void core1_push_worker(void)
{
    while(true)
    {
        int did = 0;

        /* NES 队列 */
        while(s_nes_head != s_nes_tail)
        {
            __dmb();
            int id   = s_nes_tail;
            int slot = s_nes_ord[id % NES_BAND_SLOTS].slot;
            int row  = s_nes_ord[id % NES_BAND_SLOTS].row;
            int rows = s_nes_ord[id % NES_BAND_SLOTS].rows;

            nes_push_band(nes_band_slot_ptr(slot), row, rows);

            __dmb();
            s_nes_tail = id + 1;
            __sev();
            did = 1;
        }

        /* GB 队列 */
        while(s_head != s_tail)
        {
            __dmb();
            int id   = s_tail;
            int slot = s_ord[id % GB_BAND_SLOTS].slot;
            int row  = s_ord[id % GB_BAND_SLOTS].row;
            int rows = s_ord[id % GB_BAND_SLOTS].rows;

            push_band(band_slot_ptr(slot), row, rows);

            __dmb();
            s_tail = id + 1;
            __sev();
            did = 1;
        }

        if(!did)
            __wfe();
    }
}

void pb_display_start_pusher(void)
{
    multicore_reset_core1();
    multicore_launch_core1(core1_push_worker);
}

void pb_display_wait_idle(void)
{
    /* 忙等而非 WFI：RP2040 默认无周期 tick，USB 未接时 WFI 可能永不唤醒 */
    while(s_tail < s_head || s_nes_tail < s_nes_head)
        __asm volatile ("nop");
}

/* ---- 1.5 倍最近邻映射 ---- */
#if PB_DISPLAY_SCALE15
static uint8_t s_xmap[240];
static int s_xmap_ready;

static void build_xmap(void)
{
    for(int x = 0; x < 240; x++)
        s_xmap[x] = (uint8_t)((x * 2) / 3);
    s_xmap_ready = 1;
}
#endif

void pb_push_gb_line(const uint8_t *pixels, uint8_t line)
{
#if PB_DISPLAY_SCALE15
    if(!s_xmap_ready) build_xmap();

    /* 垂直 1.5 倍：每条源线输出 1 或 2 行，累计正好 216 行 */
    int r0 = (3 * line + 1) / 2;
    if(r0 >= GB_FRAME_H) return;
    int r1 = (3 * (line + 1) + 1) / 2;
    if(r1 > GB_FRAME_H) r1 = GB_FRAME_H;
    for(int r = r0; r < r1; r++)
    {
        uint32_t *dst = band_row_ptr(r);
        for(int x = 0; x < GB_FRAME_W; x += 2)
            dst[x / 2] = mk_pair(s_pal565[pixels[s_xmap[x]] & 3],
                                 s_pal565[pixels[s_xmap[x + 1]] & 3]);
    }
    if(r1 > s_gb_row) s_gb_row = r1;
#else
    if(line >= GB_FRAME_H) return;
    uint32_t *dst = band_row_ptr(line);
    for(int x = 0; x < GB_FRAME_W; x += 2)
        dst[x / 2] = mk_pair(s_pal565[pixels[x] & 3],
                             s_pal565[pixels[x + 1] & 3]);
    s_gb_row = line + 1;
#endif
}

/* 与 pb_push_gb_line 同样的 1.5 倍几何，但像素已经是 RGB565，不再查调色板。
 * 用于 GBC：Walnut-CGB 把 fixPalette[pixel] 直接给出 RGB565。 */
void pb_push_rgb_line(const uint16_t *colors, uint8_t line)
{
#if PB_DISPLAY_SCALE15
    if(!s_xmap_ready) build_xmap();

    int r0 = (3 * line + 1) / 2;
    if(r0 >= GB_FRAME_H) return;
    int r1 = (3 * (line + 1) + 1) / 2;
    if(r1 > GB_FRAME_H) r1 = GB_FRAME_H;
    for(int r = r0; r < r1; r++)
    {
        uint32_t *dst = band_row_ptr(r);
        for(int x = 0; x < GB_FRAME_W; x += 2)
            dst[x / 2] = mk_pair(colors[s_xmap[x]], colors[s_xmap[x + 1]]);
    }
    if(r1 > s_gb_row) s_gb_row = r1;
#else
    if(line >= GB_FRAME_H) return;
    uint32_t *dst = band_row_ptr(line);
    for(int x = 0; x < GB_FRAME_W; x += 2)
        dst[x / 2] = mk_pair(colors[x], colors[x + 1]);
    s_gb_row = line + 1;
#endif
}

void pb_display_init(void)
{
    /* 默认 DMG 经典绿调色板 */
    s_pal565[0] = PB_RGB(0x9B, 0xBC, 0x0F);
    s_pal565[1] = PB_RGB(0x8B, 0xAC, 0x0F);
    s_pal565[2] = PB_RGB(0x30, 0x62, 0x30);
    s_pal565[3] = PB_RGB(0x0F, 0x38, 0x0F);

    gpio_init(PB_PIN_LCD_CS); gpio_set_dir(PB_PIN_LCD_CS, GPIO_OUT); gpio_put(PB_PIN_LCD_CS, 1);
    gpio_init(PB_PIN_LCD_DC); gpio_set_dir(PB_PIN_LCD_DC, GPIO_OUT); gpio_put(PB_PIN_LCD_DC, 1);

    if(PB_PIN_LCD_RST >= 0)
    {
        gpio_init(PB_PIN_LCD_RST); gpio_set_dir(PB_PIN_LCD_RST, GPIO_OUT);
        gpio_put(PB_PIN_LCD_RST, 1); sleep_ms(5);
        gpio_put(PB_PIN_LCD_RST, 0); sleep_ms(20);
        gpio_put(PB_PIN_LCD_RST, 1); sleep_ms(120);
    }

    if(PB_PIN_LCD_BL >= 0)
    {
        gpio_init(PB_PIN_LCD_BL); gpio_set_dir(PB_PIN_LCD_BL, GPIO_OUT);
        gpio_put(PB_PIN_LCD_BL, PB_PIN_LCD_BL_ACTIVE_HIGH ? 1 : 0);
    }

    s_pio = pio0;
    s_sm  = 0;
    s_prog_off = pio_add_program(s_pio, &st7789_lcd_program);

    pio_gpio_init(s_pio, PB_PIN_LCD_MOSI);
    pio_gpio_init(s_pio, PB_PIN_LCD_SCK);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PB_PIN_LCD_MOSI, 1, true);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PB_PIN_LCD_SCK, 1, true);

    gpio_set_drive_strength(PB_PIN_LCD_SCK, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_drive_strength(PB_PIN_LCD_MOSI, GPIO_DRIVE_STRENGTH_12MA);
    gpio_set_slew_rate(PB_PIN_LCD_SCK, GPIO_SLEW_RATE_FAST);
    gpio_set_slew_rate(PB_PIN_LCD_MOSI, GPIO_SLEW_RATE_FAST);

    lcd_pio_configure(0);
    dma_init_once();

    /* ---- ST7789 初始化序列（16bpp RGB565） ---- */
    lcd_cmd(0x01); sleep_ms(20);
    { uint8_t d[1] = { PB_LCD_MADCTL }; lcd_cmd_data(0x36, d, 1); }
    { uint8_t d[5] = { 0x0C, 0x0C, 0x00, 0x33, 0x33 }; lcd_cmd_data(0xB2, d, 5); }
    { uint8_t d[1] = { 0x55 }; lcd_cmd_data(0x3A, d, 1); }
    { uint8_t d[1] = { 0x01 }; lcd_cmd_data(0x26, d, 1); }
    { uint8_t d[1] = { 0x14 }; lcd_cmd_data(0xB7, d, 1); }
    { uint8_t d[1] = { 0x25 }; lcd_cmd_data(0xBB, d, 1); }
    { uint8_t d[1] = { 0x2C }; lcd_cmd_data(0xC0, d, 1); }
    { uint8_t d[1] = { 0x01 }; lcd_cmd_data(0xC2, d, 1); }
    { uint8_t d[1] = { 0x12 }; lcd_cmd_data(0xC3, d, 1); }
    { uint8_t d[1] = { 0x20 }; lcd_cmd_data(0xC4, d, 1); }
    { uint8_t d[2] = { 0xA4, 0xA1 }; lcd_cmd_data(0xD0, d, 2); }
    { uint8_t d[14] = { 0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                       0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23 };
      lcd_cmd_data(0xE0, d, 14); }
    { uint8_t d[14] = { 0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                       0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23 };
      lcd_cmd_data(0xE1, d, 14); }
    if(PB_LCD_INVERT) lcd_cmd(0x21); else lcd_cmd(0x20);
    sleep_ms(115);
    lcd_cmd(0x11); sleep_ms(120);
    lcd_cmd(0x29); sleep_ms(20);

    /* 清屏：直接写屏（此时还没启动 core1） */
    pb_fill_screen(PB_BLACK);
}
