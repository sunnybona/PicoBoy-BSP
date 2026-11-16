/*
 * pb_audio.cpp — 单 GPIO PWM 音频驱动（DMA 连续环形 + 可选音调/短音效）
 *
 * 连续音频：PWM 固定高载波（12bit → 约 61kHz），DMA 工作在「读地址 ring」模式，
 *           由 PWM 的 DREQ 驱动，读指针走到环形缓冲末尾时硬件自动折回开头，
 *           传输计数给一个极大值，所以 DMA 永不停止、也不需要 ISR 重挂——
 *           没有块边界，也就没有重挂间隙造成的周期性咔哒声。
 *           CPU 只在 pb_audio_submit 时写样本，并通过读地址寄存器判断积压。
 * 音调：把 PWM 载波频率直接设成目标频率，硬件持续发声。
 *
 * 两套模式共用同一个 PWM slice，必须显式互斥：
 *   - 进音调（pb_tone_start / pb_audio_play_sample）：先停 slice、摘掉 DMA，再改 wrap/clkdiv。
 *   - 出音调（pb_tone_stop）：把 DAC 的 wrap/clkdiv 还原、环形缓冲填静音中点、重挂 DMA。
 * 不这么做的话，DMA 会在下一个 wrap 把 cc 覆盖回环形缓冲里的值：音调永远停不下来，
 * 而且 DAC 的 wrap 一直是音调的，之后 pb_audio_submit 的样本会被按错误频率抽出来。
 */
#include "picoboy/pb_audio.h"
#include "picoboy/board.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"

#define RING_MASK (PB_AUDIO_RING - 1u)
#define PWM_WRAP  ((1u << PB_AUDIO_PWM_BITS) - 1u)
#define PWM_MID   (PWM_WRAP / 2u)

/* 写入余量：离读指针多近就停止写入，避免覆盖正在播放的样本 */
#define ROOM_MARGIN 4u
/* 启动/重同步时的预载：3/4 环 ≈ 25ms 提前量，吸收帧抖动 */
#define PRELOAD     ((PB_AUDIO_RING * 3u) / 4u)

/* 环形缓冲字节数对应的地址位数（DMA 读 ring 按 2^n 字节边界折回） */
static constexpr uint8_t ring_addr_bits(uint32_t bytes)
{
    return bytes <= 1u ? 0u : (uint8_t)(1u + ring_addr_bits(bytes >> 1));
}
#define RING_ADDR_BITS ring_addr_bits((uint32_t)PB_AUDIO_RING * 2u)

/* ring 折回要求缓冲按其字节大小对齐 */
static uint16_t s_ring[PB_AUDIO_RING]
    __attribute__((aligned(PB_AUDIO_RING * 2u)));

static uint32_t s_wp;                    /* 已写入样本总数（单调递增） */
static uint32_t s_read_total;            /* 读指针总数（按折回累计） */
static uint32_t s_prev_ridx;             /* 上一次的环内读索引，用于识别折回 */
static uint8_t  s_dma_ch;
static uint  s_slice;
static uint8_t  s_volume = 8;

/* 模式状态（见文件头说明） */
static uint8_t s_inited;                 /* pb_audio_init 已跑过 */
static volatile uint8_t s_stream_on;     /* DMA 流式正在跑 */
static uint8_t s_in_tone;                /* 正在发声调 */
static uint8_t s_dac_div_i;              /* DAC 载波分频，恢复时要用 */
static uint8_t s_dac_div_f;

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
    uint32_t ridx = (addr - (uint32_t)(uintptr_t)s_ring) >> 1;

    if(ridx < s_prev_ridx)               /* 读指针折回了一次 */
        s_read_total += PB_AUDIO_RING;
    s_prev_ridx = ridx;

    return s_read_total + ridx;
}

/* 欠载重同步：整环填静音中点，写指针重新拉到读指针前 PRELOAD 处。
 * 这样即使供数跟不上，也只会出现一小段干净的静音，而不是重复旧音频。 */
static void resync(uint32_t rpos)
{
    for(uint32_t i = 0; i < PB_AUDIO_RING; i++)
        s_ring[i] = PWM_MID;
    s_wp = rpos + PRELOAD;
}

