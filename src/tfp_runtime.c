/* Retail src/first_person/runtime.lua (0.5.41-smooth.66, kit commit 1d0d453) in C. The Lua file is
 * the authority; the numbers in comments are its lines. See tfp_runtime.h and
 * docs/RUNTIME-TRANSLATION.md.
 *
 * How Lua's behaviour is carried over:
 * - error() / pcall(): raise_value() / rt_pcall(), a setjmp frame per protected call. A host call
 *   that answers false has raised; CALL() passes that on, ATT() is retail's attempt().
 * - a module-level variable that is nil or an object: rt->has[]. Using a nil one raises the text
 *   LuaJIT raises, with the line.
 * - the closures on_enable hands out: one e_<name> function each, reached through tfp_rt_call. */
#include "tfp_runtime.h"
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define H (rt->host)
#define HAS(o) (rt->has[TFP_RT_O_##o])
#define MADE(s) (rt->made[TFP_RT_STAGE_##s])
#define F(t, kind, field) ((t).f[TFP_RT_##kind##_##field])
#define CHUNK "src/first_person/runtime.lua"

typedef TfpRtValue V;
typedef struct Pair { V a, b; } Pair;
typedef struct RtFrame { jmp_buf jump; struct RtFrame *prev; } RtFrame;

const double tfp_rt_camera_baseline[3] = {0, 0, .04};
const double tfp_rt_default_placement[3] = {.15, 0, -.12};
const char tfp_rt_empty_table = 0;

/* ------------------------------------------------------------------ values */
static const V VNIL = {TFP_RT_NIL, 0, 0, 0, 0};
static V vb(bool b) { V v = VNIL; v.kind = b ? TFP_RT_TRUE : TFP_RT_FALSE; return v; }
static V vn(double n) { V v = VNIL; v.kind = TFP_RT_NUMBER; v.number = n; return v; }
static V vt(const char *s) { V v = VNIL; v.kind = TFP_RT_TEXT; v.text = s; v.length = strlen(s); return v; }
static V vr(const void *ref) { V v = VNIL; v.kind = TFP_RT_REF; v.ref = ref; return v; }
static bool truthy(V v) { return v.kind != TFP_RT_NIL && v.kind != TFP_RT_FALSE; }
static bool is_true(V v) { return v.kind == TFP_RT_TRUE; }
static bool is_false(V v) { return v.kind == TFP_RT_FALSE; }
static bool is_nil(V v) { return v.kind == TFP_RT_NIL; }
static bool is_num(V v) { return v.kind == TFP_RT_NUMBER; }
static bool is_text(V v) { return v.kind == TFP_RT_TEXT; }
static bool veq(V a, V b) { /* Lua's == */
  if (a.kind != b.kind) return false;
  if (a.kind == TFP_RT_NUMBER) return a.number == b.number;
  if (a.kind == TFP_RT_TEXT) return a.length == b.length && !memcmp(a.text, b.text, a.length);
  if (a.kind == TFP_RT_REF) return a.ref == b.ref;
  return true;
}
static bool text_is(V v, const char *s) { return v.kind == TFP_RT_TEXT && v.length == strlen(s) && !memcmp(v.text, s, v.length); }
static bool num_is(V v, double n) { return v.kind == TFP_RT_NUMBER && v.number == n; }
static const char *kind_name(V v) {
  switch (v.kind) {
    case TFP_RT_NIL: return "nil";
    case TFP_RT_FALSE: case TFP_RT_TRUE: return "boolean";
    case TFP_RT_NUMBER: return "number";
    case TFP_RT_TEXT: return "string";
    default: return "table";
  }
}
static void tab_clear(TfpRtTable *t) { memset(t, 0, sizeof *t); }
static void tab_empty(TfpRtTable *t) { tab_clear(t); t->present = true; } /* {} */
static V tab_value(const TfpRtTable *t) { return t->present ? vr(t->ref) : VNIL; }
static V keep_into(V v, char *buffer, size_t room) {
  if (v.kind != TFP_RT_TEXT) return v;
  size_t n = v.length < room - 1 ? v.length : room - 1;
  memcpy(buffer, v.text, n);
  buffer[n] = 0;
  v.text = buffer;
  v.length = n;
  return v;
}
static void kept_set(TfpRtKept *kept, const TfpRtTable *t) {
  kept->table = *t;
  for (int i = 0; i < TFP_RT_FIELDS; i++) kept->table.f[i] = keep_into(t->f[i], kept->text[i], TFP_RT_KEPT_TEXT);
}
static void owner_set(TfpRtOwner *o, V identity, V body, V world) {
  o->present = true;
  o->identity = keep_into(identity, o->text[0], TFP_RT_KEPT_TEXT);
  o->body = keep_into(body, o->text[1], TFP_RT_KEPT_TEXT);
  o->world = keep_into(world, o->text[2], TFP_RT_KEPT_TEXT);
  o->frame = VNIL;
  o->cleaned_at = VNIL;
}

/* ------------------------------------------------------------------ texts and raising */
static const char *keep_bytes(TfpRt *rt, const char *s, size_t n) {
  if (rt->arena_used + n + 1 > TFP_RT_ARENA) return "(text arena exhausted)";
  char *at = rt->arena + rt->arena_used;
  memcpy(at, s, n);
  at[n] = 0;
  rt->arena_used += n + 1;
  return at;
}
static const char *keep(TfpRt *rt, const char *s) { return keep_bytes(rt, s ? s : "", s ? strlen(s) : 0); }
static const char *fmt(TfpRt *rt, const char *format, ...) {
  char buffer[1024];
  va_list list;
  va_start(list, format);
  vsnprintf(buffer, sizeof buffer, format, list);
  va_end(list);
  return keep(rt, buffer);
}
static const char *tostr(TfpRt *rt, V v) { /* tostring() */
  switch (v.kind) {
    case TFP_RT_NIL: return "nil";
    case TFP_RT_FALSE: return "false";
    case TFP_RT_TRUE: return "true";
    case TFP_RT_NUMBER: return fmt(rt, "%.14g", v.number);
    case TFP_RT_TEXT: return keep_bytes(rt, v.text, v.length);
    default: return fmt(rt, "table: %p", v.ref);
  }
}
_Noreturn static void raise_value(TfpRt *rt, V v) {
  RtFrame *frame = rt->frame;
  rt->error = v;
  rt->frame = frame->prev;
  longjmp(frame->jump, 1);
}
_Noreturn static void raise_text(TfpRt *rt, const char *text) { raise_value(rt, vt(text)); }
_Noreturn static void lua_error(TfpRt *rt, int line, const char *what) { raise_text(rt, fmt(rt, CHUNK ":%d: %s", line, what)); }
_Noreturn static void rethrow(TfpRt *rt) { raise_text(rt, keep(rt, H->raised)); }
/* assert(false, 'text'): LuaJIT puts the chunk and line before a text. */
_Noreturn static void assert_failed(TfpRt *rt, int line, const char *text) { lua_error(rt, line, text); }
/* assert(false, value): a text or a number becomes that text behind the chunk and line; any other value is raised as it is. */
_Noreturn static void assert_value(TfpRt *rt, int line, V message) {
  if (message.kind == TFP_RT_TEXT || message.kind == TFP_RT_NUMBER) lua_error(rt, line, tostr(rt, message));
  raise_value(rt, message);
}
/* The one function with a setjmp: locals of callers live in memory the body reaches by pointer. */
static bool rt_pcall(TfpRt *rt, void (*body)(TfpRt *, void *), void *arg) {
  RtFrame frame;
  frame.prev = rt->frame;
  rt->frame = &frame;
  if (setjmp(frame.jump) == 0) {
    body(rt, arg);
    rt->frame = frame.prev;
    return true;
  }
  return false;
}
/* a..b with b a value: a string or a number, else LuaJIT's error. */
static const char *concat_operand(TfpRt *rt, int line, const char *what, V v) {
  if (v.kind != TFP_RT_TEXT && v.kind != TFP_RT_NUMBER) lua_error(rt, line, fmt(rt, "attempt to concatenate %s (a %s value)", what, kind_name(v)));
  return tostr(rt, v);
}

#define NEED(o, line, what) do { if (!HAS(o)) lua_error(rt, line, "attempt to index " what " (a nil value)"); } while (0)
#define CALL(e) do { if (!(e)) rethrow(rt); } while (0)
/* attempt(target, name, ...) (:23-27): done, why; a raise is false and its text. */
static Pair att_end(TfpRt *rt, bool returned, Pair got) {
  if (returned) return got;
  Pair p = {vb(false), vt(keep(rt, H->raised))};
  return p;
}
#define ATT_NIL() lua_error(rt, 24, "attempt to index local 'target' (a nil value)")
#define OATT(p, fn, ...) do { Pair q_ = {VNIL, VNIL}; bool r_ = H->fn(H, ##__VA_ARGS__, &q_.a, &q_.b); (p) = att_end(rt, r_, q_); } while (0)
#define OATT1(p, fn, ...) do { Pair q_ = {VNIL, VNIL}; bool r_ = H->fn(H, ##__VA_ARGS__, &q_.a); (p) = att_end(rt, r_, q_); } while (0)
#define ATT(o, p, fn, ...) do { if (!HAS(o)) ATT_NIL(); OATT(p, fn, ##__VA_ARGS__); } while (0)
#define ATT1(o, p, fn, ...) do { if (!HAS(o)) ATT_NIL(); OATT1(p, fn, ##__VA_ARGS__); } while (0)
#define ATTX(o, fn, ...) do { Pair x_; ATT(o, x_, fn, ##__VA_ARGS__); (void)x_; } while (0)
#define OATTX(fn, ...) do { Pair x_; OATT(x_, fn, ##__VA_ARGS__); (void)x_; } while (0)
/* attempt(x, 'result') or {} */
static void table_or_empty(bool returned, TfpRtTable *t) { if (!returned || !t->present) tab_empty(t); }

static void log_text(TfpRt *rt, const char *text) { CALL(H->mod_log(H, text)); } /* :22 */

/* ------------------------------------------------------------------ the targets of managed_update and recover_new_owner */
typedef enum Target { TG_FOV, TG_BLUR, TG_CAMERA, TG_HEAD, TG_MOVEMENT, TG_NEAR, TG_DOF, TG_NATIVE_AIM, TG_AIM_MARKERS, TG_ARM_PIECES,
  TG_GRIP_HAND, TG_COUNT } Target;
static bool tg_has(TfpRt *rt, Target tg) {
  switch (tg) {
    case TG_FOV: return HAS(FOV);
    case TG_BLUR: return HAS(BLUR);
    case TG_CAMERA: return HAS(CAMERA);
    case TG_HEAD: return HAS(HEAD);
    case TG_MOVEMENT: return HAS(MOVEMENT);
    case TG_NEAR: return HAS(NEAR);
    case TG_DOF: return HAS(DOF);
    case TG_NATIVE_AIM: return HAS(NATIVE_AIM);
    case TG_AIM_MARKERS: return HAS(AIM_MARKERS);
    case TG_ARM_PIECES: return HAS(ARM_PIECES);
    default: return HAS(GRIP_HAND);
  }
}
static Pair tg_stop(TfpRt *rt, Target tg) {
  Pair p = {VNIL, VNIL};
  switch (tg) {
    case TG_FOV: OATT(p, fov_stop); break;
    case TG_BLUR: OATT(p, blur_stop); break;
    case TG_CAMERA: OATT(p, camera_stop); break;
    case TG_HEAD: OATT(p, head_stop); break;
    case TG_MOVEMENT: OATT(p, movement_stop); break;
    case TG_NEAR: OATT(p, near_stop); break;
    case TG_DOF: OATT(p, dof_stop); break;
    case TG_NATIVE_AIM: OATT(p, native_aim_stop); break;
    case TG_AIM_MARKERS: OATT(p, aim_markers_stop); break;
    case TG_ARM_PIECES: OATT(p, arm_pieces_stop); break;
    default: OATT(p, grip_hand_stop); break;
  }
  return p;
}
static Pair tg_is_clean(TfpRt *rt, Target tg) {
  Pair p = {VNIL, VNIL};
  switch (tg) {
    case TG_FOV: OATT1(p, fov_is_clean); break;
    case TG_BLUR: OATT1(p, blur_is_clean); break;
    case TG_CAMERA: OATT1(p, camera_is_clean); break;
    case TG_HEAD: OATT1(p, head_is_clean); break;
    case TG_MOVEMENT: OATT1(p, movement_is_clean); break;
    case TG_NEAR: OATT1(p, near_is_clean); break;
    case TG_DOF: OATT1(p, dof_is_clean); break;
    case TG_NATIVE_AIM: OATT1(p, native_aim_is_clean); break;
    case TG_AIM_MARKERS: OATT1(p, aim_markers_is_clean); break;
    case TG_ARM_PIECES: OATT1(p, arm_pieces_is_clean); break;
    default: OATT1(p, grip_hand_is_clean); break;
  }
  return p;
}
static Pair tg_update(TfpRt *rt, Target tg) {
  Pair p = {VNIL, VNIL};
  switch (tg) {
    case TG_FOV: OATT(p, fov_update); break;
    case TG_BLUR: OATT(p, blur_update); break;
    case TG_CAMERA: OATT(p, camera_update); break;
    case TG_HEAD: OATT(p, head_update); break;
    default: OATT(p, movement_update); break;
  }
  return p;
}
static void tg_result(TfpRt *rt, Target tg, TfpRtTable *state) { /* attempt(target,'result') or {} */
  bool returned;
  tab_clear(state);
  switch (tg) {
    case TG_FOV: returned = H->fov_result(H, state); break;
    case TG_BLUR: returned = H->blur_result(H, state); break;
    case TG_CAMERA: returned = H->camera_result(H, state); break;
    case TG_HEAD: returned = H->head_result(H, state); break;
    default: returned = H->movement_result(H, state); break;
  }
  table_or_empty(returned, state);
}
static const char *const tg_names[] = {"fov", "blur", "camera", "head", "movement"};

/* ------------------------------------------------------------------ small readers */
static TfpRtTable controller_lease(TfpRt *rt) {
  TfpRtTable lease;
  tab_clear(&lease);
  H->controller_lease(H, &lease);
  return lease;
}
static bool same_table(const TfpRtTable *a, const TfpRtTable *b) { return a->present && b->present && a->ref == b->ref; }
static bool context_is_current(TfpRt *rt, const TfpRtContext *context) {
  return context && rt->has_context && context->serial == rt->context.serial;
}
static V config_field(TfpRt *rt, const char *name) { /* (mod.config or {})[name] */
  if (is_nil(H->config_table(H))) return VNIL;
  return H->config_field(H, name);
}

/* monotonic_clock()'s function (:165-172). */
static double clock_now(TfpRt *rt) {
  V value = VNIL;
  if (H->clock_now) CALL(H->clock_now(H, &value));
  if (!(is_num(value) && value.number == value.number && value.number >= 0 && value.number < HUGE_VAL))
    assert_failed(rt, 167, "Engine FPV clock sample invalid");
  if (truthy(rt->clock_previous) && !(value.number >= rt->clock_previous.number)) assert_failed(rt, 169, "Engine FPV clock moved backwards");
  rt->clock_previous = value;
  return value.number;
}
static double recovery_time(TfpRt *rt) { return HAS(RUNTIME_CLOCK) ? clock_now(rt) : 0; } /* :264-267 */

/* foreign_camera (:718): not handoff_ready or compat:foreign(). */
static V foreign_camera(TfpRt *rt) {
  if (!truthy(rt->handoff_ready)) return vb(true);
  NEED(COMPAT, 718, "upvalue 'compat'");
  V value = VNIL;
  CALL(H->compat_foreign(H, &value));
  return value;
}
static V foreign_at(TfpRt *rt, int line) { /* foreign_camera() where the variable may be nil */
  if (!HAS(FOREIGN_CAMERA)) lua_error(rt, line, "attempt to call upvalue 'foreign_camera' (a nil value)");
  return foreign_camera(rt);
}
static void foreign_body(TfpRt *rt, void *out) { *(V *)out = foreign_camera(rt); }

static V fird_is_enabled(TfpRt *rt) {
  V value = VNIL;
  CALL(H->fird_is_enabled(H, &value));
  return value;
}
static bool fird_on(TfpRt *rt) { return HAS(FIRD) && truthy(fird_is_enabled(rt)); } /* fird and fird:is_enabled() */
static V controller_is_enabled(TfpRt *rt) {
  V value = VNIL;
  CALL(H->controller_is_enabled(H, &value));
  return value;
}

