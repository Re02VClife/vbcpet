# sim — ESP32-P4 桌宠板 软件替身

作为真实 WebRTC 对端替代还没到位的 P4 硬件，让 PC 侧全栈今天就能开发验证（见 [`../docs/decisions.md`](../docs/decisions.md) ADR-005）。真机到货后换成固件，PC 侧不变。

## 能力（Phase 0 回环）
- 连 pc-host `/whip` 信令，建 WebRTC：双 DataChannel（`ctrl` 可靠 + `realtime` 不可靠）+ 收视频/音频轨。
- 软解占位视频，读帧时间戳标记 → 统计 FPS / 编解码延迟（**同机才准**）。
- 收 `status`/`speech`/`emotion`/`panel`/`permission`（CharacterEvent v1 + v0 兼容）。
- 上报 `hello`/`heartbeat`/`user_text`/`button`/`presence`。
- 模拟 4 按键（yes/no/yes_always/panel）与竖/横槽在位传感器。
- `--say` 参数模拟用户说一句指令触发代理任务。

## 运行

先装依赖（仓库根）：
```bash
python -m venv .venv
.venv/Scripts/pip install -r requirements.txt   # Windows
```

**先起 pc-host**（另一个终端，见 [`../pc-host/README.md`](../pc-host/README.md)）：
```bash
.venv/Scripts/python pc-host/host.py --port 8080
```

**再起 sim**：
```bash
# 无界面冒烟（CI / 无显示环境）：跑 16s，遇授权请求自动按 yes，退出打印统计
.venv/Scripts/python sim/sim.py --url http://127.0.0.1:8080/whip --headless --duration 16

# 交互窗口：显示视频 + 4 按键 + 竖/横槽在位开关（需 Pillow + 桌面环境）
.venv/Scripts/python sim/sim.py --url http://127.0.0.1:8080/whip --gui
```

参数：`--auto-key {yes,no,yes_always}` headless 下对授权请求自动按哪个键；`-v` 详细日志。

## 已验证（回环冒烟）
- WebRTC 连接（host ICE，无 TURN）、双 DataChannel 双向（ctrl + realtime）。
- 视频轨 ~30 FPS、帧标记解码率 100%、同机编解码延迟中位 ~40-50ms。
- 授权回路：host `permission` → sim `button yes/no` → 解析为 allow/deny。
- CharacterEvent v1 解码（自动兼容 v0 flat 格式）。

> 注意：headless 的延迟数是**软解 + 同机**结果，仅作管线基线；真实 P4 **硬解**延迟须在 devkit 上按 [`../hardware/phase0-validation.md`](../hardware/phase0-validation.md) P0-2 实测。

## 下一步
- [ ] 加 sim→host 麦克风上行音频轨（当前仅收）
- [ ] Chaos testing（`--loss --delay --disconnect`）
- [ ] 真机固件替换本替身（M2）
