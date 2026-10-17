/** OV5640 bring-up 探测实现 — 详见 camera.h 的文件头说明。
 *
 * 时序 (PWDN 高有效 / RSTB 低有效, 依据 HPM 官方 hpm_ov5640.h):
 *   PWDN=1 (掉电)                    保持 ≥1ms
 *   MCLK 起振 (24MHz)
 *   RSTB=0 (复位)                    保持 ≥1ms
 *   PWDN=0 (退出掉电)
 *   RSTB=1 (释放复位) → **保持高** → 等 ≥20ms 让内部寄存器块上电
 *   读 0x300A/0x300B
 *
 * MCLK: P4 的 LEDC 时钟源只有 XTAL(40M)/PLL_F80M(80M)/RC_FAST(~20M), 没有 24M 整数倍,
 * 所以必须靠 LEDC 的小数分频: 80MHz / 3.328 ≈ 24.03MHz (误差 +0.13%, XVCLK 允许 ±10%)。
 * 解析度取 1bit = 50% 占空比, 对 XVCLK 足够 (OV5640 只把它当时钟源)。
 */
#include "camera.h"
#include "board_config.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cam";

#define OV5640_SCCB_ADDR    0x3C
#define OV5640_REG_PID_H    0x300A
#define OV5640_REG_PID_L    0x300B
#define OV5640_PID_H        0x56
#define OV5640_PID_L        0x40

#define CAM_MCLK_HZ         24000000

/* LEDC 时钟源是**全局**的: ESP32-P4 上所有 LEDC timer 必须共用同一个源
 * (LEDC_USE_XTAL_CLK / LEDC_USE_RC_FAST_CLK 尤其互斥)。
 * 本板上 LEDC 有两个用户: 背光 PWM (duty 1023/1023, timer0) 与舵机 (50Hz, timer1) —— 二者都用
 * LEDC_AUTO_CLK, 于是驱动替它们选定了 RC_FAST, 轮到我们要 PLL_F80M 时被驱动拒绝:
 *   E ledc: ledc_set_timer_div(693): timer clock conflict, already is 18 but attempt to 9
 * ⇒ 舵机侧已显式改成 LEDC_USE_PLL_DIV_CLK (50Hz/14bit 在 80MHz 下分频合法), 这里保持一致。
 * (背光是 1kHz/10bit, 同样只需整数分频, 与源无关。)
 *
 * 为什么非要 80MHz: 24MHz 在 40M(XTAL)/20M(RC_FAST) 下都不是整数分频, 只有
 * 80MHz / 3.328 ≈ 24.03MHz 才落得进 XVCLK 的 ±10%。 */
#define CAM_LEDC_TIMER      LEDC_TIMER_2      /* 0=背光, 1=舵机 */
#define CAM_LEDC_CHANNEL    LEDC_CHANNEL_3    /* 0=背光, 1/2=舵机 */

static i2c_master_dev_handle_t s_dev = NULL;
static bool s_mclk_on = false;

/** 起 24MHz MCLK (LEDC, PLL_F80M 小数分频)。失败返回 false —— 只是没时钟, 不致命, 继续试着读 */
static bool cam_mclk_start(void)
{
    /* 不做"已启动就早退"的优化: `gp 5x` 或 `cam mclk` 的 PCNT 都可能把 GPIO5 的 pad
     * 从 LEDC 手里抢走, 早退会让 MCLK 再也回不来。ledc 配置本身是幂等的。 */
    const bool first = !s_mclk_on;

    ledc_timer_config_t tcfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,   /* 50% 占空比 */
        .timer_num       = CAM_LEDC_TIMER,
        .freq_hz         = CAM_MCLK_HZ,
        .clk_cfg         = LEDC_USE_PLL_DIV_CLK,
        .deconfigure     = false,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer 配置失败 (24MHz): %s", esp_err_to_name(err));
        return false;
    }

    ledc_channel_config_t ccfg = {
        .gpio_num   = PIN_CAM_MCLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = CAM_LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = CAM_LEDC_TIMER,
        .duty       = 1,                       /* 1/2^1 = 50% */
        .hpoint     = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags      = { .output_invert = 0 },
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel 绑定 GPIO%d 失败: %s", PIN_CAM_MCLK, esp_err_to_name(err));
        return false;
    }

    s_mclk_on = true;
    if (first) {
        ESP_LOGI(TAG, "MCLK: GPIO%d ← LEDC, 目标 24MHz (80MHz 小数分频, 实测≈24.03MHz)",
                 PIN_CAM_MCLK);
    }
    return true;
}