/* :31-37 */
static bool view_snapshot(TfpRt *rt, TfpRtTable *s) {
  tab_clear(s);
  if (HAS(BACKEND) && is_true(H->backend_pin_first_person(H)) && H->backend_pin_player_snapshot) {
    if (H->backend_pin_player_snapshot(H, s) && s->present) return true;
    tab_clear(s);
  }
  if (!HAS(BACKEND)) return false;
  if (!H->backend_player_snapshot(H, s) || !s->present) { tab_clear(s); return false; }
  return true;
}
/* :38-47 */
static bool pinned_ready(TfpRt *rt, const TfpRtTable *s) {
  Pair p;
  if (!HAS(CONTROLLER)) return false;
  OATT1(p, controller_is_enabled);
  if (!is_true(p.a)) return false;
  TfpRtTable state;
  tab_clear(&state);
  if (!H->controller_result(H, &state) || !state.present || !text_is(F(state, STATE, MODE), "pose_offset")) return false;
  if (HAS(FOREIGN_CAMERA)) {
    V held = VNIL;
    if (!rt_pcall(rt, foreign_body, &held) || !is_false(held)) return false;
  }
  if (!HAS(CAMERA) || !H->camera_is_pinned) return false;
  OATT1(p, camera_is_pinned, s);
  return is_true(p.a);
}
/* fallback_ready (:687-690) */
static V fallback_ready(TfpRt *rt) {
  TfpRtTable state;
  tab_clear(&state);
  NEED(CAMERA, 688, "upvalue 'camera'");
  CALL(H->camera_result(H, &state));
  if (!state.present) lua_error(rt, 689, "attempt to index local 'state' (a nil value)");
  return vb(text_is(F(state, CAM, PHASE), "active") && is_true(F(state, CAM, ARMED)) && num_is(F(state, CAM, ERROR), 0));
}
/* :48-79 */
static V first_person_view(TfpRt *rt) {
  TfpRtTable s, state;
  if (HAS(FOREIGN_CAMERA) && truthy(foreign_camera(rt))) return vb(false);
  if (!view_snapshot(rt, &s)) return vb(false);
  if (pinned_ready(rt, &s)) return vb(true);
  V kind = F(s, SNAP, CAMERA_KIND);
  if (!num_is(kind, 4) && !num_is(kind, 6)) return vb(false);
  if (!truthy(F(s, SNAP, SAFE))) {
    if (!num_is(kind, 4) || fird_on(rt) || !HAS(CONTROLLER)) return vb(false);
    tab_clear(&state);
    CALL(H->controller_result(H, &state));
    if (!truthy(controller_is_enabled(rt))) return vb(false);
    if (!state.present) lua_error(rt, 62, "attempt to index local 'state' (a nil value)");
    if (!text_is(F(state, STATE, MODE), "pose_offset")) return vb(false);
    if (!truthy(rt->fp_dof_player) || !veq(F(s, SNAP, PLAYER_IDENTITY), rt->fp_dof_player)) return vb(false);
    return vb(is_true(fallback_ready(rt)));
  }
  if (fird_on(rt)) {
    TfpRtTable weapon;
    tab_clear(&weapon);
    H->fird_weapon_snapshot(H, &weapon);
    return vb(weapon.present && is_true(F(weapon, FIRDSNAP, SAFE)) && veq(F(weapon, FIRDSNAP, PLAYER_IDENTITY), F(s, SNAP, PLAYER_IDENTITY)));
  }
  NEED(BACKEND, 70, "upvalue 'backend'");
  TfpRtTable warm;
  tab_clear(&warm);
  H->backend_warm_lease(H, &warm);
  if (HAS(FIRD_ENTRY)) {
    V enabled = VNIL;
    CALL(H->fird_entry_is_enabled(H, &enabled));
    if (truthy(enabled) && warm.present && truthy(F(warm, LEASE, ORIGIN)) &&
        veq(F(warm, LEASE, ORIGIN_PLAYER_IDENTITY), F(s, SNAP, PLAYER_IDENTITY)) && num_is(kind, 6))
      return vb(true);
  }
  NEED(CONTROLLER, 73, "upvalue 'controller'");
  tab_clear(&state);
  CALL(H->controller_result(H, &state));
  if (!truthy(controller_is_enabled(rt))) return vb(false);
  if (!state.present) lua_error(rt, 74, "attempt to index local 'state' (a nil value)");
  if (!text_is(F(state, STATE, MODE), "pose_offset")) return vb(false);
  TfpRtTable lease = controller_lease(rt);
  if (lease.present && truthy(F(lease, LEASE, ORIGIN)))
    rt->fp_dof_player = keep_into(F(lease, LEASE, ORIGIN_PLAYER_IDENTITY), rt->fp_dof_text, sizeof rt->fp_dof_text);
  if (!truthy(rt->fp_dof_player) || !veq(F(s, SNAP, PLAYER_IDENTITY), rt->fp_dof_player)) return vb(false);
  if (num_is(kind, 6)) return vb(true);
  return fallback_ready(rt);
}

/* :83-107 */
static void ready_update(TfpRt *rt) {
  Pair p;
  if (!HAS(READY_POSE) || !rt->has_context) return;
  V done = VNIL, why = VNIL;
  if (!H->ready_pose_update(H, &rt->context, &done, &why)) {
    const char *text = keep(rt, H->raised);
    if (HAS(HELD_FIRE)) OATTX(held_fire_stop);
    OATTX(ready_pose_stop);
    log_text(rt, fmt(rt, "Ready stance cleanup retained: %s", text));
    return;
  }
  if (HAS(HELD_FIRE)) {
    V enabled = VNIL;
    CALL(H->ready_pose_is_enabled(H, &enabled));
    if (!truthy(enabled)) OATTX(held_fire_stop);
    else {
      V admission = VNIL;
      if (!H->ready_pose_filter_owner(H, &rt->context, &admission)) admission = VNIL;
      OATT(p, held_fire_update, admission, &rt->context);
      if (!is_true(p.a)) OATTX(held_fire_stop);
    }
  }
}

/* :109-121 */
typedef void FrameBody(TfpRt *rt, Pair *out);
typedef struct FrameBox { FrameBody *body; Pair *out; } FrameBox;
static void frame_body(TfpRt *rt, void *arg) { FrameBox *box = arg; box->body(rt, box->out); }
static void qualified_frame(TfpRt *rt, FrameBody *body, Pair *out) {
  if (!HAS(BACKEND)) { body(rt, out); return; }
  if (rt->has_context || truthy(H->backend_frame_active(H))) assert_failed(rt, 112, "First-person Update frame is already owned");
  CALL(H->backend_begin_frame(H));
  rt->ready_tick = rt->ready_tick + 1;
  rt->context.serial = ++rt->context_serial;
  rt->context.tick = rt->ready_tick;
  rt->has_context = true;
  FrameBox box = {body, out};
  bool ok = rt_pcall(rt, frame_body, &box);
  V raised = rt->error;
  rt->has_context = false;
  CALL(H->backend_end_frame(H));
  if (!ok) raise_value(rt, raised);
}

/* ------------------------------------------------------------------ recovery (:268-362) */
typedef struct Proof { V v[TFP_RT_PROOF_N]; } Proof;
static Proof recovery_proof(TfpRt *rt) {
  Proof proof;
  TfpRtTable s, state;
  tab_clear(&s);
  if (HAS(BACKEND)) {
    V why = VNIL;
    if (!H->backend_snapshot(H, &s, &why)) tab_clear(&s);
  }
  tab_clear(&state);
  if (HAS(CONTROLLER)) table_or_empty(H->controller_result(H, &state), &state); else tab_empty(&state);
  V owner = F(state, STATE, PLAYER_IDENTITY);
  if (!truthy(owner) && HAS(CONTROLLER)) {
    TfpRtTable lease = controller_lease(rt);
    if (lease.present && truthy(F(lease, LEASE, ORIGIN))) owner = F(lease, LEASE, ORIGIN_PLAYER_IDENTITY);
  }
  V foreign = HAS(FOREIGN_CAMERA) ? foreign_camera(rt) : vb(true);
  proof.v[TFP_RT_PROOF_FRESH] = HAS(BACKEND) ? vb(is_true(H->backend_frame_active(H)) && s.present) : VNIL;
  proof.v[TFP_RT_PROOF_SAFE] = vb(s.present && is_true(F(s, SNAP, SAFE)));
  proof.v[TFP_RT_PROOF_SAME_PLAYER] = vb(s.present && !is_nil(owner) && veq(F(s, SNAP, PLAYER_IDENTITY), owner));
  proof.v[TFP_RT_PROOF_FOREIGN] = vb(!is_false(foreign));
  return proof;
}
static const TfpRtTable *policy_row(const TfpRtPolicies *policies, const char *name) {
  for (int i = 0; i < policies->count && i < TFP_RT_POLICY_MAX; i++)
    if (policies->name[i] && !strcmp(policies->name[i], name)) return &policies->row[i];
  return NULL;
}
static Pair managed_update(TfpRt *rt, Target tg, bool owned) {
  const char *name = tg_names[tg];
  Pair p, out = {vb(true), VNIL};
  V value = VNIL;
  if (!HAS(RECOVERY) || !HAS(CONTROLLER) || !truthy(controller_is_enabled(rt))) {
    if (!owned && !tg_has(rt, tg)) ATT_NIL();
    return tg_update(rt, tg);
  }
  double timestamp = recovery_time(rt);
  V action = VNIL;
  CALL(H->recovery_action(H, name, timestamp, &action));
  if (text_is(action, "cleanup")) {
    if (!owned && !tg_has(rt, tg)) ATT_NIL();
    p = tg_stop(rt, tg);
    CALL(H->recovery_cleanup(H, name, is_true(p.a) && is_true(tg_is_clean(rt, tg).a), timestamp));
    TfpRtPolicies policies;
    memset(&policies, 0, sizeof policies);
    CALL(H->recovery_snapshot(H, &policies));
    if (!policies.present) lua_error(rt, 285, "attempt to index a nil value");
    const TfpRtTable *row = policy_row(&policies, name);
    if (row && truthy(F(*row, ROW, CORE)) && text_is(F(*row, ROW, PHASE), "fatal")) {
      if (!HAS(CONTROLLER)) ATT_NIL();
      OATTX(controller_stop, fmt(rt, "First-person %s cleanup completed after an unsafe fault", name));
    }
    out.b = p.b;
    return out;
  } else if (!text_is(action, "update")) {
    out.b = vt(fmt(rt, "Presentation %s %s", name, concat_operand(rt, 288, "local 'action'", action)));
    return out;
  }
  CALL(H->recovery_needs_proof(H, name, &value));
  if (truthy(value)) {
    Proof proof = recovery_proof(rt);
    CALL(H->recovery_revalidate(H, name, proof.v, timestamp, &value));
    if (!truthy(value)) { out.b = vt("Waiting for fresh presentation owner"); return out; }
  }
  if (!owned && !tg_has(rt, tg)) ATT_NIL();
  p = tg_update(rt, tg);
  TfpRtTable state;
  if (is_true(p.a)) {
    tg_result(rt, tg, &state);
    CALL(H->recovery_success(H, name, timestamp, text_is(F(state, TARGET, PHASE), "active") && !is_false(F(state, TARGET, ARMED))));
    out.b = p.b;
    return out;
  }
  V why = p.b;
  tg_result(rt, tg, &state);
  Proof proof = recovery_proof(rt);
  bool clean = is_true(tg_stop(rt, tg).a) && is_true(tg_is_clean(rt, tg).a);
  TfpRtTable row;
  tab_clear(&row);
  CALL(H->recovery_failure(H, name, &state, clean, proof.v, timestamp, why, &row));
  if (HAS(DIAGNOSTICS)) {
    if (!row.present) lua_error(rt, 302, "attempt to index local 'row' (a nil value)");
    V event[TFP_RT_EVENT_N];
    event[TFP_RT_EVENT_POLICY] = vt(name);
    event[TFP_RT_EVENT_REASON] = why;
    event[TFP_RT_EVENT_PHASE] = F(row, ROW, PHASE);
    event[TFP_RT_EVENT_CLEANUP_PENDING] = vb(!clean);
    CALL(H->diagnostics_event(H, rt->has_context ? rt->context.tick : 0, "policy_failure", event));
  }
  if (!row.present) lua_error(rt, 303, "attempt to index local 'row' (a nil value)");
  if (truthy(F(row, ROW, CORE))) {
    if (text_is(F(row, ROW, PHASE), "fatal")) OATTX(controller_stop, fmt(rt, "First-person %s fault: %s", name, tostr(rt, why)));
    else if (H->controller_suspend) OATTX(controller_suspend, fmt(rt, "Recovering %s: %s", name, tostr(rt, why)));
    else OATTX(controller_stop, fmt(rt, "First-person %s unavailable: %s", name, tostr(rt, why)));
  }
  out.b = why;
  return out;
}
/* :314-324. s NULL: no argument. */
static void remember_recovery_owner(TfpRt *rt, const TfpRtTable *s) {
  if (rt->owner_draining.present || !HAS(CONTROLLER)) return;
  TfpRtTable state;
  tab_clear(&state);
  table_or_empty(H->controller_result(H, &state), &state);
  TfpRtTable lease = controller_lease(rt);
  bool origin = lease.present && truthy(F(lease, LEASE, ORIGIN));
  V identity = F(state, STATE, PLAYER_IDENTITY);
  if (!truthy(identity)) identity = origin ? F(lease, LEASE, ORIGIN_PLAYER_IDENTITY) : (lease.present ? F(lease, LEASE, ORIGIN) : VNIL);
  V player, body, world;
  if (s) { player = F(*s, SNAP, PLAYER_IDENTITY); body = F(*s, SNAP, BODY); world = F(*s, SNAP, WORLD); }
  else if (origin) { player = F(lease, LEASE, ORIGIN_PLAYER_IDENTITY); body = F(lease, LEASE, ORIGIN_BODY); world = F(lease, LEASE, ORIGIN_WORLD); }
  else return;
  if (veq(player, identity) && is_num(body) && is_num(world)) owner_set(&rt->recovery_owner, identity, body, world);
}
static bool recover_new_owner(TfpRt *rt) {
  Pair p;
  V value = VNIL;
  if (!HAS(RECOVERY) || !truthy(controller_is_enabled(rt))) {
    rt->recovery_owner.present = false;
    rt->owner_draining.present = false;
    return false;
  }
  if (!rt->owner_draining.present) {
    CALL(H->recovery_has_policies(H, &value));
    if (!truthy(value)) { remember_recovery_owner(rt, NULL); return false; }
  }
  TfpRtTable s;
  tab_clear(&s);
  if (!HAS(BACKEND)) ATT_NIL();
  if (!H->backend_snapshot(H, &s, &value) || !s.present) return rt->owner_draining.present;
  TfpRtTable state;
  tab_clear(&state);
  CALL(H->controller_result(H, &state));
  if (!state.present) lua_error(rt, 335, "attempt to index a nil value");
  V identity = F(state, STATE, PLAYER_IDENTITY);
  if (!truthy(identity)) {
    TfpRtTable lease = controller_lease(rt);
    if (lease.present && truthy(F(lease, LEASE, ORIGIN))) identity = F(lease, LEASE, ORIGIN_PLAYER_IDENTITY);
  }
  V player = F(s, SNAP, PLAYER_IDENTITY), body = F(s, SNAP, BODY), world = F(s, SNAP, WORLD);
  if (!rt->owner_draining.present && veq(player, identity) && is_num(body) && is_num(world)) remember_recovery_owner(rt, &s);
  bool owner_changed = rt->recovery_owner.present && !veq(player, rt->recovery_owner.identity) && is_text(player) && is_num(body) &&
                       is_num(world) && (!veq(body, rt->recovery_owner.body) || !veq(world, rt->recovery_owner.world));
  TfpRtPolicies policies;
  memset(&policies, 0, sizeof policies);
  CALL(H->recovery_snapshot(H, &policies));
  if (!policies.present) lua_error(rt, 343, "attempt to index a nil value");
  bool faulty = policies.count > 0;
  if (!rt->owner_draining.present && !(owner_changed && faulty)) return false;
  if (!rt->has_context) lua_error(rt, 346, "attempt to index upvalue 'ready_context' (a nil value)");
  if (!rt->owner_draining.present || !veq(rt->owner_draining.identity, player)) {
    owner_set(&rt->owner_draining, player, body, world);
    rt->owner_draining.frame = vn(rt->context.tick);
    if (H->controller_suspend) OATTX(controller_suspend, "New local body: retiring presentation policies");
    if (HAS(EDITOR)) {
      OATT1(p, editor_is_active);
      if (is_true(p.a)) OATTX(editor_cancel, "Local player changed");
    }
  }
  if (controller_lease(rt).present) OATTX(controller_retry_cleanup);
  bool clean = !controller_lease(rt).present;
  for (int tg = 0; tg < TG_COUNT; tg++) { /* :352, in the order of the table constructor (see the note) */
    if (!tg_has(rt, (Target)tg)) continue;
    bool done = is_true(tg_stop(rt, (Target)tg).a);
    clean = clean && done && is_true(tg_is_clean(rt, (Target)tg).a);
  }
  if (clean) { if (!truthy(rt->owner_draining.cleaned_at)) rt->owner_draining.cleaned_at = vn(rt->context.tick); }
  else rt->owner_draining.cleaned_at = VNIL;
  if (clean && is_true(F(s, SNAP, SAFE)) && is_false(foreign_at(rt, 356)) && rt->context.tick > rt->owner_draining.cleaned_at.number) {
    CALL(H->recovery_owner_reset(H));
    if (HAS(PRESENTATION)) CALL(H->transition_reset(H, "local body changed"));
    owner_set(&rt->recovery_owner, player, body, world);
    rt->owner_draining.present = false;
    return false;
  }
  return true;
}

