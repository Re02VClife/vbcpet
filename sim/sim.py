"""sim — ESP32-P4 桌宠板 **软件替身** (Phase 0 回环接收端)。

作为真实 WebRTC 对端连到 pc-host：
- 创建 DataChannel `ctrl` + recvonly 视频/音频收发器，发 offer 到 /whip。
- 接收占位视频轨 → 软解 → 读帧时间戳标记 → 统计 FPS / 延迟（同机才准）。
- 通过 DataChannel 收 work_status/avatar/panel/prompt_confirm，
  上报 hello/heartbeat/button/dock（模拟 4 按键与在位传感器）。

两种模式：
- --headless (默认)：跑 --duration 秒，遇 prompt_confirm 自动按键，退出前打印统计。
  —— 用于无显示环境 / CI 冒烟。
- --gui：Tkinter 窗口显示视频 + 4 按键 + 竖/横槽在位开关。—— 给人交互。

运行: python sim/sim.py --url http://127.0.0.1:8080/whip [--headless|--gui]
"""
from __future__ import annotations

import argparse
import asyncio
import logging
import statistics
import sys
import threading
from pathlib import Path

import aiohttp
from aiortc import RTCPeerConnection, RTCSessionDescription
from aiortc.mediastreams import MediaStreamError

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from common import protocol as P  # noqa: E402
from common.frametag import read_tag, latency_ms  # noqa: E402

log = logging.getLogger("sim")


