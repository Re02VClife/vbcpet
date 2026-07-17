idf_component_register(
    SRCS "vbcpet_main.c" "serial_display.c"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_lcd esp_driver_jpeg
)
# audio_codec.c 暂不编译 (WT9932P4-TINY 无板载音频 codec);
# 接到 ES7210/ES8311 后加回 SRCS 并取消 esp_codec_dev 依赖