/* :364-387 */
static void observe_presentation(TfpRt *rt) {
  if (!HAS(PRESENTATION) || !HAS(BACKEND) || !HAS(RUNTIME_CLOCK)) return;
  TfpRtTable s;
  V value = VNIL;
  tab_clear(&s);
  if (!H->backend_snapshot(H, &s, &value)) tab_clear(&s);
  if (!s.present) {
    tab_clear(&s);
    if (H->backend_pin_player_snapshot && !H->backend_pin_player_snapshot(H, &s)) tab_clear(&s);
  }
  if (s.present) {
    if (HAS(RECOVERY)) {
      CALL(H->recovery_has_policies(H, &value));
      if (!truthy(value)) remember_recovery_owner(rt, &s);
    }
    TfpRtTable copy = s;
    NEED(CONTROLLER, 373, "upvalue 'controller'");
    TfpRtTable lease = controller_lease(rt), alignment;
    tab_clear(&alignment);
    H->backend_alignment_lease(H, &alignment);
    if (lease.present && truthy(F(lease, LEASE, ALIGNMENT)) && same_table(&lease, &alignment) && truthy(F(lease, LEASE, ORIGIN)) &&
        veq(F(lease, LEASE, ORIGIN_IDENTITY), F(s, SNAP, IDENTITY)) &&
        veq(F(lease, LEASE, ORIGIN_PLAYER_IDENTITY), F(s, SNAP, PLAYER_IDENTITY)) && !truthy(F(lease, LEASE, ENDED)) &&
        text_is(F(lease, LEASE, KIND), "pose")) {
      if (!truthy(F(lease, LEASE, HELPER))) lua_error(rt, 377, "attempt to index field 'helper' (a nil value)");
      if (truthy(F(lease, LEASE, DEPTH_STATS))) {
        TfpRtTable depth;
        tab_clear(&depth);
        if (!H->helper_depth_stats(H, &lease, F(lease, LEASE, HANDLE), &depth, &value)) tab_clear(&depth);
        V zoom = F(depth, DEPTH, ZOOM_INDEX);
        if (depth.present && is_true(F(depth, DEPTH, SUPPORTED)) && is_true(F(depth, DEPTH, ARMED)) && is_num(zoom) &&
            zoom.number - floor(zoom.number / 1) * 1 == 0 && zoom.number >= 0 && zoom.number < 32)
          F(copy, SNAP, ZOOM_IDENTITY) = zoom;
      }
    }
    NEED(PRESENTATION_MODULE, 384, "upvalue 'presentation_module'");
    value = VNIL;
    CALL(H->transition_native_ads(H, &copy, &value));
    F(copy, SNAP, CONTROLLER_ACTIVE) = value;
    double now = clock_now(rt);
    V enabled = controller_is_enabled(rt);
    V foreign = foreign_at(rt, 385);
    CALL(H->transition_update(H, &copy, now, enabled, foreign));
  } else {
    double now = clock_now(rt);
    NEED(CONTROLLER, 386, "upvalue 'controller'");
    V enabled = controller_is_enabled(rt);
    CALL(H->transition_update(H, NULL, now, enabled, vb(true)));
  }
}

/* :395-422 */
static V mouse_index(TfpRt *rt, const char *name) {
  V value = VNIL;
  if (!H->mouse_button_index || !H->mouse_button_index(H, name, &value) || !is_num(value)) return vb(false);
  return value;
}
static bool mouse_down(TfpRt *rt, V button) {
  V value = VNIL;
  return H->mouse_button(H, button.number, &value) && is_num(value) && value.number > 0;
}
static bool aim_released(TfpRt *rt) {
  if (!H->mouse_button) return false;
  if (is_nil(rt->aim_button)) rt->aim_button = mouse_index(rt, "right");
  if (!truthy(rt->aim_button)) return false;
  bool down = mouse_down(rt, rt->aim_button);
  bool edge = rt->aim_button_held != down;
  rt->aim_button_held = down;
  if (is_nil(rt->aim_mode_button)) rt->aim_mode_button = mouse_index(rt, "middle");
  if (truthy(rt->aim_mode_button)) {
    bool pressed = mouse_down(rt, rt->aim_mode_button);
    if (pressed && !rt->aim_mode_held) edge = true;
    rt->aim_mode_held = pressed;
  }
  return edge;
}

/* ------------------------------------------------------------------ record_presentation (:423-559) */
static double lua_min(double a, double b) { return a < b ? a : b; }
static double lua_max(double a, double b) { return a > b ? a : b; }
/* A little-endian float out of four bytes, as :462-468 and :481-486 compute it. */
static bool f32(const unsigned char *at, double *out) {
  double a = at[0], b = at[1], c = at[2], d = at[3];
  double mantissa = a + b * 256 + fmod(c, 128) * 65536;
  double exponent = (c - fmod(c, 128)) / 128 + fmod(d, 128) * 2;
  double sign = d >= 128 ? -1 : 1;
  if (exponent == 255) return false;
  if (exponent == 0) *out = sign * mantissa * 0x1p-149;
  else *out = sign * (1 + mantissa * 0x1p-23) * ldexp(1.0, (int)exponent - 127);
  return true;
}
static bool read_number(TfpRt *rt, double address, double *out) { /* backend:_ptr(address) used as a number */
  V value = VNIL;
  if (!H->backend_ptr(H, address, &value) || !is_num(value)) return false;
  *out = value.number;
  return true;
}
static bool read_floats(TfpRt *rt, V address, int count, double *out) { /* floats(backend:_read(address, count * 4), count) */
  V bytes = VNIL;
  if (!H->backend_read(H, address, count * 4.0, &bytes) || !is_text(bytes) || bytes.length < (size_t)count * 4) return false;
  for (int i = 0; i < count; i++)
    if (!f32((const unsigned char *)bytes.text + i * 4, &out[i])) out[i] = NAN;
  return true;
}
static bool read_pointer(TfpRt *rt, double address, V *out) { /* backend:_ptr(address), handed on as it is */
  *out = VNIL;
  return H->backend_ptr(H, address, out);
}
static bool alignment_object(TfpRt *rt, V ref, const TfpRtTable *s, double *out) {
  V context[TFP_RT_ACTX_N];
  TfpRtTable object;
  context[TFP_RT_ACTX_VIEWMODEL] = ref;
  context[TFP_RT_ACTX_AVATAR_OWNER] = F(*s, SNAP, AVATAR_OWNER);
  context[TFP_RT_ACTX_AVATAR_INDEX] = F(*s, SNAP, AVATAR_INDEX);
  tab_clear(&object);
  if (!H->backend_alignment_context(H, context, &object) || !object.present || !is_num(F(object, AOBJ, OBJECT))) return false;
  *out = F(object, AOBJ, OBJECT).number;
  return true;
}
static void in_camera(const double *camera, double x, double y, double z, V *out) {
  double dx = x - camera[12], dy = y - camera[13], dz = z - camera[14];
  out[0] = vn(dx * camera[0] + dy * camera[1] + dz * camera[2]);
  out[1] = vn(dx * camera[4] + dy * camera[5] + dz * camera[6]);
  out[2] = vn(dx * camera[8] + dy * camera[9] + dz * camera[10]);
}
/* :477-511: whatever was assigned before a read fails stays assigned. */
static void rig_in_camera(TfpRt *rt, const TfpRtTable *s, V *rec) {
  double camera[16], world[16], position[3], address, rig;
  V pointer;
  if (!read_number(rt, F(*s, SNAP, CAMERA).number + 0x18, &address) || !read_pointer(rt, address + 0x28, &pointer) ||
      !read_floats(rt, pointer, 16, camera))
    return;
  if (is_num(F(*s, SNAP, VIEWMODEL))) {
    if (!alignment_object(rt, F(*s, SNAP, VIEWMODEL), s, &rig)) return;
    if (!read_number(rt, rig + 0x80, &address) || !read_floats(rt, vn(address + 5 * 0x40 + 0x24), 3, position)) return;
    rec[TFP_RT_REC_VM_LX] = vn(position[0]);
    rec[TFP_RT_REC_VM_LY] = vn(position[1]);
    rec[TFP_RT_REC_VM_LZ] = vn(position[2]);
    if (!read_number(rt, rig + 0x88, &address) || !read_floats(rt, vn(address + 5 * 0x40 + 0x30), 3, world)) return;
    in_camera(camera, world[0], world[1], world[2], &rec[TFP_RT_REC_VM_CX]);
  }
  if (is_num(F(*s, SNAP, WEAPON_UNIT))) {
    if (!alignment_object(rt, F(*s, SNAP, WEAPON_UNIT), s, &rig)) return;
    if (!read_pointer(rt, rig + 0x88, &pointer) || !read_floats(rt, pointer, 16, world)) return;
    in_camera(camera, world[12], world[13], world[14], &rec[TFP_RT_REC_GUN_CX]);
    double dot = world[4] * camera[4] + world[5] * camera[5] + world[6] * camera[6];
    rec[TFP_RT_REC_GUN_TURN] = vn(acos(lua_max(-1, lua_min(1, dot))) * 57.29577951308232);
  }
}
static bool arms_wanted(TfpRt *rt) { /* :545 */
  if (!(HAS(MENU) && H->menu_fird_person_arms)) return true;
  V value = VNIL;
  CALL(H->menu_fird_person_arms(H, &value));
  return is_true(value);
}
static V sdk_handles(TfpRt *rt) { /* head and head.sdk_handles and head:sdk_handles() */
  V value = VNIL;
  if (!HAS(HEAD) || !H->head_sdk_handles) return VNIL;
  CALL(H->head_sdk_handles(H, &value));
  return value;
}
typedef struct RecordBox { bool ok; V why; } RecordBox;
static void record_presentation(TfpRt *rt, void *arg) {
  const RecordBox *box = arg;
  Pair p;
  V value = VNIL;
  if (!HAS(DIAGNOSTICS) || !rt->has_context) return;
  TfpRtTable s, state, cam, lens, transition, placement, movement, head_state;
  if (!view_snapshot(rt, &s)) tab_empty(&s);
  tab_clear(&state);
  if (HAS(CONTROLLER)) table_or_empty(H->controller_result(H, &state), &state); else tab_empty(&state);
  tab_clear(&cam);
  if (HAS(CAMERA)) table_or_empty(H->camera_result(H, &cam), &cam); else tab_empty(&cam);
  tab_clear(&lens);
  if (HAS(FOV)) table_or_empty(H->fov_result(H, &lens), &lens); else tab_empty(&lens);
  tab_clear(&transition);
  if (HAS(PRESENTATION)) { CALL(H->transition_result(H, &transition)); table_or_empty(true, &transition); } else tab_empty(&transition);
  V foreign = vb(false);
  if (HAS(FOREIGN_CAMERA)) { foreign = foreign_camera(rt); if (!truthy(foreign)) foreign = vb(false); }
  bool edit = false;
  if (HAS(EDITOR)) { OATT1(p, editor_is_active); edit = is_true(p.a); }
  V blocked = vb(false);
  if (HAS(RECOVERY)) {
    V name = VNIL, phase = VNIL;
    double at = recovery_time(rt);
    CALL(H->recovery_blocks_view(H, at, &blocked, &name, &phase));
    if (!truthy(blocked)) blocked = vb(false);
  }
  bool pinned = false;
  if (HAS(CAMERA) && H->camera_is_pinned) { OATT1(p, camera_is_pinned, &s); pinned = is_true(p.a); }
  double timestamp = recovery_time(rt);
  if (HAS(CAMERA) && !truthy(H->camera_retired(H)) && H->camera_look_stats &&
      (!truthy(rt->diagnostics_look_at) || timestamp < rt->diagnostics_look_at.number || timestamp - rt->diagnostics_look_at.number >= .1)) {
    TfpRtTable look;
    tab_clear(&look);
    if (!H->camera_look_stats(H, &look)) rt->diagnostics_look = vb(false);
    else if (!look.present) rt->diagnostics_look = VNIL;
    else { kept_set(&rt->look, &look); rt->diagnostics_look = vr(look.ref); }
    rt->diagnostics_look_at = vn(timestamp);
  }
  TfpRtTable look;
  if (rt->diagnostics_look.kind == TFP_RT_REF) look = rt->look.table; else tab_empty(&look);
  tab_clear(&placement);
  if (HAS(CONTROLLER)) table_or_empty(H->controller_get_placement(H, &placement), &placement); else tab_empty(&placement);
  tab_clear(&movement);
  if (HAS(MOTION)) table_or_empty(H->motion_result(H, &movement), &movement); else tab_empty(&movement);
  V control_mask = VNIL, control_guard = VNIL, weapon_ready = VNIL, entry_block = VNIL;
  V bytes = F(s, SNAP, CONTROLLER_BYTES);
  if (is_text(bytes) && bytes.length == 6) {
    const unsigned char *b = (const unsigned char *)bytes.text;
    control_mask = vn((double)b[0] + (double)b[1] * 256 + (double)b[2] * 65536 + (double)b[3] * 16777216);
    control_guard = vn(b[5]);
  }
  bool enabled = is_true(F(state, STATE, ENABLED));
  V phase = F(state, STATE, PHASE);
  if (enabled && !text_is(phase, "running") && HAS(BACKEND)) {
    TfpRtTable full;
    V problem = VNIL;
    tab_clear(&full);
    if (!H->backend_snapshot(H, &full, &problem)) { tab_clear(&full); problem = vt(keep(rt, H->raised)); }
    if (full.present) { weapon_ready = vb(!is_false(F(full, SNAP, WEAPON_READY))); entry_block = F(full, SNAP, REASON); }
    else entry_block = vt(tostr(rt, problem));
  }
  tab_clear(&head_state);
  if (HAS(HEAD)) table_or_empty(H->head_result(H, &head_state), &head_state); else tab_empty(&head_state);
  V rec[TFP_RT_REC_N];
  for (int i = 0; i < TFP_RT_REC_N; i++) rec[i] = VNIL;
  if (HAS(BACKEND) && is_num(F(s, SNAP, CAMERA))) { /* :456-471 */
    double address;
    V raw = VNIL;
    if (read_number(rt, F(s, SNAP, CAMERA).number + 0x18, &address) && read_number(rt, address + 0x28, &address) &&
        H->backend_read(H, vn(address + 16), 48, &raw) && is_text(raw) && raw.length == 48) {
      static const int offsets[6] = {32, 36, 40, 0, 4, 8}; /* cam_x, cam_y, cam_z, cam_fx, cam_fy, cam_fz */
      for (int i = 0; i < 6; i++) {
        double number;
        if (f32((const unsigned char *)raw.text + offsets[i], &number)) rec[TFP_RT_REC_CAM_X + i] = vn(number);
      }
    }
    rig_in_camera(rt, &s, rec);
  }
  bool safe = is_true(F(s, SNAP, SAFE));
  V kind = F(s, SNAP, CAMERA_KIND);
  rec[TFP_RT_REC_TIME] = vn(recovery_time(rt));
  rec[TFP_RT_REC_ENABLED] = vb(enabled);
  rec[TFP_RT_REC_CONTROL_MASK] = control_mask;
  rec[TFP_RT_REC_CONTROL_GUARD] = control_guard;
  rec[TFP_RT_REC_SAFE] = vb(safe);
  rec[TFP_RT_REC_AIMING] = vb(is_true(F(s, SNAP, AIMING)));
  rec[TFP_RT_REC_SNAPSHOT_REASON] = F(s, SNAP, REASON);
  rec[TFP_RT_REC_WEAPON_READY] = weapon_ready;
  rec[TFP_RT_REC_ENTRY_BLOCK] = entry_block;
  rec[TFP_RT_REC_ACTION] = F(movement, MOTION, ACTION);
  rec[TFP_RT_REC_HEAD_DISTANCE] = F(cam, CAM, HEAD_DISTANCE);
  rec[TFP_RT_REC_SOFTENED] = F(movement, MOTION, SOFTENED);
  rec[TFP_RT_REC_HEAD_CALLS] = F(cam, CAM, REPLACED);
  rec[TFP_RT_REC_CAMERA_GATE] = F(cam, CAM, ERROR);
  rec[TFP_RT_REC_HEAD_PHASE] = F(head_state, TARGET, PHASE);
  rec[TFP_RT_REC_PLAYER] = F(s, SNAP, PLAYER_IDENTITY);
  rec[TFP_RT_REC_WEAPON] = HAS(BACKEND) ? H->backend_placement_weapon_key(H) : VNIL;
  rec[TFP_RT_REC_REQUEST] = F(s, SNAP, RECORD);
  rec[TFP_RT_REC_CAMERA_KIND] = kind;
  rec[TFP_RT_REC_ADS] = vb(is_true(F(s, SNAP, CONTROLLER_ACTIVE)));
  rec[TFP_RT_REC_FOREIGN] = foreign;
  rec[TFP_RT_REC_EDITOR] = vb(edit);
  rec[TFP_RT_REC_PHASE] = phase;
  rec[TFP_RT_REC_TRANSITION_EPOCH] = F(transition, TRANS, EPOCH);
  rec[TFP_RT_REC_TRANSITION_PROGRESS] = F(transition, TRANS, PROGRESS);
  rec[TFP_RT_REC_PINNED] = vb(pinned);
  rec[TFP_RT_REC_PIN_EXPECTED] = vb(false);
  rec[TFP_RT_REC_CAMERA_RETIRED] = HAS(CAMERA) ? vb(is_true(H->camera_retired(H))) : VNIL;
  bool open_view = !truthy(blocked) && !truthy(foreign) && !edit;
  rec[TFP_RT_REC_VIEW_EXPECTED] = vb(enabled && text_is(F(state, STATE, MODE), "pose_offset") && safe && open_view);
  rec[TFP_RT_REC_VIEW_VISIBLE] = vb(num_is(kind, 6) || (num_is(kind, 4) && safe && text_is(F(cam, CAM, PHASE), "active") &&
                                                       is_true(F(cam, CAM, ARMED)) && num_is(F(cam, CAM, ERROR), 0)));
  rec[TFP_RT_REC_RECOVERING] = blocked;
  rec[TFP_RT_REC_FOV_NATIVE] = F(lens, FOV, LAST_NATIVE);
  rec[TFP_RT_REC_FOV_APPLIED] = F(lens, FOV, LAST_APPLIED);
  rec[TFP_RT_REC_CAMERA_BLEND] = F(cam, CAM, BLEND);
  rec[TFP_RT_REC_POSE_BLEND] = F(state, STATE, BLEND);
  rec[TFP_RT_REC_CLEANUP_PENDING] = F(state, STATE, CLEANUP_PENDING);
  rec[TFP_RT_REC_CAMERA_EPOCH] = F(cam, CAM, TRANSITION_EPOCH);
  rec[TFP_RT_REC_FOV_EPOCH] = F(lens, FOV, TRANSITION_EPOCH);
  NEED(CONTROLLER, 528, "upvalue 'controller'");
  TfpRtTable lease = controller_lease(rt);
  if (lease.present) {
    TfpRtTable pose;
    NEED(BACKEND, 528, "upvalue 'backend'");
    tab_clear(&pose);
    H->backend_presentation_pose_lease(H, &pose);
    if (same_table(&pose, &lease)) {
      value = H->backend_presentation_pose_epoch(H);
      if (truthy(value)) rec[TFP_RT_REC_POSE_EPOCH] = value;
    }
  }
  for (int i = 0; i < 6; i++) rec[TFP_RT_REC_PLACEMENT_X + i] = placement.f[TFP_RT_PLACE_X + i];
  rec[TFP_RT_REC_MOTION_X] = F(movement, MOTION, DX);
  rec[TFP_RT_REC_MOTION_Y] = F(movement, MOTION, DY);
  rec[TFP_RT_REC_MOTION_Z] = F(movement, MOTION, DZ);
  for (int i = 0; i < 6; i++) rec[TFP_RT_REC_LOOK_SEQUENCE + i] = look.f[TFP_RT_LOOK_SEQUENCE + i];
  rec[TFP_RT_REC_REASON] = !box->ok ? vt(tostr(rt, box->why)) : F(state, STATE, WAIT_REASON);
  CALL(H->diagnostics_record(H, rt->context.tick, rec));
  bool running = enabled && text_is(phase, "running");
  /* Last in the frame: every reader above saw the game's own aim state. */
  if (aim_released(rt) && running && open_view) {
    NEED(BACKEND, 537, "upvalue 'backend'");
    if (H->backend_keep_aim_view) H->backend_keep_aim_view(H, &s);
  }
  bool seated_view = is_true(F(s, SNAP, SEATED)) && num_is(kind, 4) && enabled && !truthy(foreign) && !edit;
  if (seated_view && !rt->seated_view_held) {
    NEED(BACKEND, 541, "upvalue 'backend'");
    if (H->backend_detach_seated_camera) H->backend_detach_seated_camera(H);
  }
  rt->seated_view_held = seated_view;
  if (HAS(ARM_PIECES)) {
    bool wanted = running && safe && num_is(kind, 6) && !truthy(foreign) && arms_wanted(rt) && is_true(H->profile_body_sleeves(H));
    V handles = sdk_handles(rt);
    CALL(H->arm_pieces_update(H, &s, wanted, handles));
  }
  if (HAS(GRIP_HAND)) {
    bool wanted = running && safe && num_is(kind, 6) && num_is(control_mask, 0) && !truthy(foreign) && arms_wanted(rt) &&
                  is_true(H->profile_grip_hand(H));
    V handles = sdk_handles(rt);
    CALL(H->grip_hand_update(H, &s, wanted, handles));
  }
}

