#include "camera_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OWNS_CAMERA "Photo Mode owns the camera"
#define NEEDS_CLEANUP "Photo camera sharing requires cleanup"

static const TfpCameraCompatValue NIL = {TFP_CAMERA_COMPAT_NIL, 0, 0};

static bool truthy(TfpCameraCompatValue v) { return v.kind != TFP_CAMERA_COMPAT_NIL && v.kind != TFP_CAMERA_COMPAT_FALSE; }
static bool is_boolean(TfpCameraCompatValue v) { return v.kind == TFP_CAMERA_COMPAT_FALSE || v.kind == TFP_CAMERA_COMPAT_TRUE; }
/* Lua's a==b for the values this module compares: nil and Booleans by value, tables and functions
 * by identity. Numbers, texts and userdata carry no value here and never compare equal. */
static bool same(TfpCameraCompatValue a, TfpCameraCompatValue b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case TFP_CAMERA_COMPAT_NIL: case TFP_CAMERA_COMPAT_FALSE: case TFP_CAMERA_COMPAT_TRUE: return true;
    case TFP_CAMERA_COMPAT_TABLE: return a.id == b.id;
    case TFP_CAMERA_COMPAT_FUNCTION: return a.shim || b.shim ? a.shim == b.shim : a.id == b.id;
    default: return false;
  }
}
static bool is_shim(TfpCameraCompatValue v, uint32_t serial) { return v.kind == TFP_CAMERA_COMPAT_FUNCTION && v.shim == serial; }
static TfpCameraCompatValue field(TfpCameraCompat *c, TfpCameraCompatValue table, TfpCameraCompatField name) {
  return c->host->table_field(c->host->user, table, name);
}
static TfpCameraCompatRecord *record_of(TfpCameraCompat *c, uint32_t serial) { return &c->records[serial - 1]; }

/* retail report(), lines 12-16: a reason is logged when it differs from the last one. */
static void report(TfpCameraCompat *c, TfpCameraCompatPhase next_phase, const char *why) {
  char reason[TFP_CAMERA_COMPAT_TEXT], line[TFP_CAMERA_COMPAT_TEXT + 32];
  c->phase = next_phase;
  if (!why) { c->has_message = false; return; }
  snprintf(reason, sizeof reason, "%s", why);
  if (!(c->has_message && strcmp(reason, c->message) == 0) && c->host->log) {
    snprintf(line, sizeof line, "FPV camera sharing: %s", reason);
    c->host->log(c->host->user, line);
  }
  memcpy(c->message, reason, sizeof reason);
  c->has_message = true;
}
static const char *message(const TfpCameraCompat *c) { return c->has_message ? c->message : NULL; }

/* retail reader(setter), lines 17-31: the function the setter holds in its upvalue 'input_reader'. */
static bool reader(TfpCameraCompat *c, TfpCameraCompatValue setter, TfpCameraCompatValue *value, const char **problem) {
  const TfpCameraCompatHost *h = c->host;
  *value = NIL;
  *problem = NULL;
  if (!h->debug_getupvalue) { *problem = "Photo camera reader ownership API unavailable"; return false; }
  for (int index = 1; index <= TFP_CAMERA_COMPAT_UPVALUES; index++) {
    TfpCameraCompatValue found = NIL;
    /* A getupvalue that went away during the loop is a call that raises. */
    TfpCameraCompatUpvalue got = h->debug_getupvalue ? h->debug_getupvalue(h->user, setter, index, &found) : TFP_CAMERA_COMPAT_UPVALUE_RAISED;
    if (got == TFP_CAMERA_COMPAT_UPVALUE_RAISED) { *problem = "Photo camera reader cannot be inspected"; return false; }
    if (got == TFP_CAMERA_COMPAT_UPVALUE_END) break;
    if (got == TFP_CAMERA_COMPAT_UPVALUE_INPUT_READER) {
      if (found.kind != TFP_CAMERA_COMPAT_NIL && found.kind != TFP_CAMERA_COMPAT_FUNCTION) {
        *problem = "Photo camera reader is invalid";
        return false;
      }
      *value = found;
      return true;
    }
  }
  *problem = "Photo camera reader contract changed";
  return false;
}

