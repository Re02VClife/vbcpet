#!/usr/bin/env python
"""Live2D 批量换皮 — 遍历 blocks 逐块调 ComfyUI API 生成 (vbcpet)

前置: 1) ComfyUI 已启动 (默认 127.0.0.1:8188)
      2) 工作流 E:\\AIHT\\ComfyUI\\workflow_l2d_block.json 已配好人设提示词
      3) 人设参考图在 ComfyUI/input/l2d_ref.jpg
用法:
  .venv/Scripts/python.exe tools/l2d_batch.py --blocks live2d/Hiyori.blocks [--min-edge 80] [--start 0] [--count 78]
结果: 每块生成图直接覆盖回 blocks/ 同名文件
完成后运行 repack 拼回贴图:
  .venv/Scripts/python.exe tools/live2d_repack.py repack live2d/Hiyori.blocks -o live2d/Hiyori.new
"""
import argparse
import json
import os
import shutil
import time
import urllib.request

COMFY_HOST = os.environ.get("COMFY_HOST", "http://127.0.0.1:8188")
COMFY_DIR = r"E:\AIHT\ComfyUI"
WF_PATH = os.path.join(COMFY_DIR, "workflow_l2d_block.json")
INPUT_DIR = os.path.join(COMFY_DIR, "input")
OUTPUT_DIR = os.path.join(COMFY_DIR, "output")
ORIG_DIR = r"D:\vscode\vbcpet\live2d\Hiyori\Hiyori.2048"   # 原始贴图 (底图来源)

TMP_BLOCK = "l2d_batch_block.png"   # ComfyUI input 里的临时块图名


def api_get(path):
    with urllib.request.urlopen(COMFY_HOST + path, timeout=15) as r:
        return json.load(r)


def api_post(path, data):
    req = urllib.request.Request(
        COMFY_HOST + path, data=json.dumps(data).encode("utf-8"),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.load(r)


def generate_one(wf, block_meta, blocks_dir):
    """对单个块生成, 覆盖回 blocks_dir/block_name。返回 True=成功。"""
    # 底图从原始贴图裁切 (blocks 目录可能被上次生成污染)
    from PIL import Image
    orig = Image.open(os.path.join(ORIG_DIR, block_meta["file"])).convert("RGBA")
    crop = orig.crop((block_meta["x0"], block_meta["y0"],
                      block_meta["x1"], block_meta["y1"]))
    crop.save(os.path.join(INPUT_DIR, TMP_BLOCK))
    wf["3"]["inputs"]["image"] = TMP_BLOCK
    try:
        r = api_post("/prompt", {"prompt": wf, "client_id": "l2d_batch"})
    except Exception as e:
        print(f"    提交失败: {e}")
        return False
    pid = r["prompt_id"]

    # 轮询完成 (每块最多 ~10min)
    hist = {}
    for _ in range(300):
        time.sleep(2)
        try:
            hist = api_get(f"/history/{pid}")
        except Exception:
            continue
        if pid in hist and hist[pid]["status"].get("completed"):
            break
    else:
        print(f"    超时")
        return False

    # 取输出图片文件
    for nid, out in hist.get(pid, {}).get("outputs", {}).items():
        for im in out.get("images", []):
            src = os.path.join(OUTPUT_DIR, im["filename"])
            if os.path.exists(src):
                shutil.copy(src, os.path.join(blocks_dir, block_name))
                return True
    return False


def main():
    ap = argparse.ArgumentParser(description="Live2D 批量换皮 (ComfyUI API)")
    ap.add_argument("--blocks", default="live2d/Hiyori.blocks")
    ap.add_argument("--min-edge", type=int, default=80, help="跳过短边小于此值的块")
    ap.add_argument("--start", type=int, default=0, help="从第几个块开始 (续跑用)")
    ap.add_argument("--count", type=int, default=9999, help="处理多少个块")
    args = ap.parse_args()

    if not os.path.exists(WF_PATH):
        print(f"找不到工作流: {WF_PATH} (先确认 ComfyUI 目录)")
        return
    # 确认 ComfyUI 活着
    try:
        api_get("/system_stats")
    except Exception:
        print("ComfyUI 未启动 (127.0.0.1:8188) — 先运行 ComfyUI 再执行")
        return

    wf = json.load(open(WF_PATH, encoding="utf-8"))
    manifest = json.load(open(os.path.join(args.blocks, "blocks.json"), encoding="utf-8"))
    blocks = [e for png in manifest.values() for e in png]
    blocks.sort(key=lambda e: e["w"] * e["h"], reverse=True)  # 大块优先

    todo = [e for e in blocks if e["w"] >= args.min_edge and e["h"] >= args.min_edge]
    todo = todo[args.start:args.start + args.count]
    skipped = len(blocks) - len(todo) - args.start
    print(f"共 {len(blocks)} 块; 跳过 <{args.min_edge}px: {skipped} 块; 本次处理 {len(todo)} 块 (每块 ~30-60s)")

    ok = 0
    for i, e in enumerate(todo):
        name = e["name"]
        print(f"[{args.start+i+1}/{len(blocks)}] {name} ({e['w']}x{e['h']})...", flush=True)
        if generate_one(wf, e, args.blocks):
            ok += 1
            print(f"    OK")
        else:
            print(f"    FAIL (块保留原图)")

    print(f"\n完成 {ok}/{len(todo)} 块。下一步拼回:")
    print(f"  .venv/Scripts/python.exe tools/live2d_repack.py repack {args.blocks} -o live2d/Hiyori.new")
    print(f"  然后: cp live2d/Hiyori.new/Hiyori.2048/*.png live2d/Hiyori/Hiyori.2048/")


if __name__ == "__main__":
    main()
