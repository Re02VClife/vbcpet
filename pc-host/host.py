"""pc-host — Phase 0 最小骨架 (导演/发送端)。

职责 (v0 回环)：
- 起本地 WHIP/HTTP 信令端点，接受 sim(P4 替身) 的 SDP offer，回 answer。
- 向 sim 推一路**占位视频轨**（动画 + 帧时间戳标记，用于测延迟）。
- (可选) 推一路占位音频轨。
- 处理 DataChannel `ctrl`：收 button/dock/hello，演示 prompt_confirm 授权回路，
  周期性下发 work_status / avatar / panel。

真实模块 (Claude Code stream-json 驱动、导演 LLM、ASR/TTS、真 Live2D 渲染) 后续接入，
此处用占位与日志代替，先验证传输 + 协议骨架。

运行: python pc-host/host.py  [--host 0.0.0.0] [--port 8080] [--no-audio]
"""
from __future__ import annotations

import argparse
import asyncio
import logging
import sys
from itertools import count
from pathlib import Path

import numpy as np
from aiohttp import web
from aiortc import RTCPeerConnection, RTCSessionDescription, VideoStreamTrack
from av import VideoFrame

# 让 `common` 包可导入（仓库根加入 sys.path）
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common import protocol as P  # noqa: E402
from common.frametag import draw_tag, tag_value  # noqa: E402
from agent_driver import DriverCallbacks, PermissionRequest, Usage  # noqa: E402
from fake_driver import FakeDriver  # noqa: E402
from claude_driver import ClaudeDriver  # noqa: E402

try:  # 情绪事件中继 (双显联动) — 可选，缺失时静默降级
    from emotion_relay import relay  # noqa: E402
except Exception:  # noqa: BLE001
    relay = None  # type: ignore[assignment]

log = logging.getLogger("host")

_pcs: set[RTCPeerConnection] = set()
_confirm_ids = count(1)

# 按键 -> Claude Code control_response 语义 (docs/protocol.md §3)
_KEY_TO_RESPONSE = {
    "yes": "allow (本次)",
    "no": "deny (本次)",
    "yes_always": "allow + 记住该工具，本会话免再问",
    "panel": "本地 UI 切换，不回 Claude Code",
}


class DeskPetVideoTrack(VideoStreamTrack):
    """占位 Live2D 画面：动画背景 + 移动方块 + 左上角帧时间戳标记。"""

    def __init__(self, width: int = 640, height: int = 480) -> None:
        super().__init__()
        self.width = width
        self.height = height
        self._n = 0

    def _render(self) -> np.ndarray:
        w, h, n = self.width, self.height, self._n
        arr = np.zeros((h, w, 3), dtype=np.uint8)
        # 渐变背景随帧缓慢变化
        arr[:, :, 0] = (n * 2) % 256          # R
        arr[:, :, 2] = (128 + n) % 256        # B
        # 移动方块（模拟“会动”的桌宠）
        bw, bh = 80, 80
        cx = int((w - bw) * (0.5 + 0.45 * np.sin(n * 0.05)))
        cy = int((h - bh) * (0.5 + 0.45 * np.cos(n * 0.07)))
        arr[cy:cy + bh, cx:cx + bw, 1] = 255  # 绿方块
        # 帧时间戳标记（供 sim 测延迟）——画在最后，避免被覆盖
        draw_tag(arr, tag_value())
        self._n += 1
        return arr

    async def recv(self) -> VideoFrame:
        pts, time_base = await self.next_timestamp()  # 基类按 ~30fps 节流
        frame = VideoFrame.from_ndarray(self._render(), format="rgb24")
        frame.pts = pts
        frame.time_base = time_base
        return frame


def _make_audio_track():
    """占位音频轨（静音），仅用于验证音频轨协商。失败则返回 None。"""
    try:
        from aiortc.mediastreams import AudioStreamTrack
        return AudioStreamTrack()
    except Exception as exc:  # noqa: BLE001
        log.warning("音频轨不可用，跳过: %s", exc)
        return None


async def _director_loop(channel, sg: P.SeqGen) -> None:
    """周期性下发状态/动作/面板，并定期发起一次授权确认，演示回路。"""
    tokens = {"input": 0, "output": 0, "total": 0}
    states = ["thinking", "editing", "running", "done"]
    try:
        i = 0
        while True:
            await asyncio.sleep(2.0)
            if channel.readyState != "open":
                break
            # 模拟 token 增长
            tokens["input"] += 800
            tokens["output"] += 220
            tokens["total"] = tokens["input"] + tokens["output"]
            state = states[i % len(states)]
            channel.send(P.e_status(sg, state, detail=f"演示步骤 {i}"))
            channel.send(P.e_emotion(sg, action="think" if state == "thinking" else "idle",
                                     emotion="happy" if state == "done" else "neutral"))
            channel.send(P.e_panel(sg, tokens=dict(tokens), cost_usd=round(tokens["total"] * 1e-6, 4),
                                   session="sess_demo", model="demo", turns=i))
            # 每 5 步发起一次授权确认
            if i > 0 and i % 5 == 0:
                cid = f"req_{next(_confirm_ids)}"
                channel.send(P.e_permission(sg, cid, reason="允许运行 `npm test`?"))
                log.info("↓ prompt_confirm id=%s 等待按键…", cid)
            i += 1
    except asyncio.CancelledError:
        pass


