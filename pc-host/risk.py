"""Risk Classifier V1 — 工具调用风险三层分级 (ADR-008)。

Low → 自动放行。Medium → 按键确认。High → 强制确认（RGB 红）。
V1 用启发式关键词规则；V2 可接导演 LLM 判定。
"""
from __future__ import annotations

import enum
import re
from typing import Any


class RiskLevel(enum.IntEnum):
    LOW = 0
    MEDIUM = 1
    HIGH = 2


# ── 高风险命令行模式 ──
_HIGH_RISK_COMMANDS = re.compile(
    r'\b(rm\s+-rf|rm\s+-r|sudo\b|chmod\s+777|chown\b|'
    r'curl\s+.*\|\s*(ba)?sh|wget\s+.*\|\s*(ba)?sh|'
    r'git\s+push\s+--force|git\s+push\s+-f|'
    r'dd\s+if=|mkfs\.|fdisk\b|shutdown\b|reboot\b|'
    r'docker\s+rm\s+-f|docker\s+system\s+prune|'
    r'pip\s+uninstall|npm\s+uninstall\s+-g|'
    r'>\s*/dev/[a-z]+|:\s*>\s*\S+)',
    re.IGNORECASE,
)

# ── 高风险文件路径 ──
_HIGH_RISK_PATHS = re.compile(
    r'(/etc/|~?/\.ssh/|~?/\.gnupg/|~?/\.aws/|~?/\.config/|'
    r'\.git/config|\.git/hooks/|'
    r'C:\\Windows\\|C:\\\\Windows\\\\|'
    r'package-lock\.json|yarn\.lock|poetry\.lock)',
    re.IGNORECASE,
)

# ── 只读工具 ──
_READONLY_TOOLS = frozenset({"Read", "Glob", "Grep", "WebSearch", "WebFetch",
                              "TaskOutput", "TodoWrite", "TaskStop"})


def classify(tool_name: str, tool_input: dict[str, Any]) -> tuple[RiskLevel, str]:
    """返回 (风险等级, 原因描述)。"""
    # 只读工具 → 自动放行
    if tool_name in _READONLY_TOOLS:
        return RiskLevel.LOW, "只读操作，自动放行"

    # Bash 命令：检查风险模式
    if tool_name == "Bash":
        cmd = str(tool_input.get("command", "") or "")
        if _HIGH_RISK_COMMANDS.search(cmd):
            return RiskLevel.HIGH, f"高风险命令: {cmd[:60]}"
        if _is_safe_bash(cmd):
            return RiskLevel.LOW, f"安全命令: {cmd[:60]}"
        return RiskLevel.MEDIUM, f"需确认: {cmd[:60]}"

    # Write / Edit：检查目标路径
    if tool_name in ("Write", "Edit"):
        fp = str(tool_input.get("file_path", "") or "")
        if _HIGH_RISK_PATHS.search(fp):
            return RiskLevel.HIGH, f"高风险路径: {fp}"
        return RiskLevel.MEDIUM, f"写入文件: {fp}"

    # Skill / EnterPlanMode / WebFetch 等 — 中等风险（有间接副作用）
    return RiskLevel.MEDIUM, f"确认操作: {tool_name}"


def _is_safe_bash(cmd: str) -> bool:
    """判断 Bash 命令是否为公认安全的（只读/信息查询/简单操作）。"""
    safe_patterns = [
        r'^(echo|ls|cat|head|tail|wc|find|grep|which|pwd|whoami|date|uname|'
        r'git\s+status|git\s+log|git\s+diff|git\s+branch|'
        r'npm\s+test|npm\s+run|npm\s+ls|npm\s+list|'
        r'pip\s+list|pip\s+show|pip\s+freeze|'
        r'python\s+--version|node\s+--version|node\s+-v|'
        r'dir\b|type\b|set\b|'
        r'npx\s+--version|cargo\s+--version|go\s+version)',
    ]
    cmd_stripped = cmd.strip()
    for pat in safe_patterns:
        if re.match(pat, cmd_stripped, re.IGNORECASE):
            return True
    return False


def permission_reason(tool_name: str, tool_input: dict[str, Any], level: RiskLevel) -> str:
    """构造给人看的权限请求描述，含风险等级前缀。"""
    prefix = {RiskLevel.LOW: "[自动]", RiskLevel.MEDIUM: "[确认]", RiskLevel.HIGH: "[⚠️高风险]"}.get(level, "")
    detail = ""
    if tool_name == "Bash":
        detail = str(tool_input.get("command", ""))[:60]
    elif tool_name in ("Write", "Edit"):
        detail = str(tool_input.get("file_path", ""))
    return f"{prefix} {tool_name}: {detail}" if detail else f"{prefix} {tool_name}"
