/* ── 内部: 处理一行完成 ── */
static void demux_line_complete(void)
{
    if (g_line_pos == 0) return;                       /* 空行跳过 */
    if (g_line_buf[g_line_pos - 1] == '\r') g_line_pos--;
    g_line_buf[g_line_pos] = '\0';
    g_line_pos = 0;
    g_line_start_us = 0;

    if (strncmp(g_line_buf, "FRAME:", 6) == 0) {
        handle_frame_line();
    } else if (strncmp(g_line_buf, "CTRL:", 5) == 0) {
        if (g_ctrl_handler)
            g_ctrl_handler(g_line_buf + 5, g_ctrl_ctx);
    } else {
        if (g_raw_handler)
            g_raw_handler(g_line_buf, g_raw_ctx);
    }
}

/* ── 内部: 单字节进行状态机 ── */
static void demux_feed_byte(uint8_t ch)
{
    if (ch == '\n') { demux_line_complete(); return; }
    if (g_line_pos < sizeof(g_line_buf) - 1) {
        if (g_line_pos == 0) g_line_start_us = esp_timer_get_time();
        g_line_buf[g_line_pos++] = (char)ch;
    }
    /* 行溢出: 静默丢弃, 等下一个 \n */
}

void channel_demux_poll(void)
{
    if (!g_transport || !g_jpeg_buf) return;

    /* 行超时: 500ms 无 \n → 重置缓冲 (防止串口丢字节导致永久卡死) */
    if (g_line_pos > 0 && g_line_start_us > 0) {
        if (esp_timer_get_time() - g_line_start_us > 500000) {
            ESP_LOGW(TAG, "Line timeout, reset (%d bytes dropped)", (int)g_line_pos);
            g_line_pos = 0;
            g_line_start_us = 0;
        }
    }

    /* 批量排空: 每次 poll 最多 8x512B, 逐字节喂状态机。
     * 旧版每 poll 只读 1 字节 + 主循环 10ms 延时 ≈ 100B/s,
     * 2M 链路 200KB/s 直接溢出丢帧 — Live2D 不显示的根因。 */
    uint8_t chunk[512];
    int rounds = 0;
    while (rounds < 8) {
        int n = g_transport->recv(g_transport, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        rounds++;
        for (int i = 0; i < n; i++) demux_feed_byte(chunk[i]);
    }
}
