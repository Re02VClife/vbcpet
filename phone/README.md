# phone (M4) — 手机 App

> 里程碑 M4，最后展开。手机作**可选高保真显示端**（手机 GPU 可跑原生/Web Live2D 高精度模型）。

## 职责
- **配对**：BLE 与桌宠板配对（一次配对长期记忆）；局域网内 WiFi 直连/WebRTC 收渲染指令。
- **停靠模式**：桌宠板在位传感器检测到手机放上竖/横槽 → 经 BLE 通知 App → App 切全屏 Live2D 显示。
- **方向**：读手机自身重力感应判定竖/横（见 [`../docs/decisions.md`](../docs/decisions.md) ADR-004）。
- **渲染**：本地跑高保真 Live2D，接收 `avatar` / `work_status` / `panel` 指令（复用 [`../docs/protocol.md`](../docs/protocol.md)）。

## 待定
- 平台：原生（iOS/Android）还是跨平台（Flutter/RN）+ Web Live2D。
- Live2D **商用授权**同样适用。
- 检测 ≠ 配对：传感器只触发模式切换，配对始终走 BLE。
