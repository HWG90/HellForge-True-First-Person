/* The pose binder of the feel build (src/wire_pose.c): the signature adapters between backend_ops'
 * TfpOpsPoseHelper and the translated pose.lua, the lease and controller fields, the runtime's depth
 * and input statistics, the session-log lines and the weapon placement jump meter.
 *
 * Two parts. First against a scripted helper (a TfpPoseHelperApi that answers what the test says).
 * Then against the real src/pose_adapter.c and src/pose_helper.c on fake game memory with this
 * build's layout: the world of the pose helper's differential test (tests/diff_pose_helper_world.h),
 * with a scripted TfpPoseHost. No Windows, no game.
 *
 * This file is compiled twice (tests/run-wire-pose.sh): as C for the test, and as C++ with
 * -DTFP_POSE_WORLD_SHIM for the few lines that build that world, which is a C++ header. */
#ifdef TFP_POSE_WORLD_SHIM
#include <cstdio>
#include <cstdlib>
#include "diff_pose_helper_world.h"
using namespace dp;
extern "C" {
struct PoseWorld {
  uint64_t game, exe, context, request, avatar_owner, weapon_descriptor, stack, api, slot[5], native[5];
  uint64_t rig_return, input_return, getter_return, api_begin, api_end;
  uint32_t avatar, weapon, body, viewmodel, weapon_unit;
};
static World W;
static Scene scene;
static void sync() { W.apply(scene, 0); scene.write_preset = false; scene.write_root_local = false; }
/* Third person: the newest record is an ordinary one, the owned row is the next to be written
 * (the differential test's Run::normal). */
void pose_world_third_person(void) {
  scene.ring[0] = static_cast<uint32_t>((scene.row + 31) % 32); scene.ring[1] = static_cast<uint32_t>(scene.row);
  scene.under[0] = 4; scene.under[1] = AVATAR; scene.under[2] = scene.under[3] = 0;
  std::memset(scene.request, 0, sizeof scene.request); scene.write_preset = true;
  std::memset(scene.controller, 0, 6); scene.flags[6] = 0; scene.flags[10] = 0; scene.flags[14] = 0; scene.flags[15] = 0;
  scene.wdesc[0] = WEAPON; scene.wdesc[1] = WEAPON_UNIT; scene.descriptor[2] = AVATAR; scene.descriptor[3] = BODY; scene.stack_index = 0;
  scene.rig_reference = VIEWMODEL;
  sync();
}
/* The game's own first-person request in the owned row, alone in the ring (Run::fpv). */
void pose_world_first_person(void) {
  auto req = [](unsigned at, uint32_t v) { std::memcpy(scene.request + at, &v, 4); };
  scene.ring[0] = static_cast<uint32_t>(scene.row); scene.ring[1] = static_cast<uint32_t>((scene.row + 1) % 32);
  req(0, 6); req(4, AVATAR); req(8, WEAPON); req(12, BODY); req(0x18, 3);
  const float pose[4] = {1, 2, 3, 0}; std::memcpy(scene.request + 0x48, pose, 16); req(0x54, 0x80000000u);
  const float zero = 0; std::memcpy(scene.request + 0x98, &zero, 4);
  sync();
}
/* An action: the first controller byte (the action flags), nothing else. 0: none. */
void pose_world_action(unsigned flags) { scene.controller[0] = static_cast<uint8_t>(flags); sync(); }
/* The gun leaves: its unit's generation retires and the request no longer names the weapon; back: both return. */
void pose_world_weapon(int present) {
  const uint32_t weapon = present ? WEAPON : 0;
  scene.generations[WEAPON_UNIT & 0x3fffff] = present ? 1 : 2;
  std::memcpy(scene.request + 8, &weapon, 4);
  sync();
}
void pose_world_seated(int seated) { scene.flags[14] = seated ? 4 : 0; sync(); }
/* Another first-person request of the same avatar, weapon and body pushed above the owned one (1), or taken off again (0).
 * Nothing of the owned record changes: only the top of the ring is another record. */
void pose_world_second_request(int pushed) {
  const uint32_t header[4] = {6, AVATAR, WEAPON, BODY}, nothing[4] = {0, 0, 0, 0};
  scene.ring[1] = static_cast<uint32_t>((scene.row + (pushed ? 2 : 1)) % 32);
  sync();
  World::put(W.request((scene.row + 1) % 32), pushed ? header : nothing);
}
/* One 32-bit field of the owned record's header, in place: 0 kind, 4 avatar, 8 weapon, 12 body. */
void pose_world_request_field(unsigned at, uint32_t value) { std::memcpy(scene.request + at, &value, 4); sync(); }
void pose_world_build(PoseWorld *out) {
  W.build(HERE);
  for (uint32_t i = 0; i < RIG_NODES; ++i) { scene.hashes[i] = 0x1000 + i; scene.links[i] = 0x10001; }
  for (const GraphRow &row : GRAPH_ROWS) { scene.hashes[row.node] = row.hash; scene.links[row.node] = row.link; }
  for (int i = 0; i < 4; ++i) W.native[i] = W.a[A_EXE] + HERE.wrappers[i];
  W.native[4] = W.a[A_EXE] + HERE.world_matrix;
  for (int i = 0; i < 5; ++i) World::put<uint64_t>(W.slot(i), W.native[i]);
  pose_world_third_person();
  out->game = W.a[A_GAME]; out->exe = W.a[A_EXE]; out->context = W.a[A_CONTEXT]; out->request = W.request(scene.row);
  out->avatar_owner = W.a[A_OWNER]; out->weapon_descriptor = W.a[A_WDESC]; out->stack = W.stack(); out->api = W.a[A_API];
  for (int i = 0; i < 5; ++i) { out->slot[i] = W.slot(i); out->native[i] = W.native[i]; }
  out->rig_return = W.a[A_GAME] + HERE.rig_call + 4; out->input_return = W.a[A_GAME] + HERE.input_return;
  out->getter_return = W.a[A_GAME] + HERE.observe_call + 3;
  out->api_begin = W.a[A_API]; out->api_end = W.a[A_API] + 0x1000;
  out->avatar = AVATAR; out->weapon = WEAPON; out->body = BODY; out->viewmodel = VIEWMODEL; out->weapon_unit = WEAPON_UNIT;
}
}
#else
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include "wire_pose.h"
#include "wire_off.h"

static unsigned checks;
#define CHECK(condition) do { checks++; if (!(condition)) { printf("wire pose: FAILED %s (line %d)\n", #condition, __LINE__); return 1; } } while (0)
static bool near(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }
static bool same(const char *got, const char *want) {
  if (!strcmp(got, want)) return true;
  printf("wire pose: got  \"%s\"\n           want \"%s\"\n", got, want);
  return false;
}

static char lines[160][440];
static int line_count;
static void capture(void *user, const char *text) {
  (void)user;
  if (line_count < 160) snprintf(lines[line_count++], sizeof lines[0], "%s", text);
}
static int logged(const char *part) {
  int n = 0;
  for (int i = 0; i < line_count; i++) if (strstr(lines[i], part)) n++;
  return n;
}

