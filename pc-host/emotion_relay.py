"""emotion_relay.py — 情绪事件中继器 (把「对话→情绪」事件广播到双显宿主)

作用：统一「对话 / 状态 → 情绪」这个事件流的出口，让桌宠的桌面窗口 (Electron)
      和 P4 硬件屏幕 (串口推流) 同时被驱动 —— 也就是 desktop_pet_display.py 的
      EventBus 所消费的事件源。

设计要点：
  1. 零依赖、进程内可复用。任何产生情绪的生产者 (agent_bridge / serial_bridge /
     director / chat_driver / host) 都可以就地调用 relay.emit(...)。
  2. 同时支持两种消费方式，按需择一：
     a) In-process: 注册本地 subscriber 回调 (同进程直接广播)。
     b) Out-of-process: 以「一行 JSON」写到可配置的管道 (默认 stdout)，供
        desktop_pet_display.py 的 stdin JSON 源、或 Electron main.js 的
        startStdinBridge 消费。契约与两者完全一致：
            {"emotion":"happy","duration":800}
            {"mood":70}
            {"message":"我喜欢你"}
            {"motion":"TapBody"}
            {"expression":"happy", "value":0.8}
     c) Named pipe / socket: 把事件推到一个本地 Unix socket / Windows 命名管道，
        供独立进程的 hub 长连订阅 (见 start_pipe_server / push_to_pipe)。

  3. soullink 语义兼容：情绪名 ('happy'/'sad'/'angry'/'surprised'/'neutral'/
     'excited'/'curious'/'shy'/'confused' 等) 直接透传，由渲染页各自的
     applyEmotion 映射到 Live2D 参数表 / PNG 状态 / VRM blendshape。

用法示例 (in-process):
    from emotion_relay import relay
    relay.subscribe(lambda ev: print(ev))       # 本地消费
    relay.emit_emotion("happy", 800)
    relay.emit_message("我今天很开心")          # → 自动本地分类 + 广播

用法示例 (out-of-process, 发给 desktop_pet_display.py):
    # 生产者侧 (serial_bridge / agent_bridge):
    relay = EmotionRelay(output=sys.stdout)     # 一行 JSON 写到 stdout
    relay.emit_emotion("excited")

    # 消费侧 (hub):
    python desktop_pet_display.py --mode live2d   # 从 stdin 读 JSON 行
"""
from __future__ import annotations

import json
import socket
import threading
from typing import Any, Callable, TextIO

# 情绪归一化: 各事件源 (director / soullink / claude 状态) 的词汇 → 统一名
# 渲染页三套管线 (Live2D/PNG/VRM) 都用这批统一名做映射。
_EMOTION_ALIASES: dict[str, str] = {
    # soullink / Claude 状态词汇 → 标准名
    "excited": "happy",
    "fun": "happy",
    "worried": "sad",
    "worry": "sad",
    "curious": "surprised",
    "confused": "surprised",
    "shy": "sad",
    "bashful": "sad",
    "think": "neutral",
    "thinking": "neutral",
    "idle": "neutral",
}

# 情绪本地分类 (无 soullink classifier 时的最小回退，与 render*.html 内置一致)
def classify_text(text: str) -> str:
    t = str(text)
    if any(k in t for k in ("错", "失败", "error", "fail", "拒绝", "不能", "糟糕", "坏")):
        return "sad"
    if any(k in t for k in ("通过", "成功", "完成", "pass", "ok", "success", "棒", "好", "哈哈", "开心", "谢谢", "喜欢")):
        return "happy"
    if any(k in t for k in ("思考", "分析", "评估", "think", "analyze")):
        return "neutral"
    if any(k in t for k in ("啊", "哇", "竟然", "什么", "惊", "天")):
        return "surprised"
    if any(k in t for k in ("气", "烦", "讨厌", "烂", "骂", "怒", "angry")):
        return "angry"
    return "neutral"