class SimSession:
    """一次 sim 连接的状态与操作。"""

    def __init__(self, url: str, frame_sink=None, msg_sink=None) -> None:
        self.url = url
        self.frame_sink = frame_sink          # callable(ndarray_rgb, latency_ms|None)
        self.msg_sink = msg_sink              # callable(dict)  收到的 ctrl 消息
        self.pc = RTCPeerConnection()
        self.channel = self.pc.createDataChannel(P.CTRL_CHANNEL)
        self.realtime_ch = self.pc.createDataChannel(
            P.REALTIME_CHANNEL,
            ordered=False, maxRetransmits=0)   # ADR-007: 实时通道不可靠无序
        self.sg = P.SeqGen()
        # 统计
        self.frames = 0
        self.tagged = 0
        self.latencies: list[int] = []
        self._hb_task: asyncio.Task | None = None
        self.auto_confirm_key: str | None = None  # headless 下自动按键
        self._closed = asyncio.Event()

    # ---- 发送 (P4 -> PC) ----
    def send(self, raw: str) -> None:
        if self.channel.readyState == "open":
            self.channel.send(raw)

    def press(self, key: str, confirm_id: str | None = None) -> None:
        self.send(P.m_button(self.sg, key, confirm_id=confirm_id))
        log.info("→ button %s%s", key, f" (confirm {confirm_id})" if confirm_id else "")

    def dock(self, slot: str, present: bool) -> None:
        self.send(P.m_dock(self.sg, slot, present))
        log.info("→ dock %s present=%s", slot, present)

    def say_user(self, text: str) -> None:
        """模拟用户说了一句话（ASR 结果）。"""
        self.send(P.m_user_text(self.sg, text))
        log.info("→ user_text: %s", text)

    # ---- 事件装配 ----
    def _wire(self) -> None:
        self.pc.add_listener("track", self._on_track)
        self.channel.on("open", self._on_open)
        self.channel.on("message", self._on_message)
        # ADR-007: 实时通道——只收不发（动画/表情消息，丢帧不重传）
        self.realtime_ch.on("message", self._on_message)
        self.realtime_ch.on("open", lambda: log.info("realtime DataChannel 打开"))

        @self.pc.on("connectionstatechange")
        async def _on_conn() -> None:
            log.info("连接状态: %s", self.pc.connectionState)
            if self.pc.connectionState in ("failed", "closed", "disconnected"):
                self._closed.set()

    def _on_track(self, track) -> None:  # noqa: ANN001
        log.info("收到 %s 轨", track.kind)
        if track.kind == "video":
            asyncio.ensure_future(self._consume_video(track))
        else:
            asyncio.ensure_future(self._drain(track))

    async def _drain(self, track) -> None:  # noqa: ANN001
        try:
            while True:
                await track.recv()
        except MediaStreamError:
            pass

    async def _consume_video(self, track) -> None:  # noqa: ANN001
        try:
            while True:
                frame = await track.recv()
                img = frame.to_ndarray(format="rgb24")
                self.frames += 1
                lat = None
                tag = read_tag(img)
                if tag is not None:
                    lat = latency_ms(tag)
                    self.latencies.append(lat)
                    self.tagged += 1
                if self.frame_sink:
                    self.frame_sink(img, lat)
        except MediaStreamError:
            log.info("视频轨结束")

    def _on_open(self) -> None:
        log.info("DataChannel 打开，发送 hello")
        self.send(P.m_hello(self.sg, fw="sim-0.1.0", board="sim-pc",
                            caps=["h264_dec", "opus", "aec"]))
        self._hb_task = asyncio.ensure_future(self._heartbeat())

    async def _heartbeat(self) -> None:
        try:
            while self.channel.readyState == "open":
                self.send(P.m_heartbeat(self.sg, rssi=-50, free_psram=24_000_000))
                await asyncio.sleep(3.0)
        except asyncio.CancelledError:
            pass

    def _on_message(self, raw) -> None:  # noqa: ANN001
        ev = P.decode_event(raw)   # 统一 CharacterEvent（自动兼容 v0 flat）
        etype = ev.type
        data = ev.data

        if etype == P.EVT_PANEL:
            tk = data.get("tokens", {})
            log.info("← panel tokens=%s cost=$%s", tk.get("total"), data.get("cost_usd"))
        elif etype == P.EVT_SPEECH:
            log.info("← say: %s", data.get("text"))
        elif etype == P.EVT_STATUS:
            log.info("← work_status %s (%s)", data.get("state"), data.get("detail"))
        elif etype == P.EVT_EMOTION:
            log.info("← avatar action=%s emotion=%s", data.get("action"), data.get("emotion"))
        elif etype == P.EVT_PERMISSION:
            cid = data.get("id")
            log.info("← prompt_confirm id=%s reason=%s options=%s",
                     cid, data.get("reason"), data.get("options"))
            if self.auto_confirm_key:
                asyncio.get_event_loop().call_later(
                    0.4, lambda: self.press(self.auto_confirm_key, confirm_id=cid))
        else:
            log.info("← [%s/%s] %s", ev.source, etype, data)
        if self.msg_sink:
            self.msg_sink(vars(ev))

    # ---- 连接 ----
    async def connect(self) -> None:
        self._wire()
        self.pc.addTransceiver("video", direction="recvonly")
        self.pc.addTransceiver("audio", direction="recvonly")
        offer = await self.pc.createOffer()
        await self.pc.setLocalDescription(offer)
        async with aiohttp.ClientSession() as sess:
            async with sess.post(self.url, data=self.pc.localDescription.sdp,
                                 headers={"Content-Type": "application/sdp"}) as resp:
                if resp.status not in (200, 201):
                    raise RuntimeError(f"信令失败 HTTP {resp.status}: {await resp.text()}")
                answer_sdp = await resp.text()
        await self.pc.setRemoteDescription(RTCSessionDescription(sdp=answer_sdp, type="answer"))
        log.info("已连接 %s", self.url)

    async def close(self) -> None:
        if self._hb_task:
            self._hb_task.cancel()
        await self.pc.close()

    def summary(self, duration: float) -> str:
        fps = self.frames / duration if duration else 0
        if self.latencies:
            lat = f"min={min(self.latencies)} med={int(statistics.median(self.latencies))} max={max(self.latencies)} ms"
        else:
            lat = "无 (未读到帧标记)"
        rate = (self.tagged / self.frames * 100) if self.frames else 0
        return (f"帧数={self.frames} FPS={fps:.1f} | 延迟(编解码,同机): {lat} | "
                f"标记解码率={rate:.0f}%")


async def run_headless(url: str, duration: float, auto_key: str, say: str | None = None) -> int:
    sess = SimSession(url)
    sess.auto_confirm_key = auto_key
    await sess.connect()
    loop = asyncio.get_event_loop()
    # 模拟“说一句指令”触发代理任务
    if say:
        loop.call_later(1.0, lambda: sess.say_user(say))
    # 中途演示一次“放上手机（竖屏）”
    loop.call_later(duration * 0.5, lambda: sess.dock("portrait", True))
    try:
        await asyncio.wait_for(sess._closed.wait(), timeout=duration)
    except asyncio.TimeoutError:
        pass
    print("\n==== sim 冒烟统计 ====")
    print(sess.summary(duration))
    ok = sess.frames > 0
    await sess.close()
    return 0 if ok else 2


