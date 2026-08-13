#!/usr/bin/env python
"""Live2D 贴图拆块/拼回工具 — ComfyUI 换皮桥接 (vbcpet)

把 Live2D 模型的 2048×2048 平铺贴图拆成独立部件块 (保留 UV 位置),
用户对每块用 ComfyUI/PS 生成新形象后, 再拼回两张贴图, 网格完全对齐。

用法 (Windows: 用 .venv/Scripts/python.exe 跑, 依赖 Pillow+numpy):
  1) 拆块:
     python tools/live2d_repack.py unpack live2d/Hiyori/Hiyori.2048 -o live2d/Hiyori.blocks
     → live2d/Hiyori.blocks/
         blocks.json           # 每块: {file, name, y0,x0,y1,x1}
         overview_00.png/01.png # 分区标注图 (每块编号)
         texture_00_000.png...  # 每块裁剪图 (保留 bbox 尺寸与透明)
  2) 逐块生成: 用 ComfyUI/PS 替换 blocks/ 里的块 (保持文件名/尺寸比例)
     (只改想改的块, 不动的块删掉即可, repack 会自动用原图)
  3) 拼回:
     python tools/live2d_repack.py repack live2d/Hiyori.blocks -o live2d/Hiyori.new
     → live2d/Hiyori.new/Hiyori.2048/texture_00.png (+ texture_01.png)
  4) 浏览器验证: 打开 live2d/render.html?res=480x800 看效果
"""
import argparse
import json
import os
import sys
from collections import deque

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ALPHA_THRESH = 100      # 不透明判定
MIN_SAMPLE_PTS = 24     # 512 降采样下最小连通域点数 (过滤噪声)


# ── 拆块 ──
def find_blocks(png_path):
    img = Image.open(png_path).convert("RGBA")
    alpha = np.array(img)[:, :, 3]
    H, W = alpha.shape

    # 降采样到 512 做连通域 (速度), 再精修 bbox 到原图
    s = alpha[::4, ::4] > ALPHA_THRESH
    h, w = s.shape
    visited = np.zeros_like(s, bool)
    comps = []
    for y in range(h):
        for x in range(w):
            if s[y, x] and not visited[y, x]:
                q = deque([(y, x)]); visited[y, x] = True
                ys0, ys1, xs0, xs1, cnt = y, y, x, x, 1
                while q:
                    cy, cx = q.popleft()
                    for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
                        ny, nx = cy+dy, cx+dx
                        if 0 <= ny < h and 0 <= nx < w and s[ny, nx] and not visited[ny, nx]:
                            visited[ny, nx] = True
                            q.append((ny, nx))
                            cnt += 1
                            if ny < ys0: ys0 = ny
                            if ny > ys1: ys1 = ny
                            if nx < xs0: xs0 = nx
                            if nx > xs1: xs1 = nx
                if cnt >= MIN_SAMPLE_PTS:
                    comps.append((ys0*4, xs0*4, (ys1+1)*4, (xs1+1)*4, cnt))
    comps.sort(key=lambda c: -c[4])

    # 精修 bbox 到原图 alpha (扩一圈, 取区域内不透明像素的精确边界)
    refined = []
    for y0, x0, y1, x1, cnt in comps:
        y0m = max(0, y0-2); x0m = max(0, x0-2)
        y1m = min(H, y1+2); x1m = min(W, x1+2)
        region = alpha[y0m:y1m, x0m:x1m]
        ys, xs = np.nonzero(region > ALPHA_THRESH)
        if len(ys) == 0:
            continue
        ry0, ry1 = int(y0m + ys.min()), int(y0m + ys.max() + 1)
        rx0, rx1 = int(x0m + xs.min()), int(x0m + xs.max() + 1)
        refined.append((ry0, rx0, ry1, rx1, int(cnt)))
    return img, refined


