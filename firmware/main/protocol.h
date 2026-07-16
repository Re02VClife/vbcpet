/** vbcpet DataChannel control protocol + Character Event Protocol (firmware side)
 *
 * 对应 docs/protocol.md (v1), common/protocol.py, docs/decisions.md ADR-006/007。
 * 两层兼容:
 *   v0 flat: {"t":"say","seq":1,"ts":…,"text":"…"}
 *   v1 event: {"source":"agent","type":"speech","data":{…},"seq":1,"ts":…}
 * vbc_decode_event() 自动识别两层。
 * 依赖 cJSON (ESP-IDF 已内建)。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── DataChannel 标签 (ADR-007) ── */
#define VBC_CH_CTRL     "ctrl"      /* 可靠控制通道 */
#define VBC_CH_REALTIME "realtime"  /* 不可靠实时通道（动画/表情） */

/* ── 消息类型常量 (v0 flat, 保留向下兼容) ── */
#define VBC_T_SPEECH_STATE    "speech_state"
#define VBC_T_SAY             "say"
#define VBC_T_AVATAR          "avatar"
#define VBC_T_WORK_STATUS     "work_status"
#define VBC_T_PANEL           "panel"
#define VBC_T_PROMPT_CONFIRM  "prompt_confirm"
#define VBC_T_BUTTON          "button"
#define VBC_T_DOCK            "dock"
#define VBC_T_VAD             "vad"
#define VBC_T_USER_TEXT       "user_text"
#define VBC_T_HELLO           "hello"
#define VBC_T_HEARTBEAT       "heartbeat"

/* ── Character Event 事件源 (v1, ADR-006) ── */
#define VBC_SOURCE_AGENT        "agent"
#define VBC_SOURCE_TOOL         "tool"
#define VBC_SOURCE_HARDWARE     "hardware"
#define VBC_SOURCE_USER         "user"
#define VBC_SOURCE_ENVIRONMENT  "environment"

/* ── Character Event 事件类型 (v1) ── */
#define VBC_EVT_SPEECH       "speech"
#define VBC_EVT_EMOTION      "emotion"
#define VBC_EVT_STATUS       "status"
#define VBC_EVT_PANEL        "panel"
#define VBC_EVT_PERMISSION   "permission"
#define VBC_EVT_TOOL_START   "tool_start"
#define VBC_EVT_TOOL_END     "tool_end"
#define VBC_EVT_BUTTON       "button"
#define VBC_EVT_PRESENCE     "presence"
#define VBC_EVT_TOUCH        "touch"
#define VBC_EVT_USER_INPUT   "user_input"
#define VBC_EVT_VAD          "vad"
#define VBC_EVT_HELLO        "hello"
#define VBC_EVT_HEARTBEAT    "heartbeat"
#define VBC_EVT_SPEECH_STATE "speech_state"

/* ── 风险等级 (ADR-008) ── */
typedef enum { VBC_RISK_LOW = 0, VBC_RISK_MEDIUM = 1, VBC_RISK_HIGH = 2 } vbc_risk_level_t;

/* 按键 / 槽位 枚举 */
#define VBC_KEY_YES          "yes"
#define VBC_KEY_NO           "no"
#define VBC_KEY_YES_ALWAYS   "yes_always"
#define VBC_KEY_PANEL        "panel"
#define VBC_DOCK_PORTRAIT    "portrait"
#define VBC_DOCK_LANDSCAPE   "landscape"

/* ── CharacterEvent 结构体 ── */
typedef struct {
    char *source;         /* agent|tool|hardware|user|environment */
    char *type;           /* speech|emotion|status|panel|… */
    char *data_json;      /* data 字段的 JSON 字符串 (cJSON 惰性解析) */
    uint32_t seq;
    uint64_t ts_ms;
} vbc_char_event_t;

/* ── 消息结构体 (v0 flat, 向下兼容) ── */
typedef struct { uint32_t seq; uint64_t ts_ms; } vbc_msg_hdr_t;
typedef struct { char *key; char *confirm_id; } vbc_button_t;
typedef struct { char *slot; bool present; } vbc_dock_t;
typedef struct { int input; int output; int cache_read; int total; } vbc_tokens_t;

/* ── CharacterEvent API (v1) ── */

/** 构造一条 CharacterEvent JSON。返回 malloc 的字符串，需 free()。 */
char *vbc_event_encode(const char *source, const char *evt_type,
                       const char *data_json, uint32_t seq, uint64_t ts_ms);

/** 解析一条消息为 CharacterEvent。自动识别 v0 flat 和 v1 event。
 *  返回 0 成功（调用者需 free(event->source), free(event->type), free(event->data_json)）。
 *  返回非 0 表示 JSON 非法。 */
int vbc_decode_event(const char *json, vbc_char_event_t *event);

/** 释放 vbc_char_event_t 内部分配的字符串。 */
void vbc_event_free(vbc_char_event_t *event);

/** 快速判断是否 v1 CharacterEvent (有 source 字段)。 */
bool vbc_is_event(const char *json);

/* ── 构造 API (P4 -> PC, v0 flat) ── */
char *vbc_build_hello(const char *fw_ver, const char *board, const char **caps, int caps_count);
char *vbc_build_heartbeat(int rssi, uint32_t free_psram);
char *vbc_build_button(const char *key, const char *confirm_id);
char *vbc_build_dock(const char *slot, bool present);
char *vbc_build_user_text(const char *text);

/* ── 解析 API (PC -> P4, v0 flat + v1 event) ── */
const char *vbc_parse_type(const char *json);
bool vbc_parse_panel(const char *json, vbc_tokens_t *out, double *out_cost_usd);
bool vbc_parse_prompt_confirm(const char *json, char **out_id, char **out_reason);
bool vbc_parse_work_status(const char *json, char **out_state, char **out_detail);
bool vbc_parse_say(const char *json, char **out_text);

#ifdef __cplusplus
}
#endif
