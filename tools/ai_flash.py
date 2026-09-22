#!/usr/bin/env python3
"""ai_flash.py —— 给 AI/无人值守用的 ESP32-P4 烧录 + 启动验证一条龙。

为什么需要它:
  * 端口号会漂 (COM16/COM22/COM8/COM2 换来换去) → 按 **VID:PID** 认口
  * 烧录前必须先清掉占口的残留进程 (streamer/monitor/powershell watcher)
  * 烧完要看启动日志才算成功, 而不是只看 esptool 的 hash verified
  * AI/CI 需要 **一条命令 + JSON 结果**, 而不是一堆交互式输出

用法:
  python tools/ai_flash.py --list-ports                  # 看口
  python tools/ai_flash.py                               # 编译产物直接烧 (默认动作)
  python tools/ai_flash.py --build                       # 先 idf.py build 再烧
  python tools/ai_flash.py --monitor 12 --selftest       # 烧完抓 12s 日志并下发 test all
  python tools/ai_flash.py --info --json                 # 只读芯片信息, JSON 输出
  python tools/ai_flash.py --dry-run                     # 只打印将执行的命令

USJ (USB-Serial-JTAG, GPIO24/25, VID:PID=303A:1001) 上 esptool 可软件复位进下载模式,
所以 V2 板一根 USB-C 即可全自动; V1 板 (USB-C 走 native OTG pin48/49) 仍需 strap 兜底。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FIRMWARE = REPO / "firmware"

ESP_VID = 0x303A
USJ_PID = 0x1001          # ESP32-P4 USB-Serial-JTAG
USB_OTG_PID = 0x4001      # ESP32-P4 USB-OTG (native, pin48/49)
STALE_PAT = (r"stream_final|serial_bridge|serial_stream|esp-idf-monitor|miniterm"
             r"|flash-v1|flash_osptek|sync_flash3")
CRASH_MARKERS = ("Guru Meditation", "StoreProhibited", "LoadProhibited", "IllegalInstruction",
                 "abort() was called", "assert failed", "task_wdt:", "panic'ed", "rst:0xc")
BOOT_OK_MARKERS = ("boot:0x", "SPI_FAST_FLASH_BOOT", "cpu_start", "app_main")

# ESP32-P4 专有烧录偏移 (bootloader 不在 0x1000!)
P4_DEFAULT_ARGS = [(0x2000, "bootloader/bootloader.bin"),
                   (0x8000, "partition_table/partition-table.bin"),
                   (0x10000, "vbcpet.bin")]


# ─────────────────────────── 基础工具 ────────────────────────────
def log(msg, quiet=False):
    if not quiet:
        print(msg, flush=True)


def run(cmd, cwd=None, quiet=False, env=None):
    log("$ " + " ".join(str(c) for c in cmd), quiet)
    p = subprocess.run([str(c) for c in cmd], cwd=str(cwd) if cwd else None,
                       capture_output=True, text=True, errors="replace", env=env)
    if p.stdout:
        log(p.stdout.rstrip(), quiet)
    if p.returncode != 0 and p.stderr:
        log(p.stderr.rstrip(), quiet)
    return p


def list_ports():
    from serial.tools import list_ports as lp
    out = []
    for p in lp.comports():
        out.append({"device": p.device, "vid": p.vid, "pid": p.pid,
                    "description": p.description, "serial": p.serial_number,
                    "esp": p.vid == ESP_VID,
                    "usj": (p.vid == ESP_VID and p.pid == USJ_PID)})
    return out


def pick_port(ports, want="auto"):
    if want and want.lower() != "auto":
        return want, None
    usj = [p for p in ports if p["usj"]]
    if usj:
        return usj[0]["device"], f"VID:PID={ESP_VID:04X}:{USJ_PID:04X} (USB-Serial-JTAG / USJ)"
    esp = [p for p in ports if p["esp"]]
    if esp:
        return esp[0]["device"], f"VID:PID={esp[0]['vid']:04X}:{esp[0]['pid']:04X} (Espressif)"
    return None, None


def port_busy(port):
    try:
        import serial
    except ImportError:
        return False, "pyserial 未安装"
    try:
        s = serial.Serial(port, 115200, timeout=0.2)
        s.close()
        return False, ""
    except Exception as e:  # noqa: BLE001
        return True, str(e)


def kill_stale(quiet=False):
    ps = ("Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match '%s' } | "
          "Select-Object ProcessId,Name,CommandLine | ConvertTo-Json -Compress" % STALE_PAT)
    p = run(["powershell", "-NoProfile", "-Command", ps], quiet=quiet)
    killed = []
    try:
        data = json.loads(p.stdout or "[]")
        if isinstance(data, dict):
            data = [data]
    except Exception:  # noqa: BLE001
        data = []
    for d in data or []:
        if d.get("ProcessId") == os.getpid():
            continue
        run(["taskkill", "/F", "/PID", str(d["ProcessId"])], quiet=True)
        killed.append({"pid": d.get("ProcessId"), "name": d.get("Name")})
    return killed


def find_esptool(override=None):
    """→ (python_or_exe, prefix_args)"""
    if override:
        return override, []
    cands = []
    env_py = r"D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
    idf_wrapper = FIRMWARE / "_idf.py"
    if idf_wrapper.exists():                      # 从项目包装器里抠出真正的 IDF python env
        m = re.search(r'IDF_PYTHON_ENV_PATH\s*=\s*r?"([^"]+)"', idf_wrapper.read_text(errors="replace"))
        if m:
            cands.append(Path(m.group(1)) / "Scripts" / "python.exe")
    if os.environ.get("IDF_PYTHON_ENV_PATH"):
        cands.append(Path(os.environ["IDF_PYTHON_ENV_PATH"]) / "Scripts" / "python.exe")
    cands.append(Path(env_py))
    cands += sorted(Path(r"D:\Espressif\python_env").glob("*/Scripts/python.exe")) \
        if Path(r"D:\Espressif\python_env").exists() else []
    cands.append(Path(sys.executable))
    for py in cands:
        if py.exists():
            try:
                r = subprocess.run([str(py), "-c", "import esptool,sys;print(esptool.__version__)"],
                                   capture_output=True, text=True, timeout=60)
                if r.returncode == 0:
                    return str(py), ["-m", "esptool"]
            except Exception:  # noqa: BLE001
                continue
    exe = None
    for name in ("esptool", "esptool.py", "esptool.exe"):
        from shutil import which
        exe = which(name)
        if exe:
            return exe, []
    raise SystemExit("找不到 esptool —— 用 --esptool 指定路径 (或用 IDF 的 python_env)")


def build(bin_hint=None, quiet=False):
    if bin_hint:
        return 0
    wrapper = FIRMWARE / "_idf.py"
    if not wrapper.exists():
        raise SystemExit("找不到 firmware/_idf.py, 无法编译 (可加 --bin 直接指定镜像)")
    return run([sys.executable, str(wrapper), "build"], quiet=quiet).returncode


def flash_args(build_dir, app_bin):
    """优先用 idf.py 生成的 flash_args, 否则回退 P4 偏移表"""
    fa = Path(build_dir) / "flash_args"
    if fa.exists():
        return ["@" + str(fa)], fa.read_text(errors="replace").strip()
    args, desc = [], []
    for off, rel in P4_DEFAULT_ARGS:
        p = Path(build_dir) / rel
        if rel == "vbcpet.bin" and app_bin:
            p = Path(app_bin)
        if p.exists():
            args += [hex(off), str(p)]
            desc.append(f"{hex(off)} {p}")
    if not args:
        raise SystemExit(f"{build_dir} 下找不到可烧镜像 (先 --build 或用 --bin 指定)")
    return args, "\n".join(desc)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def monitor(port, seconds, out_path, selftest=False, quiet=False):
    """抓启动日志 → (text, analysis)"""
    import serial
    text, marks = "", []
    try:
        s = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:  # noqa: BLE001
        return "", {"error": f"打不开日志口 {port}: {e}"}
    t0 = time.time()
    sent = False
    try:
        while time.time() - t0 < seconds:
            chunk = s.read(4096)
            if chunk:
                text += chunk.decode("utf-8", errors="replace")
            if selftest and not sent and time.time() - t0 > 3:
                s.write(b"test all\n")
                s.flush()
                sent = True
    finally:
        s.close()
    Path(out_path).write_text(text, encoding="utf-8", errors="replace")
    crashes = [m for m in CRASH_MARKERS if m in text]
    boot_ok = any(m in text for m in BOOT_OK_MARKERS)
    marks = [ln.strip() for ln in text.splitlines()
             if any(k in ln for k in BOOT_OK_MARKERS + ("psram", "PSRAM", "PASS", "FAIL"))]
    st = {"boot_ok": boot_ok, "crashes": crashes, "markers": marks[:40],
          "pass": text.count("PASS"), "fail": text.count("FAIL")}
    log(f"[monitor] {port} {seconds}s → {out_path}  boot_ok={boot_ok} crashes={crashes or '无'}", quiet)
    return text, st


# ─────────────────────────── 主流程 ────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="ESP32-P4 AI 烧录一条龙")
    ap.add_argument("--list-ports", action="store_true", help="列出串口 + VID:PID")
    ap.add_argument("--info", action="store_true", help="只读芯片信息 (不烧)")
    ap.add_argument("--build", action="store_true", help="先 idf.py build")
    ap.add_argument("--port", default="auto", help="auto | COMx")
    ap.add_argument("--baud", default="460800")
    ap.add_argument("--before", default=None, choices=["auto", "default_reset", "usb_reset", "no_reset"],
                    help="复位策略; 默认 auto(Espressif 口用 usb_reset, 其它用 default_reset)")
    ap.add_argument("--bin", default=None, help="app 镜像 (默认 build/vbcpet.bin)")
    ap.add_argument("--build-dir", default=str(FIRMWARE / "build"))
    ap.add_argument("--monitor", type=float, default=10.0, help="烧完抓日志秒数 (0=不抓)")
    ap.add_argument("--monitor-port", default="auto", help="auto|COMx|none 日志口 (V1 控制台在 UART0 CH340 上)")
    ap.add_argument("--selftest", action="store_true", help="日志期间下发 'test all'")
    ap.add_argument("--log-path", default=str(REPO / "_ai_flash_log.txt"))
    ap.add_argument("--erase", action="store_true", help="先全片擦除")
    ap.add_argument("--kill-stale", action="store_true", help="烧前杀掉占口残留进程")
    ap.add_argument("--stub", action="store_true", help="允许 RAM stub (默认 --no-stub, V1 踩过坑)")
    ap.add_argument("--esptool", default=None, help="esptool 可执行/python 路径")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--json", action="store_true", help="输出 JSON (给 AI/CI)")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:  # noqa: BLE001
        pass
    res = {"ok": False, "action": None, "port": None, "chip": None,
           "flash": {}, "boot": {}, "selftest": {}, "errors": [], "log": a.log_path}

    ports = list_ports()
    if a.list_ports:
        for p in ports:
            tag = "  ← USJ(可自动烧录)" if p["usj"] else ("  ← Espressif" if p["esp"] else "")
            log(f"{p['device']:>8}  VID:PID={p['vid'] and '%04X' % p['vid'] or '----'}:"
                f"{p['pid'] and '%04X' % p['pid'] or '----'}  {p['description']}{tag}", a.quiet)
        if a.json:
            print(json.dumps({"ok": True, "action": "list-ports", "ports": ports}, ensure_ascii=False))
        return 0

    port, why = pick_port(ports, a.port)
    res["port"] = port
    if not port:
        msg = ("未找到 Espressif 设备 (VID 303A)。请检查: ① V2 板 USB-C 是否插好 "
               "② 是否插在 USJ 口 (GPIO24/25) 而不是 USB-A 外设口 ③ 用 --port COMx 手动指定")
        res["errors"].append(msg)
        log("🔴 " + msg, a.quiet)
        for p in ports:
            log(f"    候选: {p['device']} {p['description']}", a.quiet)
        print(json.dumps(res, ensure_ascii=False)) if a.json else None
        return 2
    log(f"[port] {port}  (识别依据: {why or '手动指定'})", a.quiet)

    if a.kill_stale:
        killed = kill_stale(a.quiet)
        log(f"[port] 清理残留进程: {killed or '无'}", a.quiet)
    busy, err = port_busy(port)
    if busy:
        res["errors"].append(f"{port} 被占用: {err} (试 --kill-stale, 或先关掉 monitor/streamer)")
        log("🟡 " + res["errors"][-1], a.quiet)

    es_py, es_pre = find_esptool(a.esptool)
    before = a.before
    if not before or before == "auto":
        before = "usb_reset" if any(p["device"] == port and p["esp"] for p in ports) else "default_reset"
    base = [es_py] + es_pre + ["--chip", "esp32p4", "--port", port, "--baud", a.baud, "--before", before]
    if not a.stub:
        base.append("--no-stub")

    # ── 只读信息 ──
    if a.info:
        res["action"] = "info"
        p = run(base + ["chip_id"], quiet=a.quiet)
        res["ok"] = p.returncode == 0
        res["chip"] = (p.stdout + p.stderr).strip()[-800:]
        if a.json:
            print(json.dumps(res, ensure_ascii=False))
        return 0 if res["ok"] else 1

    # ── 编译 ──
    if a.build:
        rc = build(a.bin, a.quiet)
        if rc != 0:
            res["errors"].append(f"idf.py build 失败 (rc={rc})")
            log("🔴 " + res["errors"][-1], a.quiet)
            if a.json:
                print(json.dumps(res, ensure_ascii=False))
            return 1

    extra, desc = flash_args(a.build_dir, a.bin)
    res["flash"]["args"] = desc

    cmds = []
    if a.erase:
        cmds.append(base + ["erase_flash"])
    cmds.append(base + ["write_flash", "--verify"] + ([] if a.stub else []) + extra)
    res["action"] = "flash"
    if a.dry_run:
        for c in cmds:
            log("$ " + " ".join(map(str, c)), a.quiet)
        res["ok"] = True
        res["dry_run"] = True
        if a.json:
            print(json.dumps(res, ensure_ascii=False))
        return 0

    for c in cmds:
        # cwd=build_dir: flash_args 里是相对路径 (bootloader/bootloader.bin), 必须在 build 目录下执行
        p = run(c, cwd=a.build_dir, quiet=a.quiet)
        if p.returncode != 0:
            res["errors"].append((p.stdout + p.stderr).strip()[-1200:])
            log("🔴 烧录失败", a.quiet)
            if a.json:
                print(json.dumps(res, ensure_ascii=False))
            return 1
    app = Path(a.bin) if a.bin else Path(a.build_dir) / "vbcpet.bin"
    res["flash"]["app"] = str(app)
    res["flash"]["sha256"] = sha256(app) if app.exists() else None
    res["flash"]["verified"] = True

    # ── 抓启动日志 ──
    if a.monitor > 0 and a.monitor_port != "none":
        mport, _ = (port, "") if a.monitor_port == "auto" else (a.monitor_port, "手动指定")
        _, st = monitor(mport, a.monitor, a.log_path, a.selftest, a.quiet)
        res["boot"] = st
        if a.selftest:
            res["selftest"] = {"pass": st.get("pass", 0), "fail": st.get("fail", 0)}

    res["ok"] = (not res["errors"]) and (res["boot"].get("boot_ok", True) if res["boot"] else True) \
        and not res["boot"].get("crashes")
    if not res["ok"] and not res["errors"] and res["boot"].get("crashes"):
        res["errors"].append("启动日志发现崩溃标记: " + ", ".join(res["boot"]["crashes"]))
    log(("✅ 烧录+启动验证通过" if res["ok"] else "🔴 有问题, 见上面/JSON"), a.quiet)
    if a.json:
        print(json.dumps(res, ensure_ascii=False))
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
