"""agent_bridge — 中转站: 在"PC 上选定的对话窗口"和硬件之间互相传话

不做 resume (重新加载整个会话上下文, 缓存未命中, 成本高);
而是直接操作当前 VS Code 对话窗口 (UI 自动化): 把文字注入输入框并发送,
让当前 Agent (内存中就有完整上下文) 处理, 中转站只负责传话 + 监工。

流程: 硬件 → ASR → 文字 → inject_to_inputbox() → Agent 工作
      → poll_session() 轮询会话文件实时监工 → 结果 shorten() → TTS/字幕 → 硬件

依赖: pywin32 (win32gui/win32api/win32con/win32clipboard)
"""
from __future__ import annotations

import json
import os
import sys
import time
from pathlib import Path

# Windows 控制台 GBK 打印 emoji/中文会崩 → 强制 UTF-8 + errors=replace
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

try:
    from emotion_relay import relay  # noqa: F401  (情绪事件中继 → 双显 hub)
    _RELAY_OK = True
except Exception:  # noqa: BLE001
    relay = None  # type: ignore[assignment]
    _RELAY_OK = False

try:
    import win32gui
    import win32api
    import win32con
    import win32clipboard
    _WIN32_OK = True
except Exception:  # noqa: BLE001
    _WIN32_OK = False


# ── 窗口/输入注入 ──

def find_vscode_window() -> int | None:
    """找 VS Code 主窗口 hwnd (按类名/标题, 取可见的)"""
    if not _WIN32_OK:
        return None
    found = []

    def cb(h: int, _):  # noqa: ANN001
        if win32gui.IsWindowVisible(h):
            cls = win32gui.GetClassName(h)
            title = win32gui.GetWindowText(h)
            hay = (cls + " " + title).lower()
            if "code" in hay or "visual studio" in hay:
                found.append((h, cls, title))
    win32gui.EnumWindows(cb, None)
    if found:
        print(f"[bridge] 候选窗口 {len(found)} 个:")
        for h, cls, t in found[:6]:
            print(f"    hwnd={h}  cls={cls}  title={t[:70]}")
    # 优先 VS Code 主窗口 (类名含 Visual Studio Code)
    for h, cls, t in found:
        if "visual studio code" in cls.lower() or "visual studio code" in t.lower():
            print(f"[bridge] → 选中主窗口 hwnd={h}")
            return h
    # 其次当前项目窗口 (标题含 vbcpet)
    for h, cls, t in found:
        if "vbcpet" in t.lower():
            print(f"[bridge] → 选中项目窗口 hwnd={h}")
            return h
    return found[0][0] if found else None


def _clipboard_put(text: str) -> None:
    win32clipboard.OpenClipboard()
    win32clipboard.EmptyClipboard()
    win32clipboard.SetClipboardText(text, win32clipboard.CF_UNICODETEXT)
    win32clipboard.CloseClipboard()


def _hotkey(*keys: int) -> None:
    """同时按一组键 (按下再按释放顺序反向)"""
    for k in keys:
        win32api.keybd_event(k, 0, 0, 0)
    for k in reversed(keys):
        win32api.keybd_event(k, 0, win32con.KEYEVENTF_KEYUP, 0)


def _tap(key: int) -> None:
    win32api.keybd_event(key, 0, 0, 0)
    win32api.keybd_event(key, 0, win32con.KEYEVENTF_KEYUP, 0)


def inject_to_inputbox(text: str, hwnd: int | None = None, echo: bool = True) -> bool:
    """激活 VS Code → 命令面板聚焦 Claude Code 输入框 → 粘贴消息 → 回车发送"""
    if not _WIN32_OK:
        print("[bridge] 需要 pywin32 (pip install pywin32)")
        return False
    hwnd = hwnd or find_vscode_window()
    if not hwnd:
        print("[bridge] 未找到 VS Code 窗口")
        return False
    prev = win32gui.GetForegroundWindow()
    import ctypes
    _user32 = ctypes.windll.user32
    try:
        _user32.AllowSetForegroundWindow(-1)
        win32gui.ShowWindow(hwnd, win32con.SW_RESTORE)
        win32gui.SetForegroundWindow(hwnd)
        time.sleep(0.5)
        if win32gui.GetForegroundWindow() != hwnd:      # 前台锁 → ALT 技巧
            _tap(win32con.VK_MENU)
            win32gui.SetForegroundWindow(hwnd)
            time.sleep(0.4)
        print(f"[bridge] 前台={'OK' if win32gui.GetForegroundWindow() == hwnd else 'FAIL'}")
        time.sleep(0.2)
        # ① 命令面板 → 聚焦 Claude Code 输入框 (Claude Code: Focus input)
        _clipboard_put("claude code: focus input")
        _hotkey(win32con.VK_CONTROL, win32con.VK_SHIFT, ord("P"))   # Ctrl+Shift+P
        time.sleep(0.8)
        _hotkey(win32con.VK_CONTROL, ord("V"))                       # 粘贴命令名
        time.sleep(0.5)
        _tap(win32con.VK_RETURN)                                     # 执行
        time.sleep(0.8)
        # ② 粘贴实际消息 → 回车发送
        _clipboard_put(text)
        _hotkey(win32con.VK_CONTROL, ord("V"))
        time.sleep(0.4)
        _tap(win32con.VK_RETURN)
        if echo:
            print(f"[bridge] 已注入 → Claude Code 输入框: {text[:60]}{'…' if len(text) > 60 else ''}")
        return True
    except Exception as e:  # noqa: BLE001
        print(f"[bridge] 注入失败: {e}")
        return False
    finally:
        try:
            win32gui.SetForegroundWindow(prev)
        except Exception:
            pass


