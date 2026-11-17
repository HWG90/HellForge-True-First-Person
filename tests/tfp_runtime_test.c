/* src/tfp_runtime.c replayed against the retail runtime.lua's own answers
 * (tests/vectors/runtime_vectors.h, cut by tools/retail_ref/gen_runtime.py). Every collaborator is
 * a scripted fake (tests/vectors/runtime_fakes.h, generated from the same spec as the Lua fakes of
 * tools/retail_ref/run_runtime.lua). Per operation one text is compared, character for character:
 * what the called function answered | the ordered trace of every call into a fake with its
 * arguments, log lines among them | the runtime's own state ('=': as after the operation before). Doubles by their bits.
 *   tfp_runtime_test [retail commit the vectors must come from] [--show N]   (N: print operation N) */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfp_runtime.h"
#include "vectors/runtime_vectors.h"

static TfpRtValue K[RT_KNOBS];
static TfpRt rt;
static TfpRtHost host;
static const RtOp *op_now;
static int calls;
static uint64_t numbers_compared;

/* ------------------------------------------------------------------ text */
typedef struct Text { char *at; size_t used, room; } Text;
static Text trace;
static void put_bytes(Text *t, const char *s, size_t n) {
  if (t->used + n + 1 > t->room) {
    t->room = (t->used + n + 1) * 2;
    t->at = realloc(t->at, t->room);
    if (!t->at) abort();
  }
  memcpy(t->at + t->used, s, n);
  t->used += n;
  t->at[t->used] = 0;
}
static void put(Text *t, const char *s) { put_bytes(t, s, strlen(s)); }
static void putf(Text *t, const char *format, ...) {
  char buffer[512];
  va_list list;
  va_start(list, format);
  vsnprintf(buffer, sizeof buffer, format, list);
  va_end(list);
  put(t, buffer);
}
static void put_num(Text *t, double x) {
  if (x != x) { put(t, "nan"); return; }
  if (x > -2147483648.0 && x < 2147483648.0 && x == floor(x) && !(x == 0 && signbit(x))) {
    putf(t, "%d", (int)x);
    return;
  }
  uint64_t bits;
  memcpy(&bits, &x, sizeof bits);
  putf(t, "x%016" PRIx64, bits);
  numbers_compared++;
}
static void put_val(Text *t, TfpRtValue v) {
  switch (v.kind) {
    case TFP_RT_NIL: put(t, "~"); break;
    case TFP_RT_FALSE: put(t, "F"); break;
    case TFP_RT_TRUE: put(t, "T"); break;
    case TFP_RT_NUMBER: put_num(t, v.number); break;
    case TFP_RT_TEXT:
      put(t, "\"");
      for (size_t i = 0; i < v.length; i++) {
        unsigned char c = (unsigned char)v.text[i];
        if (c < 32 || c > 126 || c == '"' || c == '\\') putf(t, "\\x%02x", c);
        else put_bytes(t, (const char *)&c, 1);
      }
      put(t, "\"");
      break;
    case TFP_RT_REF:
      if (v.ref == tfp_rt_camera_baseline || v.ref == tfp_rt_default_placement || v.ref == &tfp_rt_empty_table) put(t, "{?}");
      else { put(t, "R"); put_num(t, (double)(uintptr_t)v.ref); }
      break;
  }
}
static void put_table(Text *t, const TfpRtTable *table) {
  if (!table || !table->present) { put(t, "~"); return; }
  put(t, "R");
  put_num(t, (double)(uintptr_t)table->ref);
}