def _setup_channel(channel, pc: RTCPeerConnection) -> None:
    sg = P.SeqGen()
    task: asyncio.Task | None = None
    log.info("DataChannel '%s' 已建立 (state=%s)", channel.label, channel.readyState)

    def _start_director() -> None:
        nonlocal task
        if task is None:
            task = asyncio.ensure_future(_director_loop(channel, sg))

    @channel.on("message")
    def on_message(raw) -> None:  # noqa: ANN001
        msg = P.decode(raw)
        t = msg.get("t")
        if t == P.T_BUTTON:
            key = msg.get("key")
            cid = msg.get("confirm_id")
            resp = _KEY_TO_RESPONSE.get(key, "?")
            log.info("↑ button key=%s confirm_id=%s  ⇒ control_response: %s", key, cid, resp)
        elif t == P.T_DOCK:
            log.info("↑ dock slot=%s present=%s ⇒ 通知手机 App 停靠模式", msg.get("slot"), msg.get("present"))
        elif t == P.T_HELLO:
            log.info("↑ hello fw=%s board=%s caps=%s", msg.get("fw"), msg.get("board"), msg.get("caps"))
        elif t == P.T_HEARTBEAT:
            log.debug("↑ heartbeat rssi=%s free_psram=%s", msg.get("rssi"), msg.get("free_psram"))
        elif t == P.T_VAD:
            log.info("↑ vad state=%s", msg.get("state"))
        else:
            log.info("↑ %s", msg)

    @channel.on("open")
    def on_open() -> None:
        _start_director()

    @pc.on("connectionstatechange")
    async def on_state() -> None:
        if pc.connectionState in ("failed", "closed", "disconnected") and task:
            task.cancel()

    # 答方收到的 DataChannel 常已处于 open 态，"open" 事件不会再触发 —— 直接启动
    if channel.readyState == "open":
        _start_director()


def _build_driver(kind: str, cb: DriverCallbacks, cwd: str | None):
    if kind == "claude":
        return ClaudeDriver(cb, cwd=cwd)
    if kind == "claude-sdk":
        from claude_sdk_driver import ClaudeSdkDriver  # 延迟导入：仅此模式需 claude-agent-sdk
        return ClaudeSdkDriver(cb, cwd=cwd)
    return FakeDriver(cb)


# 按键 -> (allow, remember)
_KEY_DECISION = {
    "yes": (True, False),
    "yes_always": (True, True),
    "no": (False, False),
}


# ADR-007: 每条连接的两条 DataChannel 引用（ctrl=可靠, realtime=不可靠）
_CHANNELS: dict[str, Any] = {}   # key: session-like id → {"ctrl": channel, "realtime": channel}


