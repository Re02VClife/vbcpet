/** vbcpet DataChannel protocol — firmware C 实现 (cJSON + ESP-IDF) */
#include "protocol.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

/* ── 内部 ── */
static uint64_t _now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static uint32_t _next_seq(void) {
    static uint32_t s = 0;
    return ++s;
}

static char *_finish_msg(cJSON *root) {
    cJSON_AddNumberToObject(root, "ts", (double)_now_ms());
    char *raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return raw;
}

/* ── 构造 (P4 -> PC) ── */
char *vbc_build_hello(const char *fw_ver, const char *board, const char **caps, int caps_count) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "t", VBC_T_HELLO);
    cJSON_AddNumberToObject(root, "seq", _next_seq());
    cJSON_AddStringToObject(root, "fw", fw_ver);
    cJSON_AddStringToObject(root, "board", board);
    cJSON *arr = cJSON_CreateStringArray(caps, caps_count);
    cJSON_AddItemToObject(root, "caps", arr);
    return _finish_msg(root);
}

char *vbc_build_heartbeat(int rssi, uint32_t free_psram) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "t", VBC_T_HEARTBEAT);
    cJSON_AddNumberToObject(root, "seq", _next_seq());
    cJSON_AddNumberToObject(root, "rssi", rssi);
    cJSON_AddNumberToObject(root, "free_psram", (double)free_psram);
    return _finish_msg(root);
}

char *vbc_build_button(const char *key, const char *confirm_id) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "t", VBC_T_BUTTON);
    cJSON_AddNumberToObject(root, "seq", _next_seq());
    cJSON_AddStringToObject(root, "key", key);
    cJSON_AddStringToObject(root, "event", "press");
    if (confirm_id) cJSON_AddStringToObject(root, "confirm_id", confirm_id);
    return _finish_msg(root);
}

char *vbc_build_dock(const char *slot, bool present) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "t", VBC_T_DOCK);
    cJSON_AddNumberToObject(root, "seq", _next_seq());
    cJSON_AddStringToObject(root, "slot", slot);
    cJSON_AddBoolToObject(root, "present", present);
    return _finish_msg(root);
}

char *vbc_build_user_text(const char *text) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "t", VBC_T_USER_TEXT);
    cJSON_AddNumberToObject(root, "seq", _next_seq());
    cJSON_AddStringToObject(root, "text", text);
    return _finish_msg(root);
}

/* ── 解析 (PC -> P4) ── */
static char _type_buf[64];

const char *vbc_parse_type(const char *json) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return NULL;
    cJSON *t = cJSON_GetObjectItem(root, "t");
    if (!t || !cJSON_IsString(t)) { cJSON_Delete(root); return NULL; }
    strncpy(_type_buf, t->valuestring, sizeof(_type_buf) - 1);
    _type_buf[sizeof(_type_buf) - 1] = '\0';
    cJSON_Delete(root);
    return _type_buf;
}

bool vbc_parse_panel(const char *json, vbc_tokens_t *out, double *out_cost_usd) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *tk = cJSON_GetObjectItem(root, "tokens");
    if (tk) {
        out->input  = (int)cJSON_GetObjectItem(tk, "input")->valuedouble;
        out->output = (int)cJSON_GetObjectItem(tk, "output")->valuedouble;
        cJSON *cr = cJSON_GetObjectItem(tk, "cache_read");
        out->cache_read = cr ? (int)cr->valuedouble : 0;
        cJSON *tot = cJSON_GetObjectItem(tk, "total");
        out->total = tot ? (int)tot->valuedouble : out->input + out->output;
    }
    cJSON *cost = cJSON_GetObjectItem(root, "cost_usd");
    if (out_cost_usd && cost) *out_cost_usd = cost->valuedouble;
    cJSON_Delete(root);
    return true;
}

bool vbc_parse_prompt_confirm(const char *json, char **out_id, char **out_reason) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *reason = cJSON_GetObjectItem(root, "reason");
    if (id && cJSON_IsString(id)) *out_id = strdup(id->valuestring);
    if (reason && cJSON_IsString(reason)) *out_reason = strdup(reason->valuestring);
    cJSON_Delete(root);
    return true;
}