/* ---- a binder with no game behind it (as tests/wire_glue_test.c) ---- */
static uint64_t no_module(void *u, TfpOpsModule which) { (void)u; (void)which; return 0; }
static bool no_read(void *u, uint64_t at, void *out, size_t size) { (void)u; (void)at; (void)out; (void)size; return false; }
static bool no_write(void *u, uint64_t at, const void *bytes, size_t size) { (void)u; (void)at; (void)bytes; (void)size; return false; }
static void no_query(void *u, uint64_t at, TfpOpsRegion *out) { (void)u; (void)at; memset(out, 0, sizeof *out); }
static bool no_call(void *u, TfpOpsNative which, uint64_t address, const uint64_t args[4], uint64_t *result, TfpOpsText *raised) {
  (void)u; (void)which; (void)address; (void)args; (void)result;
  raised->present = true;
  snprintf(raised->text, sizeof raised->text, "no game");
  return false;
}
static double test_clock(void *u) { (void)u; return 100; }
static TfpWire wire;
static TfpWirePose pose;
static void new_wire(void) {
  TfpOpsHost memory;
  TfpWirePlatform platform = {NULL, test_clock, NULL, capture};
  TfpWireHelpers helpers = {NULL, NULL, NULL, NULL};
  memset(&memory, 0, sizeof memory);
  memory.module = no_module, memory.read = no_read, memory.write = no_write, memory.query = no_query, memory.call = no_call;
  tfp_wire_init(&wire, &memory, &platform, &helpers);
}

/* ---- the scripted helper ---- */
static struct {
  uint64_t gen, next;
  bool armed, expired, input_active;
  uint32_t error, last_error, validate, begin_result, depth_supported, input_reason;
  int validate_kind, begin_kind, transform_kind;
  float blend, target, placement[3], ads[3], motion[3], q[4], progress;
  uint64_t updates, restores, epoch;
  uint32_t zoom, ads_flag, zoom_index;
  TfpPoseConfig config;
} fake;
static uint32_t f_validate(const TfpPoseConfig *c) { fake.config = *c, fake.validate_kind = 0; return fake.validate; }
static uint32_t f_validate_input(const TfpPoseConfig *c) { fake.config = *c, fake.validate_kind = 1; return fake.validate; }
static uint32_t f_begin_any(int kind, const TfpPoseConfig *c, uint64_t *g) {
  fake.config = *c, fake.begin_kind = kind;
  *g = 0;
  if (fake.begin_result == 0) return 0;
  fake.gen = *g = ++fake.next;
  fake.armed = fake.begin_result == 1, fake.expired = false;
  fake.last_error = fake.begin_result == 2 ? 13 : 0;
  return fake.begin_result;
}
static uint32_t f_begin(const TfpPoseConfig *c, uint64_t *g) { return f_begin_any(0, c, g); }
static uint32_t f_begin_steady(const TfpPoseConfig *c, uint64_t *g) { return f_begin_any(1, c, g); }
static uint32_t f_begin_input(const TfpPoseConfig *c, uint64_t *g) { return f_begin_any(2, c, g); }
static uint32_t f_begin_steady_input(const TfpPoseConfig *c, uint64_t *g) { return f_begin_any(3, c, g); }
static bool mine(uint64_t g) { if (g && g == fake.gen) return true; fake.last_error = 17; return false; }
static uint32_t f_set_placement(uint64_t g, float x, float y, float z) { if (!mine(g)) return 0; fake.placement[0] = x, fake.placement[1] = y, fake.placement[2] = z; return 1; }
static uint32_t f_set_ads(uint64_t g, float x, float y, float z) { if (!mine(g)) return 0; fake.ads[0] = x, fake.ads[1] = y, fake.ads[2] = z; return 1; }
static uint32_t f_transform_any(int kind, uint64_t g, float dx, float dy, float dz, float qx, float qy, float qz, float qw) {
  if (!mine(g)) return 0;
  fake.transform_kind = kind, fake.motion[0] = dx, fake.motion[1] = dy, fake.motion[2] = dz;
  fake.q[0] = qx, fake.q[1] = qy, fake.q[2] = qz, fake.q[3] = qw;
  return 1;
}
static uint32_t f_transform(uint64_t g, float dx, float dy, float dz, float qx, float qy, float qz, float qw) { return f_transform_any(0, g, dx, dy, dz, qx, qy, qz, qw); }
static uint32_t f_authored(uint64_t g, float dx, float dy, float dz, float qx, float qy, float qz, float qw) { return f_transform_any(1, g, dx, dy, dz, qx, qy, qz, qw); }
static uint32_t f_zoom(uint64_t g, uint32_t active) { if (!mine(g)) return 0; fake.zoom = active; return 1; }
static uint32_t f_transition(uint64_t g, uint64_t epoch, float progress, uint32_t ads) {
  if (!mine(g)) return 0;
  fake.epoch = epoch, fake.progress = progress, fake.ads_flag = ads;
  return 1;
}
static uint32_t f_end(uint64_t g) { if (!mine(g)) return 0; fake.gen = 0, fake.armed = false; return 1; }
static bool token(uint64_t g) { if (fake.gen ? g == fake.gen : g == 0) return true; fake.last_error = 17; return false; }
static uint32_t f_stats(uint64_t g, TfpPoseHelperStats *s) {
  if (!token(g)) return 0;
  s->installed = fake.gen != 0, s->armed = fake.armed, s->expired = fake.expired, s->error = fake.error;
  s->weapon_updates = fake.updates, s->zero_restores = fake.restores, s->blend = fake.blend, s->target_weight = fake.target;
  s->seen_position = s->seen_rotation = s->forwarded_position = s->forwarded_rotation = 0;
  return 1;
}
static uint32_t f_depth(uint64_t g, TfpPoseDepthStats *s) {
  if (!token(g)) return 0;
  s->installed = fake.gen != 0, s->armed = fake.armed, s->supported = fake.depth_supported, s->zoom_index = fake.zoom_index;
  return 1;
}
static uint32_t f_input(uint64_t g, TfpPoseInputStats *s) {
  if (!token(g)) return 0;
  s->installed = fake.gen != 0, s->active = fake.input_active, s->reason = fake.input_reason;
  return 1;
}
static uint32_t f_error(void) { return fake.last_error; }
static const TfpPoseHelperApi FAKE = {f_validate, f_validate_input, f_begin, f_begin_steady, f_begin_input, f_begin_steady_input, f_set_placement,
                                      f_set_ads, f_transform, f_authored, f_zoom, f_transition, f_end, f_stats, f_depth, f_input, f_error};
static uint64_t now_ms = 5000;
static uint64_t test_tick(void *u) { (void)u; return now_ms; }
static const TfpPoseAdapterIo IO = {NULL, NULL, test_tick};

static TfpOpsHelperConfig shipped_config(void) {
  TfpOpsHelperConfig c;
  memset(&c, 0, sizeof c);
  c.game = 0x180000000, c.exe = 0x140000000, c.context = 0x1000, c.request = 0x12e8, c.avatar_owner = 0x6000, c.weapon_descriptor = 0x7000;
  c.avatar_index = 1, c.avatar = 7, c.weapon = 912, c.body = 11, c.viewmodel = 77, c.weapon_unit = 78;
  c.duration_ms = 0, c.blend_ms = 150, c.offset_x = .15, c.offset_y = 0, c.offset_z = -.12;
  c.pose = true, c.steady_entry = true;
  return c;
}
/* One game update as the binder sees it: the controller's status poll, then the meter. */
static TfpOpsResult poll(TfpOpsHandle *handle, TfpOpsHelperStats *stats) {
  TfpOpsText why;
  TfpOpsResult r = pose.helper.stats(pose.helper.user, handle, stats, &why);
  tfp_wire_pose_frame(&wire);
  return r;
}