def unpack(texture_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    manifest = {}
    for png in sorted(os.listdir(texture_dir)):
        if not png.lower().endswith(".png"):
            continue
        img, blocks = find_blocks(os.path.join(texture_dir, png))
        entries = []
        for i, (y0, x0, y1, x1, cnt) in enumerate(blocks):
            name = f"{png[:-4]}_{i:03d}.png"
            crop = img.crop((x0, y0, x1, y1))
            crop.save(os.path.join(out_dir, name))
            entries.append({
                "file": png, "name": name,
                "y0": y0, "x0": x0, "y1": y1, "x1": x1,
                "w": x1-x0, "h": y1-y0, "px": int(crop.size[0]*crop.size[1]),
            })
            print(f"  {name:24s} bbox=({x0},{y0})-({x1},{y1})  {crop.size[0]}x{crop.size[1]}")
        manifest[png] = entries
        # 分区标注图
        ov = Image.new("RGB", img.size, (20, 20, 30))
        draw = ImageDraw.Draw(ov)
        import colorsys
        for i, (y0, x0, y1, x1, cnt) in enumerate(blocks):
            c = tuple(int(v*255) for v in colorsys.hsv_to_rgb((i*0.618) % 1.0, 0.8, 0.95))
            draw.rectangle([x0, y0, x1-1, y1-1], outline=c, width=3)
            draw.text((x0+6, y0+6), str(i), fill=c)
        ov.save(os.path.join(out_dir, f"overview_{png}"))
        print(f"  overview_{png}: {len(blocks)} 块")

    with open(os.path.join(out_dir, "blocks.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=1)
    total = sum(len(v) for v in manifest.values())
    print(f"\n完成: {len(manifest)} 张贴图, 共 {total} 块 → {out_dir}")


# ── 拼回 ──
def repack(blocks_dir, out_dir):
    with open(os.path.join(blocks_dir, "blocks.json"), encoding="utf-8") as f:
        manifest = json.load(f)

    os.makedirs(os.path.join(out_dir, "Hiyori.2048"), exist_ok=True)
    for png, entries in manifest.items():
        # numpy 直接写像素 (PIL paste 会 alpha 混合, 不无损)
        arr = np.zeros((2048, 2048, 4), dtype=np.uint8)
        orig_img = Image.open(os.path.join(_orig_dir, png)).convert("RGBA")
        replaced = 0
        for e in entries:
            name = e["name"]
            src = os.path.join(blocks_dir, name)
            if os.path.exists(src):
                blk_src = Image.open(src)
                has_alpha = blk_src.mode in ("RGBA", "LA")
                blk = blk_src.convert("RGBA")
                replaced += 1
            else:
                blk = orig_img.crop((e["x0"], e["y0"], e["x1"], e["y1"]))
                has_alpha = True
            if blk.size != (e["w"], e["h"]):
                blk = blk.resize((e["w"], e["h"]), Image.LANCZOS)
            if not has_alpha:
                # ComfyUI 输出无透明 → 用原块 alpha 剪掉白底/越界 (防盖邻块)
                orig_blk = orig_img.crop((e["x0"], e["y0"], e["x1"], e["y1"]))
                if orig_blk.size != blk.size:
                    orig_blk = orig_blk.resize(blk.size, Image.LANCZOS)
                blk.putalpha(orig_blk.getchannel("A"))
            arr[e["y0"]:e["y1"], e["x0"]:e["x1"]] = np.array(blk)
        Image.fromarray(arr, "RGBA").save(os.path.join(out_dir, "Hiyori.2048", png))
        print(f"  {png}: {replaced}/{len(entries)} 块为替换图")
    print(f"\n完成 → {out_dir}/Hiyori.2048/")


def main():
    ap = argparse.ArgumentParser(description="Live2D 贴图拆块/拼回")
    sub = ap.add_subparsers(dest="cmd", required=True)
    pu = sub.add_parser("unpack")
    pu.add_argument("texture_dir")
    pu.add_argument("-o", "--out", default="live2d/Hiyori.blocks")
    pr = sub.add_parser("repack")
    pr.add_argument("blocks_dir")
    pr.add_argument("-o", "--out", default="live2d/Hiyori.new")
    pr.add_argument("--orig", default="live2d/Hiyori/Hiyori.2048",
                    help="原贴图目录 (未替换块的回退)")
    args = ap.parse_args()

    if args.cmd == "unpack":
        unpack(args.texture_dir, args.out)
    else:
        # 允许覆盖原贴图目录 (repack 回退用)
        global _orig_dir
        _orig_dir = args.orig
        repack(args.blocks_dir, args.out)


_orig_dir = "live2d/Hiyori/Hiyori.2048"
if __name__ == "__main__":
    main()
