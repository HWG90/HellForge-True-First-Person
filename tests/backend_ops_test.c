/* Replays tests/vectors/backend_ops_vectors.h against src/backend_ops.c: the same scripts the
 * retail backend.lua answered under LuaJIT (tools/retail_ref/run_backend_ops.lua), on the same
 * game states written in build 25480438's layout. After every step the ordered trace (engine
 * calls with arguments, page queries, memory writes, helper and hook calls, log lines), the
 * answer and the state of the backend, of every lease and of every token must be retail's text.
 *
 * Built twice by tests/run-vectors-backend-ops.sh: against the production object (runs that use
 * the presentation switch are left out; that it refuses is checked here), and against the object
 * with the test-only open constants (-DOPS_OPEN_CONSTANTS; every run). */
#define _POSIX_C_SOURCE 200809L
#include "backend_ops.h"
#include "vectors/backend_ops_vectors.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- trace ---- */
static char *trace;
static size_t trace_used, trace_size;
static void emit(const char *format, ...) {
  va_list args;
  for (;;) {
    va_start(args, format);
    int n = vsnprintf(trace + trace_used, trace_size - trace_used, format, args);
    va_end(args);
    if (n >= 0 && (size_t)n < trace_size - trace_used) {
      trace_used += (size_t)n;
      return;
    }
    trace_size = trace_size ? trace_size * 2 : 1 << 16;
    trace = realloc(trace, trace_size);
  }
}
static void line(void) {
  if (trace_used) emit("\n");
}
/* Rotating buffers for formatted values. */
static char *scratch(void) {
  static char buffers[64][512];
  static unsigned next;
  return buffers[next++ % 64];
}
static const char *N(double x) {
  char *out = scratch();
  if (x != x) snprintf(out, 512, "nan");
  else if (x == floor(x) && fabs(x) < 1e17) snprintf(out, 512, "%.0f", x);
  else snprintf(out, 512, "%.17g", x);
  return out;
}
static const char *B(bool value) { return value ? "t" : "f"; }
static const char *tri(int value) { return value < 0 ? "-" : value ? "t" : "f"; }
static const char *hex(const uint8_t *bytes, size_t size) {
  char *out = scratch();
  for (size_t i = 0; i < size; i++) snprintf(out + i * 2, 3, "%02x", bytes[i]);
  return out;
}

/* ---- the fake process ---- */
#define MEMORY_SLOTS (1u << 18)
static struct { uint64_t at; uint8_t value, used; } *memory;
static struct { uint64_t game, exe; bool wine; struct { uint32_t state, kind, protect; int allocation; bool answered; } region; } state;
static const OpsRun *run;
static unsigned slot_of_address(uint64_t at) {
  unsigned i = (unsigned)((at * UINT64_C(0x9E3779B97F4A7C15)) >> 46) & (MEMORY_SLOTS - 1);
  while (memory[i].used && memory[i].at != at) i = (i + 1) & (MEMORY_SLOTS - 1);
  return i;
}
static void poke(uint64_t at, int value) { /* value < 0: unmapped */
  unsigned i = slot_of_address(at);
  memory[i].at = at;
  memory[i].used = value < 0 ? 2 : 1;
  memory[i].value = (uint8_t)value;
}
static bool peek(uint64_t at, uint8_t *out) {
  unsigned i = slot_of_address(at);
  if (memory[i].used != 1) return false;
  *out = memory[i].value;
  return true;
}
static void apply_records(const OpsRecord *records, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    const OpsRecord *r = records + i;
    if (r->at == 1 || r->at == 2) {
      uint64_t value;
      memcpy(&value, r->bytes, 8);
      if (r->at == 1) state.game = value;
      else state.exe = value;
      continue;
    }
    for (unsigned j = 0; j < r->size; j++) poke(r->at + j, r->bytes ? r->bytes[j] : -1);
  }
}
static void first_image(void) {
  memset(memory, 0, sizeof *memory * MEMORY_SLOTS);
  apply_records(run->init, run->init_count);
  state.game = run->game, state.exe = run->exe;
}
static void apply_patch(int index) {
  if (index >= 0) apply_records(run->patches[index].records, run->patches[index].count);
}
static struct { uint64_t at, size; int patch, count; } armed[64];
static unsigned armed_count;
/* A read that covers an armed address counts down; at zero the scene changes, after that read. */
static void sprung(uint64_t at, size_t size) {
  for (int i = (int)armed_count - 1; i >= 0; i--) {
    if (at >= armed[i].at + armed[i].size || armed[i].at >= at + size || --armed[i].count > 0) continue;
    int patch = armed[i].patch;
    memmove(armed + i, armed + i + 1, sizeof *armed * (armed_count - (unsigned)i - 1));
    armed_count--;
    apply_patch(patch);
  }
}
static const char *address_name(uint64_t at) {
  char *out = scratch();
  for (size_t i = 0; i < sizeof ops_game_names / sizeof *ops_game_names; i++)
    if (at >= state.game && at - state.game == ops_game_names[i].offset) return snprintf(out, 512, "game:%s", ops_game_names[i].name), out;
  for (size_t i = 0; i < sizeof ops_exe_names / sizeof *ops_exe_names; i++)
    if (at >= state.exe && at - state.exe == ops_exe_names[i].offset) return snprintf(out, 512, "exe:%s", ops_exe_names[i].name), out;
  return N((double)at);
}
/* A text in quotes, with retail's "at0x<address>" spelled by name, as the Lua runner does. */
static const char *Q(const char *text) {
  char *out = scratch();
  size_t used = 0;
  out[used++] = '"';
  while (*text && used < 440) {
    if (!strncmp(text, "at0x", 4)) {
      char *end;
      unsigned long long value = strtoull(text + 4, &end, 16);
      if (end != text + 4) {
        used += (size_t)snprintf(out + used, 512 - used, "at%s", address_name(value));
        text = end;
        continue;
      }
    }
    out[used++] = *text++;
  }
  out[used++] = '"';
  out[used] = 0;
  return out;
}

/* ---- scripted answers ---- */
typedef struct Answer { int code, patch; unsigned count; double nums[12]; bool has_text; char text[96]; } Answer;
typedef struct Port { char name[64]; Answer fifo[64]; unsigned head, tail; bool has_default; Answer fallback; } Port;
static Port ports[40];
static unsigned port_count;
static Port *port(const char *name, bool create) {
  for (unsigned i = 0; i < port_count; i++)
    if (!strcmp(ports[i].name, name)) return ports + i;
  if (!create) return NULL;
  memset(ports + port_count, 0, sizeof *ports);
  snprintf(ports[port_count].name, sizeof ports[port_count].name, "%s", name);
  return ports + port_count++;
}
static bool answer(const char *name, Answer *out) {
  Port *p = port(name, false);
  if (!p) return false;
  if (p->head != p->tail) {
    *out = p->fifo[p->head++ % 64];
    return true;
  }
  if (p->has_default) *out = p->fallback;
  return p->has_default;
}
/* took(): the patch, then the raise. true: raised, `why` holds the text. */
static bool took(bool has, const Answer *a, TfpOpsText *why) {
  if (!has) return false;
  apply_patch(a->patch);
  if (a->code != 99) return false;
  why->present = true;
  snprintf(why->text, sizeof why->text, "%s", a->has_text ? a->text : "raised");
  return true;
}
static void second(const Answer *a, TfpOpsText *why) {
  why->present = a->has_text;
  snprintf(why->text, sizeof why->text, "%s", a->has_text ? a->text : "");
}