/* ------------------------------------------------------------------ what the generated fakes use */
static TfpRtValue fk_bool(bool b) { TfpRtValue v = {b ? TFP_RT_TRUE : TFP_RT_FALSE, 0, 0, 0, 0}; return v; }
static TfpRtValue fk_number(double n) { TfpRtValue v = {TFP_RT_NUMBER, n, 0, 0, 0}; return v; }
static TfpRtValue fk_text(const char *s) { TfpRtValue v = {TFP_RT_TEXT, 0, s, strlen(s), 0}; return v; }
static bool fk_same(TfpRtValue a, TfpRtValue b) {
  if (a.kind != b.kind) return false;
  if (a.kind == TFP_RT_NUMBER) return a.number == b.number;
  if (a.kind == TFP_RT_TEXT) return a.length == b.length && !memcmp(a.text, b.text, a.length);
  if (a.kind == TFP_RT_REF) return a.ref == b.ref;
  return true;
}
static int arguments, method_now;
static unsigned short seen[RT_METHODS];
static void fk_trim(TfpRtValue *out, double size) {
  if (out->kind == TFP_RT_TEXT && (double)out->length > size) out->length = (size_t)size;
}
static void fk_begin(int method, const char *name) {
  method_now = method;
  if (trace.used) put(&trace, " ");
  put(&trace, name);
  put(&trace, "(");
  arguments = 0;
}
static void fk_sep(void) { if (arguments++) put(&trace, ","); }
static void fk_v(TfpRtValue v) { fk_sep(); put_val(&trace, v); }
static void fk_n(double n) { fk_sep(); put_num(&trace, n); }
static void fk_b(bool b) { fk_sep(); put(&trace, b ? "T" : "F"); }
static void fk_t(const char *s) { fk_sep(); if (s) put_val(&trace, fk_text(s)); else put(&trace, "~"); }
static void fk_r(const TfpRtTable *table) { fk_sep(); put_table(&trace, table); }
static void fk_rx(const TfpRtTable *table) {
  fk_sep();
  put_table(&trace, table);
  if (!table || !table->present) return;
  put(&trace, "[");
  put_val(&trace, table->f[TFP_RT_SNAP_ZOOM_IDENTITY]);
  put(&trace, ",");
  put_val(&trace, table->f[TFP_RT_SNAP_CONTROLLER_ACTIVE]);
  put(&trace, "]");
}
static void fk_c(const TfpRtContext *context) {
  fk_sep();
  if (!context) { put(&trace, "~"); return; }
  put(&trace, "c");
  put_num(&trace, context->tick);
}
static void fk_vals(const TfpRtValue *values, int count) {
  fk_sep();
  if (!values) { put(&trace, "~"); return; }
  put(&trace, "[");
  for (int i = 0; i < count; i++) {
    if (i) put(&trace, ",");
    put_val(&trace, values[i]);
  }
  put(&trace, "]");
}
static void fk_end(void) { put(&trace, ")"); }
static void run_entry(Text *answer, int entry, const RtValue *values, int count);
static int fk_go(TfpRtHost *h) {
  int n = ++calls, method = method_now;
  unsigned short occurrence = ++seen[method];
  for (unsigned i = 0; i < op_now->cbs; i++) {
    const RtCallback *cb = &rt_callbacks[op_now->cb0 + i];
    if (cb->method != method || cb->occurrence != occurrence) continue;
    Text answer = {0, 0, 0};
    put(&answer, "");
    run_entry(&answer, cb->entry, rt_values + cb->arg0, cb->args);
    putf(&trace, " {%s:", rt_entry_names[cb->entry]);
    put(&trace, answer.at);
    put(&trace, "}");
    free(answer.at);
  }
  for (unsigned i = 0; i < op_now->faults; i++) {
    const RtFault *fault = &rt_faults[op_now->fault0 + i];
    if (fault->call != n) continue;
    if (fault->kind == 1) h->raised = "boom";
    return fault->kind;
  }
  return 0;
}
static void fk_out(TfpRtValue *out, int knob, int index, int fault) {
  static const TfpRtValue nil;
  if (fault == 2) *out = index == 0 ? fk_bool(false) : index == 1 ? fk_text("no") : nil;
  else if (fault == 3) *out = nil;
  else *out = K[knob];
}
static void fk_tab(TfpRtTable *out, int base, int count, int fault) {
  memset(out, 0, sizeof *out);
  if (fault == 2 || fault == 3 || K[base].number != 1) return;
  out->present = true;
  out->ref = (const void *)(uintptr_t)K[base + 1].number;
  for (int i = 0; i < count; i++) out->f[i] = K[base + 2 + i];
}
static void fk_policies(TfpRtPolicies *out, int base, int fault) {
  memset(out, 0, sizeof *out);
  if (fault == 2 || fault == 3 || K[base].number != 1) return;
  out->present = true;
  out->ref = (const void *)(uintptr_t)K[base + 1].number;
  out->count = (int)K[base + 2].number;
  for (int i = 0; i < out->count; i++) {
    out->name[i] = K[base + 3 + i * 3].text;
    out->row[i].present = true;
    out->row[i].f[TFP_RT_ROW_CORE] = K[base + 4 + i * 3];
    out->row[i].f[TFP_RT_ROW_PHASE] = K[base + 5 + i * 3];
  }
}
enum { SHADOWS = 64 };
static struct { const void *ref; TfpRtValue value; } shadow[SHADOWS];
static int shadows;
static void fk_ref_set(TfpRtValue token, TfpRtValue capture) {
  for (int i = 0; i < shadows; i++) if (shadow[i].ref == token.ref) { shadow[i].value = capture; return; }
  if (shadows == SHADOWS) abort();
  shadow[shadows].ref = token.ref;
  shadow[shadows++].value = capture;
}
static void fk_global_set(TfpRtHost *h, bool ours);
static TfpRtValue fk_actions_observed(TfpRtHost *h, TfpRtValue action);
static TfpRtValue fk_config_table(TfpRtHost *h);
static TfpRtValue fk_config_field(TfpRtHost *h, const char *name);
static TfpRtValue fk_config_action_mapped(TfpRtHost *h, TfpRtValue action);
static TfpRtValue fk_ref_field(TfpRtHost *h, TfpRtValue ref, const char *name);
static TfpRtValue fk_global_get(TfpRtHost *h);
static void fk_lease(TfpRtTable *out, int knob);
#include "vectors/runtime_fakes.h"
static void fk_lease(TfpRtTable *out, int knob) {
  int slot = (int)K[knob].number;
  if (slot == 0) { memset(out, 0, sizeof *out); return; }
  fk_tab(out, K_LEASE1 + (slot - 1) * K_LEASE_STEP, TFP_RT_LEASE_N, 0);
}
static void fk_global_set(TfpRtHost *h, bool ours) { (void)h; K[K_GLOBAL] = fk_number(ours ? 1 : 0); }
static TfpRtValue fk_global_get(TfpRtHost *h) { (void)h; return K[K_GLOBAL]; }
static TfpRtValue fk_actions_observed(TfpRtHost *h, TfpRtValue action) {
  static const TfpRtValue nil;
  (void)h;
  return fk_same(action, K[K_OBSERVED_NAME]) ? K[K_OBSERVED] : nil;
}
static TfpRtValue fk_config_table(TfpRtHost *h) { (void)h; return K[K_CONFIG]; }
static TfpRtValue fk_config_field(TfpRtHost *h, const char *name) {
  static const TfpRtValue nil;
  (void)h;
  if (K[K_CONFIG].kind == TFP_RT_NIL) return nil;
  for (size_t i = 0; i < sizeof fk_config / sizeof *fk_config; i++) if (!strcmp(fk_config[i].name, name)) return K[fk_config[i].knob];
  return nil;
}
static TfpRtValue fk_config_action_mapped(TfpRtHost *h, TfpRtValue action) {
  (void)h;
  if (K[K_CONFIG].kind == TFP_RT_NIL || K[K_MAPPING].kind != TFP_RT_REF) return fk_bool(false);
  if (fk_same(action, fk_text("unused"))) return fk_bool(true);
  return fk_bool(K[K_MAPPED].kind != TFP_RT_NIL && fk_same(action, K[K_MAPPED]));
}
static TfpRtValue fk_ref_field(TfpRtHost *h, TfpRtValue ref, const char *name) {
  static const TfpRtValue nil;
  (void)h;
  if (ref.kind != TFP_RT_REF || strcmp(name, "capture")) return nil;
  for (int i = 0; i < shadows; i++) if (shadow[i].ref == ref.ref) return shadow[i].value;
  return nil;
}