/** 实测 MCLK —— 用 PCNT 在 GPIO5 上数一段时间的脉冲, 反推真实频率。
 *
 * 为什么必须实测: `ledc_channel_config()` 返回 ESP_OK 只说明"驱动接受了参数",
 * **不等于信号真的到了 pad** —— pad 被别的外设占走、走线/焊盘开路、M1→CN1 断线,
 * 这几种情况配置照样成功, 而 CN1.14 上什么都没有。
 * 而 OV5640 **没有 XVCLK(XCLK) 就完全不应答 SCCB** —— 这恰好就是本板的现象,
 * 所以 MCLK 是"软件说 OK、却从没人看过"的那个环节。
 *
 * ⚠️ 故意**不调用** `pcnt_unit_set_glitch_filter()`: 默认的 glitch 滤波阈值在
 * 1000ns 量级, 会把 24MHz(周期 41ns) 整个滤掉, 读回 0 个脉冲, 反而"证实"
 * 一个不存在的故障。 */
static void cam_mclk_measure(void)
{
    /* ⚠️ 顺序很关键 (2026-09-29 修):
     *   ledc_channel_config() → gpio_set_direction(GPIO_MODE_OUTPUT)
     *                        → gpio_ll_input_disable()      ← 会关掉输入缓冲!
     * 而 PCNT 走的是 GPIO matrix 的**输入**路径 ⇒ 如果 LEDC 配完之后不再开输入,
     * PCNT 就数到 0, 误报"MCLK 没输出"。
     * 实测就踩了这个坑: 三次 `cam mclk` 都报 0 个脉冲, 而万用表量 CN1.14 是
     * 0.75V(在翻转) —— 测量方法本身把被测信号弄没了。
     * 正确顺序: 先让 PCNT 接上输入矩阵 → 再配 LEDC 输出 → 最后单独补开输入缓冲。
     * (gpio_set_direction 不会拆掉 GPIO matrix 上已连好的输入信号。) */
    pcnt_unit_handle_t    unit = NULL;
    pcnt_channel_handle_t chan = NULL;

    pcnt_unit_config_t ucfg = {
        .high_limit = 32767,   /* 24MHz × 1ms ≈ 24000, 留余量 */
        .low_limit  = -1,
    };
    esp_err_t err = pcnt_new_unit(&ucfg, &unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MCLK 实测: pcnt_new_unit 失败 %s(%d)", esp_err_to_name(err), err);
        return;
    }

    pcnt_chan_config_t ccfg = {
        .edge_gpio_num  = PIN_CAM_MCLK,
        .level_gpio_num = -1,
    };
    err = pcnt_new_channel(unit, &ccfg, &chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MCLK 实测: pcnt_new_channel 失败 %s(%d)", esp_err_to_name(err), err);
        pcnt_del_unit(unit);
        return;
    }
    pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                      PCNT_CHANNEL_EDGE_ACTION_HOLD);

    cam_mclk_start();                  /* 配 LEDC 输出 (这一步会把输入缓冲关掉) */
    gpio_input_enable(PIN_CAM_MCLK);   /* 补开输入缓冲 —— 只开输入, 不动输出 */
    vTaskDelay(pdMS_TO_TICKS(20));

    pcnt_unit_clear_count(unit);
    pcnt_unit_start(unit);
    int64_t t0 = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(1));
    int64_t t1 = esp_timer_get_time();
    pcnt_unit_stop(unit);

    int cnt = 0;
    pcnt_unit_get_count(unit, &cnt);
    int64_t us = t1 - t0;
    if (us <= 0) us = 1;
    long hz = (long)((int64_t)cnt * 1000000 / us);

    ESP_LOGW(TAG, "MCLK 实测: GPIO%d 在 %lldus 内数到 %d 个脉冲 ⇒ ≈%ld Hz  %s",
             PIN_CAM_MCLK, (long long)us, cnt, hz,
             (hz > 1000000) ? "[有信号]" : "[≈0 —— MCLK 没有真正输出到 pad!]");

    pcnt_del_channel(chan);
    pcnt_del_unit(unit);

    /* PCNT 可能动过这个 pad 的方向/输入使能 —— 重配一次 LEDC, 保证 MCLK 继续输出 */
    cam_mclk_start();
}

