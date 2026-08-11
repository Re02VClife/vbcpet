/** 音频 (I2S0 全双工, 无 MCLK)
 *  TX → MAX98357A (PA_EN GPIO46, 3.3V→5V 功放)
 *  RX ← ZTS6672×2 (共用 DIN, L=MK1 语音, R=MK2 AEC 回采)
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 I2S0 全双工 (std mode, 48kHz, 16-bit stereo)。返回 0 成功。 */
int audio_init(void);

/** 功放使能 (MAX98357A SD_MODE, HIGH=开)。 */
void audio_pa_enable(bool on);

/** 播放一段 PCM (stereo 16bit)。 */
int audio_play(const int16_t *pcm, size_t samples);

/** 自检: 440Hz 蜂鸣 ×3 + 麦克风回放 2s。返回 0=通过。 */
int audio_self_test(void);

#ifdef __cplusplus
}
#endif
