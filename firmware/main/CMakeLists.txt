set(sources
    "vbcpet_main.c"
    "transport_usb.c"
    "transport_uart.c"
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
    # 最终板外设驱动 + bring-up 框架
    list(APPEND sources
        "bringup.c"
        "peripherals/i2c_bus.c"
        "peripherals/buttons.c"
        "peripherals/sk6812.c"
        "peripherals/touch.c"
        "peripherals/audio.c"
        "peripherals/mpu6500.c"
        "peripherals/sd_card.c"
        "peripherals/servo.c"
        "peripherals/nfc.c"
        "peripherals/camera.c"
        "peripherals/usb_host.c"
        "peripherals/self_test.c"
        "peripherals/wifi_link.c"
        "peripherals/c6_flash.c"
    )
else()
    # 默认 SPI (无 Kconfig 时回退)
    list(APPEND sources "display_pipeline_spi.c")
endif()

# C6 的合并镜像 (bootloader+分区表+ota_data+app, 烧到 0x0) — P4 用它给 C6 编程
if(CONFIG_VBCPET_C6_FLASH)
    set(c6_embed "c6_merged.bin")
    message(STATUS "vbcpet: 嵌入 C6 镜像 c6_merged.bin (P4-as-programmer)")
else()
    set(c6_embed "")
endif()

idf_component_register(
    SRCS ${sources}
    EMBED_FILES ${c6_embed}
    INCLUDE_DIRS
        "."
    REQUIRES
        hal
        nvs_flash
        driver
        esp_lcd
        esp_driver_jpeg
        json
        esp_driver_ledc
        esp_driver_pcnt
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_rmt
        esp_driver_sdmmc
        fatfs
        sdmmc
        usb
        esp_timer
        esp_mm
        esp_lcd_st7102
        esp_hosted
        esp_wifi_remote
        esp-serial-flasher
        # esp_ldo_regulator 在 IDF 5.5.3 属于 esp_hw_support (components/esp_hw_support/ldo/), 无需单列
)
# 保留不编译 (硬件 bring-up 参考):
#   display_test.c   — 原始 ST7789V bring-up
#   serial_display.c — 旧版管道
#   webrtc_skeleton.c — WebRTC 接收链路骨架 (依赖 esp-hosted/esp_webrtc, 板到后启用, 见 docs/webrtc-bringup.md)