def _setup_channel_driver(channel, pc: RTCPeerConnection, kind: str, cwd: str | None,
                          director_kind: str = "heuristic", live2d_track=None) -> None:
    """接收侧 DataChannel 处理。ctrl=可靠(全功能), realtime=不可靠(仅发动画)。"""
    label = channel.label
    log.info("DataChannel '%s' 已建立 (driver=%s, director=%s)", label, kind, director_kind)

    # 用 pc 的 id() 做连接 key（同一 WebRTC 连接的两条 channel 共享）
    conn_key = str(id(pc))
    if conn_key not in _CHANNELS:
        _CHANNELS[conn_key] = {}
    _CHANNELS[conn_key][label] = channel

    # realtime 只用于发动画消息，不需要控制逻辑
    if label != P.CTRL_CHANNEL:
        return

    sg = P.SeqGen()
    loop = asyncio.get_event_loop()
    pending: dict[str, PermissionRequest] = {}
    from director import Director  # noqa: PLC0415
    director = Director.from_config(
        {"kind": "openai", "api_base": DIRECTORS.get("openai", {}).get("api_base", ""),
         "api_key": DIRECTORS.get("openai", {}).get("api_key", ""),
         "model": DIRECTORS.get("openai", {}).get("model", "deepseek-chat")}
        if director_kind == "openai" else None)

    def _rt_ch() -> Any:
        """当前连接的实时通道（可能为 None）。"""
        return _CHANNELS.get(conn_key, {}).get(P.REALTIME_CHANNEL)

    def send(raw: str, realtime: bool = False) -> None:
        target = _rt_ch() if realtime else channel
        if target and target.readyState == "open":
            target.send(raw)

    # ---- 代理事件 -> 下行 DataChannel ----
    def on_status(state: str, detail: str) -> None:
        nonlocal _last_work_status
        _last_work_status = state
        send(P.e_status(sg, state, detail=detail))
        # 导演情绪走实时通道 (ADR-007)
        emo = {"done": "happy", "error": "sad", "blocked": "worried"}.get(state, "neutral")
        act = "think" if state == "thinking" else ("celebrate" if state == "done" else "idle")
        send(P.e_emotion(sg, action=act, emotion=emo), realtime=True)
        # Live2D 只在这里驱动——_do_say 不再重复调 apply_avatar
        if live2d_track:
            asyncio.ensure_future(live2d_track.apply_avatar(act, emo, 0.6))
        # 双显联动: 状态 → 情绪广播 (桌面窗口 + P4 via hub)
        if relay:
            relay.emit_emotion({"happy": "happy", "sad": "sad", "worried": "sad",
                                "neutral": "neutral"}.get(emo, "neutral"))
            if state == "done":
                relay.emit_motion("celebrate")

    async def _do_say(text: str) -> None:
        result = await director.compress(text, _last_work_status)
        send(P.e_speech(sg, result.speech))
        send(P.e_emotion(sg, action=result.action, emotion=result.emotion,
                         intensity=result.intensity), realtime=True)
        # Live2D 模型由 on_status 统一驱动，此处不重复调
        # 双显联动: 对话文本 → 情绪广播 (桌面窗口 + P4 via hub)
        if relay:
            relay.emit_message(result.speech, emotion=result.emotion)
        log.info("↓ say(raw:%d -> speech:%d) %s",
                 len(text), len(result.speech),
                 result.speech[:60] + ("…" if len(result.speech) > 60 else ""))

    _last_work_status = "running"

    def on_say(text: str) -> None:
        nonlocal _last_work_status
        asyncio.ensure_future(_do_say(text))

    def on_panel(u: Usage) -> None:
        send(P.e_panel(sg, tokens=u.as_panel_tokens(), cost_usd=u.cost_usd,
                       session=u.session, model=u.model, turns=u.turns))

    def on_permission(req: PermissionRequest) -> None:
        pending[req.request_id] = req
        send(P.e_permission(sg, req.request_id, reason=req.human_reason()))
        log.info("↓ prompt_confirm id=%s (%s) 等待按键…", req.request_id, req.tool_name)

    cb = DriverCallbacks(on_status=on_status, on_say=on_say, on_panel=on_panel,
                         on_permission=on_permission,
                         on_log=lambda m: log.info("[driver] %s", m))
    driver = _build_driver(kind, cb, cwd)
    asyncio.ensure_future(driver.start())

    # ---- 上行 DataChannel -> 代理 ----
    @channel.on("message")
    def on_message(raw) -> None:  # noqa: ANN001
        msg = P.decode(raw)
        t = msg.get("t")
        if t == P.T_USER_TEXT:
            text = msg.get("text", "")
            log.info("↑ user_text: %s", text)
            asyncio.ensure_future(driver.send_user(text))
        elif t == P.T_BUTTON:
            key = msg.get("key")
            cid = msg.get("confirm_id")
            if key == "panel":
                return
            if cid and cid in pending and key in _KEY_DECISION:
                allow, remember = _KEY_DECISION[key]
                pending.pop(cid, None)
                log.info("↑ button %s -> resolve %s (allow=%s remember=%s)", key, cid, allow, remember)
                asyncio.ensure_future(driver.resolve_permission(cid, allow, remember=remember))
            else:
                log.info("↑ button %s (无匹配的待授权项)", key)
        elif t == P.T_DOCK:
            log.info("↑ dock slot=%s present=%s", msg.get("slot"), msg.get("present"))
        elif t == P.T_HELLO:
            log.info("↑ hello fw=%s caps=%s", msg.get("fw"), msg.get("caps"))
        elif t in (P.T_HEARTBEAT, P.T_VAD):
            pass
        else:
            log.info("↑ %s", msg)

    @pc.on("connectionstatechange")
    async def on_state() -> None:
        if pc.connectionState in ("failed", "closed", "disconnected"):
            _CHANNELS.pop(conn_key, None)   # ADR-007: 清理双通道引用
            await director.close()
            await driver.stop()