/* ---- slots ---- */
static TfpOps backend;
static bool live;
static TfpOpsSnapshot snaps[16];
static bool has_snap[16];
static TfpOpsLease *leases[63];
static TfpOpsToken *tokens[63];
static TfpOpsAction actions[4];
static bool has_action[4];
static void *arena[4096];
static unsigned arena_count;
static void *fresh(size_t size) {
  void *p = calloc(1, size);
  arena[arena_count++] = p;
  return p;
}
static const char *lease_slot(const TfpOpsLease *l) {
  if (!l) return "-";
  for (int i = 0; i < 63; i++)
    if (leases[i] == l) return N(i);
  return "?";
}

/* ---- host ---- */
static uint64_t host_module(void *user, TfpOpsModule which) {
  (void)user;
  return which == TFP_OPS_GAME ? state.game : state.exe;
}
static bool host_read(void *user, uint64_t at, void *out, size_t size) {
  bool all = true;
  (void)user;
  for (size_t i = 0; i < size && all; i++) all = peek(at + i, (uint8_t *)out + i);
  sprung(at, size);
  return all;
}
static bool host_write(void *user, uint64_t at, const void *bytes, size_t size) {
  uint64_t value = 0;
  (void)user;
  memcpy(&value, bytes, size);
  line();
  emit("write %s %u %s", N((double)at), (unsigned)size, N((double)value));
  for (size_t i = 0; i < size; i++) poke(at + i, ((const uint8_t *)bytes)[i]);
  return true;
}
static void host_query(void *user, uint64_t at, TfpOpsRegion *out) {
  (void)user;
  line();
  emit("query %s", address_name(at));
  out->answered = state.region.answered;
  out->state = state.region.state, out->kind = state.region.kind, out->protect = state.region.protect;
  out->allocation = state.region.allocation == 1 ? state.game : state.region.allocation == 2 ? state.exe : 0;
}
static bool host_call(void *user, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  static const char *const names[] = {"allowed", "enter", "exit", "presentation", "body_exists", "body_trigger", "detach"};
  static const unsigned counts[] = {2, 3, 1, 4, 2, 2, 2};
  Answer a;
  (void)user;
  line();
  emit("call %s %s", names[which], address_name(address));
  for (unsigned i = 0; i < counts[which]; i++) emit(" %s", N((double)args[i]));
  bool has = answer(names[which], &a);
  if (took(has, &a, raised)) return false;
  *result = has ? (uint64_t)(int64_t)a.code : 1;
  return true;
}
static void host_log(void *user, const char *text) {
  (void)user;
  const char *spelled = Q(text); /* the same spelling of addresses as in answers; without the quotes */
  line();
  emit("log %.*s", (int)strlen(spelled) - 2, spelled + 1);
}
static TfpOpsForeign hook_foreign(void *user, TfpOpsText *raised) {
  Answer a;
  (void)user;
  line();
  emit("hook foreign_camera");
  bool has = answer("foreign_camera", &a);
  if (took(has, &a, raised)) return TFP_OPS_FOREIGN_RAISED;
  if (!has || a.code == 0) return TFP_OPS_FOREIGN_FALSE;
  return a.code == 1 ? TFP_OPS_FOREIGN_TRUE : TFP_OPS_FOREIGN_OTHER;
}
static TfpOpsResult hook_before_entry(void *user, TfpOpsText *why) {
  Answer a;
  (void)user;
  line();
  emit("hook camera_before_entry");
  bool has = answer("camera_before_entry", &a);
  if (took(has, &a, why)) return TFP_OPS_RAISED;
  if (!has || a.code == 1) return TFP_OPS_TRUE;
  second(&a, why);
  return a.code == 0 ? TFP_OPS_FALSE : TFP_OPS_NIL;
}
static bool placement_from(const double *nums, TfpOpsPlacement *p) {
  memset(p, 0, sizeof *p);
  if (nums[0] < 0) return false;
  p->present = (unsigned)nums[0];
  p->x = nums[1], p->y = nums[2], p->z = nums[3], p->yaw = nums[4], p->pitch = nums[5], p->roll = nums[6];
  return true;
}
static TfpOpsResult hook_resolver(void *user, const char *key, TfpOpsPlacement *out, TfpOpsText *raised) {
  Answer a;
  (void)user;
  line();
  emit("hook placement_resolver %s", key);
  bool has = answer("placement_resolver", &a);
  if (took(has, &a, raised)) return TFP_OPS_RAISED;
  if (!has || a.code != 1 || !placement_from(a.nums, out)) return TFP_OPS_NIL;
  if (!(out->present & TFP_OPS_YAW)) out->yaw = 0;
  if (!(out->present & TFP_OPS_PITCH)) out->pitch = 0;
  if (!(out->present & TFP_OPS_ROLL)) out->roll = 0;
  return TFP_OPS_TRUE;
}
static uint64_t identity_of(double slot, bool player) {
  int index = (int)slot;
  if (index == -1) return 0;
  if (index == -2) return UINT64_C(0xA07E4);
  if (index < 0 || index > 15 || !has_snap[index]) return 0;
  return player ? tfp_ops_player_key(snaps + index) : tfp_ops_identity_key(snaps + index);
}
static bool phase_from(const double *nums, TfpOpsPhase *t) {
  memset(t, 0, sizeof *t);
  if (nums[0] < 0) return false;
  t->has_epoch = nums[1] == nums[1], t->epoch = nums[1];
  t->has_progress = nums[2] == nums[2], t->progress = nums[2];
  t->ads = nums[3] == 0 ? 0 : nums[3] == 1 ? 1 : -1;
  t->identity = identity_of(nums[4], false);
  t->player_identity = identity_of(nums[5], true);
  t->has_started = nums[6] == nums[6], t->started = nums[6];
  t->has_duration = nums[7] == nums[7], t->duration = nums[7];
  t->continuous = nums[8] == 0 ? 0 : nums[8] == 1 ? 1 : -1;
  t->reason = nums[9] >= 1 && nums[9] <= 10 ? (int)nums[9] : 0;
  t->action = nums[10] >= 0 ? (int)nums[10] : -1;
  return true;
}
static TfpOpsResult hook_transition(void *user, TfpOpsPhase *out, TfpOpsText *raised) {
  Answer a;
  (void)user;
  line();
  emit("hook presentation_transition");
  bool has = answer("presentation_transition", &a);
  if (took(has, &a, raised)) return TFP_OPS_RAISED;
  if (!has || a.code != 1 || !phase_from(a.nums, out)) return TFP_OPS_NIL;
  return TFP_OPS_TRUE;
}

