# esp_lcd_st7102 (vendored from OSPTEK)

来源: https://gitee.com/osptek/4.3-tft-480x800-mipi-st7102
路径: `versions/YDP430BT009-V1/examples/esp32p4-idf5_st7102-mipi_esp-lvgl-port_lvgl9/components/esp_lcd_st7102`

为本项目屏 **YDP430BT009-V1 (4.3" 480×800 ST7102 MIPI-DSI 2-lane)** 的屏厂官方示例组件,
替代此前使用的社区组件 `kevincoooool/esp_lcd_st7102`(两者代码同源, 功能一致)。

相对 osptek 原始文件的适配 (仅 2 处, 为兼容 IDF v5.5.3):
1. `esp_lcd_st7102_mipi.c`: `panel_dev_config->color_space` → `rgb_ele_order` (IDF v5.5 字段改名)
2. `include/esp_lcd_st7102.h`: DPI 配置宏按 IDF 版本填充 `pixel_format`/`in_color_format`/`out_color_format` (IDF v6.0 起移除 pixel_format)

许可证: Espressif Apache-2.0 基础 + OSPTEK 示例 CC-BY-4.0。