/* ------------------------------------------------------------------ stop_pose and start_pose (:828-906) */
static V saved_placement(TfpRt *rt, V key, V action, V *second) { /* :156-158 */
  V first = VNIL;
  *second = VNIL;
  NEED(ACTIONS, 157, "upvalue 'Actions'");
  CALL(H->actions_resolve(H, key, truthy(action) ? action : vt("idle"), &first, second));
  return first;
}
static void recovery_restart(TfpRt *rt) {
  if (HAS(RECOVERY)) { CALL(H->recovery_reset(H)); rt->recovery_enabled = vb(false); }
}
static Pair refuse(const char *text) { Pair p = {vb(false), vt(text)}; return p; }
static Pair stop_pose(TfpRt *rt, V keep_handoff) {
  Pair fire = {vb(true), VNIL}, native_aim = fire, markers, fird = fire, warm = fire, editor = fire, done, fov, blur, camera, head, movement,
       dof, compat, near_clip;
  V value = VNIL;
  recovery_restart(rt);
  if (HAS(PRESENTATION)) CALL(H->transition_reset(H, "manual stop"));
  if (!truthy(keep_handoff)) rt->probe_deadline = VNIL;
  if (!truthy(keep_handoff) && HAS(FIRD_ENTRY)) CALL(H->fird_entry_cancel(H));
  rt->fird_capture_phase = VNIL;
  rt->fp_dof_player = VNIL;
  if (HAS(HELD_FIRE)) OATT(fire, held_fire_stop);
  if (HAS(NATIVE_AIM)) OATT(native_aim, native_aim_stop);
  OATT(markers, aim_markers_stop);
  if (HAS(FIRD)) OATT(fird, fird_stop);
  if (HAS(FIRD_WARM)) OATT(warm, fird_warm_stop);
  CALL(H->editor_is_active(H, &value));
  if (truthy(value)) OATT(editor, editor_cancel, "FPV switched off");
  OATT(done, controller_stop, "manual stop");
  OATT(fov, fov_stop);
  OATT(blur, blur_stop);
  OATT(camera, camera_stop);
  OATT(head, head_stop);
  OATT(movement, movement_stop);
  OATT(dof, dof_stop);
  OATT(compat, compat_stop);
  OATT(near_clip, near_stop);
  if (HAS(ARM_PIECES)) OATTX(arm_pieces_stop);
  if (HAS(GRIP_HAND)) OATTX(grip_hand_stop);
  OATTX(motion_reset);
  const Pair *order[] = {&fire, &native_aim, &markers, &fird, &warm, &done, &editor, &fov, &blur, &camera, &head, &movement, &dof, &near_clip,
                         &compat};
  for (size_t i = 0; i < sizeof order / sizeof *order; i++)
    if (!is_true(order[i]->a)) { Pair p = {vb(false), order[i]->b}; return p; }
  Pair ok = {vb(true), VNIL};
  return ok;
}
static const char *pending_cleanup(TfpRt *rt, bool with_controller, bool with_editor_first) {
  V value = VNIL;
  (void)with_editor_first;
  if (with_controller) {
    CALL(H->controller_is_clean(H, &value));
    if (!truthy(value)) return "A test or its cleanup is already active";
  }
  CALL(H->fov_is_clean(H, &value));
  if (!truthy(value)) return "FOV cleanup is still pending";
  CALL(H->blur_is_clean(H, &value));
  if (!truthy(value)) return "Weapon blur cleanup is still pending";
  CALL(H->camera_is_clean(H, &value));
  if (!truthy(value)) return "Head-camera cleanup is still pending";
  CALL(H->head_is_clean(H, &value));
  if (!truthy(value)) return "Head visibility cleanup is still pending";
  CALL(H->movement_is_clean(H, &value));
  if (!truthy(value)) return "Movement cleanup is still pending";
  CALL(H->dof_is_clean(H, &value));
  if (!truthy(value)) return "DoF restoration is still pending";
  CALL(H->compat_is_clean(H, &value));
  if (!truthy(value)) return "Camera handoff cleanup is pending";
  return NULL;
}
static Pair start_pose(TfpRt *rt, V seconds, V capture_entry) {
  Pair p, done;
  V value = VNIL, extra = VNIL;
  CALL(H->aim_markers_is_clean(H, &value));
  if (!truthy(value)) return refuse("Aiming-marker restoration is pending");
  if (HAS(FIRD_ENTRY)) {
    CALL(H->fird_entry_is_pending(H, &value));
    if (truthy(value) && !truthy(capture_entry)) return refuse("Fird Person is switching views");
  }
  if (HAS(FIRD)) {
    CALL(H->fird_is_clean(H, &value));
    if (!truthy(value)) return refuse("FirdPerson must finish cleanup first");
  }
  CALL(H->editor_is_active(H, &value));
  if (truthy(value)) return refuse("Placement editor or its cleanup is active");
  const char *pending = pending_cleanup(rt, true, false);
  if (pending) return refuse(pending);
  recovery_restart(rt);
  value = saved_placement(rt, VNIL, VNIL, &extra);
  CALL(H->controller_set_placement(H, value, extra, &p.a, &p.b));
  if (!is_true(p.a)) { p.a = vb(false); return p; }
  CALL(H->controller_start(H, seconds, vt("pose_offset"), &done.a, &done.b));
  if (!is_true(done.a)) { OATTX(camera_stop); OATTX(compat_stop); return done; }
  Pair adjusted = {vb(true), VNIL};
  if (!truthy(foreign_at(rt, 847))) adjusted = managed_update(rt, TG_FOV, true);
  if (is_true(adjusted.a) && !truthy(foreign_at(rt, 848))) adjusted = managed_update(rt, TG_BLUR, true);
  if (!is_true(adjusted.a)) {
    OATTX(controller_stop, "FOV policy unavailable");
    OATTX(fov_stop);
    OATTX(blur_stop);
    OATTX(camera_stop);
    OATTX(compat_stop);
    adjusted.a = vb(false);
    return adjusted;
  }
  done.a = controller_is_enabled(rt);
  return done;
}

/* ------------------------------------------------------------------ shutdown (:185-262) */
static void shutdown_body(TfpRt *rt, Pair *out) {
  Pair ok = {vb(true), VNIL};
  Pair native_aim = ok, fire = ok, render = ok, markers = ok, fird = ok, warm = ok, editor = ok, controller = ok, fov = ok, blur = ok,
       camera = ok, head = ok, movement = ok, dof = ok, near_clip = ok, compat = ok;
  V value = VNIL;
  rt->stopping = true;
  if (HAS(NATIVE_AIM)) OATT(native_aim, native_aim_shutdown);
  if (HAS(HELD_FIRE)) OATT(fire, held_fire_stop);
  if (HAS(READY_POSE)) OATTX(ready_pose_stop);
  if (HAS(FIRD_ENTRY)) CALL(H->fird_entry_cancel(H));
  if (HAS(MENU)) CALL(H->menu_begin_shutdown(H));
  if (HAS(FIRD_RENDER)) OATT(render, fird_render_stop);
  if (HAS(AIM_MARKERS)) OATT(markers, aim_markers_shutdown);
  if (HAS(FIRD)) OATT(fird, fird_stop);
  if (HAS(FIRD_WARM)) OATT(warm, fird_warm_stop);
  if (HAS(EDITOR)) OATT(editor, editor_shutdown);
  if (HAS(CONTROLLER)) OATT(controller, controller_shutdown);
  if (HAS(FOV)) OATT(fov, fov_shutdown);
  if (HAS(BLUR)) OATT(blur, blur_shutdown);
  if (HAS(CAMERA)) OATT(camera, camera_shutdown);
  if (HAS(HEAD)) OATT(head, head_shutdown);
  if (HAS(MOVEMENT)) OATT(movement, movement_shutdown);
  if (HAS(DOF)) OATT(dof, dof_shutdown);
  if (HAS(NEAR)) OATT(near_clip, near_shutdown);
  if (HAS(MOTION)) OATTX(motion_reset);
  if (HAS(COMPAT)) {
    OATT(compat, compat_shutdown);
    rt->handoff_ready = vb(is_true(compat.a));
  }
  if (rt->has_context) ready_update(rt);
  out->a = vb(false);
  if (HAS(READY_POSE)) {
    CALL(H->ready_pose_is_clean(H, &value));
    if (!truthy(value)) { out->b = vt("Ready stance restoration pending"); return; }
  }
  if (is_false(native_aim.a)) { out->b = native_aim.b; return; }
  const Pair *order[] = {&fire, &render, &markers, &fird, &warm, &controller};
  for (size_t i = 0; i < sizeof order / sizeof *order; i++)
    if (!is_true(order[i]->a)) { out->b = order[i]->b; return; }
  if (!truthy(editor.a)) { out->b = editor.b; return; }
  const Pair *rest[] = {&fov, &blur, &camera, &head, &movement, &dof, &near_clip, &compat};
  for (size_t i = 0; i < sizeof rest / sizeof *rest; i++)
    if (!is_true(rest[i]->a)) { out->b = rest[i]->b; return; }
  if (HAS(MENU)) {
    Pair done = {VNIL, VNIL};
    CALL(H->menu_shutdown(H, &done.a, &done.b));
    if (!is_true(done.a)) { out->b = done.b; return; }
  }
  if (HAS(API) && num_is(H->global_get(H), 1)) H->global_set(H, false);
  static const int cleared[] = {TFP_RT_O_CONTROLLER, TFP_RT_O_MENU, TFP_RT_O_EDITOR, TFP_RT_O_FOV, TFP_RT_O_CAMERA, TFP_RT_O_MOTION,
    TFP_RT_O_COMPAT, TFP_RT_O_HEAD, TFP_RT_O_MOVEMENT, TFP_RT_O_BLUR, TFP_RT_O_API, TFP_RT_O_FOREIGN_CAMERA, TFP_RT_O_BACKEND, TFP_RT_O_DOF,
    TFP_RT_O_FIRD, TFP_RT_O_NEAR, TFP_RT_O_FIRD_ENTRY, TFP_RT_O_FIRD_WARM, TFP_RT_O_FIRD_RENDER, TFP_RT_O_AIM_MARKERS, TFP_RT_O_NATIVE_AIM,
    TFP_RT_O_READY_POSE, TFP_RT_O_READY_TIMING, TFP_RT_O_HELD_FIRE, TFP_RT_O_RECOVERY, TFP_RT_O_DIAGNOSTICS, TFP_RT_O_PRESENTATION,
    TFP_RT_O_RUNTIME_CLOCK, TFP_RT_O_PRESENTATION_MODULE};
  for (size_t i = 0; i < sizeof cleared / sizeof *cleared; i++) rt->has[cleared[i]] = false;
  rt->recovery_enabled = vb(false);
  rt->diagnostics_fault = VNIL;
  rt->diagnostics_look = VNIL;
  rt->diagnostics_look_at = VNIL;
  rt->recovery_owner.present = false;
  rt->owner_draining.present = false;
  out->a = vb(true);
  out->b = VNIL;
}