/* ---- the scripted pose and alignment helpers ---- */
typedef struct FakeHandle { TfpOpsHandle handle; int id; } FakeHandle;
typedef struct Fake { char tag; TfpOpsPoseHelper api; FakeHandle *owned; } Fake;
static Fake fakes[2];
static int native_count, handle_count;
static const char *handle_id(const TfpOpsHandle *handle) {
  char *out = scratch();
  if (!handle) return "h-";
  snprintf(out, 512, "h%d", ((const FakeHandle *)handle)->id);
  return out;
}
static const char *config_text(const TfpOpsHelperConfig *c) {
  char *out = scratch();
  snprintf(out, 512, "%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s", address_name(c->game), address_name(c->exe),
           N((double)c->context), N((double)c->request - (double)c->context), N((double)c->avatar_owner), N(c->avatar_index),
           N(c->avatar), N(c->weapon), N(c->body), N(c->viewmodel), N(c->duration_ms), c->pose ? N((double)c->weapon_descriptor) : "-",
           c->pose ? N(c->weapon_unit) : "-", c->pose ? N(c->blend_ms) : "-", c->pose ? B(c->steady_entry) : "-",
           c->pose ? N(c->offset_x) : "-", c->pose ? N(c->offset_y) : "-", c->pose ? N(c->offset_z) : "-");
  return out;
}
static bool helper_port(Fake *f, const char *name, const char *text, Answer *a) {
  char full[40];
  line();
  emit("helper %c.%s%s%s", f->tag, name, text ? " " : "", text ? text : "");
  snprintf(full, sizeof full, "%c.%s", f->tag, name);
  return answer(full, a);
}
static TfpOpsResult plain(bool has, const Answer *a, TfpOpsText *why) {
  if (took(has, a, why)) return TFP_OPS_RAISED;
  if (!has) return TFP_OPS_TRUE;
  second(a, why);
  return a->code == 1 ? TFP_OPS_TRUE : a->code == 0 ? TFP_OPS_FALSE : TFP_OPS_NIL;
}
static FakeHandle *own(Fake *f) {
  FakeHandle *h = fresh(sizeof *h);
  h->handle.owner = &f->api;
  h->id = ++handle_count;
  f->owned = h;
  return h;
}
static void stats_from(const Answer *a, bool numeric, TfpOpsHelperStats *s) {
  double armed = a && a->count > 0 ? a->nums[0] : 1, expired = a && a->count > 1 ? a->nums[1] : 0;
  memset(s, 0, sizeof *s);
  /* stats(): 0 false, 1 true, n: the number n-1. finish(): the number itself. */
  s->armed = numeric ? (int)armed : armed <= 1 ? (int)armed : (int)armed - 1;
  s->expired = numeric ? (int)expired : expired <= 1 ? (int)expired : (int)expired - 1;
  s->has_error = !(a && a->count > 2) || a->nums[2] == a->nums[2];
  s->error = a && a->count > 2 && s->has_error ? a->nums[2] : 0;
  s->target_weight = a && a->count > 3 ? a->nums[3] : 1;
  s->blend = a && a->count > 4 ? a->nums[4] : 1;
}
static TfpOpsResult fake_prepare(void *user, const TfpOpsHelperConfig *config, void **native, TfpOpsText *why) {
  Answer a;
  bool has = helper_port(user, "prepare", config_text(config), &a);
  if (took(has, &a, why)) return TFP_OPS_RAISED;
  if (!has || a.code == 1) {
    *native = (void *)(intptr_t)++native_count;
    return TFP_OPS_TRUE;
  }
  second(&a, why);
  return a.code == 0 ? TFP_OPS_FALSE : TFP_OPS_NIL;
}
static TfpOpsResult fake_begin(void *user, const TfpOpsHelperConfig *config, void *native, TfpOpsHandle **handle, TfpOpsText *why) {
  Fake *f = user;
  Answer a;
  char text[640];
  snprintf(text, sizeof text, "%s n%s", config_text(config), N((double)(intptr_t)native));
  bool has = helper_port(f, "begin", text, &a);
  if (has) apply_patch(a.patch);
  if (!has || a.code == 1) {
    *handle = &own(f)->handle;
    return TFP_OPS_TRUE;
  }
  second(&a, why);
  if (a.code == 2) return own(f), TFP_OPS_FALSE;
  if (a.code == 98 || a.code == 99) {
    if (a.code == 98) own(f);
    why->present = true;
    snprintf(why->text, sizeof why->text, "%s", a.has_text ? a.text : "raised");
    return TFP_OPS_RAISED;
  }
  return a.code == 0 ? TFP_OPS_FALSE : TFP_OPS_NIL;
}
static TfpOpsResult fake_owned_handle(void *user, TfpOpsHandle **handle, TfpOpsText *why) {
  Fake *f = user;
  Answer a;
  bool has = helper_port(f, "owned_handle", NULL, &a);
  if (took(has, &a, why)) return TFP_OPS_RAISED;
  *handle = NULL;
  if (!has) *handle = f->owned ? &f->owned->handle : NULL;
  else if (a.code == 3) {
    FakeHandle *other = fresh(sizeof *other);
    other->handle.owner = &f->api;
    *handle = &other->handle;
  }
  return TFP_OPS_TRUE;
}
static TfpOpsResult fake_finish(void *user, TfpOpsHandle *handle, bool *has_stats, TfpOpsHelperStats *stats, TfpOpsText *why) {
  Fake *f = user;
  Answer a;
  bool has = helper_port(f, "finish", handle_id(handle), &a);
  if (took(has, &a, why)) return TFP_OPS_RAISED;
  if (has && (a.code == 0 || a.code == -1)) {
    second(&a, why);
    return a.code == 0 ? TFP_OPS_FALSE : TFP_OPS_NIL;
  }
  if (handle) handle->disarmed = true;
  if (f->owned && &f->owned->handle == handle) f->owned = NULL;
  *has_stats = has && a.code == 1;
  if (*has_stats) stats_from(&a, true, stats);
  return TFP_OPS_TRUE;
}
static TfpOpsResult fake_stats(void *user, TfpOpsHandle *handle, TfpOpsHelperStats *stats, TfpOpsText *why) {
  Answer a;
  bool has = helper_port(user, "stats", handle_id(handle), &a);
  if (took(has, &a, why)) return TFP_OPS_RAISED;
  if (!has || a.code == 1) {
    stats_from(has ? &a : NULL, false, stats);
    return TFP_OPS_TRUE;
  }
  second(&a, why);
  return a.code == 0 ? TFP_OPS_NIL : TFP_OPS_FALSE;
}
static TfpOpsResult fake_set_placement(void *user, TfpOpsHandle *handle, double x, double y, double z, TfpOpsText *why) {
  Answer a;
  char text[256];
  snprintf(text, sizeof text, "%s %s %s %s", handle_id(handle), N(x), N(y), N(z));
  bool has = helper_port(user, "set_placement", text, &a);
  return plain(has, &a, why);
}
static TfpOpsResult fake_set_ads_placement(void *user, TfpOpsHandle *handle, double x, double y, double z, TfpOpsText *why) {
  Answer a;
  char text[256];
  snprintf(text, sizeof text, "%s %s %s %s", handle_id(handle), N(x), N(y), N(z));
  bool has = helper_port(user, "set_ads_placement", text, &a);
  return plain(has, &a, why);
}
static TfpOpsResult fake_set_zoom_policy(void *user, TfpOpsHandle *handle, bool active, TfpOpsText *why) {
  Answer a;
  char text[64];
  snprintf(text, sizeof text, "%s %s", handle_id(handle), B(active));
  bool has = helper_port(user, "set_zoom_policy", text, &a);
  return plain(has, &a, why);
}
static TfpOpsResult fake_transition(void *user, TfpOpsHandle *handle, const TfpOpsPhase *token, TfpOpsText *why) {
  Answer a;
  char text[256];
  snprintf(text, sizeof text, "%s %s %s %s", handle_id(handle), N(token->epoch), N(token->progress), B(token->ads == 1));
  bool has = helper_port(user, "transition", text, &a);
  return plain(has, &a, why);
}
static TfpOpsResult fake_set_motion(void *user, TfpOpsHandle *handle, double dx, double dy, double dz, double qx, double qy, double qz,
                                    double qw, bool authored, TfpOpsText *why) {
  Answer a;
  char text[400];
  snprintf(text, sizeof text, "%s %s %s %s %s %s %s %s %s", handle_id(handle), N(dx), N(dy), N(dz), N(qx), N(qy), N(qz), N(qw), B(authored));
  bool has = helper_port(user, "set_motion", text, &a);
  return plain(has, &a, why);
}
static void fake_init(Fake *f, char tag) {
  memset(f, 0, sizeof *f);
  f->tag = tag;
  f->api = (TfpOpsPoseHelper){f, fake_prepare, fake_begin, fake_owned_handle, fake_finish, fake_stats, fake_set_placement,
                              fake_set_zoom_policy, fake_set_ads_placement, fake_transition, fake_set_motion};
}
static const TfpOpsPoseHelper *load(int which, TfpOpsText *raised) {
  Answer a;
  line();
  emit("load %c", fakes[which].tag);
  bool has = answer(which ? "load_A" : "load_P", &a);
  if (took(has, &a, raised)) return NULL;
  return &fakes[which].api;
}
static const TfpOpsPoseHelper *load_pose(void *user, TfpOpsText *raised) { return (void)user, load(0, raised); }
static const TfpOpsPoseHelper *load_alignment(void *user, TfpOpsText *raised) { return (void)user, load(1, raised); }
static TfpOpsHost host = {NULL, false, host_module, host_read, host_write, host_query, host_call, NULL, host_log,
                          NULL, NULL, NULL, NULL, load_pose, load_alignment};

