#!/usr/bin/env python3
"""解析嘉立创 EDA 本地工程 (.epro2) 的原理图数据,输出快照/报告。

格式: .epro2 = zip,内含 .epru (每行 `头JSON||数据JSON|`)
原理图元件在 SCH_PAGE 文档,引脚→网络连接在 PCB 文档 (PAD_NET)。

用法:
  python schematic_snapshot.py <xx.epro2>           # 打印元件清单 + 引脚网络
  python schematic_snapshot.py <xx.epro2> --json    # 输出机器可读 JSON 快照
"""
import json
import sys
import zipfile
from collections import defaultdict


def load_epru(path):
    """读取 .epro2 (zip) 或裸 .epru,返回 (head, data) 记录列表"""
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
    """按 DOCHEAD 分段,返回 [(docType, [recs...])]"""
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


def extract_doc(docs, want_doc):
    """提取指定文档类型的 COMPONENT/ATTR/PAD_NET 记录"""
    comp, attr, padnets = {}, defaultdict(dict), []
    for doc_type, recs in docs:
        if doc_type != want_doc:
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
                if isinstance(i, str):  # id 是 JSON 数组字符串,如 ["PAD_NET","<comp>","<pin>","<pinid>"]
                    try:
                        i = json.loads(i)
                    except Exception:
                        i = None
                if isinstance(i, list) and len(i) > 2 and d.get('padNet'):
                    padnets.append((i[1], i[2], d['padNet']))
    return comp, attr, padnets


def designator_of(attr_map):
    v = attr_map.get('Designator', '')
    return v if v and '?' not in v else None


def build_snapshot(path):
    recs = load_epru(path)
    docs = segment(recs)

    sch_comp, sch_attr, _ = extract_doc(docs, 'SCH_PAGE')
    pcb_comp, pcb_attr, pcb_nets = extract_doc(docs, 'PCB')

    # 原理图元件清单 (位号, 位置, 器件 id)
    sch = {}
    for cid, d in sch_comp.items():
        des = designator_of(sch_attr.get(cid, {}))
        if not des:
            continue
        sch[des] = {
            'part_id': d.get('partId'),
            'x': round(d.get('x', 0), 1),
            'y': round(d.get('y', 0), 1),
        }

    # PCB 引脚网络: 元件位号 -> {pin: net}
    pcb = {}
    for cid, pin, net in pcb_nets:
        des = designator_of(pcb_attr.get(cid, {}))
        if not des:
            des = cid  # 找不到位号就退回原始 id
        pcb.setdefault(des, {})[pin] = net

    # 网络名集合
    nets = sorted({net for _, _, net in pcb_nets if net})

    return {'sch_components': sch, 'pcb_pin_nets': pcb, 'nets': nets}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    path = sys.argv[1]
    snap = build_snapshot(path)

    if '--json' in sys.argv:
        print(json.dumps(snap, ensure_ascii=False, indent=1))
        return

    print(f"=== {path} ===")
    print(f"\n【原理图元件】 {len(snap['sch_components'])} 个")
    for des, info in sorted(snap['sch_components'].items(),
                            key=lambda kv: (len(kv[0]), kv[0])):
        print(f"  {des:12s} part={str(info['part_id'])[:28]:28s} @({info['x']},{info['y']})")

    print(f"\n【PCB 引脚网络】 {len(snap['pcb_pin_nets'])} 个元件")
    for des in sorted(snap['pcb_pin_nets'].keys(),
                      key=lambda d: (len(d), d)):
        pins = snap['pcb_pin_nets'][des]
        n = sum(1 for v in pins.values() if v)
        print(f"  {des:12s} 已接引脚 {n}/{len(pins)}")

    print(f"\n【网络名】 {len(snap['nets'])} 个")
    print("  " + " ".join(snap['nets']))


if __name__ == '__main__':
    main()