typedef enum Provider { PROVIDER_NONE, PROVIDER_LOCAL, PROVIDER_LEGACY } Provider;
/* retail provider(), lines 32-57. *api is nil when there is none; *why is its second result. */
static Provider provider(TfpCameraCompat *c, TfpCameraCompatValue *api, const char **why) {
  const TfpCameraCompatHost *h = c->host;
  TfpCameraCompatValue owner = h->photo_camera_ownership(h->user), legacy;
  *api = NIL;
  *why = NULL;
  if (owner.kind != TFP_CAMERA_COMPAT_NIL) {
    if (owner.kind != TFP_CAMERA_COMPAT_TABLE || !is_boolean(field(c, owner, TFP_CAMERA_COMPAT_HELD)) ||
        field(c, owner, TFP_CAMERA_COMPAT_AVAILABLE).kind != TFP_CAMERA_COMPAT_TRUE ||
        field(c, owner, TFP_CAMERA_COMPAT_DISABLED).kind == TFP_CAMERA_COMPAT_TRUE) {
      *why = "Photo camera ownership interface unavailable";
      return PROVIDER_NONE;
    }
    *api = owner;
    return PROVIDER_LOCAL;
  }
  legacy = h->photo_actioncam_compat(h->user);
  if (legacy.kind == TFP_CAMERA_COMPAT_NIL) {
    if (h->actioncam_loaded(h->user)) *why = "ActionCam camera-sharing bridge unavailable";
    return PROVIDER_NONE;
  }
  if (legacy.kind != TFP_CAMERA_COMPAT_TABLE || field(c, legacy, TFP_CAMERA_COMPAT_SET_INPUT_BLOCKED).kind != TFP_CAMERA_COMPAT_FUNCTION ||
      !is_boolean(field(c, legacy, TFP_CAMERA_COMPAT_HELD)) || field(c, legacy, TFP_CAMERA_COMPAT_DISABLED).kind == TFP_CAMERA_COMPAT_TRUE ||
      field(c, legacy, TFP_CAMERA_COMPAT_AVAILABLE).kind == TFP_CAMERA_COMPAT_FALSE) {
    *why = "Photo camera sharing interface unavailable";
    return PROVIDER_NONE;
  }
  *api = legacy;
  return PROVIDER_LEGACY;
}

/* retail enabled(), lines 58-61: a controller that raises or answers no Boolean counts as on. */
static bool enabled(TfpCameraCompat *c) {
  const TfpCameraCompatHost *h = c->host;
  if (!h->controller_is_enabled) return true;
  return h->controller_is_enabled(h->user) != TFP_CAMERA_COMPAT_ENABLED_FALSE;
}

/* retail verify(r), lines 62-70. */
static bool verify(TfpCameraCompat *c, uint32_t serial, const char **why) {
  TfpCameraCompatValue api, current;
  Provider kind = provider(c, &api, why);
  if (*why) return false;
  if (kind != PROVIDER_LEGACY || !same(api, record_of(c, serial)->provider) ||
      !same(field(c, api, TFP_CAMERA_COMPAT_SET_INPUT_BLOCKED), record_of(c, serial)->setter)) {
    *why = "Photo camera sharing provider changed";
    return false;
  }
  if (!reader(c, record_of(c, serial)->setter, &current, why)) return false;
  if (!is_shim(current, serial)) { *why = "Photo camera reader was replaced; its replacement was preserved"; return false; }
  return true;
}

/* retail reject(why), lines 71-75. Answers false; the reason is c->message. */
static bool reject(TfpCameraCompat *c, const char *why) {
  if (c->record) record_of(c, c->record)->blocking = false;
  c->conflict = true;
  report(c, TFP_CAMERA_COMPAT_CONFLICT, why);
  return false;
}
static bool answer(const TfpCameraCompat *c, bool value, const char **why) {
  if (why) *why = message(c);
  return value;
}
static bool answer_text(bool value, const char *text, const char **why) {
  if (why) *why = text;
  return value;
}