/* ------------------------------------------------------------------ update_frame (:561-640) */
static void probe_update(TfpRt *rt) { /* :1049-1053 */
  if (truthy(rt->probe_deadline) && clock_now(rt) >= rt->probe_deadline.number) {
    rt->probe_deadline = VNIL;
    stop_pose(rt, VNIL);
  }
}
static void menu_update(TfpRt *rt, int line) {
  NEED(MENU, line, "upvalue 'menu'");
  CALL(H->menu_update(H));
}
static void update_frame(TfpRt *rt, void *unused) {
  Pair p;
  V value = VNIL;
  (void)unused;
  rt->fird_tick = rt->fird_tick + 1;
  if (HAS(PROBE_UPDATE)) probe_update(rt);
  ATT(COMPAT, p, compat_update);
  rt->handoff_ready = vb(is_true(p.a));
  if (HAS(NATIVE_AIM)) OATTX(native_aim_update);
  if (HAS(FIRD_ENTRY)) {
    CALL(H->fird_entry_is_enabled(H, &value));
    if (truthy(value)) {
      OATT(p, fird_entry_update);
      if (!is_true(p.a)) log_text(rt, fmt(rt, "Fird Person: %s", tostr(rt, p.b)));
      CALL(H->fird_entry_is_pending(H, &value));
      if (truthy(value)) {
        CALL(H->fird_entry_uses_fp(H, &value));
        if (!truthy(value)) {
          if (HAS(FIRD_WARM)) {
            CALL(H->fird_warm_is_clean(H, &value));
            if (!truthy(value)) ATTX(DOF, dof_update);
          }
          menu_update(rt, 574);
          return;
        }
      }
    }
  }
  bool fird_enabled = HAS(FIRD) && truthy(fird_is_enabled(rt));
  if (fird_enabled) {
    if (HAS(FIRD_RENDER)) {
      TfpRtTable render;
      tab_clear(&render);
      CALL(H->fird_render_result(H, &render));
      if (!render.present) lua_error(rt, 578, "attempt to index a nil value");
      if (!truthy(F(render, RENDER, ACTIVE))) {
        tab_clear(&render);
        CALL(H->fird_render_result(H, &render));
        if (!render.present) lua_error(rt, 579, "attempt to index a nil value");
        V reason = truthy(F(render, RENDER, LAST_ERROR)) ? F(render, RENDER, LAST_ERROR) : vt("Render callback is unavailable");
        OATTX(fird_stop);
        log_text(rt, fmt(rt, "Fird Person: %s", tostr(rt, reason)));
        menu_update(rt, 580);
        return;
      }
    }
    OATT(p, fird_refresh_target);
    if (!is_true(p.a)) {
      ATTX(AIM_MARKERS, aim_markers_stop);
      ATTX(EDITOR, editor_cancel, "Fird Person target changed");
      OATTX(fird_stop);
      log_text(rt, fmt(rt, "Fird Person: %s", tostr(rt, p.b)));
      menu_update(rt, 586);
      return;
    }
    NEED(EDITOR, 588, "upvalue 'editor'");
    CALL(H->editor_update(H));
    CALL(H->fird_update(H));
    menu_update(rt, 588);
    return;
  } else if (HAS(FIRD)) {
    CALL(H->fird_is_clean(H, &value));
    if (!truthy(value)) {
      OATT(p, fird_stop);
      if (!is_true(p.a)) { menu_update(rt, 591); return; }
    }
  }
  if (HAS(FIRD_WARM)) {
    CALL(H->fird_warm_is_clean(H, &value));
    if (!truthy(value)) { OATTX(fird_warm_stop); rt->idle_clean = vb(false); }
  }
  if (HAS(RECOVERY)) {
    V enabled = controller_is_enabled(rt);
    if (!veq(enabled, rt->recovery_enabled)) {
      CALL(H->recovery_reset(H));
      rt->recovery_enabled = keep_into(enabled, rt->recovery_enabled_text, sizeof rt->recovery_enabled_text);
    }
  }
  if (recover_new_owner(rt)) { menu_update(rt, 598); return; }
  observe_presentation(rt);
  if (HAS(RECOVERY)) {
    CALL(H->recovery_has_policies(H, &value));
    if (truthy(value)) {
      TfpRtPolicies policies;
      memset(&policies, 0, sizeof policies);
      CALL(H->recovery_snapshot(H, &policies));
      if (!policies.present) lua_error(rt, 601, "attempt to index a nil value");
      for (int i = 0; i < policies.count && i < TFP_RT_POLICY_MAX; i++) {
        const char *name = policies.name[i];
        double at = recovery_time(rt);
        CALL(H->recovery_action(H, name, at, &value));
        CALL(H->recovery_needs_proof(H, name, &value));
        if (truthy(value)) {
          Proof proof = recovery_proof(rt);
          at = recovery_time(rt);
          CALL(H->recovery_revalidate(H, name, proof.v, at, &value));
        }
      }
    }
  }
  bool blocked = false;
  if (HAS(RECOVERY)) {
    V name = VNIL, phase = VNIL;
    double at = recovery_time(rt);
    CALL(H->recovery_blocks_view(H, at, &value, &name, &phase));
    blocked = truthy(value);
  }
  if (!blocked) CALL(H->controller_update(H));
  else if (controller_lease(rt).present) OATTX(controller_retry_cleanup);
  NEED(EDITOR, 610, "upvalue 'editor'");
  CALL(H->editor_update(H));
  bool calibrating = false;
  if (HAS(FIRD_ENTRY)) { CALL(H->fird_entry_uses_fp(H, &value)); calibrating = truthy(value); }
  if (calibrating) {
    ATT(MOTION, p, motion_capture_neutral, text_is(rt->fird_capture_phase, "ads"));
    if (!is_true(p.a)) log_text(rt, fmt(rt, "Fird Person calibration: %s", tostr(rt, p.b)));
  } else {
    NEED(MOTION, 614, "upvalue 'motion'");
    CALL(H->motion_update(H));
  }
  bool idle = !truthy(controller_is_enabled(rt));
  if (idle) { NEED(EDITOR, 615, "upvalue 'editor'"); CALL(H->editor_is_active(H, &value)); idle = !truthy(value); }
  if (!idle) rt->idle_clean = vb(false);
  else if (truthy(rt->idle_clean) && !truthy(foreign_at(rt, 617))) { menu_update(rt, 617); return; }
  if (truthy(foreign_at(rt, 618))) {
    ATTX(FOV, fov_stop);
    ATTX(BLUR, blur_stop);
    ATTX(CAMERA, camera_stop);
    ATTX(HEAD, head_stop);
    ATTX(MOVEMENT, movement_stop);
    ATTX(NEAR, near_stop);
    ATTX(DOF, dof_update);
    menu_update(rt, 622);
    return;
  }
  Pair adjusted = managed_update(rt, TG_FOV, false);
  Pair blur_ready = managed_update(rt, TG_BLUR, false);
  Pair camera_ready = managed_update(rt, TG_CAMERA, false);
  Pair head_ready = managed_update(rt, TG_HEAD, false);
  Pair movement_ready;
  CALL(H->editor_is_active(H, &value));
  if (truthy(value)) ATT(MOVEMENT, movement_ready, movement_stop);
  else movement_ready = managed_update(rt, TG_MOVEMENT, false);
  ATTX(NEAR, near_update);
  Pair dof_ready;
  ATT(DOF, dof_ready, dof_update);
  /* :634-638, each operand only when the ones before it were true */
  V clean = vb(idle && is_true(adjusted.a) && is_true(blur_ready.a) && is_true(camera_ready.a) && is_true(head_ready.a) &&
               is_true(movement_ready.a) && is_true(dof_ready.a));
  if (truthy(clean)) { NEED(NEAR, 636, "upvalue 'near'"); CALL(H->near_is_clean(H, &clean)); }
  if (truthy(clean) && HAS(FIRD_WARM)) CALL(H->fird_warm_is_clean(H, &clean));
  if (truthy(clean)) {
    TfpRtTable state;
    tab_clear(&state);
    if (!HAS(DOF)) ATT_NIL();
    table_or_empty(H->dof_result(H, &state), &state);
    clean = vb(is_false(F(state, DOF, OWNED)));
  }
  rt->idle_clean = clean;
  menu_update(rt, 639);
}

/* on_update's function (:1207-1224) */
static void update_body(TfpRt *rt, Pair *out) {
  bool ok = rt_pcall(rt, update_frame, NULL);
  V why = ok ? VNIL : rt->error;
  if (!ok) {
    if (HAS(HELD_FIRE)) OATTX(held_fire_stop);
    if (HAS(READY_POSE)) OATTX(ready_pose_stop);
  }
  ready_update(rt);
  if (HAS(AIM_MARKERS)) {
    if (ok && !rt->owner_draining.present) OATTX(aim_markers_update); else OATTX(aim_markers_stop);
  }
  RecordBox box = {ok, why};
  if (!rt_pcall(rt, record_presentation, &box)) {
    V fault = vt(tostr(rt, rt->error));
    if (!veq(fault, rt->diagnostics_fault)) {
      rt->diagnostics_fault = keep_into(fault, rt->fault_text, sizeof rt->fault_text);
      log_text(rt, fmt(rt, "Presentation diagnostics unavailable: %s", fault.text));
    }
  } else rt->diagnostics_fault = VNIL;
  if (!ok) raise_value(rt, why);
  out->a = vb(true);
}

/* ------------------------------------------------------------------ the module table */
typedef void EntryFn(TfpRt *rt, const TfpRtArgs *a, V *r);
#define ENTRY(name) static void e_##name(TfpRt *rt, const TfpRtArgs *a, V *r)
#define UNUSED_ARGS() do { (void)a; (void)r; } while (0)
static void give(V *r, Pair p) { r[0] = p.a; r[1] = p.b; }

ENTRY(on_update) { /* :1205-1225 */
  Pair out = {VNIL, VNIL};
  (void)a;
  if (!HAS(CONTROLLER) || rt->stopping) return;
  qualified_frame(rt, update_body, &out);
  r[0] = out.a;
}
ENTRY(on_disable) {
  Pair out = {VNIL, VNIL};
  (void)a;
  shutdown_body(rt, &out);
  give(r, out);
}
ENTRY(on_cleanup_poll) {
  Pair out = {VNIL, VNIL};
  (void)a;
  qualified_frame(rt, shutdown_body, &out);
  give(r, out);
}
ENTRY(on_enable) { /* :645-1204 */
  V value = VNIL, extra = VNIL;
  Pair p;
  UNUSED_ARGS();
  if (HAS(CONTROLLER)) assert_failed(rt, 646, "Previous native ADS cleanup has not completed");
  rt->fp_dof_player = VNIL;
  rt->fird_tick = 0;
  rt->ready_tick = 0;
  if (!num_is(H->global_get(H), 0)) assert_failed(rt, 648, "NativeADSCameraTest global is already owned");
  memset(rt->made, 0, sizeof rt->made); /* a new generation of closures */
  rt->entry_initialize = false;
  HAS(ACTIONS) = true;
  CALL(H->actions_configure_catalogue(H));
  if (!H->clock_now) assert_failed(rt, 163, "Engine FPV clock unavailable");
  rt->clock_previous = VNIL;
  MADE(BACKEND) = true;
  CALL(H->backend_new(H));
  HAS(BACKEND) = true;
  HAS(RUNTIME_CLOCK) = true;
  CALL(H->recovery_new(H));
  HAS(RECOVERY) = true;
  CALL(H->diagnostics_new(H, 720));
  HAS(DIAGNOSTICS) = true;
  HAS(PRESENTATION_MODULE) = true;
  CALL(H->transition_new(H, .18, .25));
  HAS(PRESENTATION) = true;
  rt->recovery_enabled = vb(false);
  MADE(PRESENT) = true;
  CALL(H->backend_set_pin_first_person(H, vb(false)));
  CALL(H->backend_set_camera_update_token(H, VNIL));
  MADE(CONTROLLER) = true;
  CALL(H->controller_new(H));
  HAS(CONTROLLER) = true;
  CALL(H->fov_new(H));
  HAS(FOV) = true;
  CALL(H->blur_new(H));
  HAS(BLUR) = true;
  CALL(H->camera_new(H));
  HAS(CAMERA) = true;
  CALL(H->compat_new(H));
  HAS(COMPAT) = true;
  CALL(H->model_shadows_new(H));
  MADE(HEAD) = true;
  CALL(H->head_new(H));
  HAS(HEAD) = true;
  CALL(H->movement_new(H));
  HAS(MOVEMENT) = true;
  value = VNIL;
  if (H->arm_pieces_new) CALL(H->arm_pieces_new(H, &value));
  HAS(ARM_PIECES) = truthy(value);
  value = VNIL;
  if (H->grip_hand_new) CALL(H->grip_hand_new(H, &value));
  HAS(GRIP_HAND) = truthy(value);
  MADE(DOF) = true;
  CALL(H->dof_new(H));
  HAS(DOF) = true;
  MADE(NEAR) = true;
  CALL(H->near_new(H));
  HAS(NEAR) = true;
  rt->handoff_ready = vb(true);
  HAS(FOREIGN_CAMERA) = true;
  MADE(FOREIGN) = true;
  CALL(H->native_aim_source(H));
  MADE(NATIVE_AIM) = true;
  CALL(H->native_aim_new(H));
  HAS(NATIVE_AIM) = true;
  MADE(MARKERS_SOURCE) = true;
  CALL(H->aim_markers_source(H));
  MADE(MARKERS) = true;
  CALL(H->aim_markers_new(H));
  HAS(AIM_MARKERS) = true;
  MADE(ENTRY) = true;
  value = saved_placement(rt, VNIL, VNIL, &extra);
  CALL(H->controller_set_placement(H, value, extra, &p.a, &p.b)); /* :742 assert(...) */
  if (!truthy(p.a)) assert_value(rt, 742, p.b);
  rt->edit_mode = 0;
  MADE(HEADPOS) = true;
  CALL(H->head_positioner_new(H));
  rt->store_mode = 0;
  MADE(EDITOR) = true;
  CALL(H->editor_new(H));
  HAS(EDITOR) = true;
  MADE(MOTION) = true;
  CALL(H->motion_new(H));
  HAS(MOTION) = true;
  rt->probe_deadline = VNIL;
  HAS(PROBE_UPDATE) = true;
  MADE(MENU) = true;
  CALL(H->menu_new(H));
  HAS(MENU) = true;
  HAS(API) = true;
  H->global_set(H, true);
  rt->stopping = false;
  CALL(H->menu_update(H));
  log_text(rt, "Idle. F11 toggles True First Person; F8 edits arms and weapon placement.");
}

/* ------------------------------------------------------------------ what on_enable hands to the collaborators */
ENTRY(log) { (void)r; log_text(rt, tostr(rt, a->v[0])); }
ENTRY(clock) { (void)a; r[0] = vn(clock_now(rt)); }
static bool frame_scope(TfpRt *rt, int line) { /* backend.frame_active==true and ready_context */
  NEED(BACKEND, line, "upvalue 'backend'");
  return is_true(H->backend_frame_active(H)) && rt->has_context;
}
ENTRY(presentation_transition) { /* :672 */
  (void)a;
  if (!frame_scope(rt, 672)) return;
  TfpRtTable result;
  tab_clear(&result);
  NEED(PRESENTATION, 672, "upvalue 'presentation'");
  CALL(H->transition_result(H, &result));
  r[0] = tab_value(&result);
}
ENTRY(presentation_action) { /* :674 */
  if (!frame_scope(rt, 674)) return;
  NEED(PRESENTATION, 674, "upvalue 'presentation'");
  double now = clock_now(rt);
  CALL(H->transition_action(H, a->v[0], now, &r[0], &r[1]));
}
ENTRY(presentation_resume) { /* :675 */
  if (!frame_scope(rt, 675)) return;
  NEED(PRESENTATION, 675, "upvalue 'presentation'");
  double now = clock_now(rt);
  CALL(H->transition_resume(H, a->v[0], a->v[1], now, &r[0], &r[1]));
}
ENTRY(camera_before_entry) { /* :733-737 */
  Pair p = {VNIL, VNIL};
  (void)a;
  NEED(COMPAT, 734, "upvalue 'compat'");
  CALL(H->compat_prepare(H, &p.a, &p.b));
  if (!is_true(p.a)) { r[0] = vb(false); r[1] = p.b; return; }
  NEED(BLUR, 735, "upvalue 'blur'");
  CALL(H->blur_stop(H, &p.a, &p.b));
  if (!is_true(p.a)) { r[0] = vb(false); r[1] = p.b; return; }
  NEED(CAMERA, 736, "upvalue 'camera'");
  CALL(H->camera_prepare(H, &r[0], &r[1]));
}
ENTRY(action_placement) { r[0] = saved_placement(rt, a->v[0], a->v[1], &r[1]); }
ENTRY(action_mapping) { (void)a; r[0] = config_field(rt, "action_state_mapping"); }
ENTRY(foreign_camera) { (void)a; r[0] = foreign_camera(rt); }
ENTRY(fallback_ready) { (void)a; r[0] = fallback_ready(rt); }
ENTRY(pinned_ready) {
  TfpRtTable none;
  tab_clear(&none);
  r[0] = vb(pinned_ready(rt, a->table ? a->table : &none));
}
ENTRY(full_body) { (void)a; r[0] = HAS(FIRD) ? fird_is_enabled(rt) : VNIL; if (!truthy(r[0])) r[0] = vb(false); }
ENTRY(hide_torso) { e_full_body(rt, a, r); }
ENTRY(show_torso) { /* :695 */
  (void)a;
  r[0] = vb(false);
  if (HAS(MENU) && H->menu_show_torso) {
    CALL(H->menu_show_torso(H, &r[0]));
    if (!truthy(r[0])) r[0] = vb(false);
  }
}
ENTRY(seated_head) { (void)a; r[0] = vb(is_true(H->profile_seated_head(H))); }
ENTRY(active_view) { (void)a; r[0] = first_person_view(rt); }
ENTRY(dof_fallback_ready) { /* :710-712 */
  (void)a;
  r[0] = fallback_ready(rt);
  if (!truthy(r[0])) return;
  TfpRtTable state;
  tab_clear(&state);
  NEED(HEAD, 711, "upvalue 'head'");
  CALL(H->head_result(H, &state));
  if (!state.present) lua_error(rt, 711, "attempt to index a nil value");
  r[0] = vb(text_is(F(state, TARGET, PHASE), "active"));
}
ENTRY(near_options) { /* :713-716; ret: the menu's answer, or nil and hip, aim of {hip=0,aim=0} */
  (void)a;
  if (HAS(MENU) && H->menu_near_options) { CALL(H->menu_near_options(H, &r[0])); return; }
  r[1] = vn(0);
  r[2] = vn(0);
}
ENTRY(native_aim_enabled) { /* :721-723 */
  V value = VNIL;
  (void)a;
  r[0] = vb(false);
  if (!HAS(MENU)) return;
  CALL(H->menu_native_ads(H, &value));
  if (!truthy(value) || fird_on(rt)) return;
  if (HAS(FIRD_ENTRY)) { CALL(H->fird_entry_is_enabled(H, &value)); if (truthy(value)) return; }
  r[0] = vb(true);
}
ENTRY(markers_fird) { (void)a; r[0] = HAS(FIRD) ? vb(true) : VNIL; }
ENTRY(markers_source_global_setting) { (void)a; r[0] = config_field(rt, "show_aim_markers"); }
ENTRY(markers_global_setting) { (void)a; r[0] = config_field(rt, "show_aim_markers"); }
ENTRY(markers_settings) { (void)a; r[0] = config_field(rt, "aim_markers"); }
static void mod_set(TfpRt *rt, int line, const char *key, V value, V *r) {
  if (!H->mod_set) lua_error(rt, line, "attempt to call field 'set' (a nil value)");
  CALL(H->mod_set(H, key, value, &r[0], &r[1]));
}
ENTRY(markers_save) { mod_set(rt, 727, "aim_markers", a->v[0], r); }
ENTRY(markers_save_global) { mod_set(rt, 729, "show_aim_markers", a->v[0], r); }
ENTRY(hp_fird_enabled) { e_full_body(rt, a, r); }
ENTRY(hp_get) { (void)a; r[0] = config_field(rt, "fird_head_camera"); }
ENTRY(hp_set) { mod_set(rt, 747, "fird_head_camera", a->v[0], r); }