def normalize_emotion(name: str) -> str:
    """把事件源的任意情绪词汇归一化到渲染页三管线都能识别的一组。"""
    n = str(name).strip().lower()
    return _EMOTION_ALIASES.get(n, n if n in (
        "neutral", "happy", "sad", "angry", "surprised") else "neutral")


_DEFAULT_EMOTIONS = {"neutral", "happy", "sad", "angry", "surprised"}


class EmotionRelay:
    """情绪事件发射器。可配置输出目标：stdout / 命名管道 / socket / 本地 subscriber。"""

    def __init__(self, output: TextIO | None = None, pipe_path: str | None = None,
                 host: str = "127.0.0.1", port: int = 0, quiet: bool = False) -> None:
        self._subscribers: list[Callable[[dict], None]] = []
        self._output = output
        self._pipe_path = pipe_path
        self._host = host
        self._port = port
        self._quiet = quiet
        self._sock: socket.socket | None = None
        self._lock = threading.Lock()
        self._clients: list[socket.socket] = []

    # ── 订阅 (in-process) ──
    def subscribe(self, fn: Callable[[dict], None]) -> None:
        """注册本地消费者，事件以 dict 形式回调。"""
        if fn not in self._subscribers:
            self._subscribers.append(fn)

    def unsubscribe(self, fn: Callable[[dict], None]) -> None:
        if fn in self._subscribers:
            self._subscribers.remove(fn)

    # ── 语义化 emit ──
    def _emit(self, event: dict) -> None:
        # 1) 本地订阅者
        for fn in list(self._subscribers):
            try:
                fn(event)
            except Exception as e:  # noqa: BLE001
                if not self._quiet:
                    print(f"[relay] subscriber 异常: {e}")
        # 2) stdout 一行 JSON (供 hub stdin 消费)
        if self._output:
            try:
                self._output.write(json.dumps(event, ensure_ascii=False) + "\n")
                self._output.flush()
            except Exception as e:  # noqa: BLE001
                if not self._quiet:
                    print(f"[relay] stdout 写入失败: {e}")
        # 3) 命名管道 / socket
        self._push_pipe(json.dumps(event, ensure_ascii=False))

    def emit_emotion(self, name: str, duration: float = 800) -> None:
        self._emit({"emotion": normalize_emotion(name), "duration": duration})

    def emit_mood(self, value: float) -> None:
        self._emit({"mood": value})

    def emit_message(self, text: str, emotion: str | None = None) -> dict:
        """消息事件: 附带本地分类结果 (若不显式给 emotion)。"""
        emo = normalize_emotion(emotion) if emotion else classify_text(text)
        self._emit({"message": str(text), "emotion": emo})
        return {"emotion": emo}

    def emit_motion(self, name: str) -> None:
        self._emit({"motion": name})

    def emit_expression(self, name: str, value: float) -> None:
        self._emit({"expression": name, "value": value})

    # ── 命名管道 / socket (进程间长连) ──
    def start_pipe_server(self) -> None:
        """起一个本地 TCP socket，供独立 hub 进程长连订阅事件流。
        生产者 listen，消费者 connect 后逐行读 JSON。"""
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self._host, self._port))
        self._sock.listen(4)
        actual_port = self._sock.getsockname()[1]
        threading.Thread(target=self._accept_loop, daemon=True).start()
        if not self._quiet:
            print(f"[relay] 情绪事件 TCP 服务: {self._host}:{actual_port}")

    def _accept_loop(self) -> None:
        while self._sock:
            try:
                conn, _addr = self._sock.accept()
                self._clients.append(conn)
            except OSError:
                break

    def _push_pipe(self, line: str) -> None:
        if not self._clients:
            return
        dead = []
        for c in self._clients:
            try:
                c.sendall((line + "\n").encode("utf-8"))
            except Exception:  # noqa: BLE001
                dead.append(c)
        for c in dead:
            try:
                self._clients.remove(c)
            except ValueError:
                pass


# ── 全局单例 (进程内默认用；支持 pipe server) ──
relay = EmotionRelay()


__all__ = ["relay", "EmotionRelay",
           "classify_text", "normalize_emotion"]
