#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""V1 板 GPIO 电平扫描 —— 逐条下发 gp 命令, 提示用万用表量模组脚, 记录读数。

用途 (V2 打样前的几个未知):
  1) pin66 / pin76 谁是 GPIO48、谁是 ESP_LDO_VO4    -> 步 1/2
  2) GPIO30/31/32 (pin57/58/59) 是不是 3.3V 推挽    -> 能否当 C6 串口编程器
  3) 背光 PWM 换脚候选 GPIO34/49/50 (pin61/77/78)   -> 步 6/7/8
  4) 带负载的对照 GPIO33 (pin60, 接了 IP5306)        -> 步 9

用法:
  python tools/gp_sweep.py --port COM23
  python tools/gp_sweep.py --port COM23 --list        # 只看流程, 不开串口
  python tools/gp_sweep.py --port COM23 --start 4     # 控制台僵了之后从第 4 步续

前提:
  * 板上跑带控制台的固件 (build_wifi 版最干净), 控制台 115200 / 8N1
  * V1 控制台在复位后 1~3 分钟会僵死。某步没回显 gp N mode=... 就是僵了:
    按 RST, 再用 --start N 从这一步续 (RST 会清掉 gp 状态, 所以要重量)
  * gp 命令只支持 pin<=51 (bringup.c:171), GPIO52/53/54 用不了