# ── 会话文件监工 ──

def _session_path(project: str, session_id: str) -> Path:
    return Path.home() / ".claude" / "projects" / project / f"{session_id}.jsonl"


def extract_assistant_text(msg: dict) -> str:
    """从一条 session 消息 (message dict) 提取 assistant 自然语言文本"""
    content = msg.get("content", [])
    if isinstance(content, list):
        return " ".join(c.get("text", "") for c in content
                        if isinstance(c, dict) and c.get("type") == "text").strip()
    if isinstance(content, str):
        return content.strip()
    return ""


def poll_replies(project: str, session_id: str, last_offset: int = 0,
                 idle_secs: float = 12.0, max_wait: float = 600.0):
    """轮询会话文件, 生成 (offset, assistant文本) 增量。

    只返回 **新增的** assistant 自然语言文本 (相对 last_offset)。
    策略: 检测到新 user 消息(注入的指令)后开始等回复;
    连续 idle_secs 无新消息 → 停止 (Agent 工作完成)。
    若一直没看到 user 消息 → 30s 兜底退出 (注入可能没进会话)。
    """
    path = _session_path(project, session_id)
    offset = last_offset
    last_new = time.time()
    t0 = time.time()
    saw_user = False
    while time.time() - t0 < max_wait:
        if not path.exists():
            time.sleep(0.5)
            continue
        size = path.stat().st_size
        if size > offset:
            with open(path, encoding="utf-8") as f:
                f.seek(offset)
                new_data = f.read()
            offset = size
            for line in new_data.splitlines():
                if not line.strip():
                    continue
                try:
                    d = json.loads(line)
                except json.JSONDecodeError:
                    continue
                msg = d.get("message", {})
                if not isinstance(msg, dict):
                    continue
                role = msg.get("role")
                if role == "assistant":
                    text = extract_assistant_text(msg)
                    if text:
                        yield offset, text
                        last_new = time.time()
                elif role == "user":
                    saw_user = True
                    last_new = time.time()      # 新指令到达 → 重置空闲时钟
        if saw_user:
            if time.time() - last_new > idle_secs:
                break                          # 回复后空闲 → 完成
        else:
            if time.time() - t0 > 30:
                break                          # 没看到指令 → 注入可能没进会话
        time.sleep(0.5)


# ── 结果过滤/缩短 ──

def shorten(text: str, max_len: int = 200) -> str:
    """把回复缩短为硬件字幕可用: 取末尾, 去 markdown/多余空行"""
    import re
    t = re.sub(r"[#*_`>|]", "", text)          # 去 markdown 符号
    t = re.sub(r"\n{3,}", "\n\n", t).strip()    # 压缩空行
    if len(t) <= max_len:
        return t
    # 取末尾 max_len 字符 (回复的结论通常在末尾)
    return "…" + t[-max_len:]


# ── 命令行测试入口 ──
if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="agent_bridge 中转站测试")
    ap.add_argument("--inject", help="注入这条消息到 VS Code 输入框并发送")
    ap.add_argument("--session", default=None, help="监工的会话 id (默认最近)")
    args = ap.parse_args()

    if args.inject:
        ok = inject_to_inputbox(args.inject)
        print(f"注入结果: {ok}")
        if not ok:
            raise SystemExit(1)
    else:
        print("未提供 --inject, 仅列出可用函数。用法示例:")
        print("  python agent_bridge.py --inject '帮我总结一下项目'")