static int scripted(void) {
  const TfpOpsPoseHelper *H;
  TfpOpsText why;
  TfpOpsHandle *handle = NULL, *other = NULL;
  TfpOpsHelperStats stats;
  TfpRtTable t;
  TfpRtValue v, v2;
  void *native = NULL;
  char fragment[200];
  bool has_stats = false;

  /* ---- bind ---- */
  new_wire();
  void (*lease_before)(TfpWire *, const TfpWireLease *, TfpRtTable *) = wire.lease_extra;
  bool (*depth_before)(TfpRtHost *, const TfpRtTable *, TfpRtValue, TfpRtTable *, TfpRtValue *) = wire.host.helper_depth_stats;
  bool (*prepare_before)(TfpRtHost *, const TfpRtTable *, TfpRtValue *, TfpRtValue *) = wire.host.backend_prepare_pose;
  CHECK(!wire.pose_helper && !lease_before && !wire.controller_extra);
  memset(&fake, 0, sizeof fake);
  fake.validate = 1, fake.begin_result = 1, fake.blend = 1, fake.target = 1, fake.input_reason = 2;
  tfp_wire_pose_bind(&wire, &pose, &FAKE, &IO, TFP_POSE_ADAPTER_PROFILE_SHIPPED);
  H = wire.pose_helper;
  CHECK(H == &pose.helper && wire.ext[TFP_WIRE_EXT_POSE] == &pose && wire.lease_extra && wire.controller_extra);
  CHECK(wire.host.helper_depth_stats != depth_before && wire.host.helper_input_stats && wire.host.backend_prepare_pose != prepare_before);
  CHECK(logged("Ready: pose helper bound") == 1 && logged("look gain asked for as retail does") == 1);
  CHECK(wire.host.backend_new(&wire.host) && wire.ops.pose == &pose.helper); /* Backend.new takes it, not the stand-in */

  /* ---- the configuration (backend:839-857 as pose:141-149 reads it) ---- */
  TfpOpsHelperConfig c = shipped_config();
  TfpPoseAdapterConfig converted;
  tfp_wire_pose_config(&c, &converted);
  CHECK(converted.missing == 0 && converted.game == 0x180000000 && converted.exe == 0x140000000 && converted.request == 0x12e8);
  CHECK(converted.avatar_index == 1 && converted.avatar == 7 && converted.weapon == 912 && converted.body == 11 && converted.viewmodel == 77);
  CHECK(converted.weapon_descriptor == 0x7000 && converted.weapon_unit == 78 && converted.duration_ms == 0 && converted.blend_ms == 150);
  CHECK(converted.offset_x == .15 && converted.offset_y == 0 && converted.offset_z == -.12 && converted.steady_entry);
  c.duration_ms = 30500.9, c.steady_entry = false;
  tfp_wire_pose_config(&c, &converted);
  CHECK(converted.duration_ms == 30500 && !converted.steady_entry);
  c.duration_ms = -1;
  tfp_wire_pose_config(&c, &converted);
  CHECK(converted.duration_ms == 0xffffffffu);
  c.duration_ms = NAN;
  tfp_wire_pose_config(&c, &converted);
  CHECK(converted.duration_ms == 0);
  c = shipped_config(), c.pose = false; /* an alignment configuration has none of the six pose fields */
  tfp_wire_pose_config(&c, &converted);
  CHECK(converted.missing == 0x1f800 && !converted.steady_entry);
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_RAISED && strstr(why.text, "weapon_descriptor unavailable") && !native);
  c = shipped_config();

  /* ---- prepare: true with a token, false with retail's text ---- */
  fake.validate = 0, fake.last_error = 7;
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_FALSE && why.present && !native);
  CHECK(!strcmp(why.text, "Pose read-only validation failed (player or camera ownership changed; native code 7)"));
  CHECK(fake.validate_kind == 1); /* the shipped profile validates through the input entry */
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_FALSE);
  CHECK(logged("Refused: pose: read-only validation: Pose read-only validation failed") == 1); /* the same refusal: one line */
  fake.validate = 1;
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && native && !why.present);
  CHECK(fake.config.size == 104 && fake.config.abi == 2 && fake.config.viewmodel == 77 && fake.config.offset_x == .15f);
  CHECK(logged("Ready: pose: the helper's read-only validation passed") == 1);

  /* ---- begin: the handle, its owner, the entry kind ---- */
  CHECK(H->begin(H->user, &c, NULL, &handle, &why) == TFP_OPS_RAISED && strstr(why.text, "Invalid native pose preparation") && !handle);
  CHECK(H->owned_handle(H->user, &other, &why) == TFP_OPS_TRUE && !other);
  CHECK(H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE && handle && handle->owner == H && !handle->disarmed);
  CHECK(fake.begin_kind == 3 && fake.gen == 1); /* steady, through the input entry */
  CHECK(H->owned_handle(H->user, &other, &why) == TFP_OPS_TRUE && other == handle);
  CHECK(logged("Pose: begin generation 1, steady entry") == 1 && logged("placement (0.150, 0.000, -0.120) = 0.1921 m") == 1);
  CHECK(H->begin(H->user, &c, native, &other, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Previous pose generation still requires cleanup") && !other);

  /* ---- statistics: the table, nil with a text, the running counts ---- */
  double updates = -1, restores = -1;
  CHECK(!tfp_wire_pose_counts(&wire, &updates, &restores));
  fake.updates = 41, fake.restores = 2, fake.blend = .25f, fake.target = 1;
  CHECK(H->stats(H->user, handle, &stats, &why) == TFP_OPS_TRUE && stats.armed == 1 && stats.expired == 0 && stats.has_error && stats.error == 0);
  CHECK(stats.blend == .25 && stats.target_weight == 1 && !stats.normalised);
  CHECK(tfp_wire_pose_counts(&wire, &updates, &restores) && updates == 41 && restores == 2);
  CHECK(H->stats(H->user, &wire.null_handle, &stats, &why) == TFP_OPS_NIL && !strcmp(why.text, "Pose statistics handle is not owned"));
  CHECK(H->stats(H->user, NULL, &stats, &why) == TFP_OPS_TRUE && stats.armed == 1); /* nil: the owned handle */

  /* ---- the LEASE table's two function fields ---- */
  static TfpWireLease lease, foreign;
  char identity[24], who[24];
  lease.ops.has_alignment = true, lease.ops.alignment.kind = TFP_OPS_KIND_POSE, lease.ops.alignment.helper = H, lease.ops.alignment.handle = handle;
  tfp_wire_lease_table(&lease, &t, identity, who);
  wire.lease_extra(&wire, &lease, &t);
  CHECK(t.f[TFP_RT_LEASE_DEPTH_STATS].kind == TFP_RT_TRUE && t.f[TFP_RT_LEASE_INPUT_STATS].kind == TFP_RT_TRUE);
  CHECK(t.f[TFP_RT_LEASE_HANDLE].kind == TFP_RT_REF && t.f[TFP_RT_LEASE_HANDLE].ref == handle);
  TfpRtTable lease_table = t;
  foreign.ops.has_alignment = true, foreign.ops.alignment.kind = TFP_OPS_KIND_POSE, foreign.ops.alignment.helper = &wire.null_pose;
  foreign.ops.alignment.handle = &wire.null_handle;
  tfp_wire_lease_table(&foreign, &t, identity, who);
  wire.lease_extra(&wire, &foreign, &t);
  CHECK(t.f[TFP_RT_LEASE_DEPTH_STATS].kind == TFP_RT_NIL && t.f[TFP_RT_LEASE_INPUT_STATS].kind == TFP_RT_NIL);

  /* ---- helper:depth_stats(handle) and helper:input_stats(handle) for the runtime ---- */
  fake.depth_supported = 1, fake.zoom_index = 2;
  CHECK(wire.host.helper_depth_stats(&wire.host, &lease_table, lease_table.f[TFP_RT_LEASE_HANDLE], &t, &v) && t.present);
  CHECK(t.f[TFP_RT_DEPTH_SUPPORTED].kind == TFP_RT_TRUE && t.f[TFP_RT_DEPTH_ARMED].kind == TFP_RT_TRUE && v.kind == TFP_RT_NIL);
  CHECK(t.f[TFP_RT_DEPTH_ZOOM_INDEX].kind == TFP_RT_NUMBER && t.f[TFP_RT_DEPTH_ZOOM_INDEX].number == 2);
  fake.depth_supported = 0;
  CHECK(wire.host.helper_depth_stats(&wire.host, &lease_table, tfp_wire_nil(), &t, &v) && t.present && t.f[TFP_RT_DEPTH_SUPPORTED].kind == TFP_RT_FALSE);
  CHECK(wire.host.helper_depth_stats(&wire.host, &lease_table, tfp_wire_ref(&wire.null_handle), &t, &v) && !t.present);
  CHECK(v.kind == TFP_RT_TEXT && !strcmp(v.text, "Depth statistics handle is not owned"));
  CHECK(wire.host.helper_input_stats(&wire.host, &lease_table, lease_table.f[TFP_RT_LEASE_HANDLE], &v, &v2) && v.kind == TFP_RT_REF && v2.kind == TFP_RT_NIL);
  CHECK(pose.input.installed && !pose.input.active && pose.input.reason == 2);
  CHECK(wire.host.helper_input_stats(&wire.host, &lease_table, tfp_wire_ref(&wire.null_handle), &v, &v2) && v.kind == TFP_RT_NIL);
  CHECK(v2.kind == TFP_RT_TEXT && !strcmp(v2.text, "Input statistics handle is not owned"));

  /* ---- controller:result().blend (ctl:94, ctl:148-155) ---- */
  tfp_wire_controller_state(&wire, &t);
  CHECK(t.f[TFP_RT_STATE_BLEND].kind == TFP_RT_NIL);
  wire.controller.has_blend = true, wire.controller.blend = .25;
  tfp_wire_controller_state(&wire, &t);
  CHECK(t.f[TFP_RT_STATE_BLEND].kind == TFP_RT_NUMBER && t.f[TFP_RT_STATE_BLEND].number == .25);

  /* ---- the debug entry `preflight`: backend:prepare_pose for real (here: no game to prepare on) ---- */
  CHECK(wire.host.backend_prepare_pose(&wire.host, NULL, &v, &v2) && v.kind != TFP_RT_TRUE && v2.kind == TFP_RT_TEXT);

  /* ---- the setters ---- */
  CHECK(H->set_placement(H->user, handle, .1, .02, -.1, &why) == TFP_OPS_TRUE && fake.placement[0] == .1f && fake.placement[2] == -.1f);
  CHECK(H->set_placement(H->user, handle, .6, 0, 0, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Pose placement must be finite XYZ within 0.5 metres"));
  CHECK(H->set_placement(H->user, &wire.null_handle, 0, 0, 0, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Pose placement handle is not owned"));
  CHECK(H->set_ads_placement(H->user, handle, .01, -.02, .03, &why) == TFP_OPS_TRUE && fake.ads[1] == -.02f);
  CHECK(H->set_ads_placement(H->user, handle, 0, .51, 0, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Invalid ADS placement"));
  CHECK(H->set_zoom_policy(H->user, handle, true, &why) == TFP_OPS_TRUE && fake.zoom == 1);
  TfpOpsPhase phase;
  memset(&phase, 0, sizeof phase);
  phase.has_epoch = phase.has_progress = true, phase.epoch = 3, phase.progress = .5, phase.ads = 1;
  CHECK(H->transition(H->user, handle, &phase, &why) == TFP_OPS_TRUE && fake.epoch == 3 && fake.progress == .5f && fake.ads_flag == 1);
  phase.ads = -1; /* not a Boolean */
  CHECK(H->transition(H->user, handle, &phase, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Invalid shared presentation phase"));
  CHECK(H->transition(H->user, handle, NULL, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Invalid shared presentation phase"));
  CHECK(H->set_motion(H->user, handle, .01, 0, -.02, 0, 0, 0, 1, false, &why) == TFP_OPS_TRUE && fake.transform_kind == 0 && fake.motion[2] == -.02f);
  CHECK(H->set_motion(H->user, handle, 0, 0, 0, 0, 0, 0, 1, true, &why) == TFP_OPS_TRUE && fake.transform_kind == 1);
  CHECK(H->set_motion(H->user, handle, .2, 0, 0, 0, 0, 0, 1, false, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Pose motion must be finite XYZ within 0.15 metres"));
  CHECK(H->set_placement(H->user, handle, .15, 0, -.12, &why) == TFP_OPS_TRUE);

  /* ---- the weapon placement jump meter ---- */
  fake.blend = 1, fake.target = 1;
  tfp_wire_pose_second(&wire, fragment, sizeof fragment); /* a new second */
  for (int i = 0; i < 10; i++) CHECK(poll(handle, &stats) == TFP_OPS_TRUE);
  CHECK(near(pose.meter.shift[0], .15, 1e-12) && near(pose.meter.shift[2], -.12, 1e-12) && pose.meter.jump_m == 0 && pose.meter.jump_deg == 0);
  CHECK(H->set_placement(H->user, handle, .15, 0, .08, &why) == TFP_OPS_TRUE); /* the placement snaps 20 cm */
  CHECK(poll(handle, &stats) == TFP_OPS_TRUE && near(pose.meter.jump_m, .2, 1e-9));
  const double ten = sin(5 * 3.14159265358979323846 / 180), ten_w = cos(5 * 3.14159265358979323846 / 180);
  CHECK(H->set_motion(H->user, handle, 0, 0, 0, ten, 0, 0, ten_w, false, &why) == TFP_OPS_TRUE); /* the rotation snaps 10 degrees */
  CHECK(poll(handle, &stats) == TFP_OPS_TRUE && near(pose.meter.jump_deg, 10, 1e-6));
  for (int i = 0; i < 5; i++) poll(handle, &stats);
  size_t n = tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(n == strlen(fragment));
  CHECK(same(fragment, "pose=armed blend=1.00 wjump=0.2000m/10.00deg placements=1 motion=1 err=0 writes=41 look=refused(2)"));
  for (int i = 0; i < 6; i++) poll(handle, &stats);
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(same(fragment, "pose=armed blend=1.00 wjump=0.0000m/0.00deg placements=0 motion=0 err=0 writes=41 look=refused(2)"));
  /* a 150 ms ease of the hip weight at 60 frames a second: centimetres at most, never the whole placement */
  CHECK(H->set_motion(H->user, handle, 0, 0, 0, 0, 0, 0, 1, false, &why) == TFP_OPS_TRUE);
  poll(handle, &stats), poll(handle, &stats);
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  for (int i = 9; i >= 0; i--) {
    double u = i / 9.0;
    fake.blend = (float)(u * u * (3 - 2 * u));
    poll(handle, &stats);
  }
  CHECK(pose.meter.jump_m > .001 && pose.meter.jump_m < .03 && pose.meter.jump_deg == 0);
  /* full ADS (target weight 0): the ADS placement is in the shift */
  fake.blend = 0, fake.target = 0;
  poll(handle, &stats);
  CHECK(near(pose.meter.shift[0], .01, 1e-9) && near(pose.meter.shift[1], -.02, 1e-9) && near(pose.meter.shift[2], .03, 1e-9));
  /* a frame without a poll breaks the history: what happened meanwhile is not a jump of this meter */
  fake.blend = 1, fake.target = 1;
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  tfp_wire_pose_frame(&wire);
  poll(handle, &stats), poll(handle, &stats);
  CHECK(pose.meter.jump_m == 0);

  /* ---- look gain: refused from the lease's second sample on, said once ---- */
  CHECK(logged("Refused: look gain: the look evaluator's code proof is not established on build 25480438") == 1);
  CHECK(logged("Retail's two extra hooks are installed") == 1);
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(logged("Refused: look gain") == 1 && strstr(fragment, "look=refused(2)"));
  fake.input_active = true, fake.input_reason = 0;
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(logged("Ready: look gain: normalisation is active") == 1 && strstr(fragment, "look=on"));
  fake.input_active = false, fake.input_reason = 2; /* and back: a change of state is one more line */
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(logged("Refused: look gain") == 2 && strstr(fragment, "look=refused(2)"));

  /* ---- a helper error: one line with the number and its meaning ---- */
  fake.error = 7, fake.armed = false;
  CHECK(H->stats(H->user, handle, &stats, &why) == TFP_OPS_TRUE && stats.armed == 0 && stats.error == 7);
  CHECK(H->stats(H->user, handle, &stats, &why) == TFP_OPS_TRUE);
  CHECK(logged("Refused: pose: helper error 7 (player or camera ownership changed); armed=0") == 1);
  tfp_wire_pose_frame(&wire);
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(!strncmp(fragment, "pose=unarmed blend=0.00 ", 24) && strstr(fragment, " err=7 "));
  fake.error = 0;

  /* ---- finish: final statistics, the handle disarmed for the backend too ---- */
  fake.updates = 99;
  CHECK(H->finish(H->user, handle, &has_stats, &stats, &why) == TFP_OPS_TRUE && has_stats && handle->disarmed && fake.gen == 0);
  CHECK(logged("Pose: finish generation 1: 99 shifted rig writes, 2 restores, helper error 0 (no error)") == 1);
  CHECK(H->owned_handle(H->user, &other, &why) == TFP_OPS_TRUE && !other);
  CHECK(H->finish(H->user, handle, &has_stats, &stats, &why) == TFP_OPS_TRUE && has_stats); /* again: the kept statistics, no second line */
  CHECK(logged("Pose: finish generation 1") == 1);
  CHECK(H->stats(H->user, handle, &stats, &why) == TFP_OPS_TRUE); /* a disarmed handle: its final statistics */
  CHECK(H->set_placement(H->user, handle, 0, 0, 0, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Pose placement handle is not owned"));
  CHECK(H->finish(H->user, &wire.null_handle, &has_stats, &stats, &why) == TFP_OPS_FALSE && !strcmp(why.text, "Pose handle is not owned by this adapter"));
  CHECK(H->finish(H->user, NULL, &has_stats, &stats, &why) == TFP_OPS_FALSE);
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(!strncmp(fragment, "pose=none ", 10) && strstr(fragment, "look=-"));

  /* ---- a second lease is another handle; a refused begin that took a generation stays owned ---- */
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE);
  c.steady_entry = false;
  CHECK(H->begin(H->user, &c, native, &other, &why) == TFP_OPS_TRUE && other && other != handle && fake.begin_kind == 2 && handle->disarmed);
  CHECK(logged("Pose: begin generation 2, blended entry") == 1);
  CHECK(H->finish(H->user, other, &has_stats, &stats, &why) == TFP_OPS_TRUE);
  fake.begin_result = 2; /* a partial install: the helper owns a generation that must be finished */
  CHECK(H->begin(H->user, &c, native, &other, &why) == TFP_OPS_FALSE && !other);
  CHECK(!strcmp(why.text, "Pose partial install cleanup required failed (owned pointer restoration is still pending; native code 13)"));
  CHECK(H->owned_handle(H->user, &other, &why) == TFP_OPS_TRUE && other && other->owner == H && !other->disarmed);
  CHECK(H->finish(H->user, other, &has_stats, &stats, &why) == TFP_OPS_TRUE && other->disarmed);
  fake.begin_result = 0, fake.last_error = 11; /* refused outright: nothing is owned */
  CHECK(H->begin(H->user, &c, native, &other, &why) == TFP_OPS_FALSE && !other);
  CHECK(!strcmp(why.text, "Pose arm failed (engine API slot ownership changed; native code 11)"));
  CHECK(H->owned_handle(H->user, &other, &why) == TFP_OPS_TRUE && !other);
  CHECK(logged("Refused: pose: begin: Pose arm failed (engine API slot ownership changed; native code 11)") == 1);

  /* ---- the profile switch off: the plain entries, and the look gain said off ---- */
  new_wire();
  fake.begin_result = 1, fake.validate = 1;
  tfp_wire_pose_bind(&wire, &pose, &FAKE, &IO, (TfpPoseAdapterProfile){2, false});
  H = wire.pose_helper;
  CHECK(logged("Refused: look gain: switched off in this build's pose profile") == 1);
  c.steady_entry = true;
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && fake.validate_kind == 0);
  CHECK(H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE && fake.begin_kind == 1);
  int said = logged("Refused: look gain");
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(strstr(fragment, "look=off") && logged("Refused: look gain") == said); /* said once, at bind */
  CHECK(H->finish(H->user, handle, &has_stats, &stats, &why) == TFP_OPS_TRUE);
  CHECK(tfp_wire_pose_error_text(18)[0] == 'n' && !strcmp(tfp_wire_pose_error_text(19), "unknown"));
  return 0;
}

/* ---- the real adapter and helper on fake game memory ------------------------------------------------ */
typedef struct PoseWorld {
  uint64_t game, exe, context, request, avatar_owner, weapon_descriptor, stack, api, slot[5], native[5];
  uint64_t rig_return, input_return, getter_return, api_begin, api_end;
  uint32_t avatar, weapon, body, viewmodel, weapon_unit;
} PoseWorld;
void pose_world_build(PoseWorld *out);
void pose_world_third_person(void);
void pose_world_first_person(void);
void pose_world_action(unsigned flags);
void pose_world_weapon(int present);
void pose_world_seated(int seated);
void pose_world_second_request(int pushed);
void pose_world_request_field(unsigned at, uint32_t value);
static PoseWorld world;
static int lock_depth[2], lock_faults;
static unsigned forwarded_calls, getter_calls, cas_calls, input_only_calls;
static float forwarded[4];
static float getter_answer[4] = {.125f, -.25f, .5f, 0};
static bool w_read(void *u, uint64_t at, void *out, size_t length) {
  struct iovec local = {out, length}, remote = {(void *)(uintptr_t)at, length};
  (void)u;
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)length;
}
static uint64_t w_tick(void *u) { (void)u; return now_ms; }
static uint32_t w_thread(void *u) { (void)u; return 1; }
static bool w_writable_slot(void *u, uint64_t at, uint64_t exe) { (void)u; return exe == world.exe && at >= world.api_begin && at + 8 <= world.api_end; }
static bool w_executable(void *u, uint64_t at) { (void)u; return at >= 65536; }
static bool w_cas(void *u, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed) {
  (void)u;
  cas_calls++;
  __atomic_compare_exchange_n((uint64_t *)(uintptr_t)slot, &expected, desired, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  *observed = expected;
  return true;
}
static bool w_write(void *u, void *out, const void *data, size_t length) { (void)u; memcpy(out, data, length); return true; }
static bool w_pin(void *u, uint64_t address) { (void)u; (void)address; return true; }
static void w_lock(void *u, int lock) { (void)u; if (lock_depth[lock]++) lock_faults++; }
static void w_unlock(void *u, int lock) { (void)u; if (--lock_depth[lock]) lock_faults++; }
/* The look-gain path past its code proof: never reached on this build. */
static bool w_writable32(void *u, uint64_t at) { (void)u; (void)at; input_only_calls++; return false; }
static bool w_cas32(void *u, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  (void)u; (void)at; (void)expected; (void)desired; (void)observed; input_only_calls++; return false;
}
static bool w_region(void *u, uint64_t at, uint64_t *base, uint64_t *size, bool *is_free) {
  (void)u; (void)at; (void)base; (void)size; (void)is_free; input_only_calls++; return false;
}
static uint64_t w_allocate(void *u, uint64_t at, uint64_t size) { (void)u; (void)at; (void)size; input_only_calls++; return 0; }
static double w_exp(void *u, uint64_t address, double x) { (void)u; (void)address; input_only_calls++; return exp(x); }
static void w_forward_setter(void *u, unsigned which, uint64_t original, uint32_t unit, uint32_t node, const float *pose_value) {
  (void)u; (void)unit; (void)node;
  if (original != world.native[which]) lock_faults++;
  if (which == 0) { forwarded_calls++; memcpy(forwarded, pose_value, 16); }
}
static const float *w_forward_getter(void *u, unsigned which, uint64_t original, uint32_t unit, uint32_t node) {
  (void)u; (void)unit; (void)node;
  if (original != world.native[which == 4 ? 4 : 3]) lock_faults++;
  getter_calls++;
  return getter_answer;
}
/* Six distinct values for the six things the helper can put in a slot. */
static const char hook_marker[TFP_POSE_HOOK_COUNT] = {0};
static uint64_t slot_now(int i) { uint64_t v = 0; memcpy(&v, (void *)(uintptr_t)world.slot[i], 8); return v; }
static uint64_t hook_value(int which) { return (uint64_t)(uintptr_t)&hook_marker[which]; }
/* One game frame in first person: the rig-root write of the look evaluator (the position hook, from the
 * pinned call site), then the tick's status poll and the meter. */
static const float NATIVE[3] = {1, 2, 3};
static TfpOpsResult game_frame(TfpOpsHandle *handle, TfpOpsHelperStats *stats, uint64_t step_ms) {
  now_ms += step_ms;
  tfp_pose_position_from(world.rig_return, world.viewmodel, 5, (const float *)(uintptr_t)(world.request + 0x48), world.stack);
  return poll(handle, stats);
}
static bool applied_is(double x, double y, double z) {
  return near(forwarded[0] - NATIVE[0], x, 2e-6) && near(forwarded[1] - NATIVE[1], y, 2e-6) && near(forwarded[2] - NATIVE[2], z, 2e-6);
}
static bool meter_is_applied(void) { /* the meter's commanded shift is what the helper really added to the game's pose */
  return applied_is(pose.meter.shift[0], pose.meter.shift[1], pose.meter.shift[2]);
}

static int real(void) {
  const TfpOpsPoseHelper *H;
  TfpOpsText why;
  TfpOpsHandle *handle = NULL;
  TfpOpsHelperStats stats;
  TfpRtTable t;
  TfpRtValue v;
  void *native = NULL;
  char fragment[200];
  bool has_stats = false;

  pose_world_build(&world);
  TfpPoseHost host;
  memset(&host, 0, sizeof host);
  for (int i = 0; i < TFP_POSE_HOOK_COUNT; i++) host.hooks[i] = hook_value(i);
  host.read = w_read, host.tick = w_tick, host.thread = w_thread, host.writable_slot = w_writable_slot, host.executable = w_executable;
  host.cas = w_cas, host.write = w_write, host.pin = w_pin;
  host.lock_exclusive = w_lock, host.unlock_exclusive = w_unlock, host.lock_shared = w_lock, host.unlock_shared = w_unlock;
  host.writable32 = w_writable32, host.cas32 = w_cas32, host.region = w_region, host.allocate = w_allocate, host.native_exp = w_exp;
  host.forward_setter = w_forward_setter, host.forward_getter = w_forward_getter;
  tfp_pose_set_host(&host);
  tfp_pose_reset_for_tests();

  line_count = 0;
  new_wire();
  tfp_wire_pose_bind(&wire, &pose, tfp_wire_pose_linked(), &IO, TFP_POSE_ADAPTER_PROFILE_SHIPPED);
  H = wire.pose_helper;
  TfpOpsHelperConfig c;
  memset(&c, 0, sizeof c);
  c.game = world.game, c.exe = world.exe, c.context = world.context, c.request = world.request, c.avatar_owner = world.avatar_owner;
  c.weapon_descriptor = world.weapon_descriptor, c.avatar_index = 0, c.avatar = world.avatar, c.weapon = world.weapon, c.body = world.body;
  c.viewmodel = world.viewmodel, c.weapon_unit = world.weapon_unit, c.duration_ms = 0, c.blend_ms = 150;
  c.offset_x = .15, c.offset_y = 0, c.offset_z = -.12, c.pose = true, c.steady_entry = true;

  /* ---- prepare is read-only; begin installs retail's hooks, the input pair included ---- */
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && native);
  CHECK(logged("Ready: pose: the helper's read-only validation passed") == 1 && cas_calls == 0);
  for (int i = 0; i < 5; i++) CHECK(slot_now(i) == world.native[i]);
  CHECK(H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE && handle && handle->owner == H);
  CHECK(slot_now(0) == hook_value(TFP_POSE_HOOK_POSITION) && slot_now(1) == hook_value(TFP_POSE_HOOK_ROTATION));
  CHECK(slot_now(2) == hook_value(TFP_POSE_HOOK_BRANCH_ROTATION));
  /* the shipped profile asks for the look gain: retail's thunk values go in, as when retail's code proof fails */
  CHECK(slot_now(3) == hook_value(TFP_POSE_HOOK_GETTER_INPUT) && slot_now(4) == hook_value(TFP_POSE_HOOK_INPUT90));
  CHECK(H->stats(H->user, handle, &stats, &why) == TFP_OPS_TRUE && stats.armed == 1 && stats.error == 0 && stats.blend == 1);

  /* ---- first person: the helper shifts the rig root by the placement; the meter says the same ---- */
  pose_world_first_person();
  CHECK(game_frame(handle, &stats, 300) == TFP_OPS_TRUE && forwarded_calls == 1); /* 300 ms: the 220 ms draw animation is over */
  CHECK(applied_is(.15, 0, -.12) && meter_is_applied() && stats.armed == 1 && stats.blend == 1);
  CHECK(H->set_placement(H->user, handle, .1, .02, -.1, &why) == TFP_OPS_TRUE);
  CHECK(game_frame(handle, &stats, 16) == TFP_OPS_TRUE && applied_is(.1, .02, -.1) && meter_is_applied());
  CHECK(near(pose.meter.jump_m, sqrt(.05 * .05 + .02 * .02 + .02 * .02), 1e-6)); /* the edit is a snap of that size */
  CHECK(H->set_motion(H->user, handle, .01, 0, -.02, 0, 0, 0, 1, false, &why) == TFP_OPS_TRUE);
  CHECK(game_frame(handle, &stats, 16) == TFP_OPS_TRUE && applied_is(.11, .02, -.12) && meter_is_applied());
  CHECK(H->set_ads_placement(H->user, handle, .01, 0, 0, &why) == TFP_OPS_TRUE);
  TfpOpsPhase phase;
  memset(&phase, 0, sizeof phase);
  phase.has_epoch = phase.has_progress = true, phase.epoch = 1, phase.progress = 1, phase.ads = 0;
  CHECK(H->transition(H->user, handle, &phase, &why) == TFP_OPS_TRUE);
  CHECK(game_frame(handle, &stats, 16) == TFP_OPS_TRUE && applied_is(.11, .02, -.12) && meter_is_applied()); /* hip: no ADS placement */
  /* the largest jump of the second: the motion step right after the placement edit, |(.06, -.02, -.04)| */
  CHECK(tfp_wire_pose_second(&wire, fragment, sizeof fragment));
  fragment[75] = 0;
  CHECK(same(fragment, "pose=armed blend=1.00 wjump=0.0748m/0.00deg placements=2 motion=1 err=0 wri"));

  /* ---- the runtime's depth statistics come from the real helper ---- */
  static TfpWireLease lease;
  char identity[24], who[24];
  lease.ops.has_alignment = true, lease.ops.alignment.kind = TFP_OPS_KIND_POSE, lease.ops.alignment.helper = H, lease.ops.alignment.handle = handle;
  tfp_wire_lease_table(&lease, &t, identity, who);
  wire.lease_extra(&wire, &lease, &t);
  TfpRtTable depth;
  CHECK(wire.host.helper_depth_stats(&wire.host, &t, t.f[TFP_RT_LEASE_HANDLE], &depth, &v) && depth.present);
  CHECK(depth.f[TFP_RT_DEPTH_ARMED].kind == TFP_RT_TRUE && pose.depth.installed);

  /* ---- look gain: the 0x90 entry forwards and changes nothing; refused with reason 2 ---- */
  uint32_t selector_before = 0, selector_after = 0;
  memcpy(&selector_before, (void *)(uintptr_t)(world.request + 0x80), 4);
  const float *answer = tfp_pose_input90_from(world.input_return, world.viewmodel, 50, world.request, world.stack);
  CHECK(answer == getter_answer);
  answer = tfp_pose_position_getter_from(world.getter_return, world.viewmodel, 50, world.stack, world.request);
  CHECK(answer == getter_answer && getter_calls == 2);
  memcpy(&selector_after, (void *)(uintptr_t)(world.request + 0x80), 4);
  CHECK(selector_before == selector_after && input_only_calls == 0);
  TfpRtValue first, second;
  CHECK(wire.host.helper_input_stats(&wire.host, &t, t.f[TFP_RT_LEASE_HANDLE], &first, &second) && first.kind == TFP_RT_REF);
  CHECK(pose.input.installed && !pose.input.active && pose.input.reason == TFP_POSE_INPUT_CODE_UNAVAILABLE && pose.input.normalized == 0);
  CHECK(pose.input.pending == 0 && pose.input.cache_entries == 0);
  game_frame(handle, &stats, 16);
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  CHECK(strstr(fragment, "look=refused(2)") && logged("Refused: look gain: the look evaluator's code proof is not established on build 25480438") == 1);
  CHECK(logged("Retail's two extra hooks are installed") == 1);

  /* ---- finish: every slot holds the game's function again ---- */
  CHECK(H->finish(H->user, handle, &has_stats, &stats, &why) == TFP_OPS_TRUE && has_stats && handle->disarmed);
  for (int i = 0; i < 5; i++) CHECK(slot_now(i) == world.native[i]);
  CHECK(logged("Pose: finish generation 1:") == 1 && lock_faults == 0 && lock_depth[0] == 0 && lock_depth[1] == 0);

  /* ---- a blended entry: the placement fades in over 150 ms, and the meter follows the real weight ---- */
  pose_world_third_person();
  c.steady_entry = false;
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE);
  CHECK(H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE && logged("Pose: begin generation 2, blended entry") == 1);
  pose_world_first_person();
  tfp_wire_pose_second(&wire, fragment, sizeof fragment);
  double largest = 0;
  for (int i = 0; i <= 10; i++) {
    CHECK(game_frame(handle, &stats, i ? 15 : 0) == TFP_OPS_TRUE && meter_is_applied());
    double u = i * 15 / 150.0, eased = u * u * (3 - 2 * u);
    CHECK(near(stats.blend, eased, 1e-6) && applied_is(.15 * eased, 0, -.12 * eased));
    if (pose.meter.jump_m > largest) largest = pose.meter.jump_m;
  }
  CHECK(stats.blend == 1 && largest > .005 && largest < .02); /* an ease: about a centimetre, not the 19 cm of the placement */
  CHECK(H->finish(H->user, handle, &has_stats, &stats, &why) == TFP_OPS_TRUE);

  /* ---- the profile switch off: the plain getter hook, UnitAPI+0x90 untouched ---- */
  new_wire();
  tfp_wire_pose_bind(&wire, &pose, tfp_wire_pose_linked(), &IO, (TfpPoseAdapterProfile){2, false});
  H = wire.pose_helper;
  pose_world_third_person();
  c.steady_entry = true;
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE);
  CHECK(H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE);
  CHECK(slot_now(3) == hook_value(TFP_POSE_HOOK_GETTER) && slot_now(4) == world.native[4]);
  CHECK(wire.host.helper_input_stats(&wire.host, NULL, tfp_wire_nil(), &first, &second) && pose.input.reason == TFP_POSE_INPUT_DISABLED);
  CHECK(H->finish(H->user, handle, &has_stats, &stats, &why) == TFP_OPS_TRUE);
  for (int i = 0; i < 5; i++) CHECK(slot_now(i) == world.native[i]);
  CHECK(lock_faults == 0 && input_only_calls == 0);

  /* ---- the action hold (not in retail). Off: the first frame of an action ends the placement, as retail ---- */
  TfpPoseHoldProbe probe;
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE);
  pose_world_first_person();
  game_frame(handle, &stats, 300);
  CHECK(applied_is(.15, 0, -.12) && stats.armed == 1);
  CHECK(tfp_pose_hold_probe(&probe) && probe.armed == 1 && probe.hold == 0 && probe.node5_calls == 0); /* nothing is counted while it is off */
  pose_world_action(2);
  game_frame(handle, &stats, 16);
  CHECK(applied_is(0, 0, 0) && tfp_pose_hold_probe(&probe) && probe.armed == 0 && probe.error == TFP_POSE_OWNER_MISMATCH);
  H->finish(H->user, handle, &has_stats, &stats, &why);
  /* On: the same frame keeps the placement, for the same owned record */
  pose_world_action(0);
  pose_world_third_person();
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE);
  pose_world_first_person();
  game_frame(handle, &stats, 300);
  CHECK(applied_is(.15, 0, -.12));
  CHECK(tfp_pose_set_hold(1) == 0 && tfp_pose_hold_probe(&probe) && probe.hold == 1);
  pose_world_action(2);
  game_frame(handle, &stats, 16);
  CHECK(applied_is(.15, 0, -.12) && tfp_pose_hold_probe(&probe) && probe.armed == 1 && probe.error == 0 && probe.node5_calls == 1);
  pose_world_action(0x40); /* any action flags, and the guard byte's neighbours: not named */
  game_frame(handle, &stats, 16);
  CHECK(applied_is(.15, 0, -.12) && tfp_pose_hold_probe(&probe) && probe.armed == 1 && probe.node5_calls == 2);
  pose_world_weapon(0); /* the gun is put away: its unit and the request's weapon field no longer match */
  game_frame(handle, &stats, 16);
  CHECK(applied_is(.15, 0, -.12) && tfp_pose_hold_probe(&probe) && probe.armed == 1 && probe.error == 0);
  CHECK(H->set_placement(H->user, handle, .1, .02, -.1, &why) == TFP_OPS_TRUE); /* a control call's own ownership test tolerates it too */
  game_frame(handle, &stats, 16);
  CHECK(applied_is(.1, .02, -.1) && tfp_pose_hold_probe(&probe) && probe.armed == 1);
  pose_world_weapon(1);
  pose_world_action(0);
  game_frame(handle, &stats, 16);
  CHECK(applied_is(.1, .02, -.1) && stats.armed == 1 && stats.error == 0); /* the action is over: nothing was lost */
  /* what a hold never tolerates: a seated avatar, another record on top */
  pose_world_action(2);
  pose_world_seated(1);
  game_frame(handle, &stats, 16);
  CHECK(applied_is(0, 0, 0) && tfp_pose_hold_probe(&probe) && probe.armed == 0 && probe.error == TFP_POSE_OWNER_MISMATCH);
  H->finish(H->user, handle, &has_stats, &stats, &why);
  pose_world_seated(0);
  pose_world_action(0);
  pose_world_third_person();
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE);
  pose_world_first_person();
  game_frame(handle, &stats, 300);
  CHECK(applied_is(.15, 0, -.12));
  pose_world_action(2);
  pose_world_third_person(); /* the game ended its first-person request: the top record is another one */
  pose_world_action(2);
  now_ms += 16;
  tfp_pose_position_from(world.rig_return, world.viewmodel, 5, (const float *)(uintptr_t)(world.request + 0x48), world.stack);
  CHECK(tfp_pose_hold_probe(&probe) && probe.armed == 0 && probe.error == TFP_POSE_OWNER_MISMATCH);
  H->finish(H->user, handle, &has_stats, &stats, &why);
  /* The record's identity, one condition at a time while a lease is armed in hold mode and an action runs. In each
   * scene exactly one thing differs from the owned record of the scenes above:
   *   0: another kind-6 request of the same avatar, weapon and body is on top (only "top is the lease's record"),
   *   1: the owned record's kind word says 4, in place,
   *   2: the owned record's body field is another body. */
  for (int scene_index = 0; scene_index < 3; scene_index++) {
    pose_world_action(0);
    pose_world_third_person();
    CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE);
    pose_world_first_person();
    game_frame(handle, &stats, 300);
    pose_world_action(2);
    game_frame(handle, &stats, 16);
    CHECK(applied_is(.15, 0, -.12) && tfp_pose_hold_probe(&probe) && probe.hold == 1 && probe.armed == 1 && probe.error == 0);
    if (scene_index == 0) pose_world_second_request(1);
    else if (scene_index == 1) pose_world_request_field(0, 4);
    else pose_world_request_field(12, world.body + 1);
    game_frame(handle, &stats, 16);
    CHECK(applied_is(0, 0, 0) && tfp_pose_hold_probe(&probe) && probe.armed == 0 && probe.error == TFP_POSE_OWNER_MISMATCH);
    H->finish(H->user, handle, &has_stats, &stats, &why);
    if (scene_index == 0) pose_world_second_request(0);
  }
  /* Hold mode is never the test of an entry: with it on, an entry during an action, or with the gun's unit gone, is
   * refused exactly as with it off. */
  for (int scene_index = 0; scene_index < 2; scene_index++) {
    pose_world_action(0);
    pose_world_third_person();
    CHECK(tfp_pose_hold_probe(&probe) && probe.hold == 1);
    CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE);
    if (scene_index == 0) pose_world_action(2);
    else pose_world_weapon(0);
    handle = NULL;
    CHECK(H->begin(H->user, &c, native, &handle, &why) != TFP_OPS_TRUE && !handle);
    CHECK(tfp_pose_hold_probe(&probe) && probe.armed == 0 && probe.error == TFP_POSE_OWNER_MISMATCH);
    for (int i = 0; i < 5; i++) CHECK(slot_now(i) == world.native[i]);
    if (scene_index == 1) pose_world_weapon(1);
  }
  /* and switched off again it is strict again */
  pose_world_action(0);
  pose_world_third_person();
  CHECK(tfp_pose_set_hold(0) == 1);
  CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE);
  pose_world_first_person();
  game_frame(handle, &stats, 300);
  pose_world_action(2);
  game_frame(handle, &stats, 16);
  CHECK(applied_is(0, 0, 0) && tfp_pose_hold_probe(&probe) && probe.armed == 0 && probe.hold == 0);
  H->finish(H->user, handle, &has_stats, &stats, &why);
  pose_world_action(0);
  for (int i = 0; i < 5; i++) CHECK(slot_now(i) == world.native[i]);
  CHECK(lock_faults == 0 && lock_depth[0] == 0 && lock_depth[1] == 0);
  return 0;
}

int main(void) {
  if (scripted()) return 1;
  unsigned first = checks;
  if (real()) return 1;
  printf("Wire pose: %u checks passed (%u against a scripted helper, %u against the real pose adapter and helper on this build's layout)\n",
         checks, first, checks - first);
  return 0;
}
#endif