/* 停掉流式：关 slice（断掉 DREQ）→ 中止 DMA → GPIO 拉低。 */
static void audio_stream_stop(void)
{
    if(!s_stream_on) return;
    s_stream_on = 0;

    pwm_set_enabled(s_slice, false);
    dma_channel_abort(s_dma_ch);
    while(dma_channel_is_busy(s_dma_ch))
        tight_loop_contents();
    pwm_set_gpio_level(PB_PIN_AUDIO, 0);
}

/* 装回流式：还原 DAC 的 wrap/clkdiv、环形缓冲填静音中点、重挂连续 ring DMA */
static void audio_stream_start(void)
{
    for(uint32_t i = 0; i < PB_AUDIO_RING; i++)
        s_ring[i] = PWM_MID;

    s_read_total = 0;
    s_prev_ridx = 0;
    s_wp = PRELOAD;

    pwm_set_clkdiv_int_frac(s_slice, s_dac_div_i, s_dac_div_f);
    pwm_set_wrap(s_slice, (uint16_t)PWM_WRAP);
    pwm_set_gpio_level(PB_PIN_AUDIO, PWM_MID);
    pwm_set_enabled(s_slice, true);

    s_in_tone = 0;
    s_stream_on = 1;
    /* 极大传输计数 + 读 ring：硬件自动折回，永不停止，无需 ISR */
    dma_channel_transfer_from_buffer_now(s_dma_ch, s_ring, 0xFFFFFFFFu);
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
    s_dac_div_i = (uint8_t)di;
    s_dac_div_f = (uint8_t)df;
    pwm_config_set_clkdiv_int_frac(&cfg, s_dac_div_i, s_dac_div_f);

    pwm_init(s_slice, &cfg, true);
    pwm_set_gpio_level(PB_PIN_AUDIO, PWM_MID);

    s_dma_ch = (uint8_t)dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(s_dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pwm_get_dreq(s_slice));
    channel_config_set_ring(&c, false, RING_ADDR_BITS);
    dma_channel_configure(s_dma_ch, &c,
                          &pwm_hw->slice[s_slice].cc,
                          s_ring, 0, false);

    s_inited = 1;
    audio_stream_start();
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
    if(!s_inited) return;
    /* 兜底：如果流式被停掉（例如某次音调没收尾），这里直接把 DAC 装回来 */
    if(!s_stream_on && !s_in_tone) audio_stream_start();
    if(!s_stream_on) return;                           /* 正在发声调，丢掉这一批 */

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
        s_ring[s_wp & RING_MASK] = to_level(mono[i]);
        s_wp++;
    }
}

/* ---- 音调（UI 用：会把 slice 切成「载波=目标频率」，DMA 必须先摘掉） ---- */
void pb_tone_start(uint32_t freq_hz)
{
    if(!s_inited) return;                 /* 没初始化就别动 slice，免得踩到别的外设 */
    if(freq_hz == 0) { pb_tone_stop(); return; }

    audio_stream_stop();                  /* 关键：先让 DMA 松手 */

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
    s_in_tone = 1;
}

void pb_tone_stop(void)
{
    if(!s_inited) { pwm_set_gpio_level(PB_PIN_AUDIO, 0); return; }

    pwm_set_gpio_level(PB_PIN_AUDIO, 0);
    if(s_in_tone)
        audio_stream_start();             /* 还原 DAC 载波 + 重挂 DMA，音调不会自己再响 */
}

void pb_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    pb_tone_start(freq_hz);
    sleep_ms(duration_ms);
    pb_tone_stop();
}

void pb_audio_play_sample(const int16_t *samples, size_t count, uint32_t sample_rate)
{
    if(!s_inited) return;

    audio_stream_stop();                  /* 同一套规矩：先摘 DMA 再改 wrap */

    pwm_set_clkdiv_int_frac(s_slice, 1, 0);
    pwm_set_wrap(s_slice, PWM_WRAP);

    uint64_t period_us = 1000000ull / sample_rate;
    for(size_t i = 0; i < count; i++)
    {
        pwm_set_gpio_level(PB_PIN_AUDIO, to_level(samples[i]));
        busy_wait_us(period_us);
    }

    pwm_set_gpio_level(PB_PIN_AUDIO, 0);
    audio_stream_start();                 /* 播放完把 DAC 交还给流式 */
}