def run_gui(url: str) -> int:
    """Tkinter 交互窗口：显示视频 + 4 按键 + 竖/横槽在位开关。"""
    import queue
    import tkinter as tk

    from PIL import Image, ImageTk  # 延迟导入，仅 GUI 需要

    frame_q: "queue.Queue" = queue.Queue(maxsize=2)
    latest_lat = {"v": None}
    out_q: "queue.Queue" = queue.Queue()
    loop_ready = threading.Event()
    session_holder: dict[str, SimSession] = {}

    def frame_sink(img, lat) -> None:
        latest_lat["v"] = lat
        try:
            frame_q.put_nowait(img)
        except queue.Full:
            pass

    # asyncio 事件循环跑在后台线程
    def loop_thread() -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)

        async def main() -> None:
            sess = SimSession(url, frame_sink=frame_sink)
            session_holder["s"] = sess
            await sess.connect()
            loop_ready.set()

            async def pump_out() -> None:
                while True:
                    try:
                        kind, a, b = out_q.get_nowait()
                        if kind == "button":
                            sess.press(a, confirm_id=b)
                        elif kind == "dock":
                            sess.dock(a, b)
                    except queue.Empty:
                        await asyncio.sleep(0.03)

            await pump_out()

        try:
            loop.run_until_complete(main())
        except Exception as exc:  # noqa: BLE001
            log.error("GUI 事件循环退出: %s", exc)

    threading.Thread(target=loop_thread, daemon=True).start()

    root = tk.Tk()
    root.title("vbcpet — 虚拟桌宠 (P4 sim)")
    canvas = tk.Label(root)
    canvas.pack()
    status = tk.Label(root, text="连接中…", anchor="w")
    status.pack(fill="x")
    last_confirm = {"id": None}

    def msg_sink(msg) -> None:
        if msg.get("t") == P.T_PROMPT_CONFIRM:
            last_confirm["id"] = msg.get("id")
            status.config(text=f"⚠ 授权请求: {msg.get('reason')} — 按 Yes/No/Yes-Always")

    def press(key: str) -> None:
        out_q.put(("button", key, last_confirm["id"]))
        if key in ("yes", "no", "yes_always"):
            last_confirm["id"] = None

    def set_dock(slot: str) -> None:
        out_q.put(("dock", slot, slot != "none"))

    btns = tk.Frame(root)
    btns.pack()
    for label, key in [("Yes", "yes"), ("No", "no"), ("Yes Always", "yes_always"), ("面板", "panel")]:
        tk.Button(btns, text=label, width=10, command=lambda k=key: press(k)).pack(side="left", padx=3, pady=4)
    dockf = tk.Frame(root)
    dockf.pack()
    tk.Label(dockf, text="手机在位:").pack(side="left")
    for label, slot in [("竖屏", "portrait"), ("横屏", "landscape"), ("移除", "none")]:
        tk.Button(dockf, text=label, width=8, command=lambda s=slot: set_dock(s)).pack(side="left", padx=3)

    # 把 session 的 msg_sink 接上（连接完成后）
    def attach_sink() -> None:
        if "s" in session_holder:
            session_holder["s"].msg_sink = msg_sink
            status.config(text="已连接")
        else:
            root.after(100, attach_sink)
    attach_sink()

    def tick() -> None:
        try:
            img = frame_q.get_nowait()
            photo = ImageTk.PhotoImage(Image.fromarray(img))
            canvas.configure(image=photo)
            canvas.image = photo  # 防 GC
            lat = latest_lat["v"]
            if lat is not None and "授权" not in status.cget("text"):
                status.config(text=f"已连接 | 编解码延迟(同机) ≈ {lat} ms")
        except Exception:  # noqa: BLE001
            pass
        root.after(30, tick)

    tick()
    root.mainloop()
    return 0


def main() -> None:
    ap = argparse.ArgumentParser(description="vbcpet sim (P4 软件替身)")
    ap.add_argument("--url", default="http://127.0.0.1:8080/whip")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--headless", action="store_true", help="无界面冒烟 (默认)")
    mode.add_argument("--gui", action="store_true", help="Tkinter 交互窗口")
    ap.add_argument("--duration", type=float, default=8.0, help="headless 运行秒数")
    ap.add_argument("--auto-key", default="yes", help="headless 下对授权请求自动按哪个键")
    ap.add_argument("--say", default=None, help="headless 下模拟说一句指令 (触发代理任务)")
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

    if args.gui:
        raise SystemExit(run_gui(args.url))
    raise SystemExit(asyncio.run(run_headless(args.url, args.duration, args.auto_key, args.say)))


if __name__ == "__main__":
    main()