/* placement_controller (:748-752): the head positioner, Fird Person or the controller. */
typedef enum PcTarget { PC_HEAD, PC_FIRD, PC_CONTROLLER } PcTarget;
static PcTarget pc_target(TfpRt *rt) {
  if (rt->edit_mode == 1) return PC_HEAD;
  if (fird_on(rt)) return PC_FIRD;
  if (!HAS(CONTROLLER)) lua_error(rt, 751, "attempt to index local 'target' (a nil value)");
  return PC_CONTROLLER;
}
static void pc_placement_target(TfpRt *rt, V *r) {
  switch (pc_target(rt)) {
    case PC_HEAD: CALL(H->head_positioner_placement_target(H, &r[0], &r[1])); break;
    case PC_FIRD: CALL(H->fird_placement_target(H, &r[0], &r[1])); break;
    default: CALL(H->controller_placement_target(H, &r[0], &r[1])); break;
  }
}
static void pc_get_placement(TfpRt *rt, V *r) {
  TfpRtTable t;
  switch (pc_target(rt)) {
    case PC_HEAD: CALL(H->head_positioner_get_placement(H, &r[0], &r[1])); break;
    case PC_FIRD: CALL(H->fird_get_placement(H, &r[0], &r[1])); break;
    default: tab_clear(&t); CALL(H->controller_get_placement(H, &t)); r[0] = tab_value(&t); break;
  }
}
ENTRY(pc_is_enabled) {
  (void)a;
  switch (pc_target(rt)) {
    case PC_HEAD: CALL(H->head_positioner_is_enabled(H, &r[0], &r[1])); break;
    case PC_FIRD: CALL(H->fird_is_enabled(H, &r[0])); break;
    default: CALL(H->controller_is_enabled(H, &r[0])); break;
  }
}
ENTRY(pc_is_clean) {
  (void)a;
  switch (pc_target(rt)) {
    case PC_HEAD: CALL(H->head_positioner_is_clean(H, &r[0], &r[1])); break;
    case PC_FIRD: CALL(H->fird_is_clean(H, &r[0])); break;
    default: CALL(H->controller_is_clean(H, &r[0])); break;
  }
}
ENTRY(pc_get_placement) { (void)a; pc_get_placement(rt, r); }
ENTRY(pc_set_placement) {
  switch (pc_target(rt)) {
    case PC_HEAD: CALL(H->head_positioner_set_placement(H, a->v[0], a->v[1], &r[0], &r[1])); break;
    case PC_FIRD: CALL(H->fird_set_placement(H, a->v[0], a->v[1], &r[0], &r[1])); break;
    default: CALL(H->controller_set_placement(H, a->v[0], a->v[1], &r[0], &r[1])); break;
  }
}
ENTRY(pc_result) {
  TfpRtTable t;
  (void)a;
  switch (pc_target(rt)) {
    case PC_HEAD: CALL(H->head_positioner_result(H, &r[0], &r[1])); break;
    case PC_FIRD: CALL(H->fird_result(H, &r[0], &r[1])); break;
    default: tab_clear(&t); CALL(H->controller_result(H, &t)); r[0] = tab_value(&t); break;
  }
}
ENTRY(pc_placement_target) { (void)a; pc_placement_target(rt, r); }
ENTRY(pc_start) {
  switch (pc_target(rt)) {
    case PC_HEAD: CALL(H->head_positioner_start(H, a->v[0], a->v[1], &r[0], &r[1])); break;
    case PC_FIRD: CALL(H->fird_start(H, a->v[0], &r[0], &r[1])); break;
    default: CALL(H->controller_start(H, a->v[0], a->v[1], &r[0], &r[1])); break;
  }
}

/* store (:753-814) */
ENTRY(store_get) {
  if (rt->edit_mode == 1) { CALL(H->head_positioner_get_saved(H, &r[0], &r[1])); return; }
  r[0] = saved_placement(rt, a->v[0], a->v[1], &r[1]);
}
ENTRY(store_get_action) { e_store_get(rt, a, r); }
static bool controller_off(TfpRt *rt, int line) { /* not controller:is_enabled() */
  NEED(CONTROLLER, line, "upvalue 'controller'");
  return !truthy(controller_is_enabled(rt));
}
ENTRY(store_can_start) {
  V value = VNIL;
  (void)a;
  r[0] = vb(false);
  if (HAS(FIRD_ENTRY)) {
    CALL(H->fird_entry_is_pending(H, &value));
    if (truthy(value)) { r[1] = vt("Fird Person is switching views"); return; }
  }
  if (rt->edit_mode == 1 && !fird_on(rt)) { r[1] = vt("Enable FirdPerson before editing its head camera"); return; }
  if (fird_on(rt)) {
    if (truthy(foreign_at(rt, 757))) { r[1] = vt("Another camera owns the view"); return; }
    if (truthy(H->fird_native_animation(H)) && rt->edit_mode != 1) {
      r[1] = vt("Natural Fird Person uses the game's arm and weapon placement. Shift+F8 edits the eye camera.");
      return;
    }
    r[0] = vb(true);
    return;
  }
  if (controller_off(rt, 761)) { NEED(FOV, 761, "upvalue 'fov'"); CALL(H->fov_is_clean(H, &value)); if (!truthy(value)) { r[1] = vt("FOV cleanup is still pending"); return; } }
  if (controller_off(rt, 762)) { NEED(BLUR, 762, "upvalue 'blur'"); CALL(H->blur_is_clean(H, &value)); if (!truthy(value)) { r[1] = vt("Weapon blur cleanup is still pending"); return; } }
  if (controller_off(rt, 763)) { NEED(CAMERA, 763, "upvalue 'camera'"); CALL(H->camera_is_clean(H, &value)); if (!truthy(value)) { r[1] = vt("Head-camera cleanup is still pending"); return; } }
  if (controller_off(rt, 764)) { NEED(HEAD, 764, "upvalue 'head'"); CALL(H->head_is_clean(H, &value)); if (!truthy(value)) { r[1] = vt("Head visibility cleanup is still pending"); return; } }
  if (controller_off(rt, 765)) { NEED(MOVEMENT, 765, "upvalue 'movement'"); CALL(H->movement_is_clean(H, &value)); if (!truthy(value)) { r[1] = vt("Movement cleanup is still pending"); return; } }
  if (controller_off(rt, 766)) { NEED(DOF, 766, "upvalue 'dof'"); CALL(H->dof_is_clean(H, &value)); if (!truthy(value)) { r[1] = vt("DoF restoration is still pending"); return; } }
  if (truthy(foreign_at(rt, 767))) { r[1] = vt("Photo Mode or another camera owns the view"); return; }
  r[0] = vb(true);
}
ENTRY(store_action_supported) { /* :769-776 */
  V action = a->v[0];
  static const char *const always[] = {"camera", "idle", "aim", "ads", "sprint", "reload"};
  r[0] = vb(true);
  for (size_t i = 0; i < sizeof always / sizeof *always; i++) if (text_is(action, always[i])) return;
  if (truthy(H->actions_observed(H, action))) return;
  if (!is_nil(H->config_table(H)) && truthy(H->config_action_mapped(H, action))) return;
  r[0] = vb(false);
}
ENTRY(store_target) { (void)a; pc_placement_target(rt, r); }
static void bump_revision(TfpRt *rt, int line) { /* backend.action_placement_revision=(backend.action_placement_revision or 0)+1 */
  NEED(BACKEND, line, "upvalue 'backend'");
  V revision = H->backend_action_placement_revision(H);
  if (truthy(revision) && !is_num(revision))
    lua_error(rt, line, fmt(rt, "attempt to perform arithmetic on a %s value", kind_name(revision)));
  CALL(H->backend_set_action_placement_revision(H, (truthy(revision) ? revision.number : 0) + 1));
}
ENTRY(store_save) { /* :776-786: value, key, identity, all_weapons, action */
  V value = a->v[0], key = a->v[1], identity = a->v[2], all_weapons = a->v[3], action = a->v[4];
  if (rt->edit_mode == 1) { CALL(H->head_positioner_save(H, value, key, identity, &r[0], &r[1])); return; }
  TfpRtTable current;
  V why = VNIL, target[2] = {VNIL, VNIL};
  tab_clear(&current);
  NEED(BACKEND, 778, "upvalue 'backend'");
  CALL(H->backend_snapshot(H, &current, &why));
  pc_placement_target(rt, target);
  if (!truthy(key) || !veq(target[0], key) || !veq(target[1], identity) || !current.present || !veq(F(current, SNAP, IDENTITY), identity)) {
    r[0] = vb(false);
    r[1] = vt("Weapon changed; reopen placement editor");
    return;
  }
  if (!H->mod_set) { r[0] = vb(false); r[1] = vt("Placement settings storage unavailable"); return; }
  V profiles = VNIL;
  CALL(H->actions_saved(H, key, truthy(action) ? action : vt("idle"), value, all_weapons, &profiles));
  CALL(H->mod_set(H, "action_placements", profiles, &r[0], &r[1]));
  if (is_true(r[0])) bump_revision(rt, 784);
}
ENTRY(store_select_mode) { /* :787-800 */
  V kind = a->v[0];
  if (!text_is(kind, "weapon") && !text_is(kind, "camera")) { r[0] = vb(false); r[1] = vt("Unknown editor mode"); return; }
  if (text_is(kind, "camera")) {
    NEED(CAMERA, 789, "upvalue 'camera'");
    if (truthy(H->camera_retired(H))) {
      r[0] = vb(false);
      r[1] = vt("Head-camera editing is unavailable because the camera policy was removed");
      return;
    }
  }
  rt->edit_mode = text_is(kind, "camera") ? 1 : 0;
  rt->store_mode = rt->edit_mode + 1;
  r[0] = vb(true);
}
ENTRY(store_reset_all) { /* :801-814 */
  (void)a;
  if (rt->edit_mode == 1) {
    V target[2] = {VNIL, VNIL};
    CALL(H->head_positioner_placement_target(H, &target[0], &target[1]));
    CALL(H->head_positioner_save(H, vr(tfp_rt_camera_baseline), target[0], target[1], &r[0], &r[1]));
    return;
  }
  static const char *const keys[3] = {"weapon_placements", "action_placements", "placement"};
  V before[3], values[3] = {vr(&tfp_rt_empty_table), vr(&tfp_rt_empty_table), vr(tfp_rt_default_placement)};
  if (is_nil(H->config_table(H))) lua_error(rt, 806, "attempt to index field 'config' (a nil value)");
  for (int i = 0; i < 3; i++) before[i] = H->config_field(H, keys[i]);
  for (int i = 0; i < 3; i++) {
    V ok[2] = {VNIL, VNIL};
    mod_set(rt, 809, keys[i], values[i], ok);
    if (!truthy(ok[0])) {
      for (int k = 0; k < 3; k++) {
        V ignored[2] = {VNIL, VNIL};
        if (!is_nil(before[k])) mod_set(rt, 810, keys[k], before[k], ignored);
      }
      r[0] = vb(false);
      r[1] = ok[1];
      return;
    }
  }
  bump_revision(rt, 812);
  r[0] = vb(true);
}
/* The store's plain fields, as select_mode leaves them (:791-798): actions, title, rotation, baseline, single_save,
 * save_label, all_label, footer. actions is Actions.actions or the text 'camera/Head camera/1' for the one-row camera list;
 * baseline is nil, 'function' or 'table'. */
ENTRY(store_fields) {
  (void)a;
  bool camera = rt->store_mode == 2;
  r[0] = camera ? vt("camera/Head camera/1") : H->actions_actions(H);
  if (rt->store_mode == 0) return;
  r[1] = vt(camera ? "HEAD CAMERA" : "WEAPON PLACEMENT");
  r[2] = vb(!camera);
  r[3] = vt(camera ? "table" : "function");
  r[4] = vb(camera);
  if (camera) { r[5] = vt("Save camera"); r[6] = vt("Save camera"); r[7] = vt("Camera only; weapon placements are unchanged"); }
}
/* store.baseline (:794): for a weapon the function's two answers; for the camera nil, nil and x, y, z. */
ENTRY(store_baseline) {
  if (rt->store_mode == 1) CALL(H->actions_default_placement(H, a->v[0], a->v[1], &r[0], &r[1]));
  else if (rt->store_mode == 2) for (int i = 0; i < 3; i++) r[2 + i] = vn(tfp_rt_camera_baseline[i]);
}
/* The motion options function (:816-821): the menu's answer, or nil and the four defaults. */
ENTRY(motion_options) {
  V defaults[TFP_RT_DEFAULTS_N];
  V scale = config_field(rt, "motion_scale");
  (void)a;
  defaults[TFP_RT_DEFAULTS_ENABLED] = vb(!is_false(config_field(rt, "motion_enabled")));
  defaults[TFP_RT_DEFAULTS_SCALE] = truthy(scale) ? scale : vn(1);
  defaults[TFP_RT_DEFAULTS_CAMERA_BOB_STRENGTH] = vn(0);
  defaults[TFP_RT_DEFAULTS_CAMERA_SWAY_STRENGTH] = vn(0);
  if (HAS(MENU) && H->menu_motion_options) { CALL(H->menu_motion_options(H, defaults, &r[0])); return; }
  for (int i = 0; i < TFP_RT_DEFAULTS_N; i++) r[1 + i] = defaults[i];
}

