/* The "module off" answers of the view build (src/wire_off.c) against retail runtime.lua.
 *
 * tests/vectors/view_off_vectors.h (tools/retail_ref/gen_view_off.py) is the unmodified retail
 * runtime.lua run through one session of the view build with scripted collaborators: the stubbed
 * modules answering what wire_off.c answers. This program replays that session against the C
 * runtime order (as tests/tfp_runtime_test.c does: answer, ordered trace and state must be retail's,
 * character for character) and, on every call into a stubbed module, also asks wire_off.c and
 * requires the same answer the scripted collaborator gave retail. Every call into a collaborator
 * that is neither wired for real nor compared here fails the test.
 * So: retail's runtime, given wire_off.c's answers, behaves as recorded, and the C runtime given
 * the same answers behaves the same. */
#include <stdbool.h>
struct TfpRtHost;
static void off_wrap(struct TfpRtHost *h);
static void off_after_op(const char *trace_text);
static long off_at_end(void);
/* -DTFP_OFF_FEEL: the feel build's session (tests/vectors/feel_off_vectors.h): the modules the feel
 * build binds (tests/feel_bound.h) answer as in ordinary play and are not compared; every module it
 * still stubs is compared as before. */
#ifdef TFP_OFF_FEEL
#define TFP_RT_VECTORS "vectors/feel_off_vectors.h"
#define OFF_NAME "Feel off"
#else
#define TFP_RT_VECTORS "vectors/view_off_vectors.h"
#define OFF_NAME "View off"
#endif
#define TFP_RT_AFTER_BIND off_wrap
#define TFP_RT_AFTER_OP off_after_op
#define TFP_RT_AT_END off_at_end
#include "tfp_runtime_test.c"
#include "wire_off.h"
#include "feel_bound.h"

static TfpRtHost off_host;
static bool off_bound;
static long off_differences, off_compared, off_unchecked, off_bound_calls;
#ifdef TFP_OFF_FEEL
static bool now_bound(const char *name) { return feel_bound(name); }
#else
static bool now_bound(const char *name) { (void)name; return false; }
#endif
static const char *off_checked[400];
static int off_checked_count;

static bool same_value(TfpRtValue a, TfpRtValue b) {
  if (a.kind != b.kind) return false;
  if (a.kind == TFP_RT_NUMBER) return a.number == b.number || (a.number != a.number && b.number != b.number);
  if (a.kind == TFP_RT_TEXT) return a.length == b.length && !memcmp(a.text, b.text, a.length);
  return true; /* a ref is known only by its identity: both sides answered a table */
}
static bool same_table(const TfpRtTable *a, const TfpRtTable *b) {
  if (a->present != b->present) return false;
  for (int i = 0; a->present && i < TFP_RT_FIELDS; i++) if (!same_value(a->f[i], b->f[i])) return false;
  return true;
}
static void compared(const char *name, bool same) {
  if (now_bound(name)) { off_bound_calls++; return; } /* bound to real code in the feel build: no off answer to hold */
  off_compared++;
  if (same) return;
  if (off_differences++ < 10) printf(OFF_NAME ": %s answers differently from the scripted retail collaborator (operation %ld)\n", name,
                                     (long)(op_now - rt_ops));
}
#define W0(name) static bool wr_##name(TfpRtHost *h) { bool f = fk_##name(h), m = off_host.name(&off_host); compared(#name, f == m); return f; }
TFP_OFF_S0(W0)
#define W1(name, kind, t) static bool wr_##name(TfpRtHost *h, TfpRtValue *o1) { \
  TfpRtValue a = {0}; bool f = fk_##name(h, o1), m = off_host.name(&off_host, &a); compared(#name, f == m && same_value(*o1, a)); return f; }