/* ---- printing what retail's tables hold ---- */
static const char *same_list(const TfpOpsSnapshot *s, bool player) {
  char *out = scratch();
  size_t used = 0;
  out[0] = 0;
  for (int i = 0; i < 16; i++) {
    if (!has_snap[i]) continue;
    bool same = player ? tfp_ops_same_player(snaps + i, s) : tfp_ops_same_identity(snaps + i, s);
    if (same) used += (size_t)snprintf(out + used, 512 - used, "%s%d", used ? "," : "", i);
  }
  return out;
}
static void action_text(const TfpOpsAction *a) {
  static const unsigned shown[] = {1, 3, 10, 11, 14};
  emit("%s %s %s %s %s ", N(a->body), N((double)a->body_object), N((double)a->graph), N((double)a->instance), N(a->layer_count));
  for (int i = 7; i >= 0; i--) emit("%02X", a->resource[i]);
  emit(" %s", B(a->resource_match));
  for (int i = 0; i < 5; i++) emit(" %s", a->sampled >> shown[i] & 1 ? N(a->states[shown[i]]) : "-");
  emit(" u");
  bool first = true;
  for (unsigned layer = 1; layer <= 32; layer++)
    if (a->unknown >> layer & 1) emit("%s%u", first ? "" : ",", layer), first = false;
  emit(" %s", B(a->sprint_hypothesis));
}
static void snapshot_text(const TfpOpsSnapshot *s) {
  emit("k=%s h=%s t=%s rec=%s ra=%s rw=%s ctx=%s cam=%s world=%s ctl=%s cb=%s ca=%s bl=%s av=%s ad=%s rl=%s own=%s ai=%s body=%s", N(s->camera_kind),
       N(s->head), N(s->tail), N((double)s->record - (double)s->context), N(s->request_avatar), N(s->request_weapon), N((double)s->context),
       N((double)s->camera), N((double)s->world), N((double)s->controller), hex(s->controller_bytes, 6), B(s->controller_active),
       B(s->blocked), N(s->avatar), hex(s->avatar_descriptor, 20), B(s->request_local), N((double)s->avatar_owner), N(s->avatar_index),
       N(s->body));
  emit(" fa=%s fl=%s vm=%s aim=%s sh=%s seat=%s rel=%s safe=%s wt=%s hct=%s pin=%s cu=%s fc=%s why=%s",
       N((double)s->flags_address - (double)s->avatar_owner - (double)s->avatar_index * OPS_STRIDE - (double)OPS_FLAGS), hex(s->flags, 24),
       N(s->viewmodel), B(s->aiming), B(s->shoulder_aim), B(s->seated), B(s->reloading), B(s->safe), B(s->weapon_transition),
       B(s->head_camera_transition), s->pin_camera ? "t" : "-", s->pin_camera ? N(s->camera_unit) : "-", s->foreign_camera ? "t" : "-",
       s->has_reason ? Q(s->reason) : "-");
  if (s->full)
    emit(" w=%s wu=%s wcd=%s wd=%s wr=%s es=%s npe=%s act=", N(s->weapon), N(s->weapon_unit), N((double)s->weapon_component_descriptor),
         hex(s->weapon_descriptor, 20), B(s->weapon_ready), B(s->engine_safe), B(s->native_pose_eligible));
  else emit(" w=- wu=- wcd=- wd=- wr=- es=- npe=- act=");
  if (s->has_action) action_text(&s->action);
  else emit("-");
  emit(" ae=%s id=%s pid=%s", s->has_action_error ? Q(s->action_error) : "-", same_list(s, false), same_list(s, true));
}
static const char *stats_text(bool has, const TfpOpsHelperStats *s) {
  char *out = scratch();
  if (!has) return "-";
  snprintf(out, 512, "%s,%s,%s,%s,%s", s->normalised ? B(s->armed) : N(s->armed), s->normalised ? B(s->expired) : N(s->expired),
           s->has_error ? N(s->error) : "-", N(s->target_weight), N(s->blend));
  return out;
}
static char *shown[64];
/* Drops what was just written from `start` when it is what this slot showed last time. */
static void unchanged(int slot, size_t start) {
  if (shown[slot] && !strcmp(shown[slot], trace + start)) {
    trace_used = start;
    trace[start] = 0;
    return;
  }
  free(shown[slot]);
  shown[slot] = strdup(trace + start);
}
static void forget_shown(void) {
  for (int i = 0; i < 64; i++) free(shown[i]), shown[i] = NULL;
}
static void state_text(void) {
  const TfpOps *b = &backend;
  line();
  emit("B e=%s a=%s al=%s wl=%s pp=", N((double)b->frame_epoch), B(b->frame_active), lease_slot(b->alignment_lease), lease_slot(b->warm_lease));
  if (b->has_pose_placement)
    emit("%s,%s,%s,%s,%s,%s", N(b->pose_placement.x), N(b->pose_placement.y), N(b->pose_placement.z), N(b->pose_placement.yaw),
         N(b->pose_placement.pitch), N(b->pose_placement.roll));
  else emit("-");
  emit(" key=%s%s keeps=%s det=%s ppe=%s ppl=%s pub=%s coi=%s rdc=", b->has_placement_weapon_key ? "1" : "-",
       b->has_placement_weapon_key ? b->placement_weapon_key : "", b->aim_view_keeps ? N(b->aim_view_keeps) : "-",
       b->seated_detaches ? N(b->seated_detaches) : "-", b->has_presentation_pose_epoch ? N(b->presentation_pose_epoch) : "-",
       lease_slot(b->presentation_pose_lease), B(b->publication.has), b->camera_only_identity.has ? B(b->camera_only_identity.hammer) : "-");
  if (b->renderer.has)
    emit("t%s%s%s", B(b->renderer.has_indices), b->renderer.has_indices && b->renderer.has_index[0] ? N(b->renderer.index[0]) : "-",
         b->renderer.has_indices && b->renderer.has_index[1] ? N(b->renderer.index[1]) : "-");
  else emit("f---");
  emit(" ph=%s ah=%s pose=%s align=%s", fakes[0].owned ? N(fakes[0].owned->id) : "-", fakes[1].owned ? N(fakes[1].owned->id) : "-",
       B(b->pose != NULL), B(b->alignment != NULL));
  for (int i = 0; i < 63; i++) { /* a lease line (and the token line) only when it changed, as the Lua runner does */
    const TfpOpsLease *l = leases[i];
    if (!l) continue;
    size_t start = trace_used;
    emit("\nL%d o=%s,%s req=%s boot=%s own=%s done=%s cu=%s err=%s pbe=%s al=", i, N(l->origin.camera_kind), N(l->origin.tail),
         N((double)l->request - (double)l->origin.context), l->bootstrap ? "t" : "-", B(l->owner != NULL), l->done ? "t" : "-",
         l->cleanup_only ? "t" : "-", l->has_error ? Q(l->error) : "-", tri(l->pose_blend_entry));
    if (l->has_alignment) {
      char tag = l->alignment.helper == &fakes[0].api ? 'P' : 'A';
      emit("%s,%s,%s,n%s,%c", l->alignment.kind == TFP_OPS_KIND_POSE ? "pose" : "alignment", B(l->alignment.ended),
           l->alignment.handle ? handle_id(l->alignment.handle) : "h-", N((double)(intptr_t)l->alignment.prepared), tag);
    } else emit("-");
    emit(" eac=%s ecc=%s bac=%s bcc=%s as=%s ps=%s pe=%s por=%s prr=%s op=%s bpa=%s bpr=%s", tri(l->entry_aim_changed),
         tri(l->entry_controller_changed), tri(l->body_event_aim_changed), tri(l->body_event_controller_changed),
         stats_text(l->has_alignment_stats, &l->alignment_stats), stats_text(l->has_pose_stats, &l->pose_stats),
         l->has_pose_error ? N(l->pose_error) : "-", l->pose_owner_retired ? "t" : "-", l->has_pose_retire_reason ? Q(l->pose_retire_reason) : "-",
         l->orphan_polls ? N(l->orphan_polls) : "-", l->body_pose_attempted ? "t" : "-", l->body_pose_released ? "t" : "-");
    unchanged(i, start);
  }
  size_t start = trace_used;
  emit("\nT ");
  bool first = true;
  for (int i = 0; i < 63; i++)
    if (tokens[i]) emit("%s%d=%s", first ? "" : " ", i, B(tokens[i]->used)), first = false;
  unchanged(63, start);
}
static void result(TfpOpsResult r, const TfpOpsText *why) {
  line();
  if (r == TFP_OPS_RAISED) {
    emit("= R %s", Q(why->text));
    return;
  }
  emit("= %s", r == TFP_OPS_TRUE ? "t" : r == TFP_OPS_FALSE ? "f" : "-");
  if (why->present) emit(" %s", Q(why->text));
}
static void config_result(TfpOpsResult r, const TfpOpsConfig *c, const TfpOpsText *why) {
  if (r != TFP_OPS_TRUE) {
    result(r, why);
    return;
  }
  line();
  emit("= %s %s %s %s %s %s %s %s %s %s", address_name(c->game), address_name(c->exe), N((double)c->context), N((double)c->camera),
       N((double)c->avatar_owner), c->has_request ? N((double)c->request - (double)c->context) : "-", N(c->avatar_index), N(c->avatar),
       N(c->body), c->has_weapon ? N(c->weapon) : "-");
  if (c->is_camera)
    emit(" f f %s f %s %s %s %s %s %s %s %s", B(c->presentation_enabled), N(c->camera_unit), N(c->camera_kind), N(c->forward), N(c->up),
         c->has_right ? N(c->right) : "-", N(c->smoothing_ms), N(c->blend_ms), N(c->max_gap_ms));
  else emit(" - - - - - - - - - - - -");
}
static void context_result(bool ok, const TfpOpsContext *c, const TfpOpsText *why) {
  line();
  if (!ok) {
    emit("= R %s", Q(why->text));
    return;
  }
  emit("= %s %s %s %s %s %s %s %s %s %s", address_name(c->exe), N(c->unit), N(c->index), N(c->generation), N((double)c->object),
       N((double)c->registry), N((double)c->units), N((double)c->generations), c->has_owner ? N((double)c->owner) : "-",
       c->has_avatar_index ? N(c->avatar_index) : "-");
  if (c->pose)
    emit(" %s %s %s %s %s %s", N((double)c->weapon_descriptor), hex(c->weapon_bytes, 20), N(c->weapon_unit), N(c->weapon_index),
         N(c->weapon_generation), N((double)c->weapon_object));
  else emit(" - - - - - -");
}

