/** microSD 驱动实现 — sdmmc + FATFS
 *
 * 2026-09-24 诊断 (V1 实板):
 *   现象 — SCR (1-bit, 首个走 D0 线的命令) 通过, 切到 4-bit 后 SSR (ACMD13, 读 64B) 返回 0xffffffff。
 *   已排除 — 把 pin66 (该组 IO 的供电节点) 从 1.175kΩ 改成 0Ω 直连 3.3V, 现象不变 ⇒ 不是供电问题。
 *   ⇒ 疑 DAT1~3 (GPIO40/41/42 → TF1 pin8/1/2) 虚焊或有其它连接问题。
 *   本版先强制 1-bit 验证: 能挂载即坐实。修复方向 = 查卡座 DAT1~3 焊接, 或 V2 直接按 1-bit 设计。
 */
#include "sd_card.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "freertos/FreeRTOS.h"
#include <dirent.h>
#include <unistd.h>
#include <string.h>

static const char *TAG = "sd";
static const char *MOUNT_POINT = "/sd";
static sdmmc_card_t *s_card = NULL;

int sd_card_init(void)
{
    if (s_card) return 0;   /* 已挂载 */

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = 1;   /* SD1 控制器 (GPIO39-44) */

    /* 2026-09-24 诊断用: 去掉 4BIT 标志, 强制 1-bit (只走 DAT0)。
     * 见文件头注释; 若 1-bit 能挂载则坐实 DAT1~3 问题。 */
    host.flags &= ~SDMMC_HOST_FLAG_4BIT;

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk  = PIN_SD_CLK;
    slot.cmd  = PIN_SD_CMD;
    slot.d0   = PIN_SD_DAT0;
    slot.d1   = PIN_SD_DAT1;
    slot.d2   = PIN_SD_DAT2;
    slot.d3   = PIN_SD_DAT3;
    slot.cd   = PIN_SD_CD;           /* 低=卡在位 */
    slot.flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD mount fail: %d", ret);
        return -1;
    }
    ESP_LOGI(TAG, "SD mounted (%llu MB, %s)", ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20,
             s_card->cid.name);
    return 0;
}

int sd_card_self_test(void)
{
    if (sd_card_init() != 0) {
        ESP_LOGE(TAG, "Self-test: 挂载失败 (卡未插或硬件问题)");
        return -1;
    }

    /* 列根目录 */
    ESP_LOGI(TAG, "根目录内容:");
    DIR *dir = opendir(MOUNT_POINT);
    if (dir) {
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            ESP_LOGI(TAG, "  %s", e->d_name);
        }
        closedir(dir);
    }

    /* 写读测试文件 */
    char path[64];
    snprintf(path, sizeof(path), "%s/_selftest.txt", MOUNT_POINT);
    FILE *f = fopen(path, "w");
    if (!f) { ESP_LOGE(TAG, "open write FAIL"); return -2; }
    fputs("vbcpet sd selftest 2026-08-11\n", f);
    fclose(f);

    f = fopen(path, "r");
    if (!f) { ESP_LOGE(TAG, "open read FAIL"); return -3; }
    char line[128];
    if (fgets(line, sizeof(line), f)) ESP_LOGI(TAG, "读回: %s", line);
    fclose(f);
    unlink(path);

    return 0;
}