/* pcall(setter, reader). A setter that is no function is not called: Lua raises "attempt to call a
 * <type> value". `text` receives the raised text or the setter's second result. */
static bool call_setter(TfpCameraCompat *c, TfpCameraCompatValue setter, TfpCameraCompatValue given, bool *accepted_is_false, bool *has_text,
                        char text[TFP_CAMERA_COMPAT_TEXT]) {
  static const char *const types[] = {"nil", "boolean", "boolean", "number", "string", "table", "function", "userdata"};
  const TfpCameraCompatHost *h = c->host;
  const char *said = NULL;
  TfpCameraCompatCall got;
  *accepted_is_false = false;
  if (setter.kind != TFP_CAMERA_COMPAT_FUNCTION) {
    snprintf(text, TFP_CAMERA_COMPAT_TEXT, "attempt to call a %s value", types[setter.kind]);
    *has_text = true;
    return false;
  }
  got = h->set_input_blocked(h->user, setter, given, accepted_is_false, &said);
  if (got == TFP_CAMERA_COMPAT_RAISED) {
    snprintf(text, TFP_CAMERA_COMPAT_TEXT, "%s", said ? said : "nil");
    *has_text = true;
    *accepted_is_false = false;
    return false;
  }
  *has_text = said != NULL;
  if (said) snprintf(text, TFP_CAMERA_COMPAT_TEXT, "%s", said);
  return true;
}

bool tfp_camera_compat_init(TfpCameraCompat *c, const TfpCameraCompatHost *host, const char **error) {
  memset(c, 0, sizeof *c);
  c->host = host;
  c->phase = TFP_CAMERA_COMPAT_IDLE;
  if (!host->controller_is_enabled) {
    if (error) *error = TFP_CAMERA_COMPAT_LUA_CHUNK ":6: Camera compatibility controller required";
    return false;
  }
  return true;
}
void tfp_camera_compat_free(TfpCameraCompat *c) {
  free(c->records);
  c->records = NULL;
  c->count = c->capacity = c->record = 0;
}

bool tfp_camera_compat_foreign(TfpCameraCompat *c, const char **why) {
  TfpCameraCompatValue api, current;
  const char *problem = NULL;
  Provider kind;
  if (c->conflict) return answer_text(true, c->has_message ? c->message : NEEDS_CLEANUP, why);
  kind = provider(c, &api, &problem);
  if (problem) return answer_text(true, problem, why);
  if (c->record && !verify(c, c->record, &problem)) return answer_text(true, problem, why);
  if (api.kind != TFP_CAMERA_COMPAT_NIL && truthy(field(c, api, TFP_CAMERA_COMPAT_HELD))) return answer_text(true, OWNS_CAMERA, why);
  if (kind == PROVIDER_LEGACY && !c->record && !reader(c, field(c, api, TFP_CAMERA_COMPAT_SET_INPUT_BLOCKED), &current, &problem))
    return answer_text(true, problem, why);
  return answer_text(false, NULL, why);
}

