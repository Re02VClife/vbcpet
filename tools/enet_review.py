#!/usr/bin/env python3
"""嘉立创 EDA 网表 (.enet) 自动复核 —— 聚焦"烧录/调试通道 + 电源保护"。

用法:
  python tools/enet_review.py JLC/原理图_2026-09-15.enet
  python tools/enet_review.py JLC/原理图_2026-09-15.enet --pcb JLC/Netlist_PCB1_2026-09-15.enet

检查项 (板级约定见 board_config.h / hardware/final-pinmap.md):
  C1 USJ(GPIO24/25) 是否引到板载 USB-C          —— 决定能否免 strap 自动烧录
  C2 USB-C CC1/CC2 是否 5.1k 下拉                —— 决定 PC 是否供电/识别
  C3 USJ D+/D- 是否有 ESD 保护
  C4 GPIO35/36 strapping 是否有确定电平/上拉     —— 兜底下载通道
  C5 GPIO24/25 是否被其它外设(舵机)抢占
  C6 主 5V 路径是否有真保险 (PPTC) + TVS         —— 防再烧主控
  C7 悬空网络 / 单脚网络 / 模组 USB2 OTG 是否 NC
退出码: 0 = 无 🔴, 1 = 有 🔴。
"""
import argparse
import collections
import json
import sys

# ── 板级约定 ────────────────────────────────────────────────────────────
USJ_PINS = {"50": "GPIO24(USJ_D-)", "51": "GPIO25(USJ_D+)"}
STRAP_PINS = {"62": "GPIO35(启动模式)", "63": "GPIO36(下载模式选择)",
              "61": "GPIO34(JTAG源)", "64": "GPIO37", "65": "GPIO38"}
NATIVE_USB_PINS = {"48": "USB2_OTG_DM", "49": "USB2_OTG_DP"}
ESD_HINT = ("USBLC6", "ESD", "TVS", "PTVS", "SRV05", "USBLC")
PPTC_HINT = ("SMD", "PTC", "MF-", "JK", "PPTC")

RED, WARN, OK, INFO = "\U0001f534", "\U0001f7e1", "\u2705", "\u2139\ufe0f"


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def build(data):
    """→ comps[desig] = {name, fp, pins{pin: (name, net)}}, net2pins[net] = [(desig, pin, pinname)]"""
    comps, net2pins = {}, collections.defaultdict(list)
    for cid, c in data.get("components", {}).items():
        p = c.get("props", {})
        ds = p.get("Designator", cid)
        comps[ds] = {"name": p.get("DeviceName") or p.get("Name", ""),
                     "fp": p.get("FootprintName") or p.get("Footprint", ""),
                     "pins": {}}
        for pn, pi in (c.get("pinInfoMap") or {}).items():
            net = pi.get("net", "")
            comps[ds]["pins"][pn] = (pi.get("name", ""), net)
            if net:
                net2pins[net].append((ds, pn, pi.get("name", "")))
    return comps, net2pins


def find_m1(comps):
    for ds, c in comps.items():
        if len(c["pins"]) > 60 and "P4" in (c["name"] + ds).upper():
            return ds
    return max(comps, key=lambda d: len(comps[d]["pins"]))


def pin_net(comps, ds, pn):
    return comps.get(ds, {}).get("pins", {}).get(pn, ("", ""))[1]


def others(net2pins, net, exclude):
    return [(d, pn, nm) for d, pn, nm in net2pins.get(net, []) if d not in exclude]


def guess_value(name):
    """从立创器件名猜阻值/容值 (启发式, 仅供人读)"""
    n = name.upper()
    table = [("5K10", "5.1k"), ("5K1", "5.1k"), ("4701", "4.7k"), ("4K7", "4.7k"),
             ("103", "10k"), ("1002", "10k"), ("4022", "40.2k"), ("1272", "12.7k"),
             ("0200", "20m"), ("15R", "15"), ("3309", "33"), ("1R0", "1uH")]
    for k, v in table:
        if k in n:
            return v
    return ""


