/*
 * pb_audio.cpp — 单 GPIO PWM 音频驱动（DMA 环形缓冲 + 可选音调/短音效）
 *
 * 连续音频：PWM 固定高载波（12bit → 约 61kHz），DMA 由 PWM 的 DREQ 驱动，
 *           每产生一个 wrap 自动从环形缓冲取下一个比较值，ISR 里重新挂 DMA 形成
 *           无限循环；CPU 只在 pb_audio_submit 时往里写，不参与逐样本输出。
 * 音调：把 PWM 载波频率直接设成目标频率，硬件持续发声。
 */
#include "picoboy/pb_audio.h"
#include "picoboy/board.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"

#define RING_MASK (PB_AUDIO_RING - 1u)
#define RING_HALF (PB_AUDIO_RING / 2u)
#define PWM_WRAP  ((1u << PB_AUDIO_PWM_BITS) - 1u)
#define PWM_MID   (PWM_WRAP / 2u)

static uint16_t s_ring[PB_AUDIO_RING];
static uint32_t s_wp;                    /* 已写入样本总数（单调递增） */
static volatile uint32_t s_blocks;       /* DMA 已完成的环形块数 */
static uint8_t  s_dma_ch;
static uint     s_slice;
static uint8_t  s_volume = 8;

void pb_audio_set_volume(uint8_t vol)
{
    s_volume = vol > 10 ? 10 : vol;
}

/* DMA 已消费的样本总数 */
static inline int32_t audio_consumed(void)
{
    uint32_t remaining = dma_channel_hw_addr(s_dma_ch)->transfer_count;
    uint32_t in_block = PB_AUDIO_RING - remaining;
    return (int32_t)(s_blocks * PB_AUDIO_RING + in_block);
}

static void audio_dma_isr(void)
{
    uint32_t mask = 1u << s_dma_ch;
    if(dma_hw->ints0 & mask)
    {
        dma_hw->ints0 = mask;
        s_blocks++;
        dma_channel_transfer_from_buffer_now(s_dma_ch, s_ring, PB_AUDIO_RING);
    }
}

void pb_audio_init(void)
{
    for(uint32_t i = 0; i < PB_AUDIO_RING; i++)
        s_ring[i] = PWM_MID;

    gpio_set_function(PB_PIN_AUDIO, GPIO_FUNC_PWM);
    s_slice = pwm_gpio_to_slice_num(PB_PIN_AUDIO);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, PWM_WRAP);

    float div = (float)clock_get_hz(clk_sys) /
                ((float)(PWM_WRAP + 1u) * (float)PB_AUDIO_SAMPLE_RATE);
    int di = (int)div;
    int df = (int)((div - (float)di) * 16.0f + 0.5f);
    if(df >= 16) { df = 0; di += 1; }
    if(di < 1)   { di = 1; df = 0; }
    pwm_config_set_clkdiv_int_frac(&cfg, (uint8_t)di, (uint8_t)df);

    pwm_init(s_slice, &cfg, true);
    pwm_set_gpio_level(PB_PIN_AUDIO, PWM_MID);

    s_dma_ch = (uint8_t)dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pwm_get_dreq(s_slice));
    dma_channel_configure(s_dma_ch, &c,
                          &pwm_hw->slice[s_slice].cc,
                          s_ring, PB_AUDIO_RING, false);

    dma_channel_set_irq0_enabled(s_dma_ch, true);
    irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_isr);
    irq_set_enabled(DMA_IRQ_0, true);
    dma_channel_transfer_from_buffer_now(s_dma_ch, s_ring, PB_AUDIO_RING);

    /* 预热到半满，避免开机读到旧数据 */
    s_wp = RING_HALF;
}

/* int16 ±32768 → PWM 比较值（中点偏置 + 音量 + 钳位） */
static inline uint16_t to_level(int16_t v)
{
    int32_t scaled = ((int32_t)v * s_volume) / 10;
    scaled = scaled / (1 << (16 - PB_AUDIO_PWM_BITS));
    if(scaled >  (int32_t)PWM_MID) scaled =  (int32_t)PWM_MID;
    if(scaled < -(int32_t)PWM_MID) scaled = -(int32_t)PWM_MID;
    return (uint16_t)((int32_t)PWM_MID + scaled);
}

void pb_audio_submit(const int16_t *mono, size_t count)
{
    int32_t consumed = audio_consumed();
    int32_t used = (int32_t)s_wp - consumed;          /* 环里还积压多少 */
    int32_t room = (int32_t)PB_AUDIO_RING - used - 4;  /* 留 4 个余量 */
    if(room <= 0) return;                               /* 满了，丢弃，不覆盖在播数据 */

    size_t n = count < (size_t)room ? count : (size_t)room;
    for(size_t i = 0; i < n; i++)
    {
        s_ring[s_wp & RING_MASK] = to_level(mono[i]);
        s_wp++;
    }
}

void pb_audio_clear(void)
{
    /* 整环立刻填成中点。DMA 正在读也没关系：接下来读到的每个值都是静音，
     * 最多带过当前这一个样本，不会出现数据错乱。 */
    for(uint32_t i = 0; i < PB_AUDIO_RING; i++)
        s_ring[i] = PWM_MID;

    /* 写指针对齐到 DMA 当前消费位置，避免 submit 误判环满而丢掉新音频 */
    s_wp = (uint32_t)audio_consumed();
}

/* ---- 音调（UI 用，会重配 slice，与流式不要同时用） ---- */
void pb_tone_start(uint32_t freq_hz)
{
    if(freq_hz == 0) { pb_tone_stop(); return; }

    uint32_t fclk = clock_get_hz(clk_sys);
    uint32_t period_cycles = fclk / freq_hz;

    uint16_t div_int = 1;
    uint32_t cnt;
    if(period_cycles <= 65536u)
        cnt = period_cycles;
    else
    {
        div_int = (uint16_t)((period_cycles + 65535u) / 65536u);
        cnt = period_cycles / div_int;
    }
    if(cnt < 2) cnt = 2;

    pwm_set_clkdiv_int_frac(s_slice, div_int, 0);
    pwm_set_wrap(s_slice, (uint16_t)(cnt - 1u));
    pwm_set_gpio_level(PB_PIN_AUDIO, (uint16_t)((cnt * s_volume) / 20u));
    pwm_set_enabled(s_slice, true);
}

void pb_tone_stop(void)
{
    pwm_set_gpio_level(PB_PIN_AUDIO, 0);
}

void pb_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    pb_tone_start(freq_hz);
    sleep_ms(duration_ms);
    pb_tone_stop();
}

void pb_audio_play_sample(const int16_t *samples, size_t count, uint32_t sample_rate)
{
    pwm_set_clkdiv_int_frac(s_slice, 1, 0);
    pwm_set_wrap(s_slice, PWM_WRAP);

    uint64_t period_us = 1000000ull / sample_rate;
    for(size_t i = 0; i < count; i++)
    {
        pwm_set_gpio_level(PB_PIN_AUDIO, to_level(samples[i]));
        busy_wait_us(period_us);
    }
    pb_tone_stop();
}