/* ------------------------------------------------------------------ replay */
static TfpRtValue value_of(const RtValue *v) {
  TfpRtValue out = {TFP_RT_NIL, 0, 0, 0, 0};
  switch (v->kind) {
    case 1: out.kind = TFP_RT_FALSE; break;
    case 2: out.kind = TFP_RT_TRUE; break;
    case 3: out.kind = TFP_RT_NUMBER; out.number = v->number; break;
    case 4: out.kind = TFP_RT_TEXT; out.text = rt_strings[v->text]; out.length = (size_t)v->number; break;
    case 5: out.kind = TFP_RT_REF; out.ref = (const void *)(uintptr_t)v->number; break;
    default: break;
  }
  return out;
}
static int entry_ids[sizeof rt_entry_names / sizeof *rt_entry_names];
static void run_entry(Text *answer, int entry, const RtValue *values, int count) {
  TfpRtArgs args;
  TfpRtTable table;
  TfpRtContext context;
  TfpRtValue ret[40];
  memset(&args, 0, sizeof args);
  memset(ret, 0, sizeof ret);
  const char *kinds = rt_entry_args[entry];
  int at = 0, v = 0;
  while (*kinds) {
    TfpRtValue given = {TFP_RT_NIL, 0, 0, 0, 0};
    if (at < count) given = value_of(&values[at]);
    at++;
    if (kinds[0] == 'v') args.v[v++] = given;
    else if (kinds[0] == 'c') {
      if (given.kind == TFP_RT_NUMBER && given.number == 1) {
        if (rt.has_context) { context = rt.context; args.context = &context; }
      } else if (given.kind == TFP_RT_NUMBER && given.number == 2) {
        context.serial = 0;
        context.tick = rt.has_context ? rt.context.tick : 0;
        args.context = &context;
      }
    } else {
      const char *group = kinds + 4;
      size_t length = strcspn(group, ",");
      for (size_t i = 0; i < sizeof fk_groups / sizeof *fk_groups; i++) {
        if (strlen(fk_groups[i].name) != length || strncmp(fk_groups[i].name, group, length)) continue;
        if (given.kind != TFP_RT_NIL && given.kind != TFP_RT_FALSE) {
          fk_tab(&table, fk_groups[i].base, fk_groups[i].count, 0);
          if (table.present) args.table = &table;
        }
      }
    }
    kinds += strcspn(kinds, ",");
    if (*kinds == ',') kinds++;
  }
  TfpRtStatus status = tfp_rt_call(&rt, (TfpRtEntry)entry_ids[entry], &args, ret);
  if (status == TFP_RT_ABSENT) { put(answer, "absent"); return; }
  if (status == TFP_RT_RAISED) { put(answer, "!"); put_val(answer, rt.error); return; }
  put(answer, "=");
  for (int i = 0; i < rt_entry_nret[entry]; i++) {
    if (i) put(answer, ",");
    put_val(answer, ret[i]);
  }
}
static void show(Text *t, const char *name, TfpRtValue v) { putf(t, " %s=", name); put_val(t, v); }
static void state(Text *t) {
  for (int i = 0; i < TFP_RT_O_N; i++) put(t, rt.has[i] ? "1" : "0");
  show(t, "stopping", fk_bool(rt.stopping));
  show(t, "idle_clean", rt.idle_clean);
  show(t, "handoff_ready", rt.handoff_ready);
  show(t, "recovery_enabled", rt.recovery_enabled);
  show(t, "fp_dof_player", rt.fp_dof_player);
  show(t, "fird_tick", fk_number(rt.fird_tick));
  show(t, "ready_tick", fk_number(rt.ready_tick));
  show(t, "fird_capture_phase", rt.fird_capture_phase);
  show(t, "diagnostics_fault", rt.diagnostics_fault);
  show(t, "diagnostics_look_at", rt.diagnostics_look_at);
  show(t, "aim_button", rt.aim_button);
  show(t, "aim_button_held", fk_bool(rt.aim_button_held));
  show(t, "aim_mode_button", rt.aim_mode_button);
  show(t, "aim_mode_held", fk_bool(rt.aim_mode_held));
  show(t, "seated_view_held", fk_bool(rt.seated_view_held));
  put(t, " look=");
  if (rt.diagnostics_look.kind == TFP_RT_REF) put_table(t, &rt.look.table); else put_val(t, rt.diagnostics_look);
  put(t, " owner=");
  if (rt.recovery_owner.present) {
    put_val(t, rt.recovery_owner.identity); put(t, ","); put_val(t, rt.recovery_owner.body); put(t, ",");
    put_val(t, rt.recovery_owner.world);
  } else put(t, "~");
  put(t, " draining=");
  if (rt.owner_draining.present) {
    put_val(t, rt.owner_draining.identity); put(t, ","); put_val(t, rt.owner_draining.body); put(t, ",");
    put_val(t, rt.owner_draining.world); put(t, ","); put_val(t, rt.owner_draining.frame); put(t, ",");
    put_val(t, rt.owner_draining.cleaned_at);
  } else put(t, "~");
  put(t, " previous=");
  if (rt.made[TFP_RT_STAGE_CONTROLLER]) put_val(t, rt.clock_previous); else put(t, "?");
  put(t, " edit_mode=");
  if (rt.made[TFP_RT_STAGE_EDITOR]) put_val(t, fk_text(rt.edit_mode ? "camera" : "weapon")); else put(t, "?");
  put(t, " probe_deadline=");
  if (rt.made[TFP_RT_STAGE_MENU]) put_val(t, rt.probe_deadline); else put(t, "?");
  bool entry = rt.made[TFP_RT_STAGE_FIRDENTRY];
  put(t, " candidate=");
  if (!entry) put(t, "?"); else if (rt.has_candidate) put_table(t, &rt.candidate.table); else put(t, "~");
  put(t, " candidate_time=");
  if (!entry) put(t, "?"); else put_val(t, rt.candidate_time);
  put(t, " hip_capture=");
  if (!entry) put(t, "?"); else if (rt.has_hip_capture) put_table(t, &rt.hip_capture.table); else put(t, "~");
  put(t, " global=");
  put_num(t, K[K_GLOBAL].number);
}

