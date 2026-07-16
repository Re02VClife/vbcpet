idf_component_register(
    SRCS "vbcpet_main.c" "protocol.c" "audio_codec.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_codec_dev nvs_flash esp_wifi
)
