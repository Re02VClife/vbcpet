#!/usr/bin/env python3
"""网表自动审查 — 检测嘉立创 EDA 原理图 (.epro2) 的连接问题。

用法:
  python schematic_review.py <xx.epro2>

检查项:
  1. USB 外设口极性 (USB2 D-/D+ 是否对位)
  2. 悬空网络 (命名网络只连 1 个引脚)
  3. 关键元件连接完整性 (功放/麦/MPU/SD/舵机/电池/摄像头)

2026-08-05 改造: 关键元件按 part_id / 网络定位, **位号重排不影响**。
part_id 从 SCH_PAGE 文档读取 (PCB 文档不含 partId)。
"""
import json
import sys
import zipfile
from collections import defaultdict


def load_epru(path):
    if path.lower().endswith('.epro2'):
        with zipfile.ZipFile(path) as z:
            name = next(n for n in z.namelist() if n.endswith('.epru'))
            data = z.read(name).decode('utf-8', errors='replace')
    else:
        with open(path, encoding='utf-8', errors='replace') as f:
            data = f.read()
    recs = []
    for ln in data.split('\n'):
        if '||' not in ln:
            continue
        head, payload = ln.split('||', 1)
        try:
            recs.append((json.loads(head),
                         json.loads(payload.rstrip('|').rstrip('\r'))))
        except Exception:
            pass
    return recs


def segment(recs):
    docs, cur, cur_type = [], [], None
    for h, d in recs:
        if h.get('type') == 'DOCHEAD':
            if cur:
                docs.append((cur_type, cur))
            cur, cur_type = [], d.get('docType')
        else:
            cur.append((h, d))
    if cur:
        docs.append((cur_type, cur))
    return docs


def _comp_attr(recs):
    """从一段记录提取 COMPONENT + ATTR, 返回 (comp, attr)。"""
    comp, attr = {}, defaultdict(dict)
    for h, d in recs:
        t = h.get('type')
        if t == 'COMPONENT':
            comp[h['id']] = d
        elif t == 'ATTR':
            pid = d.get('parentId') or ''
            if pid and d.get('key') is not None:
                attr[pid][d['key']] = d.get('value')
    return comp, attr


def extract_pcb(docs):
    comp, attr, padnets = {}, defaultdict(dict), []
    for doc_type, recs in docs:
        if doc_type != 'PCB':
            continue
        c, a, = _comp_attr(recs)
        comp.update(c)
        for k, v in a.items():
            attr[k].update(v)
        for h, d in recs:
            if h.get('type') != 'PAD_NET':
                continue
            i = h.get('id')
            if isinstance(i, str):  # id 是 JSON 数组字符串
                try:
                    i = json.loads(i)
                except Exception:
                    i = None
            if isinstance(i, list) and len(i) > 2 and d.get('padNet'):
                padnets.append((i[1], i[2], d['padNet']))
    return comp, attr, padnets


def extract_sch(recs):
    """从 SCH_PAGE 提取 位号→part_id (part_id 重排后不变)。"""
    des2part = {}
    for doc_type, docrecs in recs:
        if doc_type != 'SCH_PAGE':
            continue
        comp, attr = _comp_attr(docrecs)
        for cid, d in comp.items():
            des = designator(attr.get(cid, {}))
            if des and d.get('partId'):
                des2part[des] = str(d['partId'])
    return des2part


def designator(attr_map):
    v = attr_map.get('Designator', '')
    return v if v and '?' not in v else None


def find_by_part(des2part, keyword):
    """按 part_id 子串定位元件 → {位号: part_id} (位号重排不影响)"""
    kw = keyword.lower()
    return {des: pid for des, pid in des2part.items() if kw in pid.lower()}


