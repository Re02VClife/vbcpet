# Live2D 换皮指南 — 基于 Hiyori 骨架换自己形象

> 目标: 保留 Hiyori 的骨骼/网格/动作/表情, 只替换贴图, 让角色变成自己人设的形象。
> 工具: `tools/live2d_repack.py` (拆块/拼回) + ComfyUI (生成) + 浏览器 (预览)。

## 原理

Live2D 模型 = `.moc3` 骨骼网格 + **2 张 2048×2048 平铺贴图** (所有部件拼在一起)。
每块对应一个部位 (脸/头发/身体/衣服…), 有固定 UV 坐标。换皮的关键: **新图必须保持每块的 UV 位置**, 否则网格错位。
ComfyUI 不知道 UV 分区, 所以用脚本桥接: 拆块 → 逐块生成 → 拼回。

## 一、拆块 (已完成, 产物在 `live2d/Hiyori.blocks/`)

```bash
.venv/Scripts/python.exe tools/live2d_repack.py unpack live2d/Hiyori/Hiyori.2048 -o live2d/Hiyori.blocks
```

- `overview_texture_00.png` / `overview_texture_01.png` — **分区标注图**, 每个部件有编号
- `texture_00_000.png`… — 每块裁剪图 (保留原 bbox 尺寸)
- `blocks.json` — 每块 UV 坐标 (拼回用)
- Hiyori 共拆出 **78 块** (2 张贴图各 39 块)

## 二、逐块生成新形象 (ComfyUI)

**不必 78 块全重画** — 先挑大块 (overview 图里编号靠前、面积大的: 头/身体/主要服装), 小块细节保留。

推荐做法 (用你的 `E:\AIHT\ComfyUI`):
1. **人设特征注入**: 用 `workflow_char_swap.json` (IPAdapter) 把你的人设参考图/三视图作为角色锚点
2. **逐块生成**: 对每个要换的块, 以 `texture_XX_YYY.png` 原块为 **img2img 底图** (`workflow_img2img.json`), 加人设提示词, 低 denoise (0.3~0.6) 或 IPAdapter 强度
3. **铁律**: 保持原块的**形状轮廓、大致位置、尺寸比例** — 只改配色/纹理/服装样式/细节。这是网格不变形的关键
4. 生成结果如果尺寸不同, repack 会自动 resize 回原块 bbox

> 想整块彻底改轮廓 (比如换发型形状) 可以, 但别改到**相邻块的范围** (UV 会互相覆盖)。

## 三、拼回

```bash
# 生成的块按原名覆盖到 live2d/Hiyori.blocks/ (只放改过的, 没改的删掉/留原图都行)
.venv/Scripts/python.exe tools/live2d_repack.py repack live2d/Hiyori.blocks -o live2d/Hiyori.new

# 备份原贴图后替换
cp -r live2d/Hiyori/Hiyori.2048 live2d/Hiyori/Hiyori.2048.bak
cp live2d/Hiyori.new/Hiyori.2048/*.png live2d/Hiyori/Hiyori.2048/
```

## 四、预览验证

```bash
cd live2d && python server.py
# 浏览器 http://127.0.0.1:8000/render.html?res=480x800
# 动作/表情应照常; 用 ?scale= &x= &y= 微调位置
```

## 常见问题

- **网格错位/穿帮**: 某块的生成图轮廓越界了 → 重画该块, 严格贴原块轮廓
- **想彻底换体型** (不只是换皮): 工具链做不到 (网格固定), 需在 Live2D Cubism Editor 里重新建模
- **颜色突兀**: 生成时把原块作为 img2img 底图, 低 denoise 能保持光影一致

## 相关

- `render.html` 已支持 `?model=` 换模型、`?scale=`/`?x=`/`?y=` 微调
- 表情参数 (EMOTION_PRESETS) 目前是 Hiyori 的, 若换模型需按 F12 打印的 `model params` 重调