/** PWDN 极性: 1 = 高有效(掉电=高, HPM 官方 hpm_ov5640.h 的定义)
 *            0 = 低有效(掉电=低) —— 本板文档一直标着 "PWDNB 极性待确认",
 *                所以两种都要能扫, 否则把模块摁在掉电里会表现为"0x3C 无应答"。 */
static int s_pwdn_active_high = 1;

/** PWDN / RSTB 上电时序 —— 2026-09-28 按 Linux 内核驱动 `ov5640_reset()` 重写,
 *  2026-09-29 修掉"抄逻辑值当物理电平"的 bug。
 *
 *  内核的时序是:
 *    reset_gpio=deassert → PWDN 掉电 → 5~10ms → PWDN 上电 → 5~10ms
 *    → reset_gpio=assert → 1~2ms → reset_gpio=deassert → 20~25ms 之后才谈 SCCB
 *  关键: **上电过程要一直摁着复位**, 结束时要**放开**复位。
 *
 *  ⚠️ 曾经的 bug: 直接照抄了内核的 `gpiod_set_value(reset, 1)` / `(0)` 这两个
 *  **逻辑值**当物理电平用。内核 gpiod 的 1/0 是"断言/释放", 物理电平由 DT 的
 *  GPIO_ACTIVE_LOW 决定; 对本板 (REST 低有效, expansion.md:79) 就是
 *  断言=物理低 / 释放=物理高。原实现最后写了 `RSTB=0` 并注释成"常态低",
 *  等于**把模块一直摁在复位里** ⇒ test cam / cam pol 全部必然 NACK。
 *
 *  出处: drivers/media/i2c/ov5640.c (ov5640_reset / ov5640_power)。 */
static void cam_power_sequence(void)
{
    const int pwdn_on  = s_pwdn_active_high ? 1 : 0;   /* 掉电电平 */
    const int pwdn_off = s_pwdn_active_high ? 0 : 1;   /* 工作电平 */

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_CAM_RSTB) | (1ULL << PIN_CAM_PWDN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    gpio_set_level(PIN_CAM_RSTB, 0);   /* 摁住复位 (低有效) */
    gpio_set_level(PIN_CAM_PWDN, pwdn_on);
    vTaskDelay(pdMS_TO_TICKS(8));

    cam_mclk_start();                  /* 上电前就给 XVCLK */

    gpio_set_level(PIN_CAM_PWDN, pwdn_off);   /* 上电 —— 此时复位仍被摁住 */
    vTaskDelay(pdMS_TO_TICKS(8));

    gpio_set_level(PIN_CAM_RSTB, 1);   /* 释放复位 (低有效的 REST ⇒ 释放 = 物理高) */
    vTaskDelay(pdMS_TO_TICKS(25));     /* 保持释放, 等内部寄存器块就绪 */
}