int main(int argc, char **argv) {
  long show_op = -1;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--show") && i + 1 < argc) show_op = atol(argv[++i]);
    else if (strcmp(argv[i], RT_VECTOR_COMMIT)) {
      fprintf(stderr, "Runtime: the vectors come from retail commit %s, not %s\n", RT_VECTOR_COMMIT, argv[i]);
      return 2;
    }
  }
  for (size_t i = 0; i < sizeof rt_entry_names / sizeof *rt_entry_names; i++) {
    entry_ids[i] = -1;
    for (int e = 0; e < TFP_RT_E_COUNT; e++) if (!strcmp(tfp_rt_entry_name((TfpRtEntry)e), rt_entry_names[i])) entry_ids[i] = e;
    if (entry_ids[i] < 0 || tfp_rt_entry_values((TfpRtEntry)entry_ids[i]) != rt_entry_nret[i]) {
      fprintf(stderr, "Runtime: entry %s differs between the vectors and the C\n", rt_entry_names[i]);
      return 2;
    }
  }
  long failed = 0, sessions = 0, traced = 0, raised = 0;
  Text actual = {0, 0, 0}, now = {0, 0, 0}, before = {0, 0, 0};
  put(&before, "");
  for (long index = 0; index < RT_OPS; index++) {
    const RtOp *op = &rt_ops[index];
    if (op->first) {
      for (int i = 0; i < RT_KNOBS; i++) K[i] = value_of(&rt_defaults[i]);
      shadows = 0;
      memset(&host, 0, sizeof host);
      tfp_rt_init(&rt, &host);
      sessions++;
    }
    for (unsigned i = 0; i < op->sets; i++) K[rt_sets[op->set0 + i].knob] = value_of(&rt_sets[op->set0 + i].value);
    fk_bind(&host);
    op_now = op;
    calls = 0;
    memset(seen, 0, sizeof seen);
    trace.used = 0;
    put(&trace, "");
    actual.used = 0;
    put(&actual, "");
    run_entry(&actual, op->entry, rt_values + op->arg0, op->args);
    if (actual.at[0] == '!') raised++;
    put(&actual, "|");
    put(&actual, trace.at);
    put(&actual, "|");
    now.used = 0;
    put(&now, "");
    state(&now);
    if (!op->first && !strcmp(now.at, before.at)) put(&actual, "="); else put(&actual, now.at);
    before.used = 0;
    put(&before, now.at);
    traced += calls;
    const char *expect = rt_expect[op->expect];
    bool same = !strcmp(actual.at, expect);
    if (!same || index == show_op) {
      if (!same) failed++;
      if (!same && getenv("TFP_RUNTIME_ALL")) {
        size_t at = 0;
        while (actual.at[at] && actual.at[at] == expect[at]) at++;
        size_t from = at > 60 ? at - 60 : 0;
        printf("%ld %s @%zu\n  L: %.200s\n  C: %.200s\n", index, rt_entry_names[op->entry], at, expect + from, actual.at + from);
      } else if (failed <= 5 || index == show_op) {
        size_t at = 0;
        while (actual.at[at] && actual.at[at] == expect[at]) at++;
        size_t from = at > 160 ? at - 160 : 0;
        printf("Runtime: operation %ld (%s) %s at character %zu\n  Lua: %s%.400s\n  C:   %s%.400s\n", index, rt_entry_names[op->entry],
               same ? "shown" : "differs", at, from ? "..." : "", expect + from, from ? "..." : "", actual.at + from);
      }
    }
  }
  printf("Runtime: %ld of %d operations in %ld sessions equal to retail runtime.lua at %s (%ld calls into the fakes, %ld raised, "
         "%" PRIu64 " non-whole doubles by their bits)\n", (long)RT_OPS - failed, RT_OPS, sessions, RT_VECTOR_COMMIT, traced, raised,
         numbers_compared);
  free(actual.at);
  free(now.at);
  free(before.at);
  free(trace.at);
  return failed ? 1 : 0;
}
