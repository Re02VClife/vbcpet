#!/usr/bin/env python3
"""串口控制台/日志工具 — bring-up 常用 (读日志 / 发命令 / 存盘)。

用法:
  python tools/serial_console.py --port COM8 --baud 115200 --seconds 20
  python tools/serial_console.py --port COM8 --baud 2000000 --send "wifi status" --seconds 8
  python tools/serial_console.py --port COM8 --send "test all" --log _run.txt
"""
import argparse, sys, time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM8")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--send", action="append", default=[], help="发送一行 (可多次; 自动补 \\n)")
    ap.add_argument("--send-after", type=float, default=1.5, help="连接后多久开始发送")
    ap.add_argument("--log", default=None, help="把收到的原始日志写文件")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    import serial
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    s = serial.Serial(a.port, a.baud, timeout=0.2)
    s.reset_input_buffer()
    # 热身: 打开串口的瞬间, 线路上的毛刺会被板子当成一个坏字节, 吃掉紧随其后的
    # 第一条命令的首字符 (2026-09-24 实测 "ldo 4 3300" 被读成 "?ldo" -> 未知命令,
    # 导致整个 LDO 验证白跑一轮)。先发一个空行顶掉它; 板子控制台对空行直接 continue。
    try:
        s.write(b"\r\n")
        s.flush()
    except Exception:
        pass
    t0 = time.time()
    sent = set()
    buf = bytearray()
    while time.time() - t0 < a.seconds:
        chunk = s.read(8192)
        if chunk:
            buf += chunk
            if not a.quiet:
                sys.stdout.write(chunk.decode("utf-8", errors="replace"))
                sys.stdout.flush()
        if a.send:
            for i, line in enumerate(a.send):
                if i not in sent and time.time() - t0 > a.send_after * (i + 1):
                    payload = (line + "\n").encode("utf-8")
                    s.write(payload)
                    s.flush()
                    sent.add(i)
                    print(f"\n>>> [sent] {line}", flush=True)
    s.close()
    if a.log:
        Path(a.log).write_bytes(bytes(buf))
        print(f"\n--- {len(buf)} 字节 → {a.log} ---")
    else:
        print(f"\n--- 共 {len(buf)} 字节 ---")


if __name__ == "__main__":
    main()
