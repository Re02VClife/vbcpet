/** 板级供电轨 — GPIO39~48 那组 IO 的 LDO_VO4 (模组 pin66)
 *
 * 2026-09-25 实测定案: 模组 pin66 = `ESP_LDO_VO4` 的**输出**, 同时就是 GPIO39~48
 * 这组 pad 的 IO 供电轨。复位后停在 ~1.2V 的默认档, 必须由 `app_main` 最早期配置,
 * 否则一整组外设全瘫: SD 卡 init failed / PA_EN(GPIO46) 拉不起 /
 * MPU_INT(GPIO47) 收不到 / BL_PWM(GPIO48) 驱不动 MT3608 EN。
 *
 * 实测 (V1 板): `ldo 4 1800 / 2700 / 3300` 三档 pin66 与 GPIO48 同步跟随 ⇒ 通道号 4 正确。
 * 注意 3300 走的是 "rail bypass" (直通输入轨), 1800/2700 才是片内 LDO 调压档。
 *
 * ⚠️ IDF 约束: adjustable 通道不能被 acquire 两次。通道 4 的 handle 在这里共享 ——
 * 控制台 `ldo 4 <mv>` 命令直接复用它调压, 不要再自己 acquire。
 */
#pragma once
#include "esp_ldo_regulator.h"

/** 由 `ldo_vo4_init()` 在 app_main 早期赋值; `CONFIG_VBCPET_LDO_VO4_MV=0` 时为 NULL */
extern esp_ldo_channel_handle_t g_ldo_vo4;