/* ------------------------------------------------------------------ the menu's and the api's functions */
ENTRY(start_pose) { give(r, start_pose(rt, a->v[0], a->v[1])); }
ENTRY(start_true_first_person) { (void)a; give(r, start_pose(rt, vn(0), VNIL)); }
ENTRY(stop_pose) { give(r, stop_pose(rt, a->v[0])); }
ENTRY(toggle_pose) { /* :908-911 */
  V value = VNIL;
  bool stop;
  (void)a;
  CALL(H->controller_is_enabled(H, &value));
  stop = truthy(value);
  if (!stop && HAS(FIRD)) { CALL(H->fird_is_clean(H, &value)); stop = !truthy(value); }
  if (!stop && HAS(FIRD_ENTRY)) { CALL(H->fird_entry_is_enabled(H, &value)); stop = truthy(value); }
  give(r, stop ? stop_pose(rt, VNIL) : start_pose(rt, vn(0), VNIL));
}
ENTRY(start_native) { /* :912-925 */
  V value = VNIL;
  r[0] = vb(false);
  CALL(H->aim_markers_is_clean(H, &value));
  if (!truthy(value)) { r[1] = vt("Aiming-marker restoration is pending"); return; }
  if (HAS(FIRD_ENTRY)) {
    CALL(H->fird_entry_is_pending(H, &value));
    if (truthy(value)) { r[1] = vt("Fird Person is switching views"); return; }
  }
  CALL(H->editor_is_active(H, &value));
  if (truthy(value)) { r[1] = vt("Placement editor or its cleanup is active"); return; }
  const char *pending = pending_cleanup(rt, false, false);
  if (pending) { r[1] = vt(pending); return; }
  if (truthy(foreign_at(rt, 923))) { r[1] = vt("Photo Mode or another camera owns the view"); return; }
  CALL(H->controller_start(H, a->v[0], vt("native"), &r[0], &r[1]));
}
ENTRY(start_free_aim) { /* :926-934 */
  V value = VNIL, ignored[2] = {VNIL, VNIL};
  (void)a;
  r[0] = vb(false);
  if (truthy(H->camera_retired(H))) { r[1] = vt("Free aim is unavailable because the camera policy was removed"); return; }
  CALL(H->editor_is_active(H, &value));
  if (truthy(value)) { r[1] = vt("Close the placement editor first"); return; }
  if (HAS(FIRD)) { CALL(H->fird_is_clean(H, &value)); if (!truthy(value)) { r[1] = vt("FirdPerson must be off"); return; } }
  if (truthy(foreign_at(rt, 930))) { r[1] = vt("Return to normal gameplay first"); return; }
  CALL(H->controller_is_enabled(H, &value));
  if (!truthy(value)) { r[1] = vt("Enable native FPV with F11 first"); return; }
  CALL(H->camera_observe_parent(H, &ignored[0], &ignored[1]));
  CALL(H->camera_free_aim(H, 30000, &r[0], &r[1]));
}
static V view_enabled(TfpRt *rt) { /* :1041-1043 */
  V value = VNIL;
  CALL(H->controller_is_enabled(H, &value));
  if (truthy(value)) return value;
  if (HAS(FIRD)) { value = fird_is_enabled(rt); if (truthy(value)) return value; }
  if (HAS(FIRD_ENTRY)) { CALL(H->fird_entry_is_enabled(H, &value)); if (truthy(value)) return value; }
  return vb(false);
}
static V view_clean(TfpRt *rt) { /* :1044-1047 */
  V value = VNIL;
  CALL(H->controller_is_clean(H, &value));
  if (!truthy(value)) return value;
  CALL(H->aim_markers_is_clean(H, &value));
  if (!truthy(value)) return value;
  if (HAS(NATIVE_AIM)) { CALL(H->native_aim_is_clean(H, &value)); if (!truthy(value)) return value; }
  if (HAS(FIRD)) { CALL(H->fird_is_clean(H, &value)); if (!truthy(value)) return value; }
  if (HAS(FIRD_WARM)) { CALL(H->fird_warm_is_clean(H, &value)); if (!truthy(value)) return value; }
  if (HAS(FIRD_ENTRY)) { CALL(H->fird_entry_is_pending(H, &value)); return vb(!truthy(value)); }
  return vb(true);
}
ENTRY(view_enabled) { (void)a; r[0] = view_enabled(rt); }
ENTRY(view_clean) { (void)a; r[0] = view_clean(rt); }
ENTRY(presentation_settings) { (void)a; r[0] = H->config_table(H); }
ENTRY(current_weapon) { /* :1060-1065; ret: true, key, identity, label, family, or nil */
  TfpRtTable info;
  (void)a;
  CALL(H->controller_placement_target(H, &r[1], &r[2]));
  if (!truthy(r[1])) { r[1] = VNIL; r[2] = VNIL; return; }
  tab_clear(&info);
  CALL(H->actions_weapon_info(H, r[1], &info));
  r[0] = vb(true);
  if (info.present) { r[3] = F(info, WINFO, NAME); r[4] = F(info, WINFO, FAMILY); }
}
ENTRY(save_presentation) { /* :1066-1080: name, value, expected_key, expected_identity */
  V name = a->v[0], value = a->v[1], expected_key = a->v[2], expected_identity = a->v[3];
  bool family = text_is(name, "family_placement_defaults"), near_clip = text_is(name, "near_clip_profiles");
  r[0] = vb(false);
  if (!family && !near_clip) { r[1] = vt("Unknown presentation setting"); return; }
  if (family && value.kind != TFP_RT_TRUE && value.kind != TFP_RT_FALSE) { r[1] = vt("Placement defaults must be on or off"); return; }
  if (near_clip && value.kind != TFP_RT_REF) { r[1] = vt("Clipping profiles must be a table"); return; }
  if (truthy(expected_key)) {
    V target[2] = {VNIL, VNIL}, why = VNIL;
    TfpRtTable fresh;
    CALL(H->controller_placement_target(H, &target[0], &target[1]));
    tab_clear(&fresh);
    if (!HAS(BACKEND)) ATT_NIL();
    if (!H->backend_snapshot(H, &fresh, &why)) tab_clear(&fresh);
    if (!veq(target[0], expected_key) || !veq(target[1], expected_identity) || !fresh.present ||
        !veq(F(fresh, SNAP, IDENTITY), expected_identity) || !truthy(F(fresh, SNAP, SAFE))) {
      r[1] = vt("Weapon changed; reopen its presentation settings");
      return;
    }
  }
  mod_set(rt, 1077, family ? "family_placement_defaults" : "near_clip_profiles", value, r);
  if (is_true(r[0]) && family) bump_revision(rt, 1078);
}
ENTRY(aiming_markers) { (void)a; CALL(H->aim_markers_target(H, &r[0], &r[1])); }
ENTRY(toggle_aiming_markers) { CALL(H->aim_markers_toggle(H, a->v[0], a->v[1], &r[0], &r[1])); }
ENTRY(aim_markers_setting) { (void)a; CALL(H->aim_markers_setting(H, &r[0], &r[1])); }
ENTRY(set_aim_markers_setting) { CALL(H->aim_markers_set_setting(H, a->v[0], &r[0], &r[1])); }
ENTRY(dev_status) { /* :1086-1095 */
  (void)a;
  if (HAS(RECOVERY)) {
    V blocked = VNIL, name = VNIL, phase = VNIL;
    double now = clock_now(rt);
    CALL(H->recovery_blocks_view(H, now, &blocked, &name, &phase));
    if (truthy(blocked)) {
      const char *phase_text = concat_operand(rt, 1089, "local 'phase'", phase);
      r[0] = vt(fmt(rt, "First person suspended: %s (%s)", concat_operand(rt, 1089, "local 'name'", name), phase_text));
      return;
    }
  }
  V fault = H->aim_markers_fault(H);
  if (truthy(fault)) r[0] = vt(fmt(rt, "Aiming markers: %s", concat_operand(rt, 1091, "field 'fault'", fault)));
}
ENTRY(seamless_version) { (void)rt; (void)a; r[0] = vt("0.5.41-smooth.66-cffi-26568081"); }
ENTRY(presentation_state) {
  TfpRtTable result;
  (void)a;
  if (!HAS(PRESENTATION)) return;
  tab_clear(&result);
  CALL(H->transition_result(H, &result));
  r[0] = tab_value(&result);
}
ENTRY(diagnostics) {
  (void)a;
  if (HAS(DIAGNOSTICS)) CALL(H->diagnostics_snapshot(H, &r[0]));
  if (!truthy(r[0])) r[0] = vr(&tfp_rt_empty_table);
}
ENTRY(recovery_status) {
  (void)a;
  r[0] = vr(&tfp_rt_empty_table);
  if (HAS(RECOVERY)) {
    TfpRtPolicies policies;
    memset(&policies, 0, sizeof policies);
    CALL(H->recovery_snapshot(H, &policies));
    if (policies.present) r[0] = vr(policies.ref);
  }
}
ENTRY(clear_diagnostics) { (void)a; if (HAS(DIAGNOSTICS)) CALL(H->diagnostics_clear(H)); r[0] = vb(true); }
ENTRY(observe_camera_parent) { (void)a; CALL(H->camera_observe_parent(H, &r[0], &r[1])); }
ENTRY(profile_camera) { (void)a; CALL(H->camera_profile(H, &r[0], &r[1])); }
ENTRY(camera_profile) { (void)a; CALL(H->camera_profile_stats(H, &r[0], &r[1])); }
ENTRY(camera_kick_stats) { (void)a; CALL(H->camera_kick_stats(H, &r[0], &r[1])); }
ENTRY(camera_parent_stats) { (void)a; CALL(H->camera_parent_stats(H, &r[0], &r[1])); }
ENTRY(camera_handoff) { CALL(H->camera_set_handoff(H, a->v[0], a->v[1], &r[0], &r[1])); }
ENTRY(camera_handoff_stats) { (void)a; CALL(H->camera_handoff_stats(H, &r[0], &r[1])); }
ENTRY(camera_cut_hold) { CALL(H->camera_set_cut_hold(H, a->v[0], a->v[1], a->v[2], a->v[3], &r[0], &r[1])); }
ENTRY(stop_free_aim) { (void)a; CALL(H->camera_free_aim(H, 0, &r[0], &r[1])); }
ENTRY(free_aim_stats) { (void)a; CALL(H->camera_free_aim_stats(H, &r[0], &r[1])); }
ENTRY(edit_placement) { (void)a; CALL(H->editor_request_open(H, &r[0], &r[1])); }
ENTRY(placement) { (void)a; pc_get_placement(rt, r); }
static bool pose_helper(TfpRt *rt, TfpRtTable *lease, int field) { /* :1133-1136 */
  *lease = controller_lease(rt);
  return lease->present && truthy(F(*lease, LEASE, ALIGNMENT)) && truthy(F(*lease, LEASE, HELPER)) &&
         text_is(F(*lease, LEASE, KIND), "pose") && truthy(lease->f[field]);
}
ENTRY(depth_stats) {
  TfpRtTable lease, depth;
  (void)a;
  if (!pose_helper(rt, &lease, TFP_RT_LEASE_DEPTH_STATS)) { r[1] = vt("No active depth-compensation lease"); return; }
  tab_clear(&depth);
  if (!H->helper_depth_stats(H, &lease, F(lease, LEASE, HANDLE), &depth, &r[1])) { r[1] = vt(keep(rt, H->raised)); return; }
  r[0] = tab_value(&depth);
}
ENTRY(input_stats) {
  TfpRtTable lease;
  (void)a;
  if (!pose_helper(rt, &lease, TFP_RT_LEASE_INPUT_STATS)) { r[1] = vt("No active first-person input lease"); return; }
  if (!H->helper_input_stats(H, &lease, F(lease, LEASE, HANDLE), &r[0], &r[1])) { r[0] = VNIL; r[1] = vt(keep(rt, H->raised)); }
}
/* api.status (:1154-1177): the status text, then the fields of the result table in the order of
 * STATUS_FIELDS (tools/retail_ref/runtime_spec.py). */
enum { ST_TEXT, ST_SELF, ST_FIRD, ST_FIRD_PHASE, ST_FIRD_ENABLED, ST_FIRD_WRITES, ST_FIRD_WEAPON, ST_FIRD_REFERENCE_NEEDED,
  ST_FIRD_SCENE_FOV, ST_FIRD_RENDER_FRAMES, ST_FIRD_PENDING, ST_FIRD_RENDERER, ST_ENABLED, ST_EDITOR_ACTIVE, ST_EDITOR_STATUS, ST_FOV,
  ST_ARM_PIECES, ST_GRIP_HAND, ST_BLUR, ST_CAMERA, ST_MOTION, ST_COMPAT, ST_HEAD, ST_MOVEMENT, ST_DOF, ST_NATIVE_AIM, ST_NEAR,
  ST_AIM_MARKERS, ST_READY_POSE, ST_HELD_FIRE };
#define RESULT_OF(slot, fn) do { TfpRtTable t_; tab_clear(&t_); CALL(H->fn(H, &t_)); r[slot] = tab_value(&t_); } while (0)
ENTRY(status) {
  TfpRtTable result;
  V value = VNIL;
  (void)a;
  tab_clear(&result);
  CALL(H->controller_result(H, &result));
  if (!result.present) lua_error(rt, 1157, "attempt to index local 'result' (a nil value)");
  r[ST_SELF] = tab_value(&result);
  r[ST_FIRD] = vb(HAS(FIRD));
  if (HAS(FIRD)) {
    r[ST_FIRD_PHASE] = H->fird_phase(H);
    r[ST_FIRD_ENABLED] = H->fird_enabled(H);
    r[ST_FIRD_WRITES] = H->fird_writes(H);
    r[ST_FIRD_WEAPON] = H->fird_weapon_key(H);
    r[ST_FIRD_REFERENCE_NEEDED] = vb(is_true(H->fird_reference_needed(H)));
    r[ST_FIRD_SCENE_FOV] = H->fird_scene_fov(H);
    r[ST_FIRD_RENDER_FRAMES] = H->fird_render_frames(H);
  }
  if (HAS(FIRD_RENDER)) RESULT_OF(ST_FIRD_RENDERER, fird_render_result);
  if (HAS(FIRD_ENTRY)) {
    CALL(H->fird_entry_is_pending(H, &value));
    if (truthy(value)) {
      for (int i = ST_FIRD_PHASE; i <= ST_FIRD_PENDING; i++) r[i] = VNIL;
      r[ST_FIRD] = vb(true);
      CALL(H->fird_entry_status(H, &r[ST_FIRD_PHASE]));
      r[ST_FIRD_ENABLED] = vb(true);
      r[ST_FIRD_PENDING] = vb(true);
    }
  }
  r[ST_ENABLED] = view_enabled(rt);
  CALL(H->editor_is_active(H, &r[ST_EDITOR_ACTIVE]));
  CALL(H->editor_status(H, &r[ST_EDITOR_STATUS]));
  RESULT_OF(ST_FOV, fov_result);
  if (HAS(ARM_PIECES)) RESULT_OF(ST_ARM_PIECES, arm_pieces_result);
  if (HAS(GRIP_HAND)) RESULT_OF(ST_GRIP_HAND, grip_hand_result);
  RESULT_OF(ST_BLUR, blur_result);
  RESULT_OF(ST_CAMERA, camera_result);
  RESULT_OF(ST_MOTION, motion_result);
  RESULT_OF(ST_COMPAT, compat_result);
  RESULT_OF(ST_HEAD, head_result);
  RESULT_OF(ST_MOVEMENT, movement_result);
  RESULT_OF(ST_DOF, dof_result);
  if (HAS(NATIVE_AIM)) RESULT_OF(ST_NATIVE_AIM, native_aim_result);
  RESULT_OF(ST_NEAR, near_result);
  RESULT_OF(ST_AIM_MARKERS, aim_markers_result);
  if (HAS(READY_POSE)) RESULT_OF(ST_READY_POSE, ready_pose_result);
  if (HAS(HELD_FIRE)) RESULT_OF(ST_HELD_FIRE, held_fire_result);
  V part[6] = {VNIL, VNIL, VNIL, VNIL, VNIL, VNIL};
  CALL(H->controller_status(H, &part[0]));
  CALL(H->fov_status(H, &part[1]));
  CALL(H->camera_status(H, &part[2]));
  CALL(H->blur_status(H, &part[3]));
  CALL(H->dof_status(H, &part[4]));
  CALL(H->near_status(H, &part[5]));
  for (int i = 5; i >= 0; i--) /* .. is right associative: the rightmost bad operand is reported */
    if (!is_text(part[i]) && !is_num(part[i])) lua_error(rt, 1176, fmt(rt, "attempt to concatenate a %s value", kind_name(part[i])));
  r[ST_TEXT] = vt(fmt(rt, "%s; FOV %s; camera %s; blur %s; DoF %s; near %s", tostr(rt, part[0]), tostr(rt, part[1]), tostr(rt, part[2]),
                      tostr(rt, part[3]), tostr(rt, part[4]), tostr(rt, part[5])));
}
ENTRY(preflight) { /* :1178-1198 */
  V value = VNIL;
  TfpRtTable snapshot;
  (void)a;
  r[0] = vb(false);
  CALL(H->controller_is_clean(H, &value));
  if (truthy(value)) CALL(H->editor_is_active(H, &value)); else value = vb(true);
  if (truthy(value)) { r[1] = vt("A test/editor is active"); return; }
  const char *pending = pending_cleanup(rt, false, false);
  if (pending) { r[1] = vt(pending); return; }
  if (truthy(foreign_at(rt, 1187))) { r[1] = vt("Photo Mode or another camera owns the view"); return; }
  tab_clear(&snapshot);
  NEED(BACKEND, 1188, "upvalue 'backend'");
  CALL(H->backend_snapshot(H, &snapshot, &r[1]));
  if (!snapshot.present) return;
  CALL(H->backend_prepare_pose(H, &snapshot, &r[0], &r[1]));
  if (!is_true(r[0])) return;
  CALL(H->fov_preflight(H, &r[0], &r[1]));
  if (!is_true(r[0])) return;
  CALL(H->blur_preflight(H, &r[0], &r[1]));
  if (!is_true(r[0])) return;
  CALL(H->camera_preflight(H, &r[0], &r[1]));
  if (truthy(r[0])) r[1] = vt("Ready; pose, FOV and pre-pin head-camera preparation passed");
}

/* ------------------------------------------------------------------ ready pose and Fird Person (parked in retail; :123-153, :935-1040) */
static void ensure_ready_pose(TfpRt *rt) {
  if (HAS(READY_POSE)) return;
  MADE(HELD) = true;
  CALL(H->held_fire_new(H));
  HAS(HELD_FIRE) = true;
  MADE(READY) = true;
  CALL(H->ready_pose_new(H));
  HAS(READY_POSE) = true;
}
ENTRY(held_fire_authorized) { /* :129-131 */
  r[0] = vb(false);
  if (!rt->has_context) return;
  if (!context_is_current(rt, a->context)) return;
  NEED(BACKEND, 130, "upvalue 'backend'");
  r[0] = vb(is_true(H->backend_frame_active(H)));
}
ENTRY(native_ready_gate) { /* :132-147: expected, context */
  r[0] = vb(false);
  bool scope = rt->has_context && context_is_current(rt, a->context);
  if (scope) { NEED(BACKEND, 133, "upvalue 'backend'"); scope = is_true(H->backend_frame_active(H)); }
  if (!scope) { r[1] = vt("Ready stance requires the owned Update scope"); return; }
  if (!a->table) lua_error(rt, 136, "attempt to index local 'expected' (a nil value)");
  const TfpRtTable *e = a->table;
  if (!is_true(F(*e, EXPECTED, SAFENORMAL4)) || truthy(F(*e, EXPECTED, ACTUALAIM)) || truthy(F(*e, EXPECTED, RELOAD)) ||
      truthy(F(*e, EXPECTED, SPRINT)) || truthy(F(*e, EXPECTED, FIRING)) || truthy(F(*e, EXPECTED, WEAPON_TRANSITION)) ||
      truthy(F(*e, EXPECTED, FOREIGN)) || truthy(F(*e, EXPECTED, ADS))) {
    r[1] = vt("A native action owns the stance");
    return;
  }
  if (!HAS(READY_TIMING)) { CALL(H->ready_timing_new(H)); HAS(READY_TIMING) = true; }
  V timing[TFP_RT_TIMING_N];
  TfpRtTable report;
  V why = VNIL;
  timing[TFP_RT_TIMING_PHASE] = vt("ready_pose_before_native");
  timing[TFP_RT_TIMING_TICK] = vn(a->context->tick);
  timing[TFP_RT_TIMING_EPOCH] = H->backend_frame_epoch(H);
  timing[TFP_RT_TIMING_EXPECTED] = F(*e, EXPECTED, SNAPSHOT);
  tab_clear(&report);
  CALL(H->ready_timing_sample(H, timing, &report, &why));
  if (!report.present) { r[1] = why; return; }
  if (!is_true(F(report, REPORT, OBSERVED_IDLE)) || !num_is(F(report, REPORT, PENDING_BATCHES), 0) ||
      !num_is(F(report, REPORT, INCOMPLETE_AFTER), 0) || !num_is(F(report, REPORT, CAMERA_KIND), 4) ||
      truthy(F(report, REPORT, RELOADING)) || truthy(F(report, REPORT, SHOULDER_AIM))) {
    r[1] = vt("Animation work or a native action is active");
    return;
  }
  r[0] = vb(true);
}
ENTRY(ready_source_factory) { (void)a; MADE(GATE) = true; CALL(H->ready_source_new(H, &r[0])); }
ENTRY(ready_filter_valid) { /* :149: context, admission */
  NEED(HELD_FIRE, 149, "upvalue 'held_fire'");
  CALL(H->held_fire_matches(H, a->v[0], a->context, &r[0], &r[1]));
}
ENTRY(ready_log) { (void)r; log_text(rt, fmt(rt, "Ready stance %s: %s", tostr(rt, a->v[1]), tostr(rt, a->v[0]))); }

