"""导演后端实现：OpenAI-兼容 API + 规则启发式 (fallback)。"""

from __future__ import annotations

import json
import logging
import re

from director import CompressedResult, DirectorBackend, _heuristic_compress

log = logging.getLogger("director")

SYSTEM_PROMPT = """你是一个"桌宠导演"。把 AI 编程助手的冗长输出压缩成：
1. 适合语音朗读的口语化中文短句 (≤100字)，去掉代码块、Markdown、符号
2. 当前该做的动作和表情

输出纯 JSON（不要markdown代码块）:
{"speech": "...", "action": "...", "emotion": "...", "intensity": 0.8}

动作: idle(待机) think(思考) wave(挥手) nod(点头) celebrate(庆祝) worry(担心) sleep(休眠)
情绪: neutral(中性) happy(开心) sad(难过) worried(担忧) excited(兴奋)
intensity: 0.0~1.0

规则:
- 成功/完成 → celebrate + happy
- 错误/失败 → worry + worried(sad)
- 思考/分析 → think + neutral
- 普通回复 → idle + neutral, speech精简到核心信息
- speech 必须口语化中文，适合朗读"""


class HeuristicBackend(DirectorBackend):
    """纯规则，零成本，离线可用。"""

    async def compress(self, text: str, work_status: str) -> CompressedResult:
        return _heuristic_compress(text, work_status)

    async def close(self) -> None:
        pass


class OpenAIBackend(DirectorBackend):
    """调用任意 OpenAI-兼容 /v1/chat/completions 端点。

    适用：DeepSeek (经 ccswitch)、OpenAI、本地 vLLM/ollama 等。
    """

    def __init__(self, api_base: str, api_key: str, model: str = "deepseek-chat") -> None:
        self.api_base = api_base.rstrip("/")
        self.api_key = api_key
        self.model = model
        self._session = None

    async def _ensure_session(self):
        import aiohttp  # 延迟导入，仅此后端需要
        if self._session is None:
            self._session = aiohttp.ClientSession()

    async def compress(self, text: str, work_status: str) -> CompressedResult:
        await self._ensure_session()
        url = f"{self.api_base}/chat/completions"
        headers = {"Authorization": f"Bearer {self.api_key}", "Content-Type": "application/json"}
        payload = {
            "model": self.model,
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                {"role": "user", "content": f"工作状态: {work_status}\n\n原文:\n{text}"},
            ],
            "max_tokens": 200,
            "temperature": 0.3,
            "response_format": {"type": "json_object"},
        }
        async with self._session.post(url, json=payload, headers=headers, timeout=aiohttp.ClientTimeout(total=15)) as resp:
            if resp.status != 200:
                body = await resp.text()
                log.warning("导演 API HTTP %d: %s", resp.status, body[:200])
                raise RuntimeError(f"导演 API 错误 {resp.status}")
            data = await resp.json()

        raw = data["choices"][0]["message"]["content"].strip()
        # 去可能的 ```json 包覆
        raw = re.sub(r'^```(?:json)?\s*', '', raw)
        raw = re.sub(r'\s*```$', '', raw)
        obj = json.loads(raw)
        return CompressedResult(
            speech=str(obj.get("speech", "")),
            action=str(obj.get("action", "idle")),
            emotion=str(obj.get("emotion", "neutral")),
            intensity=float(obj.get("intensity", 0.5)),
        )

    async def close(self) -> None:
        if self._session:
            await self._session.close()
            self._session = None