async def whip(request: web.Request) -> web.Response:
    offer_sdp = await request.text()
    pc = RTCPeerConnection()
    _pcs.add(pc)
    audio = request.app["audio"]
    live2d_path = request.app.get("live2d", "")
    log.info("← 收到 offer (%d 字节)，新建 PeerConnection", len(offer_sdp))

    # Live2D 视频轨（--live2d 启用时替代占位动画）
    live2d_track = None
    if live2d_path:
        from live2d_track import Live2DVideoTrack  # noqa: PLC0415
        live2d_track = Live2DVideoTrack(html_path=live2d_path)
        await live2d_track.start()
        request.app["_live2d_track"] = live2d_track  # 供 close 清理

    @pc.on("datachannel")
    def on_datachannel(channel) -> None:  # noqa: ANN001
        kind = request.app["driver"]
        dk = request.app.get("director", "heuristic")
        if kind == "demo":
            _setup_channel(channel, pc)
        else:
            _setup_channel_driver(channel, pc, kind, request.app["cwd"],
                                  director_kind=dk, live2d_track=live2d_track)

    @pc.on("connectionstatechange")
    async def on_state() -> None:
        log.info("连接状态: %s", pc.connectionState)
        if pc.connectionState in ("failed", "closed"):
            if live2d_track:
                await live2d_track.stop()
            await pc.close()
            _pcs.discard(pc)

    await pc.setRemoteDescription(RTCSessionDescription(sdp=offer_sdp, type="offer"))
    # 附加发送轨
    pc.addTrack(live2d_track if live2d_track else DeskPetVideoTrack())
    if audio:
        atrack = _make_audio_track()
        if atrack:
            pc.addTrack(atrack)
    answer = await pc.createAnswer()
    await pc.setLocalDescription(answer)
    return web.Response(text=pc.localDescription.sdp, content_type="application/sdp", status=201)


async def health(_request: web.Request) -> web.Response:
    return web.json_response({"ok": True, "pcs": len(_pcs)})


async def on_shutdown(app: web.Application) -> None:
    await asyncio.gather(*[pc.close() for pc in list(_pcs)], return_exceptions=True)
    _pcs.clear()


def build_app(audio: bool, driver: str, cwd: str | None, director_kind: str,
              live2d_path: str = "") -> web.Application:
    app = web.Application()
    app["audio"] = audio
    app["driver"] = driver
    app["cwd"] = cwd
    app["director"] = director_kind
    app["live2d"] = live2d_path
    app.router.add_post("/whip", whip)
    app.router.add_get("/health", health)
    app.on_shutdown.append(on_shutdown)
    return app


# 导演 OpenAI 后端配置（通过环境变量注入，避免写死在命令行参数中）
DIRECTORS: dict = {
    "openai": {
        "api_base": "",
        "api_key": "",
        "model": "deepseek-chat",
    },
}


def main() -> None:
    import os
    # 从环境变量加载 OpenAI 导演配置
    for key, env_var in [("api_base", "VBCPET_OPENAI_BASE"), ("api_key", "VBCPET_OPENAI_KEY"), ("model", "VBCPET_OPENAI_MODEL")]:
        val = os.environ.get(env_var, "")
        if val:
            DIRECTORS["openai"][key] = val

    ap = argparse.ArgumentParser(description="vbcpet pc-host (Phase 0 回环发送端)")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--no-audio", action="store_true", help="不推占位音频轨")
    ap.add_argument("--driver", choices=["demo", "fake", "claude", "claude-sdk"], default="fake",
                    help="demo=脚本演示；fake=假代理(含授权回路,无成本)；"
                         "claude=真实CLI(文本/token)；claude-sdk=真实+can_use_tool按键授权")
    ap.add_argument("--director", choices=["heuristic", "openai"], default="heuristic",
                    help="导演/压缩后端：heuristic=纯规则(零成本)；openai=OpenAI兼容API")
    ap.add_argument("--cwd", default=None, help="claude 驱动的工作目录 (默认当前目录)")
    ap.add_argument("--live2d", default="", help="Live2D 渲染页路径 (如 live2d/render.html)，留空用占位视频")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8")  # Windows 控制台默认 GBK，中文日志会乱码
        except Exception:  # noqa: BLE001
            pass
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(name)s %(levelname)s %(message)s")
    logging.getLogger("aioice").setLevel(logging.WARNING)
    logging.getLogger("aiohttp.access").setLevel(logging.WARNING)
    log.info("信令端点: http://%s:%d/whip  | driver=%s director=%s live2d=%s",
             args.host, args.port, args.driver, args.director, args.live2d or "(占位)")
    web.run_app(build_app(audio=not args.no_audio, driver=args.driver, cwd=args.cwd,
                          director_kind=args.director, live2d_path=args.live2d),
                host=args.host, port=args.port, print=None)


if __name__ == "__main__":
    main()