bool tfp_camera_compat_prepare(TfpCameraCompat *c, const char **why) {
  TfpCameraCompatValue api, previous, current;
  TfpCameraCompatRecord *r;
  const char *problem = NULL, *ownership_problem = NULL;
  char text[TFP_CAMERA_COMPAT_TEXT];
  bool called, accepted_is_false, has_text, owned;
  uint32_t serial;
  Provider kind;
  if (c->closing) return answer_text(false, "Camera compatibility is shutting down", why);
  if (c->conflict) return answer_text(false, c->has_message ? c->message : NEEDS_CLEANUP, why);
  kind = provider(c, &api, &problem);
  if (problem) return answer(c, reject(c, problem), why);
  if (c->record && !verify(c, c->record, &problem)) return answer(c, reject(c, problem), why);
  if (api.kind != TFP_CAMERA_COMPAT_NIL && truthy(field(c, api, TFP_CAMERA_COMPAT_HELD))) {
    report(c, TFP_CAMERA_COMPAT_FOREIGN, OWNS_CAMERA);
    return answer(c, false, why);
  }
  if (api.kind == TFP_CAMERA_COMPAT_NIL) { report(c, TFP_CAMERA_COMPAT_ABSENT, NULL); return answer_text(true, NULL, why); }
  if (kind == PROVIDER_LOCAL) { report(c, TFP_CAMERA_COMPAT_OBSERVING, NULL); return answer_text(true, NULL, why); }
  if (c->record) {
    record_of(c, c->record)->blocking = true;
    report(c, TFP_CAMERA_COMPAT_ACTIVE, NULL);
    return answer_text(true, NULL, why);
  }
  if (!reader(c, field(c, api, TFP_CAMERA_COMPAT_SET_INPUT_BLOCKED), &previous, &problem)) return answer(c, reject(c, problem), why);
  if (c->count == c->capacity) {
    uint32_t capacity = c->capacity ? c->capacity * 2 : 8;
    TfpCameraCompatRecord *grown = realloc(c->records, capacity * sizeof *grown);
    /* No memory for the record (no counterpart in Lua): nothing is installed. */
    if (!grown) return answer(c, reject(c, "Photo camera reader installation was not accepted"), why);
    c->records = grown;
    c->capacity = capacity;
  }
  serial = ++c->count;
  r = record_of(c, serial);
  *r = (TfpCameraCompatRecord){.provider = api, .setter = field(c, api, TFP_CAMERA_COMPAT_SET_INPUT_BLOCKED), .previous = previous, .blocking = true};
  /* The record is kept before the setter runs: a setter that raises may already hold the shim. */
  c->record = serial;
  called = call_setter(c, r->setter, (TfpCameraCompatValue){TFP_CAMERA_COMPAT_FUNCTION, 0, serial}, &accepted_is_false, &has_text, text);
  owned = reader(c, record_of(c, serial)->setter, &current, &ownership_problem);
  if (!called || accepted_is_false || !owned || !is_shim(current, serial)) {
    const char *reason = !called || has_text ? text : ownership_problem ? ownership_problem : "Photo camera reader installation was not accepted";
    return answer(c, reject(c, reason), why);
  }
  if (!verify(c, serial, &problem)) return answer(c, reject(c, problem), why);
  report(c, TFP_CAMERA_COMPAT_ACTIVE, NULL);
  return answer_text(true, NULL, why);
}

bool tfp_camera_compat_stop(TfpCameraCompat *c, const char **why) {
  TfpCameraCompatValue current, after;
  const char *problem = NULL;
  uint32_t serial = c->record;
  if (!serial) {
    c->conflict = false;
    report(c, TFP_CAMERA_COMPAT_IDLE, NULL);
    return answer_text(true, NULL, why);
  }
  record_of(c, serial)->blocking = false;
  if (!reader(c, record_of(c, serial)->setter, &current, &problem)) {
    report(c, TFP_CAMERA_COMPAT_CLEANUP_PENDING, problem);
    return answer(c, false, why);
  }
  if (is_shim(current, serial)) {
    char text[TFP_CAMERA_COMPAT_TEXT];
    bool accepted_is_false, has_text;
    bool called = call_setter(c, record_of(c, serial)->setter, record_of(c, serial)->previous, &accepted_is_false, &has_text, text);
    bool inspected = reader(c, record_of(c, serial)->setter, &after, &problem);
    if (!inspected || is_shim(after, serial)) {
      report(c, TFP_CAMERA_COMPAT_CLEANUP_PENDING, !called || has_text ? text : problem ? problem : "Photo camera reader restoration is pending");
      return answer(c, false, why);
    }
    /* The readback proves the shim is gone, also when the setter raised after restoring. A
     * replacement by someone else stays. */
  }
  c->record = 0;
  c->conflict = false;
  report(c, TFP_CAMERA_COMPAT_IDLE, NULL);
  return answer_text(true, NULL, why);
}

