/** 音频 codec 初始化 — 小智同款方案 (ES7210 + ES8311)
 *
 * I2C: ES8311@0x18 (DAC), ES7210@0x40 (ADC)
 * I2S: 共用 MCLK/BCLK/WS。ES8311=STD(发送), ES7210=TDM(接收,4ch,CH1=AEC回采)
 * 采样率: 输入 16kHz, 输出 24kHz
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 I2C 总线 + ES7210/ES8311 codec。返回 0 成功。 */
int audio_codec_init(void);

/** 反初始化 codec。 */
void audio_codec_deinit(void);

/** 开始/停止录音 (ES7210 → I2S RX) */
bool audio_start_recording(void);
bool audio_stop_recording(void);

/** 开始/停止放音 (I2S TX → ES8311 → PA) */
bool audio_start_playback(void);
bool audio_stop_playback(void);

/** 设置功放使能 (高有效) */
void audio_pa_enable(bool on);

#ifdef __cplusplus
}
#endif