def check_part(label, des2part, pin_of, keyword, pins_need):
    """检查一类元件 (按 part_id) 的关键引脚是否都接上"""
    found = find_by_part(des2part, keyword)
    if not found:
        print(f"  {label:10s} ⚠️ 未找到 (part_id 不含 '{keyword}')")
        return
    for des in found:
        got = pin_of.get(des, {})
        missing = [p for p in pins_need if p not in got]
        print(f"  {label}{des:8s} 连接={len(got):2d} 缺失={missing if missing else '无'}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    recs = load_epru(path)
    docs = segment(recs)

    des2part = extract_sch(docs)
    comp, attr, padnets = extract_pcb(docs)

    desmap = {}
    for cid in comp:
        d = designator(attr.get(cid, {}))
        if d:
            desmap[cid] = d

    pin_of = defaultdict(dict)
    net_mem = defaultdict(list)
    for cid, pin, net in padnets:
        if not net:
            continue
        name = desmap.get(cid, cid)
        pin_of[name][pin] = net
        net_mem[net].append((name, pin))

    print(f"═══ 网表审查: {path} ═══")

    # 主控模组 = 引脚最多的元件 (P4C6-Core, 位号重排后仍正确)
    module = max(pin_of, key=lambda d: len(pin_of[d])) if pin_of else None

    # 1. USB 外设口 (先按 part_id 找 USB-A 母座, 再按网络兜底)
    print("\n[1] USB 外设口")
    usb_seat = None
    usb_by_part = find_by_part(des2part, 'qhh')          # QHHTZB USB-A 母座
    if usb_by_part:
        usb_seat = next(iter(usb_by_part))
    if not usb_seat:  # 兜底: 网络定位, 排除主控模组
        usb_seat = next((des for des, pins in pin_of.items()
                         if des != module
                         and 'USB_A_DP' in pins.values()
                         and 'USB_A_DM' in pins.values()), None)
    if usb_seat:
        u2 = pin_of[usb_seat]
        dm, dp = u2.get('2'), u2.get('3')
        ok = dm == 'USB_A_DM' and dp == 'USB_A_DP'
        print(f"  {usb_seat}: D-(pin2)={dm}  D+(pin3)={dp}  极性: {'✅' if ok else '❌ 反了'}")
        vbus = next((n for n in u2.values() if n.startswith(('+5V', '5V', '$1N'))), '?')
        print(f"  VBUS={vbus}  GND={'✅' if 'GND' in u2.values() else '❌'}")
    else:
        print("  ⚠️ 未找到 USB-A 外设口 (无 USB_A_DP/USB_A_DM 网络)")

    # 2. 悬空网络 (忽略正常保留的)
    print("\n[2] 悬空网络")
    ok_dangling = {'UART0_TXD', 'UART0_RXD', 'C6_U0RXD', 'C6_U0TXD',
                   'ESP_LDO_VO4', 'RTC_VBAT', 'CAM_PWDN', 'CAM_RSTB',
                   'CAM_MCLK', 'CHIP_PU', 'I2S_MCLK', 'STATUS_LED',
                   'LAN_OUT', 'IP_LED1', 'GPIO33'}  # 有意悬空: 模组自带天线座/电量灯/KEY
    issues = [(n, m) for n, m in net_mem.items()
              if not n.startswith('$1N') and len(m) == 1 and n not in ok_dangling]
    if issues:
        for n, m in issues:
            print(f"  ⚠️  {n:14s} ← {m}")
    else:
        print("  ✅ 无非预期悬空")

    # 3. 关键元件连接 (part_id 定位, 位号重排不影响)
    print("\n[3] 关键元件连接")
    check_part('功放', des2part, pin_of, 'MAX98357A',
               {'1', '2', '4', '7', '8', '9', '10', '14', '16'})
    check_part('麦', des2part, pin_of, 'ZTS6672', {'1', '4', '6'})
    check_part('六轴MPU', des2part, pin_of, 'MPU-6500', {'12', '23', '24'})
    check_part('SD卡', des2part, pin_of, 'sd.', {'1', '2', '3', '5', '7', '8'})
    check_part('电池IP5306', des2part, pin_of, 'IP5306', {'1', '6', '7', '8', '9'})

    # 摄像头座: 关键网络是否都接入 FPC
    print("  摄像头座:")
    for des in find_by_part(des2part, 'KH-FG'):
        nets = set(pin_of.get(des, {}).values())
        need = {'CSI_D0P', 'CSI_D0N', 'CSI_D1P', 'CSI_D1N', 'CSI_CLKP',
                'CSI_CLKN', 'CAM_MCLK', 'I2C_SDA', 'I2C_SCL',
                'CAM_RSTB', 'CAM_PWDN', '+2V8', '+1.5V'}
        missing = need - nets
        print(f"    {des}: 关键网络缺失={sorted(missing) if missing else '无'}")

    # 舵机座: 按 SERVO 网络反查 (排除主控模组)
    print("  舵机座:")
    for net, role in {'SERVO1_YAW': 'Yaw', 'SERVO2_PITCH': 'Pitch',
                      'SERVO3': '预留1', 'SERVO4': '预留2'}.items():
        members = [m for m in net_mem.get(net, []) if m[0] != module]
        if not members:
            print(f"    {role:6s} ⚠️ 未找到 (无 {net} 网络)")
            continue
        des = members[0][0]
        got = pin_of.get(des, {})
        missing = [p for p in {'2', '3'} if p not in got]
        print(f"    {role:6s}-{des:8s} 连接={len(got):2d} 缺失={missing if missing else '无'}")

    # 4. 未命名网络
    n_unnamed = sum(1 for n in net_mem if n.startswith('$1N'))
    print(f"\n[4] 未命名网络: {n_unnamed} 个 (多为 Buck/Boost 内部节点)")
    print("\n审查完成。")


if __name__ == '__main__':
    main()