"""
import argparse
import sys
import threading
import time
from pathlib import Path

STEPS_GP = [
    dict(cmd="gp 48h", title="驱动 GPIO48 输出高", pins=[
        ("pin76", "BL_PWM / 疑似 GPIO48", "哪个脚变高, 哪个才是 GPIO48"),
        ("pin66", "疑似 ESP_LDO_VO4", "对照: 它不该跟着变"),
    ]),
    dict(cmd="gp 48l", title="驱动 GPIO48 输出低", pins=[
        ("pin76", "BL_PWM / 疑似 GPIO48", "上一步跟着变的, 这里应变 0V"),
        ("pin66", "疑似 ESP_LDO_VO4", "仍不该变"),
    ]),
    dict(cmd="gp 30h", title="驱动 GPIO30 输出高", pins=[
        ("pin57", "GPIO30 (V1 上 NC, 无负载)", "约 3.3V 就是能用, 可作 C6_U0RXD"),
    ]),
    dict(cmd="gp 31h", title="驱动 GPIO31 输出高", pins=[
        ("pin58", "GPIO31 (V1 上 NC, 无负载)", "约 3.3V 就是能用, 可作 C6_U0TXD"),
    ]),
    dict(cmd="gp 32h", title="驱动 GPIO32 输出高", pins=[
        ("pin59", "GPIO32 (V1 上 NC, 无负载)", "约 3.3V 就是能用, 可作 C6_IO9 控制"),
    ]),
    dict(cmd="gp 34h", title="驱动 GPIO34 输出高", pins=[
        ("pin61", "GPIO34 (V1 上 NC)", "背光 PWM 换脚候选"),
    ]),
    dict(cmd="gp 49h", title="驱动 GPIO49 输出高", pins=[
        ("pin77", "GPIO49 (V1 上 NC)", "背光 PWM 换脚候选"),
    ]),
    dict(cmd="gp 50h", title="驱动 GPIO50 输出高", pins=[
        ("pin78", "GPIO50 (V1 上 NC)", "背光 PWM 换脚候选"),
    ]),
    dict(cmd="gp 33h", title="驱动 GPIO33 输出高 (带负载对照)", pins=[
        ("pin60", "GPIO33 -> IP5306 pin5", "有负载时的输出电平, 与 NC 脚对比"),
    ]),
]

# ── 第二阶段: LDO_VO4 调压验证 ──────────────────────────────────────────
# 背景: 实测 GPIO48 推挽输出只有 1.33V, 同时 pin66 (ESP_LDO_VO4) = 1.24V。
# IDF 的 components/sdmmc/include/sd_pwr_ctrl_by_on_chip_ldo.h 写明:
#   "set to 4 is the LDO_VO4 is connected to power the SDMMC IO"
# 而 GPIO48 = SD1_CDATA7, 属这一组 ⇒ GPIO39~48 整组 (SD 卡 / PA_EN / MPU_INT /
# BL_PWM) 都被压在 1.24V。把 LDO_VO4 拉到 3300mV 后逐脚复测:
# 若全部变成 3.3V, 则这四个问题一次全解, V2 板不用为它们改线。
STEPS_LDO = [
    dict(cmd="ldo 4 3300", title="LDO_VO4 拉到 3300mV", pins=[
        ("pin66", "ESP_LDO_VO4", "关键: 期望 3.3V (改前 1.24V)"),
        ("pin76", "GPIO48 静态", "此时未驱动, 只看静态值"),
    ]),
    dict(cmd="gp 48h", title="GPIO48 推挽输出高", pins=[
        ("pin76", "BL_PWM / GPIO48", "期望 3.3V (改前 1.33V) => 背光可调光"),
    ]),
    dict(cmd="gp 46h", title="GPIO46 推挽输出高 (PA_EN)", pins=[
        ("pin74", "PA_EN -> MAX98357A SD_MODE", "期望 3.3V => 功放能开"),
    ]),
    dict(cmd="gp 47h", title="GPIO47 推挽输出高 (MPU_INT)", pins=[
        ("pin75", "MPU_INT", "期望 3.3V"),
    ]),
    dict(cmd="test sd", title="SD 卡自检 (只看串口回显)", pins=[]),
]

BAUD_DEFAULT = 115200


class Reader(threading.Thread):
    """后台读串口: 正常打印, quiet 时只缓存 (避免打断 input() 提示)"""

    def __init__(self, ser):
        super().__init__(daemon=True)
        self.ser = ser
        self.quiet = threading.Event()
        self.stop = threading.Event()
        self.echo = bytearray()

    def run(self):
        while not self.stop.is_set():
            try:
                n = self.ser.in_waiting
                data = self.ser.read(n) if n else b""
            except Exception:
                break
            if data:
                self.echo += data
                if not self.quiet.is_set():
                    sys.stdout.write(data.decode("utf-8", "replace"))
                    sys.stdout.flush()
            else:
                time.sleep(0.05)


def ask(prompt):
    try:
        return input(prompt).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return "q"


def main():
    ap = argparse.ArgumentParser(description="V1 板 GPIO 电平扫描")
    ap.add_argument("--port", default="COM23", help="板载 CH340 的口 (插拔后会变)")
    ap.add_argument("--baud", type=int, default=BAUD_DEFAULT)
    ap.add_argument("--start", type=int, default=1, help="从第几步开始 (续测用)")
    ap.add_argument("--stage", choices=["gp", "ldo"], default="gp",
                    help="gp=电平扫描(默认); ldo=拉高 LDO_VO4 后复测那几个脚")
    ap.add_argument("--list", action="store_true", help="只打印步骤表")
    ap.add_argument("--dry-run", action="store_true", help="发命令但不收读数")
    ap.add_argument("--out", default=None)
    ap.add_argument("--settle", type=float, default=1.2, help="发命令后等多久 (秒)")
    a = ap.parse_args()

    STEPS = STEPS_GP if a.stage == "gp" else STEPS_LDO
    if a.out is None:
        a.out = ("hardware/bringup-v1-measure.md" if a.stage == "gp"
                 else "hardware/bringup-v1-ldo.md")

    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stdin.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

    if a.list:
        print("步骤表 (共 %d 步):" % len(STEPS))
        for i, st in enumerate(STEPS, 1):
            print("  %d) %-28s %s" % (i, st["cmd"], st["title"]))
            for pin, desc, hint in st["pins"]:
                print("        量 %-6s %-32s (%s)" % (pin, desc, hint))
        print()
        print("跑法: python tools/gp_sweep.py --port COM23")
        return 0

    try:
        import serial
    except ImportError:
        print("缺 pyserial: .venv/Scripts/pip.exe install pyserial")
        return 2

    try:
        ser = serial.Serial(a.port, a.baud, timeout=0.1)
    except Exception as e:
        print("打不开 %s: %s" % (a.port, e))
        print("提示: python tools/ai_flash.py --list-ports  找 CH340 的口")
        return 2

    rd = Reader(ser)
    rd.start()

    print("=" * 72)
    print(" V1 GPIO 电平扫描   口=%s  波特率=%d" % (a.port, a.baud))
    print(" 万用表: 黑笔接 GND (模组 pin1/3/15/33/40/47/52/83)")
    print(" 控制台僵了就按 RST, 再用 --start N 续测")
    print("=" * 72)
    print("")
    print("[先看 6 秒启动日志, 确认板子活着、看清是哪版固件]")
    print("")
    time.sleep(6)

    results = []
    try:
        for i, st in enumerate(STEPS, 1):
            if i < a.start:
                continue
            print("")
            print("-" * 72)
            print("步骤 %d/%d : %s   [%s]" % (i, len(STEPS), st["title"], st["cmd"]))
            print("-" * 72)
            ans = ask("  回车 = 执行 / s = 跳过 / q = 结束: ")
            if ans.lower() == "q":
                break
            if ans.lower() == "s":
                results.append(dict(step=i, cmd=st["cmd"], title=st["title"], vals={}, note="(跳过)", ran=False))
                continue

            # 要量电压的步骤才静默 (免得刷屏); "看回显"的步骤必须保持实时打印,
            # 否则用户根本看不到 test 命令的输出 (2026-09-24 踩过)。
            if st["pins"]:
                rd.quiet.set()
            else:
                rd.quiet.clear()
            rd.echo = bytearray()
            ser.write((st["cmd"] + chr(10)).encode())
            ser.flush()
            time.sleep(a.settle)
            echo = rd.echo.decode("utf-8", "replace")
            if st["cmd"].split()[0] == "gp":
                if "mode=" not in echo:
                    print("  [!] 没看到命令回显 —— 控制台可能已僵死")
                    print("      处理: 按 RST -> 重跑并加 --start %d" % i)
                else:
                    print("  [ok] 命令已执行")
            elif not echo.strip():
                print("  [!] 没有任何回显 —— 控制台可能已僵死 (按 RST 后用 --start %d 续)" % i)

            vals = {}
            if not a.dry_run:
                if st["pins"]:
                    print("  现在量 (敲数字回车; 没条件量就空回车):")
                    for pin, desc, hint in st["pins"]:
                        v = ask("    %-6s %-32s = " % (pin, desc))
                        vals[pin] = v
                        if not v:
                            print("           (%s)" % hint)
                else:
                    print("  ^^^ 以上是完整回显 (不用量电压)")
                    ask("  看完按回车继续: ")
            results.append(dict(step=i, cmd=st["cmd"], title=st["title"], vals=vals, ran=True))
            rd.quiet.clear()
    finally:
        rd.stop.set()
        time.sleep(0.2)
        ser.close()

    L = []
    L.append("# V1 板 GPIO 电平实测记录")
    L.append("")
    L.append("> 由 tools/gp_sweep.py 生成 | 端口 %s @ %d" % (a.port, a.baud))
    L.append("> 目的: 定 pin66/pin76 身份、C6 三根线可用性、背光换脚候选")
    L.append("")
    L.append("| 步 | 命令 | 量点 | 读数 (V) | 说明 |")
    L.append("|---|---|---|---|---|")
    for r in results:
        if not r["vals"]:
            note = r.get("note") or ("(看回显)" if r.get("ran") else "(未记录)")
            L.append("| %d | %s | - | %s | %s |" % (r["step"], r["cmd"], note, r["title"]))
            continue
        first = True
        for pin, v in r["vals"].items():
            L.append("| %s | %s | %s | %s | %s |" % (
                r["step"] if first else "", r["cmd"] if first else "",
                pin, v or "(空)", r["title"] if first else ""))
            first = False
    L.append("")
    L.append("## 判读")
    L.append("")
    L.append("- 步 1/2: pin76 跟着 48h/48l 变 -> 手册没错(pin76=GPIO48); pin66 跟着变 -> 手册 66/76 写反了")
    L.append("- 步 3/4/5: pin57/58/59 约 3.3V -> 三根线可接 C6 当串口编程器; 约 1.2V -> 必须换脚")
    L.append("- 步 6/7/8: 谁约 3.3V 谁就能当背光 PWM (替代 GPIO48)")
    L.append("- 步 9: 与 NC 脚对比, 看有负载时会不会被拉低")
    L.append("")
    L.append("## 已知基准 (前测)")
    L.append("")
    L.append("| 点 | 读数 |")
    L.append("|---|---|")
    L.append("| pin64 UART0_TXD | 3.3V (推挽, 基准) |")
    L.append("| pin23 I2C_SDA | 3.3V |")
    L.append("| pin30 I2C_SCL | 3.3V |")
    L.append("| pin73 SD_CD | 3.3V |")
    L.append("| pin9 C6_IO9 | 约 3.3V |")
    L.append("| pin10 C6_IO8 | 约 3.3V |")
    L.append("| pin5 C6_U0TXD | 1.4V |")
    L.append("| pin66 ESP_LDO_VO4 | 1.24V |")
    L.append("| pin76 BL_PWM | 1.03V |")
    L.append("")

    out = Path(a.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(chr(10).join(L), encoding="utf-8")
    print("")
    print("=" * 72)
    print(" 记录已写入: %s" % out)
    print("=" * 72)
    return 0


if __name__ == "__main__":
    sys.exit(main())