/** SCCB = I2C 兼容读: 写 2 字节寄存器地址(大端) → 重复起始 → 读 1 字节 */
static esp_err_t sccb_read16(uint16_t reg, uint8_t *val)
{
    uint8_t addr[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_transmit_receive(s_dev, addr, sizeof(addr), val, 1, 100);
}

int camera_probe(void)
{
    /* ⚠️ 2026-09-28 修 bug: 这里最初只 init 了总线就去做事务 —— s_dev 还是 NULL,
     * i2c_master_transmit_receive(NULL,...) 立刻返回 ESP_ERR_INVALID_STATE(0x103),
     * 表现为"SCCB 读 PID 失败"且耗时只有几十微秒 (真超时是 100ms 级)。
     * 共享总线必须先 add_device 拿到设备句柄。 */
    if (!s_dev) {
        ESP_LOGW(TAG, "[诊断] s_dev=NULL, 开始注册 (bus=%p)", i2c_bus_get_handle());
        esp_err_t err = i2c_bus_init();
        if (err != ESP_OK) { ESP_LOGE(TAG, "I2C 总线不可用: %s(%d)", esp_err_to_name(err), err); return -1; }
        ESP_LOGW(TAG, "[诊断] bus=%p, add_device(0x%02X, %uHz)...",
                 i2c_bus_get_handle(), OV5640_SCCB_ADDR, (unsigned)I2C_FREQ_HZ);
        err = i2c_bus_add_device(OV5640_SCCB_ADDR, I2C_FREQ_HZ, &s_dev);
        ESP_LOGW(TAG, "[诊断] add_device 返回 %s(%d), s_dev=%p",
                 esp_err_to_name(err), err, s_dev);
        if (err != ESP_OK) { ESP_LOGE(TAG, "SCCB 地址 0x%02X 注册失败: %s", OV5640_SCCB_ADDR, esp_err_to_name(err)); return -1; }
    } else {
        ESP_LOGW(TAG, "[诊断] 复用已有 s_dev=%p", s_dev);
    }

    ESP_LOGI(TAG, "OV5640 探测: PWDN=GPIO%d RSTB=GPIO%d MCLK=GPIO%d SCCB=0x%02X",
             PIN_CAM_PWDN, PIN_CAM_RSTB, PIN_CAM_MCLK, OV5640_SCCB_ADDR);
    cam_power_sequence();

    /* 先用 i2c_master_probe 判定"从机是否 ACK 自己的地址":
     * ESP_OK = ACK(模块活着) · ESP_ERR_NOT_FOUND = NACK(地址上没人) · 其它 = 总线问题。
     * 这比直接读寄存器好判 —— 读失败时 NACK 与超时都表现为同一个 INVALID_STATE。 */
    esp_err_t pe = i2c_bus_probe(OV5640_SCCB_ADDR);
    ESP_LOGW(TAG, "probe(0x%02X) = %s(%d)  [0=ACK 有器件, 258=NOT_FOUND 无应答]",
             (unsigned)OV5640_SCCB_ADDR, esp_err_to_name(pe), pe);

    uint8_t h = 0, l = 0;
    int64_t t0 = esp_timer_get_time();
    esp_err_t e1 = sccb_read16(OV5640_REG_PID_H, &h);
    int64_t t1 = esp_timer_get_time();
    esp_err_t e2 = sccb_read16(OV5640_REG_PID_L, &l);
    int64_t t2 = esp_timer_get_time();
    if (e1 != ESP_OK || e2 != ESP_OK) {
        ESP_LOGE(TAG, "SCCB 读 PID 失败: 0x300A=%s(%d) 0x300B=%s(%d) 耗时=%lldus/%lldus",
                 esp_err_to_name(e1), e1, esp_err_to_name(e2), e2,
                 (long long)(t1 - t0), (long long)(t2 - t1));
        return -3;
    }

    ESP_LOGI(TAG, "PID: 0x300A=0x%02X 0x300B=0x%02X (OV5640 期望 0x56 / 0x40)", h, l);
    if (h == OV5640_PID_H && l == OV5640_PID_L) {
        ESP_LOGI(TAG, "OV5640 已应答 — CN1 电源/FPC/SCCB/复位链路全部正常");
        return 0;
    }
    ESP_LOGW(TAG, "PID 不匹配 — SCCB 通了但不是 OV5640 (或模块型号/地址不对)");
    return -4;
}

int camera_self_test(void)
{
    /* 连读 3 次: 顺带暴露"能读但内容不稳"的接触问题 */
    for (int i = 0; i < 3; i++) {
        int r = camera_probe();
        if (r != 0) return r;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return 0;
}

void camera_cmd(int addr)
{
    if (addr == -3) {
        /* cam mclk —— 实测 GPIO5 上到底有没有 24MHz, 不碰 I2C。
         * 这是唯一一个"软件自证 MCLK 真的输出"的手段 (否则只能靠万用表/示波器量 CN1.14)。 */
        ESP_LOGW(TAG, "MCLK 实测开始 (PCNT @ GPIO%d)...", PIN_CAM_MCLK);
        cam_mclk_measure();
        return;
    }
    if (addr == -2) {
        /* cam raw —— **不碰** RSTB/PWDN/MCLK, 只按当前引脚状态做一次 I2C probe。
         * 用途: 先用 gp 命令手动放出复位、任意等待, 再单独验证 SCCB 有没有应答,
         * 从而把"复位释放到第一个事务之间的等待时间不够"这条排除掉。 */
        if (!s_dev) {
            if (i2c_bus_init() != ESP_OK ||
                i2c_bus_add_device(OV5640_SCCB_ADDR, I2C_FREQ_HZ, &s_dev) != ESP_OK) {
                ESP_LOGE(TAG, "raw: 设备注册失败"); return;
            }
        }
        uint8_t h = 0, l = 0;
        int64_t t0 = esp_timer_get_time();
        esp_err_t pe = i2c_bus_probe(OV5640_SCCB_ADDR);
        int64_t t1 = esp_timer_get_time();
        esp_err_t e1 = sccb_read16(OV5640_REG_PID_H, &h);
        int64_t t2 = esp_timer_get_time();
        esp_err_t e2 = sccb_read16(OV5640_REG_PID_L, &l);
        int64_t t3 = esp_timer_get_time();
        ESP_LOGW(TAG, "raw probe(0x%02X)=%s(%d) %lldus | PID 0x300A=%s(%d) 0x%02X %lldus | 0x300B=%s(%d) 0x%02X %lldus",
                 OV5640_SCCB_ADDR, esp_err_to_name(pe), pe, (long long)(t1 - t0),
                 esp_err_to_name(e1), e1, h, (long long)(t2 - t1),
                 esp_err_to_name(e2), e2, l, (long long)(t3 - t2));
        return;
    }
    if (addr == -1) {
        /* 极性扫描: 两种 PWDN 极性 × 三个候选地址, 现场一次跑完 (约 1 秒)。
         * 只要某一组合 probe 回 OK, 就说明模块活着、是极性/地址的问题。 */
        s_pwdn_active_high = 1;
        for (int p = 0; p < 2; p++) {
            s_pwdn_active_high = p ? 0 : 1;
            for (uint16_t a = 0x3C; a <= 0x3E; a++) {
                if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
                if (i2c_bus_add_device(a, I2C_FREQ_HZ, &s_dev) != ESP_OK) { s_dev = NULL; continue; }
                cam_power_sequence();
                esp_err_t pe = i2c_bus_probe(a);
                ESP_LOGW(TAG, "  极性=%s 地址=0x%02X -> probe=%s(%d)%s",
                         s_pwdn_active_high ? "高有效" : "低有效", a,
                         esp_err_to_name(pe), pe,
                         pe == ESP_OK ? "   <== 有器件!" : "");
            }
        }
        ESP_LOGW(TAG, "极性/地址扫描完毕: 没有任何组合得到 ACK ⇒ 不是极性问题, 查 FPC 接触/模块");
        return;
    }
    if (addr > 0 && addr != OV5640_SCCB_ADDR) {
        /* 换地址: 注销旧设备句柄, 用新地址注册。s_dev 是文件级静态, 这里直接重建。 */
        if (s_dev) { i2c_master_bus_rm_device(s_dev); s_dev = NULL; }
        esp_err_t err = i2c_bus_add_device((uint16_t)addr, I2C_FREQ_HZ, &s_dev);
        ESP_LOGW(TAG, "换 SCCB 地址 -> 0x%02X: %s(%d) s_dev=%p",
                 addr, esp_err_to_name(err), err, s_dev);
        if (err != ESP_OK) { s_dev = NULL; return; }
        cam_power_sequence();   /* 换地址重来一遍时序 */
    }
    int r = camera_probe();
    ESP_LOGW(TAG, "cam 探测结果: %d (%s)", r,
             r == 0 ? "OK" : (r == -3 ? "SCCB 无应答" : "见上面日志"));
}