def review(sch_path, pcb_path=None):
    out, red, warn = [], 0, 0

    def line(icon, msg):
        nonlocal red, warn
        if icon == RED:
            red += 1
        elif icon == WARN:
            warn += 1
        out.append(f"{icon} {msg}")

    comps, net2pins = build(load(sch_path))
    m1 = find_m1(comps)
    line(INFO, f"原理图: {sch_path}  元件 {len(comps)} 个 / 网络 {len(net2pins)} 个; 主控位号 {m1}")

    if pcb_path:
        pc, pn2 = build(load(pcb_path))
        pm = find_m1(pc)
        diff = 0
        for net in set(net2pins) | set(pn2):
            a = sorted((d, p) for d, p, _ in net2pins.get(net, []))
            b = sorted((d, p) for d, p, _ in pn2.get(net, []))
            if a != b:
                diff += 1
                out.append(f"   [net diff] {net}\n       SCH {a}\n       PCB {b}")
        line(OK if diff == 0 else RED,
             f"PCB 网表一致性: {'完全一致' if diff == 0 else str(diff) + ' 个网络不一致 (PCB 未同步!)'}")

    # ── C1 USJ → USB-C ────────────────────────────────────────────────
    usb_c = None
    for ds, c in comps.items():
        if "TYPE-C" in c["name"].upper() or "USB-C" in c["fp"].upper():
            usb_c = ds
            break
    if not usb_c:
        line(RED, "C1 未找到板载 USB-C 座 —— 无法一根线即插即用")
    else:
        d_nets = {}
        for pn, (nm, net) in comps[usb_c]["pins"].items():
            if nm in ("DN1", "DN2", "DP1", "DP2", "D-", "D+"):
                d_nets.setdefault(net, []).append(nm)
        usj_ok = False
        for pin, label in USJ_PINS.items():
            net = pin_net(comps, m1, pin)
            hit = net in d_nets
            usj_ok |= hit
            line(OK if hit else WARN,
                 f"C1 {label} (模组 pin{pin}) 网络={net or '(未连接)'} "
                 f"{'→ 已接 ' + usb_c if hit else '→ 未接到 USB-C'}")
            if net and not hit:
                peer = others(net2pins, net, {m1})
                line(INFO, f"     该网络的其它连接: {peer or '无'}")
        # 舵机抢占
        for pin, label in USJ_PINS.items():
            net = pin_net(comps, m1, pin)
            for d, p, nm in others(net2pins, net, {m1}):
                if d in usb_c:
                    continue
                if any(x in ("SG90", "SERVO") for x in (comps[d]["name"].upper(),)):
                    line(RED, f"C5 {label} 被舵机 {d} 占用 —— 必须让位 (V2: GPIO28/29)")
                else:
                    line(WARN, f"C5 {label} 还挂在 {d}({comps[d]['name']}) 上 —— 确认不与 USJ 抢焊盘")
        # CC 下拉
        for ccpin, ccnet in [(p, n) for p, (nm, n) in comps[usb_c]["pins"].items() if nm in ("CC1", "CC2")]:
            peers = [d for d, _, _ in net2pins.get(ccnet, []) if d != usb_c]
            if not peers:
                line(WARN, f"C2 {ccnet} ({ccpin}) 未接 5.1k 下拉 —— PC 可能不给电")
            for d in peers:
                to_gnd = any(n2 == "GND" for _, n2 in comps[d]["pins"].values())
                val = guess_value(comps[d]["name"])
                good = to_gnd and val == "5.1k"
                line(OK if good else WARN,
                     f"C2 {ccnet} ({ccpin}) → {d} {comps[d]['name']} {val or '?'}"
                     + (" 到 GND ✅" if good else " (应为 5.1k 到 GND)"))
        # ESD on USJ lines
        for pin, label in USJ_PINS.items():
            net = pin_net(comps, m1, pin)
            if net in d_nets:
                prot = [(d, comps[d]["name"]) for d, _, _ in others(net2pins, net, {m1, usb_c})]
                if any(any(h in nm.upper() for h in ESD_HINT) for _, nm in prot):
                    line(OK, f"C3 {label} 有 ESD/保护器件: {prot}")
                else:
                    line(WARN, f"C3 {label} 直连 USB-C, 无 ESD 保护 (建议 USBLC6/ESD 阵列; "
                               f"参考 WT9932P4-TINY 的 D1/D2/D3)。当前挂载: {prot or '无'}")
        # VBUS
        vb = [n for p, (nm, n) in comps[usb_c]["pins"].items() if nm == "VBUS"]
        if vb:
            line(INFO, f"    USB-C VBUS 网络 = {vb[0]} → {others(net2pins, vb[0], {usb_c})}")

    # ── C4 strapping ──────────────────────────────────────────────────
    for pin, label in STRAP_PINS.items():
        net = pin_net(comps, m1, pin)
        if not net:
            line(INFO, f"C4 模组 pin{pin} {label}: 未连接")
            continue
        peers = others(net2pins, net, {m1})
        pull = []
        for d, pn, nm in peers:
            c = comps[d]
            gnd = any(n2 == "GND" for _, n2 in c["pins"].values())
            v33 = any(n2 == "3V3" for _, n2 in c["pins"].values())
            kind = "下拉GND" if gnd else ("上拉3V3" if v33 else "开关/其它")
            pull.append(f"{d}({c['name']},{guess_value(c['name']) or '?'},{kind})")
        line(WARN if not pull else INFO,
             f"C4 模组 pin{pin} {label} 网络={net} → {pull or '悬空'}")

    # ── C6 电源保护 ───────────────────────────────────────────────────
    for net in ("3V3", "+5V", "VBUS_IN"):
        tvs = [d for d, _, _ in net2pins.get(net, [])
               if any(h in comps[d]["name"].upper() for h in ("PTVS", "TVS", "SMBJ", "SMF"))]
        line((OK if tvs else INFO), f"C6 {net} 上的 TVS/钳位: {tvs or '无'}")
    for net in ("+5V", "VBUS_IN"):
        CAP = ("FCC", "CL10", "CL05", "YBK", "GRM", "CC0")
        fuses = [d for d, _, _ in net2pins.get(net, [])
                 if any(h in comps[d]["name"].upper() for h in PPTC_HINT)
                 and not comps[d]["name"].upper().startswith(CAP)]
        line((OK if fuses else WARN),
             f"C6 {net} 上的 PPTC/保险: {[(d, comps[d]['name']) for d in fuses] or '无 (禁 0Ω 跳线)'}")
    # USB-A 口保护 + D1 pin5
    for d, c in comps.items():
        if "USBLC6" in c["name"].upper():
            p5 = c["pins"].get("5", ("", ""))[1]
            line(OK if p5 == "+5V" else WARN,
                 f"C6 {d}(USBLC6) pin5 = {p5 or '(悬空)'} (应为 +5V, 勿 VBUS_IN —— 电池模式会钳死)")

    # ── C7 悬空 / 单脚网络 ────────────────────────────────────────────
    for pin, label in NATIVE_USB_PINS.items():
        net = pin_net(comps, m1, pin)
        line(INFO, f"C7 模组 pin{pin} {label}: 网络={net or '(未连接/NC)'} "
                   f"{'← 单脚悬空网络, 建议标 NC' if net and len(net2pins[net]) == 1 else ''}")
    singles = sorted(n for n, v in net2pins.items() if len(v) == 1 and not n.startswith("$1N"))
    if singles:
        for n in singles:
            line(WARN, f"C7 '{n}' 只有一个连接点: {net2pins[n]}")
    unknowns = sorted(n for n in net2pins if n.startswith("$1N") and len(net2pins[n]) > 1)
    line(INFO, f"C7 自动命名网络 (未起netlabel) 共 {len(unknowns)} 个: {unknowns}")

    print("\n".join(out))
    tail = f"\U0001f534 {red} 项必改 / \U0001f7e1 {warn} 项建议" if (red or warn) else "\u2705 全部通过"
    print(f"\n== 汇总: {tail} ==")
    return 1 if red else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("enet", help="原理图导出的 .enet")
    ap.add_argument("--pcb", help="PCB 导出的 .enet, 用于一致性比对")
    a = ap.parse_args()
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass
    sys.exit(review(a.enet, a.pcb))


if __name__ == "__main__":
    main()