/* ---- steps ---- */
static const TfpOpsSnapshot *snap_arg(double index) { return index >= 0 && index < 16 && has_snap[(int)index] ? snaps + (int)index : NULL; }
static TfpOpsLease *lease_arg(double index) { return index >= 0 && index < 63 ? leases[(int)index] : NULL; }
static TfpOpsToken *token_arg(double index) { return index >= 0 && index < 63 ? tokens[(int)index] : NULL; }
static void keep_lease(double index, TfpOpsResult r, TfpOpsLease *l) {
  if (r == TFP_OPS_TRUE && index >= 0 && index < 63) leases[(int)index] = l;
}
static void keep_token(double index, TfpOpsResult r, TfpOpsToken *t) {
  if (r == TFP_OPS_TRUE && index >= 0 && index < 63) tokens[(int)index] = t;
}
static const void *binding_ids[8][4];
static TfpOpsBinding binding(double kind, double which) {
  static const uint64_t offsets[3] = {OPS_RENDER_GET, OPS_RENDER_SET, 0x1234};
  TfpOpsBinding b = {&binding_ids[(int)kind][(int)which], state.exe + offsets[(int)which], kind != 0};
  return b;
}
static void step(const OpsStep *s) {
  const double *a = s->nums;
  const char *op = s->op;
  TfpOps *b = &backend;
  TfpOpsText why;
  TfpOpsResult r;
  memset(&why, 0, sizeof why);
#define IS(name) (!strcmp(op, name))
  if (IS("new")) {
    state.wine = a[0] == 1;
    host.wine = state.wine;
    fake_init(fakes, 'P');
    fake_init(fakes + 1, 'A');
    tfp_ops_init(b, &host, a[2] == 1 ? &fakes[1].api : NULL, a[1] == 1 ? &fakes[0].api : NULL);
    memset(has_snap, 0, sizeof has_snap);
    memset(leases, 0, sizeof leases);
    memset(tokens, 0, sizeof tokens);
    memset(has_action, 0, sizeof has_action);
    native_count = handle_count = 0;
    forget_shown();
    live = true;
  } else if (IS("patch")) {
    apply_patch((int)a[0]);
  } else if (IS("load")) {
    first_image();
    apply_patch((int)a[0]);
    armed_count = 0;
    for (unsigned i = 0; i < port_count; i++) ports[i].head = ports[i].tail = 0;
  } else if (IS("trap")) {
    if (a[0] < 0) armed_count = 0;
    else {
      armed[armed_count].at = run->traps[(int)a[0]].at, armed[armed_count].size = run->traps[(int)a[0]].size;
      armed[armed_count].patch = (int)a[1], armed[armed_count++].count = (int)a[2];
    }
  } else if (IS("ans")) {
    char name[64];
    const char *bar = strchr(s->text, '|');
    snprintf(name, sizeof name, "%.*s", bar ? (int)(bar - s->text) : (int)strlen(s->text), s->text);
    Port *p = port(name, true);
    Answer entry;
    memset(&entry, 0, sizeof entry);
    entry.code = (int)a[0], entry.patch = (int)a[1];
    for (unsigned i = 3; i < s->count && entry.count < 12; i++) entry.nums[entry.count++] = a[i];
    entry.has_text = bar != NULL;
    if (bar) snprintf(entry.text, sizeof entry.text, "%s", bar + 1);
    if (a[2] == 1) p->has_default = true, p->fallback = entry;
    else if (a[2] == 2) p->has_default = false, p->head = p->tail = 0;
    else p->fifo[p->tail++ % 64] = entry;
  } else if (IS("hook")) {
    bool on = a[0] == 1;
    if (!strcmp(s->text, "foreign_camera")) host.foreign_camera = on ? hook_foreign : NULL;
    else if (!strcmp(s->text, "camera_before_entry")) host.camera_before_entry = on ? hook_before_entry : NULL;
    else if (!strcmp(s->text, "placement_resolver")) host.placement_resolver = on ? hook_resolver : NULL;
    else if (!strcmp(s->text, "presentation_transition")) host.presentation_transition = on ? hook_transition : NULL;
    else if (!strcmp(s->text, "P.owned_handle")) fakes[0].api.owned_handle = on ? fake_owned_handle : NULL;
    else if (!strcmp(s->text, "P.transition")) fakes[0].api.transition = on ? fake_transition : NULL;
    else abort();
  } else if (IS("set")) {
    if (!strcmp(s->text, "region")) {
      state.region.state = (uint32_t)a[0], state.region.kind = (uint32_t)a[1], state.region.protect = (uint32_t)a[2];
      state.region.allocation = (int)a[3], state.region.answered = a[4] == 1;
    } else if (!strcmp(s->text, "keep_aim_view")) b->profile.keep_aim_view = a[0] == 1;
    else if (!strcmp(s->text, "seated_head")) b->profile.seated_head = a[0] == 1;
    else if (!strcmp(s->text, "camera_profile")) b->profile.has_camera = a[0] == 1;
    else if (!strcmp(s->text, "support_grip_tracking")) b->support_grip_tracking = a[0] == 1;
    else if (!strcmp(s->text, "loader")) host.load_pose = a[0] == 1 ? load_pose : NULL, host.load_alignment = a[0] == 1 ? load_alignment : NULL;
    else if (!strcmp(s->text, "fird")) {
      b->fird_camera_active = a[0] == 1;
      b->has_fird_camera_offset = a[1] == 1;
      if (b->has_fird_camera_offset) b->fird_camera_offset[0] = a[2], b->fird_camera_offset[1] = a[3], b->fird_camera_offset[2] = a[4];
    } else abort();
  } else if (IS("begin_frame")) {
    tfp_ops_begin_frame(b);
  } else if (IS("end_frame")) {
    tfp_ops_end_frame(b);
  } else if (IS("camera_ready")) {
    r = tfp_ops_camera_ready(b, &why);
    result(r, &why);
  } else if (IS("snap")) {
    TfpOpsSnapshot taken;
    r = a[0] == 0 ? tfp_ops_snapshot(b, &taken, &why) : a[0] == 1 ? tfp_ops_player_snapshot(b, &taken, &why) : tfp_ops_pin_player_snapshot(b, &taken, &why);
    if (r == TFP_OPS_TRUE) {
      snaps[(int)a[1]] = taken, has_snap[(int)a[1]] = true;
      line();
      emit("= ");
      snapshot_text(&taken);
    } else result(r, &why);
  } else if (IS("backpack")) {
    TfpOpsBackpack pack;
    r = tfp_ops_backpack(b, snap_arg(a[0]), &pack, &why);
    if (r == TFP_OPS_TRUE) {
      line();
      emit("= %s %s %s", N(pack.ref), N(pack.entity), hex(pack.descriptor, 20));
    } else result(r, &why);
  } else if (IS("camera_only")) {
    r = tfp_ops_camera_only_weapon(b, snap_arg(a[0]), &why);
    result(r, &why);
  } else if (IS("action")) {
    TfpOpsAction action;
    r = tfp_ops_action_state(b, snap_arg(a[0]), &action, &why);
    if (r == TFP_OPS_TRUE) {
      actions[(int)a[1]] = action, has_action[(int)a[1]] = true;
      line();
      emit("= ");
      action_text(&action);
    } else result(r, &why);
  } else if (IS("on_foot")) {
    bool on = tfp_ops_on_foot(b, snap_arg(a[0]), a[1] >= 0 && has_action[(int)a[1]] ? actions + (int)a[1] : NULL);
    result(on ? TFP_OPS_TRUE : TFP_OPS_FALSE, &why);
  } else if (IS("camera_config") || IS("fov_config") || IS("blur_config") || IS("fird_fov_config")) {
    TfpOpsConfig c;
    memset(&c, 0, sizeof c);
    r = IS("camera_config") ? tfp_ops_camera_config(b, &c, &why)
        : IS("fov_config") ? tfp_ops_fov_config(b, lease_arg(a[0]), &c, &why)
        : IS("blur_config") ? tfp_ops_blur_config(b, lease_arg(a[0]), &c, &why) : tfp_ops_fird_fov_config(b, &c, &why);
    config_result(r, &c, &why);
  } else if (IS("warm_start")) {
    TfpOpsLease *l = fresh(sizeof *l);
    r = tfp_ops_warm_start(b, snap_arg(a[0]), l, &why);
    keep_lease(a[1], r, l);
    result(r, &why);
  } else if (IS("warm_finish")) {
    r = tfp_ops_warm_finish(b, lease_arg(a[0]), &why);
    result(r, &why);
  } else if (IS("presentation")) {
    r = tfp_ops_invoke_presentation(b, snap_arg(a[0]), a[1] == 1, &why);
    result(r, &why);
  } else if (IS("prepare_pose") || IS("prepare_alignment") || IS("prepare_body_pose")) {
    TfpOpsToken *t = fresh(sizeof *t);
    r = IS("prepare_pose") ? tfp_ops_prepare_pose(b, snap_arg(a[0]), t, &why)
        : IS("prepare_alignment") ? tfp_ops_prepare_alignment(b, snap_arg(a[0]), t, &why) : tfp_ops_prepare_body_pose(b, snap_arg(a[0]), t, &why);
    keep_token(a[1], r, t);
    result(r, &why);
  } else if (IS("enter")) {
    TfpOpsLease *l = fresh(sizeof *l);
    r = tfp_ops_enter(b, snap_arg(a[0]), token_arg(a[1]), l, &why);
    keep_lease(a[2], r, l);
    result(r, &why);
  } else if (IS("enter_pose")) {
    TfpOpsLease *l = fresh(sizeof *l);
    r = tfp_ops_enter_pose(b, snap_arg(a[0]), token_arg(a[1]), a[2], a[3] < 0 ? -1 : (int)a[3], l, &why);
    keep_lease(a[4], r, l);
    result(r, &why);
  } else if (IS("set_placement_defaults") || IS("set_pose_placement") || IS("set_ads_placement")) {
    TfpOpsPlacement p;
    unsigned at = IS("set_placement_defaults") ? 0 : 1;
    bool has = placement_from(a + at, &p);
    r = IS("set_placement_defaults") ? tfp_ops_set_placement_defaults(b, has ? &p : NULL, &why)
        : IS("set_pose_placement") ? tfp_ops_set_pose_placement(b, lease_arg(a[0]), has ? &p : NULL, &why)
                                   : tfp_ops_set_ads_placement(b, lease_arg(a[0]), has ? &p : NULL, &why);
    result(r, &why);
  } else if (IS("set_pose_zoom_policy")) {
    r = tfp_ops_set_pose_zoom_policy(b, lease_arg(a[0]), a[1] == 1, &why);
    result(r, &why);
  } else if (IS("set_pose_transition")) {
    TfpOpsPhase token;
    bool has = phase_from(a + 1, &token);
    r = tfp_ops_set_pose_transition(b, lease_arg(a[0]), has ? &token : NULL, &why);
    result(r, &why);
  } else if (IS("set_pose_motion")) {
    TfpOpsMotion m;
    memset(&m, 0, sizeof m);
    if (a[1] >= 0) {
      m.q_present = (unsigned)a[1];
      m.dx = a[2], m.dy = a[3], m.dz = a[4];
      for (int i = 0; i < 4; i++) m.q[i] = a[6 + i];
      m.authored_ads = a[10] == 1;
    }
    r = tfp_ops_set_pose_motion(b, lease_arg(a[0]), a[1] >= 0 ? &m : NULL, &why);
    result(r, &why);
  } else if (IS("pose_status") || IS("alignment_status")) {
    TfpOpsHelperStats stats;
    r = IS("pose_status") ? tfp_ops_pose_status(b, lease_arg(a[0]), &stats, &why) : tfp_ops_alignment_status(b, lease_arg(a[0]), &stats, &why);
    if (r == TFP_OPS_TRUE) {
      line();
      emit("= %s", stats_text(true, &stats));
    } else result(r, &why);
  } else if (IS("release_body_pose")) {
    r = tfp_ops_release_body_pose(b, lease_arg(a[0]), token_arg(a[1]), &why);
    result(r, &why);
  } else if (IS("keep_aim_view")) {
    r = tfp_ops_keep_aim_view(b, snap_arg(a[0]), &why);
    result(r, &why);
  } else if (IS("detach_seated_camera")) {
    r = tfp_ops_detach_seated_camera(b, &why);
    result(r, &why);
  } else if (IS("release")) {
    r = tfp_ops_release(b, lease_arg(a[0]), &why);
    result(r, &why);
  } else if (IS("renderer")) {
    static const TfpOpsSetting settings[] = {TFP_OPS_SETTING_DOF, TFP_OPS_SETTING_VIGNETTE, TFP_OPS_SETTING_OTHER, TFP_OPS_SETTING_DEFAULT};
    r = tfp_ops_renderer_dof_value(b, binding(a[0], a[1]), binding(a[2], a[3]), settings[(int)a[4]], &why);
    result(r, &why);
  } else if (IS("context")) {
    const TfpOpsSnapshot *snap = snap_arg(a[0]);
    TfpOpsContext c;
    if (!snap) {
      line();
      emit("= -");
    } else {
      bool ok = a[1] == 3 ? tfp_ops_pose_context(b, snap, &c, &why)
                          : tfp_ops_alignment_context(b, a[1] == 0 ? snap->viewmodel : a[1] == 1 ? snap->body : snap->full ? snap->weapon_unit : 0,
                                                      true, snap->avatar_owner, true, snap->avatar_index, true, &c, &why);
      context_result(ok, &c, &why);
    }
  } else if (IS("module") || IS("executable")) {
    uint64_t base = 0;
    bool ok = IS("module") ? tfp_ops_module(b, (TfpOpsScope)(int)a[0], &base, &why) : tfp_ops_alignment_executable(b, (TfpOpsExecutable)(int)a[0], &base, &why);
    line();
    if (ok) emit("= %s", address_name(base));
    else emit("= R %s", Q(why.text));
  } else if (IS("invalidate")) {
    tfp_ops_invalidate_frame_checks(b);
  } else if (IS("map") || IS("descriptor") || IS("peek")) {
    const TfpOpsSnapshot *snap = snap_arg(IS("map") ? a[3] : IS("descriptor") ? a[1] : a[2]);
    line();
    if (!snap) emit("= -");
    else if (IS("map")) {
      uint32_t index = 0;
      uint64_t at = a[0] == 0 ? snap->world + OPS_ENTITY_MAP : a[0] == 1 ? snap->avatar_owner + 0xf8 : snap->world + OPS_LIGHT_MAP;
      if (tfp_ops_map(b, at, (uint32_t)a[1], a[2] >= 0 ? (uint32_t)a[2] : 0, a[2] >= 0, &index, &why)) emit("= %s", N(index));
      else emit("= R %s", Q(why.text));
    } else if (IS("descriptor")) {
      uint8_t d[24];
      if (tfp_ops_descriptor(b, snap->world, (uint32_t)a[0], d, &why)) emit("= %s", hex(d, 24));
      else emit("= R %s", Q(why.text));
    } else {
      const uint64_t places[] = {snap->controller, snap->context, snap->context + 0x1f8, 4096, snap->camera, UINT64_C(0x800000000000) - 4};
      const size_t sizes[] = {0, 0, 0, 6, 0, 1025, 1024};
      uint64_t at = places[(int)a[1]], pointer = 0;
      uint8_t bytes[1025], byte = 0;
      uint32_t word = 0;
      int kind = (int)a[0];
      bool ok = kind == 0 ? tfp_ops_u8(b, at, &byte, &why) : kind == 1 ? tfp_ops_u32(b, at, &word, &why)
                : kind == 2 ? tfp_ops_ptr(b, at, &pointer, &why) : tfp_ops_read(b, at, bytes, sizes[kind], &why);
      if (!ok) emit("= R %s", Q(why.text));
      else if (kind == 0) emit("= %s", N(byte));
      else if (kind == 1) emit("= %s", N(word));
      else if (kind == 2) emit("= %s", N((double)pointer));
      else emit("= %u:%s", (unsigned)sizes[kind], hex(bytes, sizes[kind] < 8 ? sizes[kind] : 8));
    }
  } else {
    fprintf(stderr, "unknown op %s\n", op);
    exit(2);
  }
#undef IS
  if (live) state_text();
}