bool vbc_parse_work_status(const char *json, char **out_state, char **out_detail) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *st = cJSON_GetObjectItem(root, "state");
    cJSON *dt = cJSON_GetObjectItem(root, "detail");
    if (st && cJSON_IsString(st)) *out_state = strdup(st->valuestring);
    if (out_detail && dt && cJSON_IsString(dt)) *out_detail = strdup(dt->valuestring);
    else if (out_detail) *out_detail = NULL;
    cJSON_Delete(root);
    return true;
}

bool vbc_parse_say(const char *json, char **out_text) {
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    cJSON *text = cJSON_GetObjectItem(root, "text");
    if (text && cJSON_IsString(text)) *out_text = strdup(text->valuestring);
    cJSON_Delete(root);
    return true;
}

/* ═══════ Character Event Protocol (v1, ADR-006) — C 实现 ═══════ */

char *vbc_event_encode(const char *source, const char *evt_type,
                       const char *data_json, uint32_t seq, uint64_t ts_ms) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "source", source);
    cJSON_AddStringToObject(root, "type", evt_type);
    cJSON *data = cJSON_Parse(data_json ? data_json : "{}");
    cJSON_AddItemToObject(root, "data", data ? data : cJSON_CreateObject());
    cJSON_AddNumberToObject(root, "seq", seq);
    cJSON_AddNumberToObject(root, "ts", (double)ts_ms);
    char *raw = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return raw;
}

int vbc_decode_event(const char *json, vbc_char_event_t *event) {
    if (!json || !event) return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    memset(event, 0, sizeof(*event));

    cJSON *src = cJSON_GetObjectItem(root, "source");
    if (src && cJSON_IsString(src)) {
        event->source = strdup(src->valuestring);
        event->type   = strdup(cJSON_GetObjectItem(root, "type") ?
            cJSON_GetObjectItem(root, "type")->valuestring : "");
        cJSON *data = cJSON_GetObjectItem(root, "data");
        event->data_json = data ? cJSON_PrintUnformatted(data) : strdup("{}");
        event->seq  = (uint32_t)cJSON_GetObjectItem(root, "seq")->valuedouble;
        event->ts_ms = (uint64_t)cJSON_GetObjectItem(root, "ts")->valuedouble;
    } else {
        /* v0 flat — 映射到 event */
        cJSON *t = cJSON_GetObjectItem(root, "t");
        const char *flat_type = (t && cJSON_IsString(t)) ? t->valuestring : "";
        event->source = strdup(""); /* 填 source 由调用者根据事件类型推导 */
        event->type   = strdup(flat_type);
        /* data = 除 t/seq/ts 外的所有字段 */
        cJSON *data = cJSON_Duplicate(root, 1);
        if (data) {
            cJSON_DeleteItemFromObject(data, "t");
            cJSON_DeleteItemFromObject(data, "seq");
            cJSON_DeleteItemFromObject(data, "ts");
            event->data_json = cJSON_PrintUnformatted(data);
            cJSON_Delete(data);
        } else {
            event->data_json = strdup("{}");
        }
        event->seq  = (uint32_t)(cJSON_GetObjectItem(root, "seq") ?
            cJSON_GetObjectItem(root, "seq")->valuedouble : 0);
        event->ts_ms = (uint64_t)(cJSON_GetObjectItem(root, "ts") ?
            cJSON_GetObjectItem(root, "ts")->valuedouble : 0);
    }
    cJSON_Delete(root);
    return 0;
}

void vbc_event_free(vbc_char_event_t *event) {
    if (!event) return;
    free(event->source);
    free(event->type);
    free(event->data_json);
    memset(event, 0, sizeof(*event));
}

bool vbc_is_event(const char *json) {
    if (!json) return false;
    cJSON *root = cJSON_Parse(json);
    if (!root) return false;
    bool ok = cJSON_HasObjectItem(root, "source");
    cJSON_Delete(root);
    return ok;
}
