"""帧时间戳标记：把一个 32bit 值编码进帧左上角的像素块，供接收端读回。

用于回环中测量「编码→解码」延迟：发送端把 now_ms() 的低 32 位画进帧，
接收端解码后与本地 now_ms() 相减。仅在**同一台机器**上有意义（共用时钟）。
块取较大尺寸并二值化，以抵抗 H.264 压缩带来的像素抖动。
"""
from __future__ import annotations

import numpy as np

from .protocol import now_ms

NBITS = 32          # 编码位数（ms 低 32 位，约 49.7 天回绕）
BLOCK = 16          # 每个 bit 的像素块边长
_MASK = (1 << NBITS) - 1


def tag_value() -> int:
    """当前时间戳的低 32 位。"""
    return now_ms() & _MASK


def draw_tag(arr: np.ndarray, value: int) -> None:
    """就地把 value 画进帧顶部。arr: HxWx3 uint8 (rgb24)。

    块 0 = 同步块 (恒白)，块 1..NBITS = 低位到高位的 bit。
    """
    h, w, _ = arr.shape
    if h < BLOCK or w < (NBITS + 1) * BLOCK:
        return  # 帧太小，放不下标记
    # 同步块
    arr[0:BLOCK, 0:BLOCK, :] = 255
    for i in range(NBITS):
        bit = (value >> i) & 1
        x0 = (i + 1) * BLOCK
        arr[0:BLOCK, x0:x0 + BLOCK, :] = 255 if bit else 0


def read_tag(arr: np.ndarray) -> int | None:
    """从帧顶部读回 value；若无有效同步块则返回 None。"""
    h, w, _ = arr.shape
    if h < BLOCK or w < (NBITS + 1) * BLOCK:
        return None

    def block_hi(bx: int) -> bool:
        x0 = bx * BLOCK
        region = arr[0:BLOCK, x0:x0 + BLOCK, :]
        return float(region.mean()) > 128.0

    if not block_hi(0):  # 同步块必须为白
        return None
    value = 0
    for i in range(NBITS):
        if block_hi(i + 1):
            value |= (1 << i)
    return value


def latency_ms(sent_value: int) -> int:
    """由发送端标记值算出与当前时刻的延迟 (ms)，处理 32bit 回绕。"""
    return (tag_value() - sent_value) & _MASK
