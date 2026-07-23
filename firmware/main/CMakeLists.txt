set(sources
    "vbcpet_main.c"
    "transport_usb.c"
    "channel_demux.c"
    "display_pipeline.c"
    "protocol.c"
    "text_render.c"
    "text_input.c"
    "interaction_engine.c"
)

# 显示后端：Kconfig 编译时选择
if(CONFIG_DISPLAY_BACKEND_SPI)
    list(APPEND sources "display_pipeline_spi.c")
elseif(CONFIG_DISPLAY_BACKEND_DSI)
    list(APPEND sources "display_pipeline_dsi.c")
else()
    # 默认 SPI (无 Kconfig 时回退)
    list(APPEND sources "display_pipeline_spi.c")
endif()

idf_component_register(
    SRCS ${sources}
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash
        driver
        esp_lcd
        esp_driver_jpeg
        json
        esp_timer
        esp_mm
)
# audio_codec.c 暂不编译 (WT9932P4-TINY 无板载音频 codec)

# 保留不编译 (硬件 bring-up 参考):
#   display_test.c  — 原始 ST7789V bring-up
#   serial_display.c — 旧版管道
