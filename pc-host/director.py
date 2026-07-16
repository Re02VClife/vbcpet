"""导演/压缩器 — 把大段 AI 代理输出压成短语音 + 动作/情绪标签。

设计（对应 ADR-003）：
- 可插拔后端（OpenAI-兼容 API / 规则启发式 / 后续本地模型）。
- Director.compress(text, work_status) → CompressedResult
- 零依赖时回退到 HeuristicBackend（纯规则，无 API 调用）。

集成点（在 host.py 的 _setup_channel_driver 中）：
  代理 on_say(raw_text) → director.compress(raw_text) → DataChannel say + avatar
"""
from __future__ import annotations

import abc
import logging
from dataclasses import dataclass

log = logging.getLogger("director")

# 可选的动作 / 情绪枚举
ACTIONS = {"idle", "think", "wave", "nod", "celebrate", "worry", "sleep"}
EMOTIONS = {"neutral", "happy", "sad", "worried", "excited"}


@dataclass
class CompressedResult:
    speech: str
    action: str = "idle"
    emotion: str = "neutral"
    intensity: float = 0.5

    def __post_init__(self) -> None:
        if self.action not in ACTIONS:
            self.action = "idle"
        if self.emotion not in EMOTIONS:
            self.emotion = "neutral"
        self.intensity = max(0.0, min(1.0, self.intensity))


class DirectorBackend(abc.ABC):
    @abc.abstractmethod
    async def compress(self, text: str, work_status: str) -> CompressedResult:
        """把原始文本压缩为适合朗读+控制动作的结构。"""

    @abc.abstractmethod
    async def close(self) -> None:
        """释放资源（如 HTTP 连接池）。"""


class Director:
    """导演：组合后端，提供统一的 compress() 接口。"""

    def __init__(self, backend: DirectorBackend) -> None:
        self.backend = backend

    async def compress(self, text: str, work_status: str = "running") -> CompressedResult:
        """压缩原始输出文本。太短的直接返回；否则委托后端。"""
        stripped = text.strip()
        if not stripped:
            return CompressedResult("…", action="idle", emotion="neutral")
        if len(stripped) <= 50:
            return CompressedResult(stripped, action="idle", emotion="neutral")
        try:
            return await self.backend.compress(stripped, work_status)
        except Exception as exc:  # noqa: BLE001
            log.warning("导演后端异常，回退规则: %s", exc)
            return _heuristic_compress(stripped, work_status)

    async def close(self) -> None:
        await self.backend.close()

    @classmethod
    def from_config(cls, config: dict | None = None) -> Director:
        """根据配置字典构建导演。无配置则使用纯规则启发式。
        config 示例:
          {"kind": "openai", "api_base": "https://api.deepseek.com/v1", "api_key": "...", "model": "deepseek-chat"}
          或 {"kind": "heuristic"}
        """
        from director_backends import HeuristicBackend, OpenAIBackend  # noqa: PLC0415
        cfg = config or {}
        kind = cfg.get("kind", "heuristic")
        if kind == "openai":
            return cls(OpenAIBackend(
                api_base=cfg.get("api_base", ""),
                api_key=cfg.get("api_key", ""),
                model=cfg.get("model", "deepseek-chat"),
            ))
        return cls(HeuristicBackend())


def _heuristic_compress(text: str, work_status: str) -> CompressedResult:
    """纯规则启发式压缩（无 API 调用，零成本 fallback）"""
    import re
    clean = re.sub(r'```[\s\S]*?```', '', text)   # 去代码块
    clean = re.sub(r'`[^`]+`', '', clean)          # 去行内代码
    clean = re.sub(r'\n{2,}', '\n', clean).strip()
    if not clean:
        clean = text.strip()[:100]

    # 提取首句或前 100 字
    speech = clean.split('\n')[0].strip()
    if len(speech) > 120:
        speech = speech[:120] + "…"
    if not speech:
        speech = clean[:100]

    # 情绪判断
    lower = clean.lower()
    if any(kw in lower for kw in ("通过", "成功", "完成", "pass", "ok", "success")):
        return CompressedResult(speech, action="celebrate", emotion="happy", intensity=0.9)
    if any(kw in lower for kw in ("错误", "失败", "error", "fail", "denied", "拒绝")):
        return CompressedResult(speech, action="worry", emotion="worried", intensity=0.8)
    if any(kw in lower for kw in ("思考", "分析", "think", "analyze", "评估")):
        return CompressedResult(speech, action="think", emotion="neutral", intensity=0.6)
    if work_status == "done":
        return CompressedResult(speech, action="idle", emotion="happy", intensity=0.5)
    if work_status == "error":
        return CompressedResult(speech, action="worry", emotion="sad", intensity=0.7)
    if work_status == "blocked":
        return CompressedResult(speech, action="idle", emotion="worried", intensity=0.5)
    return CompressedResult(speech, action="idle", emotion="neutral", intensity=0.5)