bool tfp_camera_compat_shutdown(TfpCameraCompat *c, const char **why) {
  c->closing = true;
  return tfp_camera_compat_stop(c, why);
}

bool tfp_camera_compat_update(TfpCameraCompat *c, const char **why) {
  TfpCameraCompatValue api;
  const char *problem = NULL;
  if (c->closing || !enabled(c)) return tfp_camera_compat_stop(c, why);
  if (c->conflict) return answer(c, false, why);
  provider(c, &api, &problem);
  if (problem) return answer(c, reject(c, problem), why);
  if (c->record && !verify(c, c->record, &problem)) return answer(c, reject(c, problem), why);
  if (api.kind != TFP_CAMERA_COMPAT_NIL && truthy(field(c, api, TFP_CAMERA_COMPAT_HELD))) {
    if (c->record) record_of(c, c->record)->blocking = false;
    report(c, TFP_CAMERA_COMPAT_FOREIGN, OWNS_CAMERA);
    return answer(c, true, why);
  }
  return tfp_camera_compat_prepare(c, why);
}

void tfp_camera_compat_result(TfpCameraCompat *c, TfpCameraCompatResult *out) {
  const char *why = NULL, *problem = NULL;
  bool foreign = tfp_camera_compat_foreign(c, &why);
  bool installed = false;
  if (c->record) {
    TfpCameraCompatValue current;
    installed = reader(c, record_of(c, c->record)->setter, &current, &problem) && is_shim(current, c->record);
  }
  out->phase = c->phase;
  out->installed = installed;
  out->blocking = installed && record_of(c, c->record)->blocking && enabled(c) &&
                  field(c, record_of(c, c->record)->provider, TFP_CAMERA_COMPAT_HELD).kind != TFP_CAMERA_COMPAT_TRUE;
  out->foreign = foreign;
  out->foreign_reason = why;
  out->cleanup_pending = c->phase == TFP_CAMERA_COMPAT_CLEANUP_PENDING;
  out->error_message = message(c);
  out->closing = c->closing;
}

const char *tfp_camera_compat_status(TfpCameraCompat *c) {
  TfpCameraCompatResult value;
  tfp_camera_compat_result(c, &value);
  snprintf(c->status, sizeof c->status, "%s; ActionCam blocked=%s; Photo owns camera=%s", tfp_camera_compat_phase_name(value.phase),
           value.blocking ? "true" : "false", value.foreign ? "true" : "false");
  return c->status;
}

/* retail r.shim, lines 108-121. The older reader is always honoured: when it raises or answers
 * anything but false, input stays blocked. */
bool tfp_camera_compat_shim(TfpCameraCompat *c, uint32_t serial) {
  const TfpCameraCompatHost *h = c->host;
  bool blocked = false;
  if (serial == 0 || serial > c->count) return true;      /* no such shim was ever handed out */
  if (record_of(c, serial)->reading) return true;
  record_of(c, serial)->reading = true;
  if (record_of(c, serial)->previous.kind != TFP_CAMERA_COMPAT_NIL)
    blocked = h->previous_reader_call(h->user, record_of(c, serial)->previous) != TFP_CAMERA_COMPAT_READ_FALSE;
  record_of(c, serial)->reading = false;
  if (blocked) return true;
  return record_of(c, serial)->blocking && enabled(c) &&
         field(c, record_of(c, serial)->provider, TFP_CAMERA_COMPAT_HELD).kind != TFP_CAMERA_COMPAT_TRUE;
}

const char *tfp_camera_compat_phase_name(TfpCameraCompatPhase phase) {
  static const char *const names[] = {"idle", "absent", "observing", "active", "foreign", "conflict", "cleanup_pending"};
  return names[phase];
}
