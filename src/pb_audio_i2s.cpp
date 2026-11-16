/*
 * pb_audio_i2s.cpp — PIO I2S 音频驱动（外部 I2S DAC，DMA 连续环形）
 *
 * 与 PWM 版同一套 API、同一种「读地址 ring」连续 DMA 思路：
 *   - PIO 状态机按固定速率（61035 帧/秒）消费，每个 32bit FIFO 字含左右两个
 *     16bit 样本；单声道把同一个值填进两半。
 *   - DMA 工作在读地址 ring 模式，硬件自动折回、传输计数给极大值，永不停止、
 *     不需要 ISR，没有块边界间隙。
 *   - 积压直接从 DMA 读地址寄存器判断；欠载时整环填 0（I2S 的静音是 0）并
 *     resync，只出现一小段干净静音，不重复旧音频。
 *
 * I2S 是数字总线，必须外接 I2S DAC（如 MAX98357A）。引脚见 board.h。
 */
#include "picoboy/pb_audio.h"
#include "picoboy/board.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "pb_i2s.pio.h"

#define RING_MASK (PB_AUDIO_RING - 1u)

/* 写入余量：离读指针多近就停止写入，避免覆盖正在播放的样本 */
#define ROOM_MARGIN 4u
/* 启动/重同步时的预载：3/4 环 ≈ 25ms 提前量，吸收帧抖动 */
#define PRELOAD     ((PB_AUDIO_RING * 3u) / 4u)

/* 提示音幅度（不顶满，留余量不爆） */
#define BEEP_AMP    12000

/* 环形缓冲字节数对应的地址位数（DMA 读 ring 按 2^n 字节边界折回） */
static constexpr uint8_t ring_addr_bits(uint32_t bytes)
{
    return bytes <= 1u ? 0u : (uint8_t)(1u + ring_addr_bits(bytes >> 1));
}
#define RING_ADDR_BITS ring_addr_bits((uint32_t)PB_AUDIO_RING * 4u)

/* ring 折回要求缓冲按其字节大小对齐 */
static uint32_t s_ring[PB_AUDIO_RING]
    __attribute__((aligned(PB_AUDIO_RING * 4u)));

static PIO    s_pio = pio1;
static uint   s_sm;
static uint   s_prog_offset;
static uint   s_dma_ch;
static uint32_t s_wp;                    /* 已写入帧总数（单调递增） */
static uint32_t s_read_total;            /* 读指针总数（按折回累计） */
static uint32_t s_prev_ridx;             /* 上环内读索引，用于识别折回 */
static uint8_t  s_volume = 8;

static uint8_t s_inited;                 /* pb_audio_init 已跑过 */
static volatile uint8_t s_stream_on;     /* DMA 流式正在跑 */
static uint8_t s_in_tone;                /* 正在手动喂音调/音效 */

static_assert((PB_AUDIO_RING & RING_MASK) == 0u,
              "PB_AUDIO_RING must be a power of two");

void pb_audio_set_volume(uint8_t vol)
{
    s_volume = vol > 10 ? 10 : vol;
}

/* 当前读指针总数（硬件读地址 → 环内索引，再按折回累计） */
static uint32_t read_position(void)
{
    uint32_t addr = dma_channel_hw_addr(s_dma_ch)->read_addr;
    uint32_t ridx = (addr - (uint32_t)(uintptr_t)s_ring) >> 2;

    if(ridx < s_prev_ridx)               /* 读指针折回了一次 */
        s_read_total += PB_AUDIO_RING;
    s_prev_ridx = ridx;

    return s_read_total + ridx;
}

/* 音量缩放 + 钳位（I2S 是有符号、中心 0，不需要 PWM 中点偏置） */
static inline int16_t scale_sample(int16_t v)
{
    int32_t s = ((int32_t)v * s_volume) / 10;
    if(s > 32767)  s = 32767;
    if(s < -32768) s = -32768;
    return (int16_t)s;
}

/* 单声道样本打包成左右相同的 32bit I2S 字 */
static inline uint32_t pack_stereo(int16_t v)
{
    uint16_t u = (uint16_t)v;
    return ((uint32_t)u << 16) | (uint32_t)u;
}

/* 欠载重同步：整环填 0，写指针重新拉到读指针前 PRELOAD 处。 */
static void resync(uint32_t rpos)
{
    for(uint32_t i = 0; i < PB_AUDIO_RING; i++)
        s_ring[i] = 0;
    s_wp = rpos + PRELOAD;
}

/* 把 PIO 状态机相位拉回 entry_point（清 FIFO、复位 PC），不重跑完整配置 */
static void sm_phase_reset(void)
{
    pio_sm_set_enabled(s_pio, s_sm, false);
    pio_sm_clear_fifos(s_pio, s_sm);
    pio_sm_restart(s_pio, s_sm);
    pio_sm_exec(s_pio, s_sm,
                pio_encode_jmp(s_prog_offset + pb_i2s_offset_entry_point));
}

static void audio_stream_stop(void)
{
    if(!s_stream_on) return;
    s_stream_on = 0;
    dma_channel_abort(s_dma_ch);
    while(dma_channel_is_busy(s_dma_ch))
        tight_loop_contents();
    sm_phase_reset();
}

static void audio_stream_start(void)
{
    for(uint32_t i = 0; i < PB_AUDIO_RING; i++)
        s_ring[i] = 0;

    s_read_total = 0;
    s_prev_ridx = 0;
    s_wp = PRELOAD;

    sm_phase_reset();
    s_in_tone = 0;
    s_stream_on = 1;
    /* 极大传输计数 + 读 ring：硬件自动折回，永不停止，无需 ISR */
    dma_channel_transfer_from_buffer_now(s_dma_ch, s_ring, 0xFFFFFFFFu);
    pio_sm_set_enabled(s_pio, s_sm, true);
}

