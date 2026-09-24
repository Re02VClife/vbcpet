# vbcpet — ESP32-C6 co-processor (esp-hosted slave) firmware
#
# 模组 ESP32-P4C6-Core 里那颗 ESP32-C6FH4 需要跑 esp-hosted 的 **slave/CP** 固件,
# P4 侧的 esp_hosted host 才能通过模组内部 SDIO(GPIO14-19) 拿到 WiFi。
# 如果 C6 是空片/固件不匹配, host 侧会报 "sdio_card_fn_init failed"。
#
# 构建 (目标 esp32c6): 见 _build_c6.bat
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vbcpet_c6_cp)