#ifndef OPS_OPEN_CONSTANTS
static bool uses_presentation(const OpsRun *r) {
  for (unsigned i = 0; i < r->step_count; i++)
    if (!strcmp(r->steps[i].op, "presentation")) return true;
  return false;
}
#endif
int main(int argc, char **argv) {
  const char *commit = argc > 1 ? argv[1] : "1d0d453";
  if (strcmp(BACKEND_OPS_VECTOR_COMMIT, commit) || strlen(BACKEND_OPS_VECTOR_SHA256) != 64) {
    fprintf(stderr, "backend ops vectors were cut from retail %s, expected %s\n", BACKEND_OPS_VECTOR_COMMIT, commit);
    return 2;
  }
  unsigned run_count = sizeof ops_runs / sizeof *ops_runs, steps = 0, calls = 0, failures = 0, skipped = 0, natives = 0, writes = 0;
  memory = malloc(sizeof *memory * MEMORY_SLOTS);
  for (unsigned n = 0; n < run_count; n++) {
    run = ops_runs + n;
#ifndef OPS_OPEN_CONSTANTS
    if (uses_presentation(run)) {
      skipped++;
      continue;
    }
#endif
    memset(&state, 0, sizeof state);
    state.region.state = 0x1000, state.region.kind = 0x1000000, state.region.protect = 0x20, state.region.allocation = 1;
    state.region.answered = true;
    port_count = 0;
    armed_count = 0;
    live = false;
    host.foreign_camera = NULL, host.camera_before_entry = NULL, host.placement_resolver = NULL, host.presentation_transition = NULL;
    host.load_pose = load_pose, host.load_alignment = load_alignment;
    first_image();
    for (unsigned i = 0; i < run->step_count; i++) {
      const OpsStep *s = run->steps + i;
      trace_used = 0;
      emit("%s", "");
      trace[0] = 0;
      step(s);
      steps++;
      if (strcmp(s->op, "new") && strcmp(s->op, "patch") && strcmp(s->op, "ans") && strcmp(s->op, "hook") && strcmp(s->op, "set") &&
          strcmp(s->op, "trap") && strcmp(s->op, "invalidate") && strcmp(s->op, "load"))
        calls++;
      for (const char *p = trace; (p = strstr(p, "\ncall ")); p++) natives++;
      if (!strncmp(trace, "call ", 5)) natives++;
      for (const char *p = trace; (p = strstr(p, "write ")); p++) writes += p == trace || p[-1] == '\n';
      if (strcmp(trace, s->expected)) {
        if (failures++ < 6)
          fprintf(stderr, "run %s, step %u (%s): the C differs from retail\n--- C\n%s\n--- retail\n%s\n", run->name, i, s->op, trace, s->expected);
        break;
      }
    }
    for (unsigned i = 0; i < arena_count; i++) free(arena[i]);
    arena_count = 0;
  }
#ifndef OPS_OPEN_CONSTANTS
  { /* the open constants: the production object must refuse, touching nothing */
    TfpOpsText why;
    TfpOpsSnapshot none;
    memset(&none, 0, sizeof none);
    memset(&why, 0, sizeof why);
    run = ops_runs;
    trace_used = 0;
    trace[0] = 0;
    fake_init(fakes, 'P');
    tfp_ops_init(&backend, &host, NULL, NULL);
    if (tfp_ops_invoke_presentation(&backend, &none, true, &why) != TFP_OPS_RAISED || strcmp(why.text, "FP presentation is not established on build 25480438 (function, code pins and mode byte are open)") ||
        trace_used) {
      fprintf(stderr, "the presentation switch must refuse while its constants are open: \"%s\"\n", why.text);
      failures++;
    }
  }
#endif
  { /* a write the host cannot make (retail stores through a raw pointer and has no such answer): raised, nothing counted */
    static TfpOpsHost dry;
    TfpOpsText why;
    TfpOpsSnapshot view;
    uint8_t active[6] = {0, 0, 0, 0, 1, 0};
    memset(&why, 0, sizeof why);
    memset(&view, 0, sizeof view);
    memset(memory, 0, sizeof *memory * MEMORY_SLOTS);
    dry = host;
    dry.write = NULL;
    view.safe = view.controller_active = view.request_local = true;
    view.camera_kind = 6, view.controller = 0x500000000, view.record = 0x300000200;
    memcpy(view.controller_bytes, active, 6);
    for (int i = 0; i < 6; i++) poke(view.controller + (unsigned)i, active[i]);
    tfp_ops_init(&backend, &dry, NULL, NULL);
    if (tfp_ops_keep_aim_view(&backend, &view, &why) != TFP_OPS_RAISED || strcmp(why.text, "Memory write unavailable") || backend.aim_view_keeps) {
      fprintf(stderr, "a refused write must be raised: \"%s\"\n", why.text);
      failures++;
    }
  }
  unsigned hold_worlds = 0;
  { /* the action hold's view (not in retail): on every run's first image that has a safe player with a firearm */
    static TfpOpsHost quiet;
    quiet = host;
    quiet.log = NULL, quiet.foreign_camera = NULL, quiet.camera_before_entry = NULL, quiet.placement_resolver = NULL, quiet.presentation_transition = NULL;
    for (unsigned n = 0; n < run_count && !failures; n++) {
      TfpOpsSnapshot full, again;
      TfpOpsHold hold;
      TfpOpsText why;
      uint64_t wield = 0, rows = 0;
      uint32_t index = 0;
      const char *wrong = NULL;
      run = ops_runs + n;
      memset(&state, 0, sizeof state);
      armed_count = 0;
      first_image();
      tfp_ops_init(&backend, &quiet, NULL, NULL);
      if (tfp_ops_snapshot(&backend, &full, &why) != TFP_OPS_TRUE || !full.safe) continue;
      uint64_t physics = full.avatar_owner + 0x547844 + (uint64_t)full.avatar_index * 0x78;
      if (tfp_ops_hold_snapshot(&backend, &hold, &why) != TFP_OPS_NIL) wrong = "an unreadable ragdoll byte must be no answer";
      poke(physics, 0x01);
      if (!wrong && (tfp_ops_hold_snapshot(&backend, &hold, &why) != TFP_OPS_TRUE || !tfp_ops_same_player(&hold.player, &full) || hold.player.full ||
                     !hold.player.safe || hold.ragdoll || hold.foreign_camera || hold.wielded != full.weapon ||
                     hold.player.record != full.record || hold.player.camera_kind != full.camera_kind))
        wrong = "a safe player with its firearm";
      poke(full.controller, 2); /* an action */
      if (!wrong && (tfp_ops_snapshot(&backend, &again, &why) != TFP_OPS_TRUE || again.safe)) wrong = "the full snapshot must be unsafe in an action";
      if (!wrong && (tfp_ops_hold_snapshot(&backend, &hold, &why) != TFP_OPS_TRUE || hold.player.safe || hold.wielded != full.weapon ||
                     !tfp_ops_same_player(&hold.player, &full)))
        wrong = "an action: the same player, unsafe, the firearm still wielded";
      if (!wrong && (!tfp_ops_ptr(&backend, full.base + 53634080ULL, &wield, &why) || !tfp_ops_map(&backend, wield + 0x30, full.avatar, 4096, true, &index, &why) ||
                     !tfp_ops_ptr(&backend, wield + 0x60, &rows, &why)))
        wrong = "the wield row of the test image";
      if (!wrong) {
        for (int i = 0; i < 4; i++) poke(rows + (uint64_t)index * 0x1d0 + (unsigned)i, 0); /* empty hands */
        if (tfp_ops_snapshot(&backend, &again, &why) != TFP_OPS_NIL) wrong = "the full snapshot must fail with empty hands";
        else if (tfp_ops_hold_snapshot(&backend, &hold, &why) != TFP_OPS_TRUE || hold.wielded || hold.player.safe ||
                 !tfp_ops_same_player(&hold.player, &full))
          wrong = "empty hands: still the player, nothing wielded";
      }
      poke(physics, 0x11);
      if (!wrong && (tfp_ops_hold_snapshot(&backend, &hold, &why) != TFP_OPS_TRUE || !hold.ragdoll)) wrong = "the ragdoll bit";
      if (wrong) {
        fprintf(stderr, "hold view, run %s: %s (\"%s\")\n", run->name, wrong, why.present ? why.text : "");
        failures++;
      }
      hold_worlds++;
    }
    if (!hold_worlds && !failures) {
      fprintf(stderr, "hold view: no run starts with a safe full snapshot\n");
      failures++;
    }
  }
  if (failures) {
    fprintf(stderr, "%u runs differ from retail\n", failures);
    return 1;
  }
#ifdef OPS_OPEN_CONSTANTS
  const char *object = "test-only open constants", *presentation = "included";
#else
  const char *object = "production object", *presentation = "left out (it refuses: open constants)";
#endif
  printf("Backend operations (%s): %u runs, %u steps, %u method calls answered as retail's backend.lua (%s); %u engine calls and %u memory "
         "writes compared; %u runs with the presentation switch %s\n",
         object, run_count - skipped, steps, calls, BACKEND_OPS_VECTOR_COMMIT, natives, writes, skipped, presentation);
  printf("Backend operations, hold view (not in retail): on %u first images: the player with a firearm, in an action, with empty hands, as a ragdoll\n",
         hold_worlds);
  return 0;
}
