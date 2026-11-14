/*
 * pb_audio.h — 音频 API（单 GPIO PWM 当 DAC，DMA 环形缓冲）
 *
 * 连续游戏音频（推荐，非阻塞）：
 *   pb_audio_init();
 *   pb_audio_submit(mono, count);      // 每帧把单声道样本写进环形缓冲
 *
 * 短音/提示音：
 *   pb_beep(440, 200);                 // 阻塞：响 440Hz 200ms
 *   pb_tone_start(880); ... pb_tone_stop();
 *
 *   pb_audio_set_volume(7);            // 0..10
 */
#ifndef PB_AUDIO_H
#define PB_AUDIO_H

#include <stdint.h>
#include <stddef.h>

#include "board.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 PWM + DMA 环形缓冲（默认静音，DMA 自动续流） */
void pb_audio_init(void);

/* 音量 0..10（默认 8） */
void pb_audio_set_volume(uint8_t vol);

/* 非阻塞：把单声道 16bit PCM（中心 0）写进环形缓冲；缓冲满则丢弃，不覆盖在播数据 */
void pb_audio_submit(const int16_t *mono, size_t count);

/* 非阻塞：开始/改变一个持续音调（freq=0 等同于停止）；PWM 硬件持续发声，不占 CPU */
void pb_tone_start(uint32_t freq_hz);
void pb_tone_stop(void);

/* 阻塞：响一个音调后停止 */
void pb_beep(uint32_t freq_hz, uint32_t duration_ms);

/* 阻塞：播放一段单声道 16bit PCM（中心 0），用于短音效 */
void pb_audio_play_sample(const int16_t *samples, size_t count, uint32_t sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* PB_AUDIO_H */