TFP_OFF_S1(W1)
#define W2(name, kind, t) static bool wr_##name(TfpRtHost *h, TfpRtValue *o1, TfpRtValue *o2) { \
  TfpRtValue a = {0}, b = {0}; bool f = fk_##name(h, o1, o2), m = off_host.name(&off_host, &a, &b); \
  compared(#name, f == m && same_value(*o1, a) && same_value(*o2, b)); return f; }
TFP_OFF_S2(W2)
#define WT(name, shape) static bool wr_##name(TfpRtHost *h, TfpRtTable *o1) { \
  TfpRtTable a; bool f = fk_##name(h, o1), m = off_host.name(&off_host, &a); compared(#name, f == m && same_table(o1, &a)); return f; }
TFP_OFF_ST(WT)
#define WG(name, kind) static TfpRtValue wr_##name(TfpRtHost *h) { TfpRtValue f = fk_##name(h), m = off_host.name(&off_host); \
  compared(#name, same_value(f, m)); return f; }
TFP_OFF_G0(WG)
/* The stubs with arguments that the session reaches. */
static bool wr_actions_resolve(TfpRtHost *h, TfpRtValue key, TfpRtValue action, TfpRtValue *o1, TfpRtValue *o2) {
  TfpRtValue a = {0}, b = {0};
  bool f = fk_actions_resolve(h, key, action, o1, o2), m = off_host.actions_resolve(&off_host, key, action, &a, &b);
  compared("actions_resolve", f == m && same_value(*o1, a) && same_value(*o2, b));
  return f;
}
static bool wr_actions_weapon_info(TfpRtHost *h, TfpRtValue key, TfpRtTable *o1) {
  TfpRtTable a;
  bool f = fk_actions_weapon_info(h, key, o1), m = off_host.actions_weapon_info(&off_host, key, &a);
  compared("actions_weapon_info", f == m && same_table(o1, &a));
  return f;
}
static bool wr_backend_alignment_context(TfpRtHost *h, const TfpRtValue *actx, TfpRtTable *o1) {
  TfpRtTable a;
  bool f = fk_backend_alignment_context(h, actx, o1), m = off_host.backend_alignment_context(&off_host, actx, &a);
  compared("backend_alignment_context", f == m && same_table(o1, &a));
  return f;
}
static bool wr_camera_free_aim(TfpRtHost *h, double ms, TfpRtValue *o1, TfpRtValue *o2) {
  TfpRtValue a = {0}, b = {0};
  bool f = fk_camera_free_aim(h, ms, o1, o2), m = off_host.camera_free_aim(&off_host, ms, &a, &b);
  compared("camera_free_aim", f == m && same_value(*o1, a) && same_value(*o2, b));
  return f;
}
static bool wr_aim_markers_toggle(TfpRtHost *h, TfpRtValue key, TfpRtValue identity, TfpRtValue *o1, TfpRtValue *o2) {
  TfpRtValue a = {0}, b = {0};
  bool f = fk_aim_markers_toggle(h, key, identity, o1, o2), m = off_host.aim_markers_toggle(&off_host, key, identity, &a, &b);
  compared("aim_markers_toggle", f == m && same_value(*o1, a) && same_value(*o2, b));
  return f;
}
static bool wr_aim_markers_set_setting(TfpRtHost *h, TfpRtValue value, TfpRtValue *o1, TfpRtValue *o2) {
  TfpRtValue a = {0}, b = {0};
  bool f = fk_aim_markers_set_setting(h, value, o1, o2), m = off_host.aim_markers_set_setting(&off_host, value, &a, &b);
  compared("aim_markers_set_setting", f == m && same_value(*o1, a) && same_value(*o2, b));
  return f;
}
static bool wr_backend_prepare_pose(TfpRtHost *h, const TfpRtTable *snapshot, TfpRtValue *o1, TfpRtValue *o2) {
  TfpRtValue a = {0}, b = {0};
  bool f = fk_backend_prepare_pose(h, snapshot, o1, o2), m = off_host.backend_prepare_pose(&off_host, snapshot, &a, &b);
  compared("backend_prepare_pose", f == m && same_value(*o1, a) && same_value(*o2, b));
  return f;
}
static TfpRtValue wr_config_field(TfpRtHost *h, const char *name) {
  TfpRtValue f = fk_config_field(h, name), m = off_host.config_field(&off_host, name);
  compared("config_field", same_value(f, m));
  return f;
}

static void checked(const char *name) { off_checked[off_checked_count++] = name; }
static void off_wrap(TfpRtHost *h) {
  if (!off_bound) {
    off_bound = true;
    tfp_wire_off_bind(&off_host);
#define NOTE(name, ...) checked(#name);
    TFP_OFF_S0(NOTE) TFP_OFF_S1(NOTE) TFP_OFF_S2(NOTE) TFP_OFF_ST(NOTE)
#undef NOTE
    checked("actions_resolve"), checked("actions_weapon_info"), checked("backend_alignment_context"), checked("camera_free_aim");
    checked("aim_markers_toggle"), checked("aim_markers_set_setting"), checked("backend_prepare_pose");
  }
#define PUT(name, ...) h->name = wr_##name;
  TFP_OFF_S0(PUT) TFP_OFF_S1(PUT) TFP_OFF_S2(PUT) TFP_OFF_ST(PUT) TFP_OFF_G0(PUT)
#undef PUT
  h->actions_resolve = wr_actions_resolve;
  h->actions_weapon_info = wr_actions_weapon_info;
  h->backend_alignment_context = wr_backend_alignment_context;
  h->camera_free_aim = wr_camera_free_aim;
  h->aim_markers_toggle = wr_aim_markers_toggle;
  h->aim_markers_set_setting = wr_aim_markers_set_setting;
  h->backend_prepare_pose = wr_backend_prepare_pose;
  h->config_field = wr_config_field;
  /* The pointers wire_off.c leaves NULL must be the ones the session switched off. */
  const void *absent[] = {(const void *)h->menu_fird_person_arms, (const void *)h->menu_show_torso, (const void *)h->menu_near_options,
                          (const void *)h->menu_motion_options, (const void *)h->arm_pieces_new, (const void *)h->grip_hand_new,
                          (const void *)h->head_sdk_handles, (const void *)h->camera_is_pinned, (const void *)h->camera_look_stats,
                          (const void *)h->mod_set};
  const void *stubbed[] = {(const void *)off_host.menu_fird_person_arms, (const void *)off_host.menu_show_torso,
                           (const void *)off_host.menu_near_options, (const void *)off_host.menu_motion_options,
                           (const void *)off_host.arm_pieces_new, (const void *)off_host.grip_hand_new, (const void *)off_host.head_sdk_handles,
                           (const void *)off_host.camera_is_pinned, (const void *)off_host.camera_look_stats, (const void *)off_host.mod_set};
  static const char *const optional[] = {"menu_fird_person_arms", "menu_show_torso", "menu_near_options", "menu_motion_options", "arm_pieces_new",
                                         "grip_hand_new", "head_sdk_handles", "camera_is_pinned", "camera_look_stats", "mod_set"};
  for (size_t i = 0; i < sizeof absent / sizeof *absent; i++) {
    if (now_bound(optional[i])) compared(optional[i], absent[i] != NULL); /* counted as bound; the session has the method */
    else compared("an optional pointer", !absent[i] && !stubbed[i]);
  }
}
/* Every collaborator named in the trace is either part of the wired view or was compared above. */
static void off_after_op(const char *text) {
  static const char *const wired[] = {"backend_", "controller_", "fov_", "blur_", "camera_", "near_", "recovery_", "transition_",
                                      "diagnostics_", "mod_log", "clock_now", "mouse_"};
  for (const char *at = text; *at;) {
    while (*at == ' ') at++;
    const char *end = at;
    while (*end && (*end == '_' || (*end >= 'a' && *end <= 'z') || (*end >= '0' && *end <= '9'))) end++;
    if (end > at && *end == '(' && (at == text || at[-1] == ' ')) {
      bool known = false;
      for (size_t i = 0; i < sizeof wired / sizeof *wired && !known; i++) known = !strncmp(at, wired[i], strlen(wired[i]));
      char called[64];
      snprintf(called, sizeof called, "%.*s", (int)(end - at), at);
      if (now_bound(called)) known = true;
      /* Stubbed although their names start like a wired group: listed in off_checked. */
      for (int i = 0; i < off_checked_count; i++)
        if (strlen(off_checked[i]) == (size_t)(end - at) && !strncmp(off_checked[i], at, (size_t)(end - at))) known = true;
      if (!known && off_unchecked++ < 10) printf(OFF_NAME ": %.*s was called and is neither wired nor compared\n", (int)(end - at), at);
    }
    /* next token: skip to the end of this call's argument list or callback text */
    at = end;
    while (*at && *at != ' ') at++;
  }
}
static long off_at_end(void) {
  printf(OFF_NAME ": %ld answers of src/wire_off.c equal to what the scripted collaborators answered retail; %ld differ; %ld calls unchecked",
         off_compared - off_differences, off_differences, off_unchecked);
#ifdef TFP_OFF_FEEL
  printf("; %ld calls went to modules the feel build binds", off_bound_calls);
#endif
  printf("\n");
  return off_differences + off_unchecked;
}
