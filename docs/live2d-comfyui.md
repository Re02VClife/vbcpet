# ComfyUI 换皮工作流 — 使用说明

> 配套工具: `tools/live2d_repack.py` (拆块/拼回)。工作流: `E:\AIHT\ComfyUI\workflow_l2d_block.json`
> 一个工作流处理**一个部件块**; 78 块里先挑大块做, 跑通再铺开。

## 工作流是什么

`workflow_l2d_block.json` = 你的 char_swap (IPAdapter 人设锚点) 改成**针对单个 Live2D 块**的 img2img:

```
人设参考图 ──IPAdapter──▶ 特征注入
Hiyori 块原图 → 放大到1MP → img2img(denoise 0.55 保轮廓) → 新块图
```

**为什么 denoise 0.55 低**: 块原图是 UV 对齐的底图, denoise 越低越贴原轮廓, 网格就不变形。太高会跑飞导致穿帮。

## 使用步骤

1. **准备两张图**, 放进 `E:\AIHT\ComfyUI\input\`:
   - `l2d_ref.png` — 你的人设参考图/三视图 (IPAdapter 锚点, 角色特征来源)
   - `l2d_block.png` — 要换的 Hiyori 块 (从 `live2d/Hiyori.blocks/` 复制某块改名)
2. **打开工作流**: ComfyUI 里 Load `workflow_l2d_block.json` (或复制内容粘贴)
3. **改提示词** (节点 8 的 `[角色特征]`): 填入你的人设描述, 见下方模板
4. **跑一次** (Queue), 看输出块图
5. **存回 + 拼回**:
   ```
   # 生成的块图放到 blocks 目录 (改名回 texture_XX_YYY.png)
   cp ComfyUI/output/l2d_block_00001.png live2d/Hiyori.blocks/texture_00_000.png
   .venv/Scripts/python.exe tools/live2d_repack.py repack live2d/Hiyori.blocks -o live2d/Hiyori.new
   cp live2d/Hiyori.new/Hiyori.2048/*.png live2d/Hiyori/Hiyori.2048/
   ```
6. **预览**: 浏览器 `live2d/render.html?res=480x800`

## 提示词模板 (按部件类型)

在节点 8 里, 把 `[角色特征]` 替换成具体描述。基础模板:

```
masterpiece, best quality, anime style, flat color, cel shading, clean lineart,
game asset, single character part, same silhouette as reference,
[角色特征], 
```

按部件追加:

| 部件 | 追加描述 |
|------|---------|
| 头发 | `flowing [发色] hair, anime hair, detailed strands` |
| 脸 | `anime face, [瞳色] eyes, soft shading, no background` |
| 身体/服装 | `[服装描述: 黑色哥特连衣裙], costume detail, folds` |
| 衣服饰边 | `trim, ribbon, lace, clean edge` |

**负面提示词** (节点 9, 已内置): `lowres, bad anatomy, text, watermark, logo, background, frame, jpeg artifacts` — 强调去掉背景/文字, 因为块图不能有背景。

## 参数调优 (效果不对时)

| 参数 | 位置 | 作用 | 建议 |
|------|------|------|------|
| `denoise` | 节点 10 | 保轮廓 vs 自由发挥 | 0.4~0.5 更贴原图, 0.6~0.7 更大改 |
| `weight` | 节点 7 | IPAdapter 人设强度 | 0.7 低 / 0.85 默认 / 1.0 强 |
| `seed` | 节点 10 | 换一帧看不同效果 | 随机换号 |
| `megapixels` | 节点 4 | 块放大分辨率 | 0.5~1.0, 越小越快 |

## 轮廓改动策略 (分三档)

**技术现实**: Hiyori 的网格顶点钉在原贴图轮廓上, 轮廓改得越大, 超出网格支撑的部分在动作时会变形/穿帮。按你要的自由度选档:

| 档位 | 轮廓改动 | denoise | 效果 | 适合 |
|------|---------|---------|------|------|
| **保守** | 只换颜色/纹理/款式细节 | 0.4~0.5 | 网格无忧, 最稳 | 先验证管线 |
| **标准** | 轮廓小幅调整 (刘海/裙摆/衣领) | 0.55~0.65 | 轻微变形可接受 | 大多数部件 |
| **自由** | 服饰/发型大改 (全新设计) | **0.7~0.8** | 轮廓变化处可能穿帮/不跟动作 | 想彻底换形象 |

> ⚠️ 轮廓大改的块, **静态看可能很惊艳, 但动作时超出网格的区域会撕裂**。先跑一个块看动作效果再决定。

## 穿帮了怎么办

- 只是**静态穿帮** (造型突兀) → 该块重跑, denoise 降 0.1
- **动作时撕裂** → 该块轮廓超网格了, 二选一:
  1. 接受 (该部位动作幅度小/不显眼)
  2. 回到标准档, 把发型/服饰收敛回原轮廓范围
- 真想彻底换体型/发型 → 工具链到极限了, 需 **Live2D Cubism Editor** 重新建模 (网格重绑定), 我可以给操作步骤

## 硬规则 (网格保护, 不能破)

1. **别盖到相邻块的范围** — 每块有自己的 UV 领地, 越界会互相覆盖
2. **先小批量**: 换 1~2 个块 → repack → 预览 (含动作) → 满意再铺开
3. 生成立绘整图**不能**直接当贴图 (UV 会全错), 必须走"拆块→逐块→拼回"

## 想做批量?

单块跑通后, 可以让我写个批量脚本: 自动遍历 `blocks.json`, 对每块提交 ComfyUI API 生成 + repack。需要你提供人设参考图固定后再说。
