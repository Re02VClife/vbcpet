#!/usr/bin/env python3
"""网表自动审查 — 检测嘉立创 EDA 原理图 (.epro2) 的连接问题。

用法:
  python schematic_review.py <xx.epro2>

检查项:
  1. USB 外设口极性 (USB2 D-/D+ 是否对位)
  2. 悬空网络 (命名网络只连 1 个引脚)
  3. 关键元件连接完整性 (功放/麦/MPU/SD/舵机)
  4. 未命名网络统计
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


def extract_pcb(docs):
    comp, attr, padnets = {}, defaultdict(dict), []
    for doc_type, recs in docs:
        if doc_type != 'PCB':
            continue
        for h, d in recs:
            t = h.get('type')
            if t == 'COMPONENT':
                comp[h['id']] = d
            elif t == 'ATTR':
                pid = d.get('parentId') or ''
                if pid and d.get('key') is not None:
                    attr[pid][d['key']] = d.get('value')
            elif t == 'PAD_NET':
                i = h.get('id')
                if isinstance(i, str):  # id 是 JSON 数组字符串
                    try:
                        i = json.loads(i)
                    except Exception:
                        i = None
                if isinstance(i, list) and len(i) > 2 and d.get('padNet'):
                    padnets.append((i[1], i[2], d['padNet']))
    return comp, attr, padnets


def designator(attr_map):
    v = attr_map.get('Designator', '')
    return v if v and '?' not in v else None


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    recs = load_epru(path)
    docs = segment(recs)
    comp, attr, padnets = extract_pcb(docs)

    desmap = {}
    for cid in comp:
        d = designator(attr.get(cid, {}))
        if d:
            desmap[cid] = d
    rev = {v: k for k, v in desmap.items()}

    # 元件引脚 -> 网络
    pin_of = defaultdict(dict)
    net_mem = defaultdict(list)
    for cid, pin, net in padnets:
        if not net:
            continue
        name = desmap.get(cid, cid)
        pin_of[name][pin] = net
        net_mem[net].append((name, pin))

    print(f"═══ 网表审查: {path} ═══")

    # 1. USB 外设口极性
    print("\n[1] USB 外设口")
    u2 = pin_of.get('USB2', {})
    dm, dp = u2.get('2'), u2.get('3')
    ok = dm == 'USB_A_DM' and dp == 'USB_A_DP'
    print(f"  D-(pin2)={dm}  D+(pin3)={dp}  极性: {'✅' if ok else '❌ 反了'}")
    print(f"  VBUS={u2.get('1')}  GND={u2.get('4')}")

    # 2. 悬空网络 (忽略正常保留的)
    print("\n[2] 悬空网络")
    ok_dangling = {'UART0_TXD', 'UART0_RXD', 'C6_U0RXD', 'C6_U0TXD',
                   'ESP_LDO_VO4', 'RTC_VBAT', 'CAM_PWDN', 'CAM_RSTB',
                   'CAM_MCLK', 'CHIP_PU', 'I2S_MCLK', 'STATUS_LED'}
    issues = [(n, m) for n, m in net_mem.items()
              if not n.startswith('$1N') and len(m) == 1 and n not in ok_dangling]
    if issues:
        for n, m in issues:
            print(f"  ⚠️  {n:14s} ← {m}")
    else:
        print("  ✅ 无非预期悬空")

    # 3. 关键元件连接
    print("\n[3] 关键元件连接")
    checks = {
        '功放U12':   ('U12', {'1', '2', '4', '7', '8', '9', '10', '14', '16'}),
        '麦U3':      ('U3', {'1', '4', '6'}),
        '麦U5':      ('U5', {'1', '4', '6'}),
        'MPU-U7':    ('U7', {'12', '23', '24'}),
        'SD-TF1':    ('TF1', {'1', '2', '3', '5', '7', '8'}),
        '舵机Yaw-U8':   ('U8', {'2', '3'}),
        '舵机Pitch-U10': ('U10', {'2', '3'}),
        '舵机预留-U9':   ('U9', {'2', '3'}),
        '舵机预留-U11':  ('U11', {'2', '3'}),
    }
    for label, (des, pins) in checks.items():
        got = pin_of.get(des, {})
        missing = [p for p in pins if p not in got]
        print(f"  {label:14s} 连接={len(got):2d} 缺失={missing if missing else '无'}")

    # 4. 未命名网络
    n_unnamed = sum(1 for n in net_mem if n.startswith('$1N'))
    print(f"\n[4] 未命名网络: {n_unnamed} 个 (多为 Buck/Boost 内部节点)")
    print("\n审查完成。")


if __name__ == '__main__':
    main()