static void render_fird_pose(TfpRt *rt, V *r) { /* :175-183 */
  r[0] = vb(true);
  if (rt->stopping || !HAS(FIRD) || !truthy(fird_is_enabled(rt)) || !HAS(BACKEND) || truthy(H->backend_frame_active(H))) return;
  CALL(H->backend_begin_frame(H));
  Pair p = {VNIL, VNIL};
  bool called = H->fird_render_pose(H, &p.a, &p.b);
  const char *text = called ? NULL : keep(rt, H->raised);
  CALL(H->backend_end_frame(H));
  if (!called) { r[0] = vb(false); r[1] = vt(text); return; }
  give(r, p);
}
ENTRY(render_fird_pose) { (void)a; render_fird_pose(rt, r); }
ENTRY(fird_render_active) { /* :967 */
  (void)a;
  r[0] = vb(!rt->stopping && HAS(FIRD) && truthy(fird_is_enabled(rt)) && HAS(BACKEND) && !truthy(H->backend_frame_active(H)));
}
ENTRY(fird_tick) { (void)a; r[0] = vn(rt->fird_tick); }
ENTRY(fird_aim_mode) { /* :954 */
  (void)a;
  if (HAS(MENU)) CALL(H->menu_ads_mode(H, &r[0]));
  if (!truthy(r[0])) r[0] = vt("fird");
}
ENTRY(fird_editor) { (void)a; r[0] = HAS(EDITOR) ? vb(true) : VNIL; }
ENTRY(fird_prepare) { /* :956 */
  NEED(FIRD, 956, "upvalue 'fird'");
  CALL(H->fird_prepare_native_ads(H, a->table, &r[0], &r[1]));
}
ENTRY(fe_running) { (void)a; NEED(FIRD, 1016, "upvalue 'fird'"); r[0] = fird_is_enabled(rt); }
ENTRY(fe_target) {
  TfpRtTable target;
  (void)a;
  NEED(FIRD, 1017, "upvalue 'fird'");
  tab_clear(&target);
  CALL(H->fird_reference_status(H, &target));
  r[0] = tab_value(&target);
}
ENTRY(fe_stop) { (void)a; give(r, stop_pose(rt, vb(true))); }
ENTRY(fe_cached) { (void)a; NEED(FIRD, 1019, "upvalue 'fird'"); CALL(H->fird_cached_capture(H, &r[0], &r[1])); }
ENTRY(fe_initialize) { NEED(FIRD_WARM, 1020, "upvalue 'fird_warm'"); CALL(H->fird_warm_update(H, a->table, &r[0], &r[1])); }
ENTRY(fe_begin_fp) { /* :1021-1027 */
  (void)a;
  CALL(H->capture_gate_reset(H));
  rt->has_candidate = false;
  rt->candidate_time = VNIL;
  rt->has_hip_capture = false;
  rt->fird_capture_phase = vt("hip");
  Pair done = start_pose(rt, vn(0), vb(true));
  if (!is_true(done.a)) { give(r, done); return; }
  if (!controller_lease(rt).present) { r[0] = vb(false); r[1] = vt("This weapon has no native first-person placement to capture"); return; }
  r[0] = vb(true);
}
static bool same_weapon(const TfpRtTable *x, const TfpRtTable *y) {
  return veq(F(*x, CAPTURE, WEAPON_IDENTITY), F(*y, CAPTURE, WEAPON_IDENTITY)) && veq(F(*x, CAPTURE, IDENTITY), F(*y, CAPTURE, IDENTITY)) &&
         veq(F(*x, CAPTURE, WEAPON_KEY), F(*y, CAPTURE, WEAPON_KEY));
}
static double capture_number(TfpRt *rt, int line, const TfpRtTable *capture, int field, const char *what) {
  if (!is_num(capture->f[field])) lua_error(rt, line, fmt(rt, "attempt to perform arithmetic on %s (a %s value)", what, kind_name(capture->f[field])));
  return capture->f[field].number;
}
ENTRY(fe_sample) { /* sample_capture, :977-1014 */
  Pair p = {VNIL, VNIL};
  V gate[TFP_RT_GATE_N], value = VNIL, token = VNIL;
  TfpRtTable snapshot, target, lease, table, capture;
  (void)a;
  CALL(H->fov_update(H, &p.a, &p.b));
  if (!is_true(p.a)) assert_value(rt, 979, truthy(p.b) ? p.b : vt("First-person FOV owner is unavailable"));
  CALL(H->camera_update(H, &p.a, &p.b));
  if (!is_true(p.a)) assert_value(rt, 981, truthy(p.b) ? p.b : vt("First-person camera owner is unavailable"));
  tab_clear(&snapshot);
  NEED(BACKEND, 982, "upvalue 'backend'");
  CALL(H->backend_snapshot(H, &snapshot, &value));
  tab_clear(&target);
  NEED(FIRD, 982, "upvalue 'fird'");
  CALL(H->fird_reference_status(H, &target));
  lease = controller_lease(rt);
  V pose = VNIL;
  if (lease.present) { CALL(H->backend_pose_status(H, &lease, &pose)); if (!truthy(pose)) pose = VNIL; }
  V actual = VNIL;
  CALL(H->capture_gate_scene_lens(H, snapshot.present ? &snapshot : NULL, &actual));
  gate[TFP_RT_GATE_SNAPSHOT] = tab_value(&snapshot);
  gate[TFP_RT_GATE_TARGET] = tab_value(&target);
  gate[TFP_RT_GATE_LEASE] = tab_value(&lease);
  tab_clear(&table); CALL(H->controller_result(H, &table)); gate[TFP_RT_GATE_CONTROLLER] = tab_value(&table);
  gate[TFP_RT_GATE_POSE] = pose;
  tab_clear(&table); CALL(H->fov_result(H, &table)); gate[TFP_RT_GATE_FOV] = tab_value(&table);
  tab_clear(&table); CALL(H->camera_result(H, &table)); gate[TFP_RT_GATE_CAMERA] = tab_value(&table);
  tab_clear(&table); CALL(H->motion_result(H, &table)); gate[TFP_RT_GATE_MOTION] = tab_value(&table);
  gate[TFP_RT_GATE_LENS] = actual;
  if (!target.present) lua_error(rt, 989, "attempt to index local 'target' (a nil value)");
  gate[TFP_RT_GATE_NEUTRAL] = F(target, TARGETST, NEUTRAL);
  gate[TFP_RT_GATE_TICK] = vn(rt->fird_tick);
  CALL(H->capture_gate_update(H, gate, &value, &token));
  if (!truthy(value)) { rt->has_candidate = false; rt->candidate_time = VNIL; r[0] = vb(false); r[1] = token; return; }
  tab_clear(&capture);
  CALL(H->fird_capture(H, token, &capture));
  double now = clock_now(rt);
  bool stable = rt->has_candidate;
  if (stable) {
    const TfpRtTable *c = &rt->candidate.table;
    if (!capture.present) lua_error(rt, 992, "attempt to index local 'capture' (a nil value)");
    stable = same_weapon(c, &capture);
    if (stable)
      stable = fabs(capture_number(rt, 994, c, TFP_RT_CAPTURE_SOURCE_FOV, "field 'source_fov'") -
                    capture_number(rt, 994, &capture, TFP_RT_CAPTURE_SOURCE_FOV, "field 'source_fov'")) <= .0001;
    if (stable) {
      double distance = 0, dot = 0;
      for (int i = 0; i < 3; i++) {
        double d = capture_number(rt, 997, c, TFP_RT_CAPTURE_PX + i, "field '?'") -
                   capture_number(rt, 997, &capture, TFP_RT_CAPTURE_PX + i, "field '?'");
        distance = distance + d * d;
      }
      for (int i = 0; i < 4; i++)
        dot = dot + capture_number(rt, 998, c, TFP_RT_CAPTURE_RX + i, "field '?'") *
                        capture_number(rt, 998, &capture, TFP_RT_CAPTURE_RX + i, "field '?'");
      stable = distance <= .02 * .02 && fabs(dot) >= cos(2 * 0.017453292519943295 / 2);
    }
  }
  if (!stable) {
    rt->has_candidate = capture.present;
    if (capture.present) kept_set(&rt->candidate, &capture);
    rt->candidate_time = vn(now);
    r[0] = vb(false);
    r[1] = vt("Waiting for the weapon draw animation to settle");
    return;
  }
  if (now - rt->candidate_time.number < .05) { r[0] = vb(false); r[1] = vt("Waiting for the next settled weapon pose"); return; }
  if (!text_is(rt->fird_capture_phase, "ads")) {
    rt->has_hip_capture = true;
    kept_set(&rt->hip_capture, &capture);
    rt->fird_capture_phase = vt("ads");
    CALL(H->capture_gate_reset(H));
    rt->has_candidate = false;
    rt->candidate_time = VNIL;
    r[0] = vb(false);
    r[1] = vt("Capturing native sight alignment");
    return;
  }
  if (!(rt->has_hip_capture && same_weapon(&rt->hip_capture.table, &capture))) assert_failed(rt, 1008, "Weapon changed between hip and ADS calibration");
  V hip = tab_value(&rt->hip_capture.table);
  CALL(H->fird_capture_set_aim(H, hip, truthy(F(capture, CAPTURE, PROJECTION)) ? tab_value(&capture) : VNIL));
  if (token.kind != TFP_RT_REF) lua_error(rt, 1013, fmt(rt, "attempt to index local 'token' (a %s value)", kind_name(token)));
  CALL(H->capture_gate_token_set_capture(H, token, hip));
  r[0] = vb(true);
  r[1] = token;
}
ENTRY(fe_capture) { /* :1029; a tail call, so LuaJIT has no line to put before the text */
  V token = a->v[0];
  if (token.kind != TFP_RT_REF && token.kind != TFP_RT_TEXT)
    lua_error(rt, 1029, fmt(rt, "attempt to index local 'token' (a %s value)", kind_name(token)));
  if (token.kind == TFP_RT_REF) r[0] = H->ref_field(H, token, "capture");
  if (!truthy(r[0])) raise_text(rt, "Settled first-person reference unavailable");
}
ENTRY(fe_enter) { /* :1030-1036 */
  V saved[2] = {VNIL, VNIL};
  Pair p = {VNIL, VNIL};
  r[0] = vb(false);
  if (truthy(foreign_at(rt, 1031))) { r[1] = vt("Another camera owns the view"); return; }
  CALL(H->head_positioner_get_saved(H, &saved[0], &saved[1]));
  NEED(BACKEND, 1032, "upvalue 'backend'");
  CALL(H->backend_set_fird_camera_offset(H, saved[0]));
  NEED(FIRD, 1033, "upvalue 'fird'");
  bool called = H->fird_start(H, a->v[0], &p.a, &p.b);
  if (!called || !is_true(p.a)) {
    V reason = called ? (truthy(p.b) ? p.b : p.a) : vt(keep(rt, H->raised));
    Pair ignored = {VNIL, VNIL};
    CALL(H->fird_stop(H, &ignored.a, &ignored.b));
    r[1] = vt(tostr(rt, reason));
    return;
  }
  r[0] = vb(true);
}
static void start_fird(TfpRt *rt, V *r) { /* :935-1040 */
  V value = VNIL;
  Pair p = {VNIL, VNIL};
  r[0] = vb(false);
  if (truthy(H->camera_retired(H))) { r[1] = vt("Fird Person is unavailable because the camera policy was removed"); return; }
  if (rt->stopping) { r[1] = vt("Mod is stopping"); return; }
  CALL(H->editor_is_active(H, &value));
  if (truthy(value)) { r[1] = vt("Close the placement editor before Fird Person"); return; }
  if (HAS(FIRD_ENTRY)) { CALL(H->fird_entry_is_enabled(H, &value)); if (truthy(value)) { r[0] = vb(true); return; } }
  bool natural = is_true(H->fird_module_native_animation_default(H));
  if (!natural && !truthy(H->fird_module_render_pose_verified(H))) { r[1] = vt("Fird Person render placement is unavailable"); return; }
  if (natural) ensure_ready_pose(rt);
  if (!HAS(FIRD)) {
    V options[TFP_RT_FIRDOPT_N];
    for (int i = 0; i < TFP_RT_FIRDOPT_N; i++) options[i] = VNIL;
    options[TFP_RT_FIRDOPT_NATIVE_ANIMATION] = vb(natural);
    options[TFP_RT_FIRDOPT_RENDER_DRIVEN] = vb(!natural);
    if (natural) {
      options[TFP_RT_FIRDOPT_READY_POSE] = vb(true);
      if (HAS(HELD_FIRE)) options[TFP_RT_FIRDOPT_FIRE_FILTER] = vb(true);
    } else {
      for (int i = TFP_RT_FIRDOPT_CAMERA_FRAME; i <= TFP_RT_FIRDOPT_CHILDREN; i++) options[i] = vb(true);
      CALL(H->fird_aim_new(H, &value));
      if (truthy(value)) options[TFP_RT_FIRDOPT_AIM] = value;
    }
    MADE(PREPARE) = true;
    CALL(H->fird_native_ads_new(H, &options[TFP_RT_FIRDOPT_NATIVE_ADS]));
    CALL(H->fird_fov_new(H, &options[TFP_RT_FIRDOPT_FOV]));
    MADE(FIRD) = true;
    CALL(H->fird_new(H, options));
    HAS(FIRD) = true;
  }
  if (natural) {
    if (truthy(fird_is_enabled(rt))) { r[0] = vb(true); return; }
    p = stop_pose(rt, VNIL);
    if (!is_true(p.a)) { r[1] = p.b; return; }
    if (!H->fird_start_native(H, &p.a, &p.b)) {
      const char *text = keep(rt, H->raised);
      ATTX(FIRD, fird_stop);
      r[1] = vt(text);
      return;
    }
    if (!is_true(p.a)) ATTX(FIRD, fird_stop);
    give(r, p);
    return;
  }
  if (!HAS(FIRD_RENDER)) { MADE(RENDER) = true; CALL(H->fird_render_new(H)); HAS(FIRD_RENDER) = true; }
  CALL(H->fird_render_start(H, &p.a, &p.b));
  if (!is_true(p.a)) { r[1] = p.b; return; }
  if (!HAS(FIRD_ENTRY)) {
    NEED(BACKEND, 973, "upvalue 'backend'");
    if (truthy(H->backend_camera_ready(H))) { MADE(WARM) = true; CALL(H->fird_warm_new(H)); HAS(FIRD_WARM) = true; }
    CALL(H->capture_gate_new(H));
    rt->has_candidate = false;
    rt->candidate_time = VNIL;
    rt->has_hip_capture = false;
    rt->entry_initialize = HAS(FIRD_WARM);
    MADE(FIRDENTRY) = true;
    CALL(H->fird_entry_new(H));
    HAS(FIRD_ENTRY) = true;
  }
  CALL(H->fird_entry_request(H, &r[0], &r[1]));
}
ENTRY(start_fird) { (void)a; start_fird(rt, r); }
ENTRY(probe_fird) { /* :1105-1113 */
  V seconds = a->v[0];
  r[0] = vb(false);
  if (!is_num(seconds) || seconds.number != seconds.number || seconds.number < 1 || seconds.number > 10) {
    r[1] = vt("Fird probe duration must be from1to10seconds");
    return;
  }
  bool busy = truthy(view_enabled(rt));
  if (!busy) busy = !truthy(view_clean(rt));
  if (busy) { r[1] = vt("Turn the active view off before a bounded probe"); return; }
  start_fird(rt, r);
  if (is_true(r[0])) rt->probe_deadline = vn(clock_now(rt) + seconds.number);
}

/* ------------------------------------------------------------------ the entry table */
static const struct EntryRow { const char *name; int stage; int values; EntryFn *call; } entries[TFP_RT_E_COUNT] = {
#define TFP_RT_ENTRY_ROW(name, stage, nret) {#name, TFP_RT_STAGE_##stage, nret, e_##name},
  TFP_RT_ENTRY_LIST(TFP_RT_ENTRY_ROW)
#undef TFP_RT_ENTRY_ROW
};
const char *tfp_rt_entry_name(TfpRtEntry entry) { return entries[entry].name; }
int tfp_rt_entry_values(TfpRtEntry entry) { return entries[entry].values; }

void tfp_rt_init(TfpRt *rt, TfpRtHost *host) {
  memset(rt, 0, sizeof *rt);
  rt->host = host;
  rt->idle_clean = vb(false);
  rt->recovery_enabled = vb(false);
}
typedef struct CallBox { EntryFn *call; const TfpRtArgs *args; V *ret; } CallBox;
static void call_body(TfpRt *rt, void *arg) { CallBox *box = arg; box->call(rt, box->args, box->ret); }
TfpRtStatus tfp_rt_call(TfpRt *rt, TfpRtEntry entry, const TfpRtArgs *args, TfpRtValue *ret) {
  static const TfpRtArgs none;
  const struct EntryRow *row = &entries[entry];
  bool exists = row->stage == TFP_RT_STAGE_NONE || (row->stage == TFP_RT_STAGE_API ? HAS(API) : rt->made[row->stage]);
  if (entry == TFP_RT_E_fe_initialize && !rt->entry_initialize) exists = false;
  if (!exists) return TFP_RT_ABSENT;
  for (int i = 0; i < row->values; i++) ret[i] = VNIL;
  if (rt->depth == 0) rt->arena_used = 0;
  rt->depth++;
  CallBox box = {row->call, args ? args : &none, ret};
  bool ok = rt_pcall(rt, call_body, &box);
  rt->depth--;
  return ok ? TFP_RT_OK : TFP_RT_RAISED;
}