void pb_audio_init(void)
{
    for(uint32_t i = 0; i < PB_AUDIO_RING; i++)
        s_ring[i] = 0;

    s_sm = pio_claim_unused_sm(s_pio, true);
    s_prog_offset = pio_add_program(s_pio, &pb_i2s_program);

    pio_gpio_init(s_pio, PB_PIN_I2S_DATA);
    pio_gpio_init(s_pio, PB_PIN_I2S_BCLK);
    pio_gpio_init(s_pio, PB_PIN_I2S_LRCLK);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PB_PIN_I2S_BCLK, 2, true);
    pio_sm_set_consecutive_pindirs(s_pio, s_sm, PB_PIN_I2S_DATA, 1, true);

    pio_sm_config c = pb_i2s_program_get_default_config(s_prog_offset);
    sm_config_set_out_pins(&c, PB_PIN_I2S_DATA, 1);
    sm_config_set_sideset_pins(&c, PB_PIN_I2S_BCLK);
    sm_config_set_out_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    /* 64 PIO 周期/帧：clkdiv(1/256) = sysclk * 4 / sample_rate */
    uint32_t div256 = clock_get_hz(clk_sys) * 4u / PB_AUDIO_SAMPLE_RATE;
    sm_config_set_clkdiv_int_frac(&c, div256 >> 8, div256 & 0xffu);

    pio_sm_init(s_pio, s_sm, s_prog_offset, &c);
    pio_sm_set_pins(s_pio, s_sm, 0);
    pio_sm_exec(s_pio, s_sm,
                pio_encode_jmp(s_prog_offset + pb_i2s_offset_entry_point));

    s_dma_ch = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(s_pio, s_sm, true));
    channel_config_set_ring(&dc, false, RING_ADDR_BITS);
    dma_channel_configure(s_dma_ch, &dc,
                          &s_pio->txf[s_sm],
                          s_ring, 0, false);

    s_inited = 1;
    audio_stream_start();
}

void pb_audio_submit(const int16_t *mono, size_t count)
{
    if(!s_inited) return;
    /* 兜底：如果流式被停掉（例如某次音调没收尾），这里直接装回来 */
    if(!s_stream_on && !s_in_tone) audio_stream_start();
    if(!s_stream_on) return;                           /* 正在手动喂音，丢掉这一批 */

    uint32_t rpos = read_position();
    int32_t used = (int32_t)(s_wp - rpos);             /* 环里还积压多少 */

    if(used <= 0)                                      /* 供数跟不上：欠载重同步 */
    {
        resync(rpos);
        used = (int32_t)PRELOAD;
    }

    int32_t room = (int32_t)PB_AUDIO_RING - used - (int32_t)ROOM_MARGIN;
    if(room <= 0) return;                              /* 满了，丢弃，不覆盖在播数据 */

    size_t n = count < (size_t)room ? count : (size_t)room;
    for(size_t i = 0; i < n; i++)
    {
        s_ring[s_wp & RING_MASK] = pack_stereo(scale_sample(mono[i]));
        s_wp++;
    }
}

/* ---- 音调（UI 用）。I2S 无载波可调，改成手动向 PIO 喂方波 ---- */
void pb_tone_start(uint32_t freq_hz)
{
    if(!s_inited) return;
    if(freq_hz == 0) { pb_tone_stop(); return; }

    audio_stream_stop();
    s_in_tone = 1;
    /* 非阻塞连续音调在没有后台任务时无法持续；UI 实际使用阻塞的 pb_beep。
     * 这里先让状态机跑起来，pb_beep 会持续喂数。 */
    pio_sm_set_enabled(s_pio, s_sm, true);
}

void pb_tone_stop(void)
{
    if(!s_inited) return;
    if(s_in_tone)
    {
        s_in_tone = 0;
        audio_stream_start();
    }
}

void pb_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if(!s_inited || freq_hz == 0) return;

    audio_stream_stop();
    s_in_tone = 1;
    pio_sm_set_enabled(s_pio, s_sm, true);

    /* 阻塞写，put 自动按 PIO 速率节流；时长由总帧数决定 */
    uint32_t total = (uint32_t)(((uint64_t)PB_AUDIO_SAMPLE_RATE * duration_ms) / 1000u);
    uint32_t half = PB_AUDIO_SAMPLE_RATE / freq_hz / 2u;
    if(half == 0) half = 1;

    for(uint32_t i = 0; i < total; i++)
    {
        int16_t level = ((i / half) & 1u) ? -(int16_t)BEEP_AMP : (int16_t)BEEP_AMP;
        pio_sm_put_blocking(s_pio, s_sm, pack_stereo(level));
    }

    s_in_tone = 0;
    audio_stream_start();
}

void pb_audio_play_sample(const int16_t *samples, size_t count, uint32_t sample_rate)
{
    if(!s_inited) return;
    (void)sample_rate;                                 /* 按系统速率直接播 */

    audio_stream_stop();
    s_in_tone = 1;
    pio_sm_set_enabled(s_pio, s_sm, true);

    for(size_t i = 0; i < count; i++)
        pio_sm_put_blocking(s_pio, s_sm, pack_stereo(scale_sample(samples[i])));

    s_in_tone = 0;
    audio_stream_start();
}
