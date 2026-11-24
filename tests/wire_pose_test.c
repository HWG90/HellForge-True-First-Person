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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "diff_pose_helper_world.h"
using namespace dp;
extern "C" {
struct PoseWorld {
  uint64_t game, exe, context, request, avatar_owner, weapon_descriptor, stack, api, slot[5], native[5];
  uint64_t rig_return, input_return, getter_return, carrier_return, api_begin, api_end;
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
/* Full aim: the controller's aim byte (byte 4), as the game sets it in the sights. */
void pose_world_aim(int on) { scene.controller[4] = on ? 1 : 0; sync(); }
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
/* The optic, for the zoom depth (the differential test's optic()): the middle zoom level selected and the lens
 * settled on it, the gate clear. The depth term then has something to add. */
static float optic_target;
void pose_world_optic(void) {
  const unsigned level = 1; const float zero = 0; const uint8_t clear = 0;
  scene.depth.packed = 64 | (level << 4);
  optic_target = (scene.depth.degrees / scene.depth.zoom[level]) * .01745329238474369f;
  const float distance = .03391552f / (std::tan(optic_target * .5f) * scene.depth.coefficient[level]);
  std::memcpy(scene.request + 0x90, &distance, 4); std::memcpy(scene.request + 0x9C, &zero, 4); std::memcpy(scene.request + 0xA0, &optic_target, 4);
  std::memcpy(scene.request + 0x8D, &clear, 1); std::memcpy(scene.request + 0x8F, &clear, 1);
  std::memset(scene.frame, 0, sizeof scene.frame);
  std::memcpy(scene.frame + 0x18, &optic_target, 4); std::memcpy(scene.frame + 0x1C, &optic_target, 4); std::memcpy(scene.request + 0x84, &optic_target, 4);
  sync();
}
/* The game's unzoom gate for the next frame: the avatar's packed bit (the evaluator's local at stack +0x44) and
 * request+0x8f. Either set: the lens target is the wide 90 degrees and the lens on its way (67.6); both clear: the
 * optic's own target, the lens on it. */
void pose_world_gate(int avatar_bit, int ability) {
  const float wide = 1.5707964f, on_the_way = 1.18052f; const bool gated = avatar_bit || ability;
  scene.frame[0] = avatar_bit ? 1 : 0; scene.request[0x8F] = ability ? 1 : 0;
  std::memcpy(scene.frame + 0x18, gated ? &wide : &optic_target, 4); std::memcpy(scene.frame + 0x1C, gated ? &on_the_way : &optic_target, 4);
  std::memcpy(scene.request + 0x84, gated ? &on_the_way : &optic_target, 4);
  sync();
}
/* The evaluator's lens target and lens for the next frame, as they stand (after pose_world_gate). */
void pose_world_lens(float target, float lens) {
  std::memcpy(scene.frame + 0x18, &target, 4); std::memcpy(scene.frame + 0x1C, &lens, 4); std::memcpy(scene.request + 0x84, &lens, 4);
  sync();
}
/* The first of the nine code pins of the zoom depth (the evaluator's frame and the getters it reads): as this build
 * has it (1), or one byte of it another (0). */
void pose_world_depth_pin(int intact) {
  static uint8_t kept; static bool have;
  uint8_t *at = reinterpret_cast<uint8_t *>(W.a[A_GAME] + HERE.depth_pin[0]);
  if (!have) { kept = *at; have = true; }
  *at = intact ? kept : static_cast<uint8_t>(kept ^ 0xff);
}
/* The avatar's main row (avatar owner + 0x53d8b0 + index * 0x1238): byte +5, byte +0x3d, the float at +0x11ac. */
void pose_world_main_row(unsigned index, int five, int three_d, float scalar) {
  const uint64_t row = W.a[A_OWNER] + 0x53d8b0 + static_cast<uint64_t>(index) * 0x1238;
  World::put<uint8_t>(row + 5, five ? 1 : 0); World::put<uint8_t>(row + 0x3d, three_d ? 1 : 0); World::put(row + 0x11ac, scalar);
}
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
  out->getter_return = W.a[A_GAME] + HERE.observe_call + 3; out->carrier_return = W.a[A_GAME] + HERE.carrier_call + 4;
  out->api_begin = W.a[A_API]; out->api_end = W.a[A_API] + 0x1000;
  out->avatar = AVATAR; out->weapon = WEAPON; out->body = BODY; out->viewmodel = VIEWMODEL; out->weapon_unit = WEAPON_UNIT;
}
}
#else
#define _GNU_SOURCE
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include "wire_pose.h"
#include "wire_off.h"
#include "near_helper.h"
#include "pose_helper.h"

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
  uint64_t rig_return, input_return, getter_return, carrier_return, api_begin, api_end;
  uint32_t avatar, weapon, body, viewmodel, weapon_unit;
} PoseWorld;
void pose_world_build(PoseWorld *out);
void pose_world_third_person(void);
void pose_world_optic(void);
void pose_world_gate(int avatar_bit, int ability);
void pose_world_main_row(unsigned index, int five, int three_d, float scalar);
void pose_world_lens(float target, float lens);
void pose_world_depth_pin(int intact);
void pose_world_first_person(void);
void pose_world_action(unsigned flags);
void pose_world_weapon(int present);
void pose_world_seated(int seated);
void pose_world_aim(int on);
void pose_world_second_request(int pushed);
void pose_world_request_field(unsigned at, uint32_t value);
static PoseWorld world;
static int lock_depth[2], lock_faults;
static unsigned forwarded_calls, getter_calls, cas_calls, input_only_calls;
static float forwarded[4], root_forwarded[4];
static unsigned root_calls;
static float getter_answer[4] = {.125f, -.25f, .5f, 0};
static bool w_read(void *u, uint64_t at, void *out, size_t length) {
  struct iovec local = {out, length}, remote = {(void *)(uintptr_t)at, length};
  (void)u;
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == (ssize_t)length;
}
static uint64_t w_tick(void *u) { (void)u; return now_ms; }
static struct { unsigned calls, gated; float target, lens; } lens_notes; /* round 20: what the pose helper hands the field-of-view helper */
static void w_note_lens(void *u, bool gated, float target, float lens) { (void)u; ++lens_notes.calls; lens_notes.gated += gated; lens_notes.target = target; lens_notes.lens = lens; }
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
  (void)u; (void)unit;
  if (original != world.native[which]) lock_faults++;
  if (which == 0 && node == 0) { root_calls++; memcpy(root_forwarded, pose_value, 16); } /* the rig's root: only the held-depth scene writes it */
  else if (which == 0) { forwarded_calls++; memcpy(forwarded, pose_value, 16); }
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
/* The same with the look evaluator's read of the rig eye before it (the position getter, from its pinned call site):
 * that is where the helper reads the evaluator's lens locals, without which the zoom depth is passed. */
static void depth_frame(uint64_t step_ms) {
  now_ms += step_ms;
  tfp_pose_position_getter_from(world.getter_return, world.viewmodel, 50, world.stack, world.request);
  tfp_pose_position_from(world.rig_return, world.viewmodel, 5, (const float *)(uintptr_t)(world.request + 0x48), world.stack);
}
/* The same, and then the evaluator's write of the rig's root (from its pinned call site, its own stack pose). */
static void root_frame(uint64_t step_ms) {
  depth_frame(step_ms);
  tfp_pose_position_from(world.carrier_return, world.viewmodel, 0, (const float *)(uintptr_t)(world.stack + 0x240), world.stack);
}
static bool applied_is(double x, double y, double z) {
  return near(forwarded[0] - NATIVE[0], x, 2e-6) && near(forwarded[1] - NATIVE[1], y, 2e-6) && near(forwarded[2] - NATIVE[2], z, 2e-6);
}
/* Full aim, the hip placement eased out on the transition's own (exponential) step: x and z near 0, the forward as given. */
static bool aimed_is(double y) {
  return near(forwarded[0] - NATIVE[0], 0, 1e-4) && near(forwarded[1] - NATIVE[1], y, 1e-4) && near(forwarded[2] - NATIVE[2], 0, 1e-4);
}
static bool meter_is_applied(void) { /* the meter's commanded shift is what the helper really added to the game's pose */
  return applied_is(pose.meter.shift[0], pose.meter.shift[1], pose.meter.shift[2]);
}

/* ---- round 20, the review's B1 (2026-11-24): the gun and the near clip, frame by frame ----
 * The pose helper moves the gun toward the eye in one frame; the near clip has to be there in the same frame, or it
 * stands behind the sight's rear and cuts into the sight. Here both real helpers, each on its own fake game memory
 * (tests/near_world_shim.cpp for the near helper), are stepped through aims at 60, 116 and 144 fps, with the game
 * calling the near setter before the gun's node in a frame and after it. The game's sight (its near clip) is S; the
 * sight's rear is drawn at S less what the gun was moved toward the eye in that frame (the hip placement's forward is 0
 * here). In no frame may the near clip handed to the game be farther than that. */
int near_world_begin(float hip, uint64_t now_us);
float near_world_frame(float native, int full_aim, uint64_t now_us);
void near_world_end(void);
static float ads_sight_cut(float sight, float near_clip) { return near_clip - (sight - (NATIVE[1] - forwarded[1])); }
/* One frame: the game's near setter before (0) or after (1) the rig's node 5; returns the near clip handed on. */
static float ads_frame(TfpOpsHandle *handle, TfpOpsHelperStats *stats, uint64_t step, int order, float sight, int aim) {
  float near_clip = 0;
  if (order == 0) near_clip = near_world_frame(sight, aim, (now_ms + step) * 1000);
  game_frame(handle, stats, step);
  if (order == 1) near_clip = near_world_frame(sight, aim, now_ms * 1000);
  return near_clip;
}
static int ads_frames(const TfpOpsPoseHelper *H, const TfpOpsHelperConfig *c) {
  static const uint64_t steps[3] = {16, 9, 7};
  static const float sights[3] = {.173f, .22f, .35f};
  TfpOpsText why;
  TfpOpsHandle *handle = NULL;
  TfpOpsHelperStats stats;
  void *native = NULL;
  bool has_stats = false;
  tfp_near_set_ads_source(tfp_pose_ads_near); /* as src/wire_win.c binds it */
  for (int s = 0; s < 3; s++) for (int f = 0; f < 3; f++) for (int order = 0; order < 2; order++) {
    const float sight = sights[s];
    pose_world_third_person();
    CHECK(H->prepare(H->user, c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, c, native, &handle, &why) == TFP_OPS_TRUE);
    pose_world_first_person();
    game_frame(handle, &stats, 300);
    CHECK(near_world_begin(.05f, now_ms * 1000));
    tfp_pose_set_ads_seat(sight, .10f);
    float near_clip = 0, worst = 0, worst_out = 0;
    for (int i = 0; i < 10; i++) near_clip = ads_frame(handle, &stats, steps[f], order, sight, 0);
    CHECK(near(near_clip, .05, 1e-6) && near(forwarded[1] - NATIVE[1], 0, 1e-6)); /* at the hip: the hip clearance, nothing pulled */
    pose_world_aim(1);
    for (int i = 0; i < (int)(600 / steps[f]); i++) {
      near_clip = ads_frame(handle, &stats, steps[f], order, sight, 1);
      const float cut = ads_sight_cut(sight, near_clip);
      if (cut > worst) worst = cut;
    }
    /* settled: the sight 10 cm from the eye (at most 0.25 m pulled) and the near clip at its rear */
    const float pulled = sight - .10f > .25f ? .25f : sight - .10f;
    CHECK(near(NATIVE[1] - forwarded[1], pulled, 1e-4) && near(near_clip, sight - pulled, 2e-3));
    pose_world_aim(0);
    for (int i = 0; i < (int)(600 / steps[f]); i++) {
      near_clip = ads_frame(handle, &stats, steps[f], order, sight, 0);
      const float cut = ads_sight_cut(sight, near_clip);
      if (cut > worst_out) worst_out = cut;
    }
    if (worst > 1e-4f || worst_out > 1e-4f)
      printf("wire pose: sight %.3f m, %llu ms frames, near setter %s the gun: the near clip cut %.2f mm into the sight aiming in, %.2f mm aiming out\n",
             sight, (unsigned long long)steps[f], order ? "after" : "before", worst * 1000, worst_out * 1000);
    near_world_end();
    H->finish(H->user, handle, &has_stats, &stats, &why);
    if (!getenv("TFP_ADS_ALL")) CHECK(worst <= 1e-4f && worst_out <= 1e-4f);
  }
  tfp_near_set_ads_source(NULL);
  pose_world_third_person();
  return 0;
}

/* ---- round 20, the review's B2 (2026-11-24): a zoom level changes in full aim ----
 * The game's sight (its near clip) jumps to the new zoom level's at once; the pull's target with it, a tick later; the
 * game moves its own seat, the depth D (request+0x90 blended over the lens, src/depth_steady.h), at a pace of its own
 * (here 0 to 250 ms). The sight is where the game's seat has it: S_from + (D - D_from), less what the gun is drawn in.
 * In no frame may the sight come nearer than the wanted distance (10 cm, and 15, the default), nor the near clip stand beyond the
 * sight's rear. Zoom levels: 0.35 m with D 0.40 m (lens 0.40 rad) and 0.22 m with D 0.27 m (lens 0.20 rad). */
static void put_f32(unsigned at, float v) { uint32_t u; memcpy(&u, &v, 4); pose_world_request_field(at, u); }
static void ads_seat_world(float rest, float before, float prior, float target, float lens) {
  put_f32(0x90, rest); put_f32(0x9C, before); put_f32(0xA0, prior);
  pose_world_lens(target, lens);
}
static int ads_zoom_frames(const TfpOpsPoseHelper *H, const TfpOpsHelperConfig *c) {
  static const uint64_t steps[3] = {16, 9, 7};
  static const double seats[4] = {0, 80, 150, 250};
  static const float S[2] = {.35f, .22f}, D[2] = {.40f, .27f}, LENS[2] = {.40f, .20f};
  TfpOpsText why;
  TfpOpsHandle *handle = NULL;
  TfpOpsHelperStats stats;
  void *native = NULL;
  bool has_stats = false;
  tfp_near_set_ads_source(tfp_pose_ads_near);
  CHECK(tfp_pose_set_depth_steady(2) == 1 || tfp_pose_set_depth_steady(2) == 0); /* the game's depth worked out, nothing added (as depth_steady_off.txt) */
  static const float WANTED[2] = {.10f, .15f};
  for (int w = 0; w < 2; w++) for (int from = 0; from < 2; from++) for (int z = 0; z < 4; z++) for (int f = 0; f < 3; f++) for (int order = 0; order < 2; order++) {
    const int to = 1 - from;
    const float wanted_m = WANTED[w];
    pose_world_third_person();
    CHECK(H->prepare(H->user, c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, c, native, &handle, &why) == TFP_OPS_TRUE);
    pose_world_first_person();
    pose_world_optic();
    ads_seat_world(D[from], 0, LENS[from], LENS[from], LENS[from]);
    game_frame(handle, &stats, 300);
    CHECK(near_world_begin(.05f, now_ms * 1000));
    tfp_pose_set_ads_seat(S[from], wanted_m);
    pose_world_aim(1);
    float near_clip = 0;
    for (int i = 0; i < (int)(800 / steps[f]); i++) {
      if (order == 0) near_clip = near_world_frame(S[from], 1, (now_ms + steps[f]) * 1000);
      depth_frame(steps[f]);
      if (order == 1) near_clip = near_world_frame(S[from], 1, now_ms * 1000);
    }
    const float settled = NATIVE[1] - forwarded[1];
    CHECK(near(settled, S[from] - wanted_m, 1e-4) && near(near_clip, wanted_m, 2e-3));    /* settled: at the wanted distance */
    /* the zoom level changes: the game's sight at once, the tick's a frame later, the lens over seats[z] ms */
    float nearest = 1, cut = 0, back = 0, before = NATIVE[1] - forwarded[1];
    /* round 20c (review 27, B1): the largest step of the sight in a frame, against the game's own (feel.51: nothing
     * wanted, the sight goes with the depth). Toward the nearer zoom level the pull's limit used to take the new near
     * clip at once and the gun jumped 8 to 13 cm out in one frame. In both directions now. */
    float step_most = 0, game_step_most = 0, last_sight = S[from] - settled, last_depth = D[from];
    const uint64_t begun = now_ms;
    for (int i = 0; i < (int)(800 / steps[f]); i++) {
      const double t = (double)(now_ms + steps[f] - begun);
      const double u = seats[z] > 0 ? (t / seats[z] > 1 ? 1 : t / seats[z]) : 1, eased = u * u * (3 - 2 * u);
      const float lens = (float)(LENS[from] + (LENS[to] - LENS[from]) * eased);
      ads_seat_world(D[to], D[from], LENS[from], LENS[to], lens);
      if (i == 1) tfp_pose_set_ads_seat(S[to], wanted_m);
      if (order == 0) near_clip = near_world_frame(S[to], 1, (now_ms + steps[f]) * 1000);
      depth_frame(steps[f]);
      if (order == 1) near_clip = near_world_frame(S[to], 1, now_ms * 1000);
      /* the game's own depth this frame, as it works it out (src/depth_steady.h) */
      const float span = fabsf(LENS[to] - LENS[from]), tt = fabsf(LENS[to] - lens) / span, depth = (1 - (1 - tt)) * D[from] + (1 - tt) * D[to];
      const float sight = S[from] + (depth - D[from]) - (NATIVE[1] - forwarded[1]);
      if (sight < nearest) nearest = sight;
      if (near_clip - sight > cut) cut = near_clip - sight;
      if (fabsf(sight - last_sight) > step_most) step_most = fabsf(sight - last_sight);
      if (fabsf(depth - last_depth) > game_step_most) game_step_most = fabsf(depth - last_depth);
      last_sight = sight; last_depth = depth;
      /* to a farther zoom level the gun only comes nearer: it never jumps back out for a frame */
      if (S[to] > S[from] && before - (NATIVE[1] - forwarded[1]) > back) back = before - (NATIVE[1] - forwarded[1]);
      before = NATIVE[1] - forwarded[1];
    }
    const float pulled = S[to] - wanted_m > .25f ? .25f : S[to] - wanted_m;
    CHECK(near(NATIVE[1] - forwarded[1], pulled, 1e-4) && near(near_clip, S[to] - pulled, 2e-3));    /* settled at the new zoom level */
    if (nearest < wanted_m - 1e-4f || cut > 1e-4f || back > 1e-4f || step_most > game_step_most + 1e-3f)
      printf("wire pose: %.0f cm wanted, %.2f -> %.2f m, the game's seat over %.0f ms, %llu ms frames, near setter %s the gun: the sight came to %.1f cm, the near clip cut %.2f mm into it, the gun went back out %.1f cm, "
             "the sight's largest step %.2f cm (the game's own %.2f cm)\n",
             wanted_m * 100, S[from], S[to], seats[z], (unsigned long long)steps[f], order ? "after" : "before", nearest * 100, cut * 1000, back * 100,
             step_most * 100, game_step_most * 100);
    near_world_end();
    pose_world_aim(0);
    H->finish(H->user, handle, &has_stats, &stats, &why);
    if (!getenv("TFP_ADS_ALL")) CHECK(nearest >= wanted_m - 1e-4f && cut <= 1e-4f && back <= 1e-4f && step_most <= game_step_most + 1e-3f);
  }
  /* round 20c (review 27, B1): the game changes its near clip to the other zoom level's and its depth does not move
   * at all (nothing has shown that it does). The gun is not moved: the pull stays where it was (it was 13 cm out and
   * stayed there), the sight where it was, and the near clip still short of the sight's rear. */
  for (int from = 0; from < 2; from++) for (int f = 0; f < 3; f++) {
    const float wanted_m = .15f;
    pose_world_third_person();
    CHECK(H->prepare(H->user, c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, c, native, &handle, &why) == TFP_OPS_TRUE);
    pose_world_first_person();
    pose_world_optic();
    ads_seat_world(D[from], 0, LENS[from], LENS[from], LENS[from]);
    game_frame(handle, &stats, 300);
    CHECK(near_world_begin(.05f, now_ms * 1000));
    tfp_pose_set_ads_seat(S[from], wanted_m);
    pose_world_aim(1);
    float near_clip = 0;
    for (int i = 0; i < (int)(800 / steps[f]); i++) { depth_frame(steps[f]); near_clip = near_world_frame(S[from], 1, now_ms * 1000); }
    const float settled = NATIVE[1] - forwarded[1];
    CHECK(near(settled, S[from] - wanted_m, 1e-4));
    float step_most = 0, cut = 0, last = settled;
    tfp_pose_set_ads_seat(S[1 - from], wanted_m);
    for (int i = 0; i < (int)(800 / steps[f]); i++) {
      depth_frame(steps[f]); near_clip = near_world_frame(S[1 - from], 1, now_ms * 1000);
      const float pulled = NATIVE[1] - forwarded[1], sight = S[from] - pulled;
      if (fabsf(pulled - last) > step_most) step_most = fabsf(pulled - last);
      if (near_clip - sight > cut) cut = near_clip - sight;
      last = pulled;
    }
    if (step_most > 1e-4f || cut > 1e-4f || !near(last, settled, 1e-4))
      printf("wire pose: %.2f -> %.2f m with the game's depth standing still, %llu ms frames: the gun's largest step %.2f cm, the near clip cut %.2f mm, the pull %.3f m (was %.3f)\n",
             S[from], S[1 - from], (unsigned long long)steps[f], step_most * 100, cut * 1000, last, settled);
    /* either way the gun stays where it was: toward the nearer the pull is not cut (the depth did not come in), toward
     * the farther it is not grown (the aim's pair says the sight did not go out) */
    CHECK(cut <= 1e-4f && step_most <= 1e-4f && near(last, settled, 1e-4));
    near_world_end();
    pose_world_aim(0);
    H->finish(H->user, handle, &has_stats, &stats, &why);
  }
  /* another weapon in the next aim: the sight and depth paired in the last aim (a Nova-like scope: sight 0.066 m, D
   * 0.348 m) say nothing of it (the Scythe: 0.173 m, D 0.21 m); paired anew each aim, the pull is the whole 7.3 cm from
   * the first frame of the aim, not held to nothing until the new weapon has stood still */
  {
    TfpPoseAdsProbe ads;
    pose_world_third_person();
    CHECK(H->prepare(H->user, c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, c, native, &handle, &why) == TFP_OPS_TRUE);
    pose_world_first_person();
    pose_world_optic();
    ads_seat_world(.348f, 0, .2f, .2f, .2f);
    game_frame(handle, &stats, 300);
    tfp_pose_set_ads_seat(.066f, .10f);
    pose_world_aim(1);
    for (int i = 0; i < 40; i++) depth_frame(16);
    pose_world_aim(0);
    for (int i = 0; i < 40; i++) depth_frame(16);
    ads_seat_world(.21f, .30f, .6f, .5f, .55f);                      /* the lens on its way: D (0.255 m) not at its rest */
    tfp_pose_set_ads_seat(.173f, .10f);
    for (int i = 0; i < 40; i++) depth_frame(16);                   /* the pull's target glided there at the hip */
    pose_world_aim(1);
    for (int i = 0; i < 6; i++) { depth_frame(16); CHECK(tfp_pose_ads_probe(&ads) && near(ads.pull, .073, 1e-4)); }
    pose_world_aim(0);
    H->finish(H->user, handle, &has_stats, &stats, &why);
  }
  tfp_pose_set_depth_steady(0);
  tfp_near_set_ads_source(NULL);
  pose_world_third_person();
  return 0;
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
  host.forward_setter = w_forward_setter, host.forward_getter = w_forward_getter, host.note_lens = w_note_lens;
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

  /* ---- the "Gate:" line's counts and the depth switch (src/gate_log.h; neither is retail's) ---- */
  {
    TfpGateSecond g;
    char line[640];
    CHECK(tfp_pose_gate_take(&g) == 1 && g.frames > 0 && g.lens_frames <= 1 && g.applied == 0); /* the frames so far: the lens was read for the last only, with no optic set */
    CHECK(tfp_pose_gate_take(&g) == 1 && g.frames == 0 && !tfp_gate_worth(&g));
    pose_world_optic();
    for (int i = 0; i < 4; i++) depth_frame(16);
    const float depth = forwarded[1] - NATIVE[1] - .02f; /* what the zoom depth adds along the rig's forward axis on top of the placement */
    CHECK(fabsf(depth) > .01f && fabsf(depth) < 1 && near(forwarded[0] - NATIVE[0], .11, 2e-6) && near(forwarded[2] - NATIVE[2], -.12, 2e-6));
    CHECK(tfp_pose_gate_take(&g) == 1 && g.generation == 1 && g.frames == 4 && g.lens_frames == 4 && g.applied == 4 && g.nothing == 0 && tfp_gate_passed(&g) == 0);
    CHECK(near(g.correction_min, depth, 2e-6) && near(g.correction_max, depth, 2e-6) && g.avatar_set == 0 && g.request_set == 0 && !tfp_gate_worth(&g));
    CHECK(g.row_asked == 0 && g.row_frames == 0 && g.depth_off == 0 && near(g.target_min, g.target_max, 0) && near(g.lens_min * 57.29578, 36, .01));
    /* the avatar bit set every other frame: the depth term goes on and off with it, as it did before this was counted */
    for (int i = 0; i < 6; i++) {
      pose_world_gate(i % 2 == 0, 0);
      depth_frame(16);
      CHECK(i % 2 == 0 ? applied_is(.11, .02, -.12) : applied_is(.11, .02 + (double)depth, -.12));
    }
    CHECK(tfp_pose_gate_take(&g) == 1 && g.frames == 6 && g.avatar_set == 3 && g.avatar_changes == 6 && g.request_set == 0 && g.request_changes == 0);
    CHECK(g.applied == 3 && g.passed[TFP_GATE_GATED] == 3 && tfp_gate_passed(&g) == 3 && tfp_gate_worth(&g));
    CHECK(near(g.target_min * 57.29578, 36, .01) && near(g.target_max * 57.29578, 90, .01) && near(g.lens_max * 57.29578, 67.64, .01));
    /* request+0x8f the same; the main row read from now on (the binder asks for it), its byte +5 following a frame behind */
    CHECK(tfp_pose_set_gate_reads(1) == 1);
    for (int i = 0; i < 6; i++) {
      pose_world_gate(0, i % 2);
      pose_world_main_row(0, i % 2 == 0, 1, .25f + .125f * (float)(i % 2));
      depth_frame(16);
    }
    CHECK(tfp_pose_gate_take(&g) == 1 && g.frames == 6 && g.request_set == 3 && g.request_changes == 5 && g.avatar_set == 0 && g.avatar_changes == 0);
    CHECK(g.row_asked == 6 && g.row_frames == 6 && g.row5_set == 3 && g.row5_changes == 5 && g.row3d_set == 6 && g.row11ac_min == .25f && g.row11ac_max == .375f);
    CHECK(g.applied == 3 && g.passed[TFP_GATE_GATED] == 3 && g.request_8d == 0);
    tfp_gate_line(&g, false, line, sizeof line);
    CHECK(strstr(line, "; request+0x8f set in 3, changed 5 times; ") && strstr(line, "main row read in 6: +5 set in 3, changed 5 times, +0x3d set in 6, +0x11ac 0.2500 to 0.3750"));
    CHECK(tfp_pose_set_gate_reads(0) == 1);
    /* depth_off.txt: nothing is added on any frame, gated or clear, and the line says why */
    pose_world_gate(0, 0);
    CHECK(tfp_pose_set_depth_off(1) == 1);
    for (int i = 0; i < 4; i++) {
      pose_world_gate(i == 3, 0);
      depth_frame(16);
      CHECK(applied_is(.11, .02, -.12));
    }
    CHECK(tfp_pose_gate_take(&g) == 1 && g.frames == 4 && g.applied == 0 && g.nothing == 0 && g.passed[TFP_GATE_SWITCHED_OFF] == 3 && g.passed[TFP_GATE_GATED] == 1);
    CHECK(g.depth_off == 1 && g.avatar_changes == 1 && g.row_asked == 0 && tfp_gate_worth(&g));
    tfp_gate_line(&g, true, line, sizeof line);
    CHECK(strstr(line, "passed in 4 (unzoom gate or request state 1, depth_off.txt 3)")
          && strstr(line, "; main row not read; depth_off.txt: the mod's zoom depth is forced to nothing; the first second of a native entry"));
    CHECK(tfp_pose_set_depth_off(0) == 1);
    pose_world_gate(0, 0);
    depth_frame(16);
    CHECK(applied_is(.11, .02 + (double)depth, -.12)); /* and back */
    CHECK(tfp_pose_gate_take(&g) == 1 && g.frames == 1 && g.applied == 1 && g.depth_off == 0 && g.avatar_changes == 1);

    /* ---- the depth held steady (src/depth_steady.h; not retail's): the gate flips, the rig and its root do not ---- */
    TfpSteadySecond held;
    float current = 0;
    memcpy(&current, (void *)(uintptr_t)(world.request + 0x90), 4);
    const float gated_depth = (1 - (1.5707964f - 1.18052f) / (1.5707964f - 36 * .01745329238474369f)) * current; /* the game's own blend on a gated frame of this world: the distance before is 0 */
    CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 0);                     /* not reached until it is switched on */
    CHECK(tfp_pose_set_depth_steady(3) == 0 && tfp_pose_set_depth_steady(1) == 1);
    root_frame(16);
    CHECK(applied_is(.11, .02, -.12) && root_calls == 1 && root_forwarded[1] == 0);    /* begun at the game's own depth: nothing added in the first frame */
    for (int i = 0; i < 60; i++) root_frame(16);                                      /* and eased to the mod's reference for the optic, where the zoom depth term had it */
    CHECK(applied_is(.11, .02 + (double)depth, -.12) && near(root_forwarded[1], -(double)depth, 2e-6) && root_forwarded[0] == 0 && root_forwarded[2] == 0);
    CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 61 && held.worked == 61 && held.roots == held.shifted && held.shifted >= 59 && tfp_steady_passed(&held) == 0);
    CHECK(tfp_pose_gate_take(&g) == 1 && g.applied == 0 && g.passed[TFP_GATE_STEADY] == 61);
    memset(&lens_notes, 0, sizeof lens_notes);
    for (int i = 0; i < 6; i++) {
      pose_world_gate(i % 2 == 0, 0);
      root_frame(16);
      /* round 20: each of these frames' lens goes to the field-of-view helper, the gate as the evaluator took it */
      CHECK(lens_notes.calls == (unsigned)i + 1 && lens_notes.gated == (unsigned)i / 2 + 1 && near(lens_notes.target * 57.29578, i % 2 == 0 ? 90 : 36, .01) && lens_notes.lens > 0);
      const double added = i % 2 == 0 ? (double)current + (double)depth - (double)gated_depth : (double)depth;
      CHECK(applied_is(.11, .02 + added, -.12) && near(root_forwarded[1], -added, 2e-6));
      /* what the player sees of it: the game's depth plus the shift is the same distance on every frame */
      CHECK(near((i % 2 == 0 ? (double)gated_depth : (double)current) + added, (double)current + (double)depth, 2e-6));
    }
    CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 6 && held.gated.count == 3 && held.clear.count == 3 && held.shifted == 6 && held.roots == 6);
    CHECK(held.aim_frames == 0); /* round 20: the hip, not full aim: nothing for the "Aim:" line */
    CHECK(near(held.gated.least, gated_depth, 1e-6) && near(held.clear.most, current, 0) && held.held.least == held.held.most && near(held.held.most, current + depth, 1e-6));
    CHECK(tfp_steady_worth(&held) && held.off == 0 && held.lease == 1 && near(held.argument_gated.least, NATIVE[1], 0));
    CHECK(tfp_pose_gate_take(&g) == 1 && g.avatar_changes == 6 && g.applied == 0 && g.passed[TFP_GATE_STEADY] == 3 && g.passed[TFP_GATE_GATED] == 3);
    tfp_gate_line(&g, false, line, sizeof line);
    CHECK(strstr(line, "passed in 6 (unzoom gate or request state 3, the depth held steady instead 3)"));
    /* request+0x8f with the reload blend settled: the game leaves the depth out of the rig's vector, and so does the hold */
    pose_world_gate(0, 1);
    pose_world_request_field(0x8C, 0x01000100); /* +0x8d set, +0x8f set */
    pose_world_request_field(0x88, 0x3f800000); /* the weight at 1 */
    memset(&lens_notes, 0, sizeof lens_notes);
    root_frame(16);
    CHECK(applied_is(.11, .02, -.12));
    CHECK(lens_notes.calls == 1 && lens_notes.gated == 1); /* round 20: request+0x8f alone is the gate too, as the evaluator takes it */
    pose_world_request_field(0x8C, 0);
    pose_world_gate(0, 0);
    root_frame(16);
    CHECK(applied_is(.11, .02 + (double)depth, -.12));
    /* a frame without the evaluator's lens: passed, nothing added, and the zoom depth term's own answer (nothing) stands */
    game_frame(handle, &stats, 16);
    CHECK(applied_is(.11, .02, -.12));
    CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 3 && held.passed[TFP_STEADY_NO_LENS] == 1);
    /* round 20, review 24 IMPORTANT 3: full aim, the hold at full strength there. A frame the game scales (the
     * reload blend running, the gate bit set) is passed by the hold; in full aim the held distance is kept through
     * it, node 5 and the root together, instead of the eye going back to the game's own depth for the frame.
     * Round 20c: only with zoom_steady.txt (tfp_pose_set_aim_steady). Without it, as feel.51: nothing is held in full
     * aim, the scaled frame is the game's own, and the depth in full aim is still counted for the "Aim:" line. */
    for (int steady_aim = 1; steady_aim >= 0; --steady_aim) {
      tfp_pose_set_aim_steady(steady_aim);
      pose_world_aim(1);
      for (int i = 0; i < 60; i++) root_frame(16);
      CHECK(tfp_pose_steady_take(&held) == 1 && held.aim_frames > 50 && held.worked == 60 && tfp_steady_passed(&held) == 0);
      const float before = forwarded[1], root_before = root_forwarded[1];
      pose_world_gate(1, 0);
      pose_world_request_field(0x8C, 0x00000100); /* +0x8d set */
      pose_world_request_field(0x88, 0x3f000000); /* the weight at .5: the blend runs, and with the bit set the game scales the rig */
      root_frame(16);
      const double kept = steady_aim ? .5 * ((double)current - (double)gated_depth) : 0; /* the blend's share of it, unscaled; off: nothing */
      CHECK(near(forwarded[1] - before, kept, 2e-6) && near(root_forwarded[1] - root_before, -kept, 2e-6));
      CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 1 && held.passed[TFP_STEADY_SCALED] == 1 && held.shifted == (unsigned)steady_aim && held.roots == (unsigned)steady_aim);
      pose_world_request_field(0x8C, 0); pose_world_request_field(0x88, 0x3f800000);
      pose_world_gate(0, 0);
      pose_world_aim(0);
      for (int i = 0; i < 60; i++) root_frame(16);
      CHECK(applied_is(.11, .02 + (double)depth, -.12));
      tfp_pose_steady_take(&held);
    }
    tfp_pose_set_aim_steady(0);
    /* round 20c (review 27, I1): with zoom_steady.txt the gun is drawn at the held depth in full aim, so the sight
     * distance's pull follows that depth, not the game's own: the gate flipping (the game's depth thrown back and
     * forth, the drawn one still) leaves the pull where it is. Taken from the game's own depth it would give back what
     * the gate throws, on a gun that did not move. */
    {
      TfpPoseAdsProbe ads;
      tfp_pose_set_aim_steady(1);
      pose_world_aim(1);
      tfp_pose_set_ads_seat(.35f, .15f);
      for (int i = 0; i < 60; i++) root_frame(16);
      CHECK(tfp_pose_ads_probe(&ads) && ads.full_ads && near(ads.pull, .20, 1e-4));
      float least = 1, most = 0;
      for (int i = 0; i < 8; i++) {
        pose_world_gate(i % 2 == 0, 0);
        root_frame(16);
        CHECK(tfp_pose_ads_probe(&ads));
        if (ads.pull < least) least = ads.pull;
        if (ads.pull > most) most = ads.pull;
      }
      if (most - least > 1e-4f) printf("wire pose: zoom_steady.txt, the gate flipping in full aim: the pull went %.4f to %.4f m\n", least, most);
      CHECK(most - least <= 1e-4f && near(most, .20, 1e-4));
      pose_world_gate(0, 0);
      tfp_pose_set_ads_seat(0, 0);
      pose_world_aim(0);
      for (int i = 0; i < 60; i++) root_frame(16);
      tfp_pose_set_aim_steady(0);
      tfp_pose_steady_take(&held);
    }
    /* a shift too small for node 5's float (the lens one step short of its target): nothing is moved, nothing is
     * asked of the root, and neither is counted; it is not a root that failed to follow */
    CHECK(tfp_pose_set_depth_off(1) == 1); /* the hold then rests on the optic's own distance, where the game's depth is */
    pose_world_gate(0, 0);
    for (int i = 0; i < 60; i++) root_frame(16);
    CHECK(applied_is(.11, .02, -.12) && root_forwarded[1] == 0 && tfp_pose_steady_take(&held) == 1);
    pose_world_gate(1, 0);
    pose_world_lens(1.5707964f, nextafterf(1.5707964f, 0));
    for (int i = 0; i < 3; i++) { root_frame(16); CHECK(applied_is(.11, .02, -.12) && root_forwarded[1] == 0); }
    CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 3 && held.worked == 3 && tfp_steady_passed(&held) == 0 && held.gated.count == 3 && held.gated.most < current);
    CHECK(held.shifted == 0 && held.roots == 0 && held.delta.count == 0 && !tfp_steady_worth(&held));
    pose_world_gate(1, 0);                 /* and a real one after them is held as ever */
    root_frame(16);
    CHECK(near(forwarded[1] - NATIVE[1], .02 + (double)current - (double)gated_depth, 2e-6) && near(root_forwarded[1], (double)gated_depth - (double)current, 2e-6));
    CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 1 && held.shifted == 1 && held.roots == 1 && near(held.delta.most, current - gated_depth, 2e-6));
    CHECK(tfp_pose_set_depth_off(0) == 1);
    pose_world_gate(0, 0);
    for (int i = 0; i < 60; i++) root_frame(16);
    CHECK(applied_is(.11, .02 + (double)depth, -.12) && tfp_pose_steady_take(&held) == 1 && tfp_pose_gate_take(&g) == 1);
    /* depth_steady_off.txt: counted, nothing held; the zoom depth term goes on and off with the gate as before */
    CHECK(tfp_pose_set_depth_steady(2) == 1);
    const unsigned roots_before = root_calls;
    for (int i = 0; i < 6; i++) {
      pose_world_gate(i % 2 == 0, 0);
      root_frame(16);
      CHECK(i % 2 == 0 ? applied_is(.11, .02, -.12) : applied_is(.11, .02 + (double)depth, -.12));
    }
    CHECK(root_calls == roots_before + 6);
    CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 6 && held.off == 1 && held.shifted == 0 && held.roots == 0 && held.passed[TFP_STEADY_OFF] == 6 && tfp_steady_worth(&held));
    tfp_steady_line(&held, line, sizeof line);
    CHECK(strstr(line, "Steady: 6 frames of the rig, the game's depth worked out in 6: ") && strstr(line, "nothing held; the rig moved to it in 0 frames, its root with it in 0; ")
          && strstr(line, "passed in 6 (depth_steady_off.txt 6); depth_steady_off.txt: nothing is held"));
    /* node 5 never keeps moving without its root. The evaluator's root write not coming (depth_frame is the node 5
     * half alone): once is borne, twice running stops the held depth for the lease (and so does the third of a
     * lease, running or not: the scene of the root coming every other frame, in the lease after next) */
    CHECK(tfp_pose_set_depth_steady(1) == 1);
    pose_world_gate(0, 0);
    for (int i = 0; i < 60; i++) root_frame(16);
    CHECK(applied_is(.11, .02 + (double)depth, -.12) && tfp_pose_steady_take(&held) == 1 && held.stops == 0);
    {
      const double added = (double)current + (double)depth - (double)gated_depth; /* what the hold adds on a gated frame; the zoom depth term adds nothing on one */
      pose_world_gate(1, 0);
      depth_frame(16);
      CHECK(applied_is(.11, .02 + added, -.12));                                       /* alone, once */
      root_frame(16);
      CHECK(applied_is(.11, .02 + added, -.12) && near(root_forwarded[1], -added, 2e-6)); /* followed again */
      depth_frame(16);
      CHECK(applied_is(.11, .02 + added, -.12));
      depth_frame(16);
      CHECK(applied_is(.11, .02 + added, -.12));                                       /* alone, twice running */
      depth_frame(16);
      CHECK(applied_is(.11, .02, -.12));                                               /* stopped: node 5 is left where the game put it */
      root_frame(16);
      CHECK(applied_is(.11, .02, -.12) && root_forwarded[1] == 0);                     /* a root that comes now starts nothing */
      CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 6 && held.worked == 6 && held.shifted == 4 && held.roots == 1);
      CHECK(held.stops == 1 && held.stop_lease == 1 && held.passed[TFP_STEADY_NO_ROOT] == 2 && tfp_steady_passed(&held) == 2 && tfp_steady_worth(&held));
      CHECK(tfp_steady_stop_line(&held, line, sizeof line) && strstr(line, "Steady: stopped for lease 1: the rig was moved in 3 frames of this lease and its root did not follow (the last 2 running)"));
      tfp_steady_line(&held, line, sizeof line);
      CHECK(strstr(line, "the rig moved to it in 4 frames, its root with it in 1, by ") && strstr(line, "passed in 2 (the root did not follow 2)"));
      for (int i = 0; i < 6; i++) { /* for the rest of the lease: as with the switch, the zoom depth term on and off with the gate */
        pose_world_gate(i % 2 == 0, 0);
        root_frame(16);
        CHECK(i % 2 == 0 ? applied_is(.11, .02, -.12) : applied_is(.11, .02 + (double)depth, -.12));
      }
      CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 6 && held.shifted == 0 && held.stops == 0 && held.passed[TFP_STEADY_NO_ROOT] == 6);
      CHECK(tfp_steady_worth(&held) && !tfp_steady_stop_line(&held, line, sizeof line)); /* the game's depth still moves and is still counted; the stop is said once */
    }
    CHECK(tfp_pose_set_depth_steady(0) == 1);
    pose_world_gate(0, 0);
    depth_frame(16);
    CHECK(applied_is(.11, .02 + (double)depth, -.12) && tfp_pose_steady_take(&held) == 1 && held.frames == 0);
    tfp_pose_gate_take(&g);
  }

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

  /* ---- the held depth in later leases: a new lease begins it again after the stop above; and with one of the zoom
   * depth's code pins not this build's, the evaluator's locals are not read as a lens at all ---- */
  for (int pinned = 1; pinned >= 0; pinned--) {
    TfpSteadySecond held;
    char line[640];
    pose_world_depth_pin(pinned);
    pose_world_third_person();
    CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE);
    pose_world_first_person();
    CHECK(tfp_pose_set_depth_steady(1) == 1 && tfp_pose_steady_take(&held) == 1);
    game_frame(handle, &stats, 300);
    pose_world_optic();
    for (int i = 0; i < 61; i++) root_frame(16);
    const double rest = (double)forwarded[1] - (double)NATIVE[1]; /* at rest: the mod's reference for the optic, where the code is pinned */
    CHECK(near(forwarded[0] - NATIVE[0], .15, 2e-6) && near(forwarded[2] - NATIVE[2], -.12, 2e-6) && (pinned ? fabs(rest) > .01 && fabs(rest) < 1 : rest == 0));
    for (int i = 0; i < 6; i++) {
      pose_world_gate(i % 2 == 0, 0);
      root_frame(16);
      const double now_added = (double)forwarded[1] - (double)NATIVE[1];
      if (pinned) CHECK(i % 2 == 0 ? now_added > rest + .01 && near(root_forwarded[1], -now_added, 2e-6) : near(now_added, rest, 2e-6)); /* held: more on a gated frame, by what the game took */
      else CHECK(now_added == 0 && root_forwarded[1] == 0);                                                                              /* nothing, on node 5 or the root */
    }
    CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 68 && held.stops == 0 && held.passed[TFP_STEADY_NO_ROOT] == 0);
    if (pinned) CHECK(held.worked == 67 && held.shifted >= 65 && held.roots == held.shifted && held.passed[TFP_STEADY_NO_CODE] == 0);
    else {
      CHECK(held.worked == 0 && held.shifted == 0 && held.roots == 0 && held.passed[TFP_STEADY_NO_CODE] == 68 && tfp_steady_passed(&held) == 68 && tfp_steady_worth(&held));
      tfp_steady_line(&held, line, sizeof line);
      CHECK(strstr(line, "Steady: 68 frames of the rig, the game's depth worked out in 0: ") && strstr(line, "passed in 68 (the evaluator's code is not the pinned one 68)"));
    }
    if (pinned) {
      /* the root comes every other frame. No two frames alone are running, and without a count for the lease node 5
       * would go on alone in half of all frames; the third of the lease stops it */
      pose_world_gate(1, 0);
      root_frame(16);
      const double gated_added = (double)forwarded[1] - (double)NATIVE[1];
      CHECK(gated_added > rest + .01 && near(root_forwarded[1], -gated_added, 2e-6));
      unsigned alone = 0, followed = 0;
      for (int i = 0; i < 120; i++) {
        if (i % 2 == 0) depth_frame(16); else root_frame(16);
        const double now_added = (double)forwarded[1] - (double)NATIVE[1];
        if (now_added == 0) continue;                                                    /* stopped: node 5 is left where the game put it */
        CHECK(near(now_added, gated_added, 2e-6));
        if (i % 2 == 0) ++alone; else { ++followed; CHECK(near(root_forwarded[1], -gated_added, 2e-6)); }
      }
      CHECK(alone == TFP_STEADY_ALONE_LEASE && alone == 3 && followed == 2);             /* 60 of 120 without the count */
      CHECK(root_forwarded[1] == 0);                                                     /* a root that comes after the stop takes nothing */
      CHECK(tfp_pose_steady_take(&held) == 1 && held.frames == 121 && held.shifted == 6 && held.roots == 3 && held.stops == 1 && held.stop_lost == 3 && held.stop_running == 1);
      CHECK(held.passed[TFP_STEADY_NO_ROOT] == 115 && tfp_steady_stop_line(&held, line, sizeof line));
      CHECK(strstr(line, ": the rig was moved in 3 frames of this lease and its root did not follow (no two of them running), so the gun moved against the eye in them; "));
      for (int i = 0; i < 6; i++) root_frame(16);
      CHECK((double)forwarded[1] - (double)NATIVE[1] == 0 && tfp_pose_steady_take(&held) == 1 && held.stops == 0 && held.shifted == 0 && !tfp_steady_stop_line(&held, line, sizeof line));
    }
    pose_world_gate(0, 0);
    CHECK(tfp_pose_set_depth_steady(0) == 1);
    CHECK(H->finish(H->user, handle, &has_stats, &stats, &why) == TFP_OPS_TRUE);
  }
  pose_world_depth_pin(1);
  pose_world_third_person();
  {
    TfpGateSecond g;
    tfp_pose_gate_take(&g);
  }

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

  /* ---- round 20: the sight brought closer in full aim (src/ads_seat.h; not retail's). Off until the binder hands a
   * wanted distance; then, in full aim, the gun goes toward the eye along node 5's forward axis by the game's sight
   * (its near clip) less the wanted distance, eased in and out with the hip placement's blend, and the ADS placement's
   * forward (the editor's) on top. The root is never written for it: the eye stays where the game puts it. ---- */
  {
    TfpPoseAdsProbe ads;
    pose_world_third_person();
    CHECK(H->prepare(H->user, &c, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, &c, native, &handle, &why) == TFP_OPS_TRUE);
    pose_world_first_person();
    game_frame(handle, &stats, 300);
    CHECK(applied_is(.15, 0, -.12));
    tfp_pose_set_ads_seat(.173f, .10f);                               /* the Scythe's sight, 10 cm wanted */
    game_frame(handle, &stats, 16);
    CHECK(applied_is(.15, 0, -.12) && tfp_pose_ads_probe(&ads) && !ads.full_ads && ads.share == 0 && ads.closer == 0); /* at the hip: nothing */
    pose_world_aim(1);
    const unsigned roots_before = root_calls;
    float last = forwarded[1], worst = 0;
    for (int i = 0; i < 40; i++) {
      game_frame(handle, &stats, 16);
      if (fabsf(forwarded[1] - last) > worst) worst = fabsf(forwarded[1] - last);
      CHECK(forwarded[1] <= last + 1e-6f);                            /* toward the eye only, never back and forth */
      last = forwarded[1];
    }
    CHECK(aimed_is(-.073) && worst < .02f && root_calls == roots_before);  /* full aim: the hip placement gone, the gun 7.3 cm closer */
    CHECK(tfp_pose_ads_probe(&ads) && ads.full_ads && near(ads.share, 1, 1e-3) && near(ads.pull, .073, 1e-4) && near(ads.closer, .073, 1e-4));
    /* the editor's ADS forward on top: 2 cm back from the eye; the near clip comes in by 5.3 cm then */
    CHECK(H->set_ads_placement(H->user, handle, 0, .02, 0, &why) == TFP_OPS_TRUE);
    game_frame(handle, &stats, 16);
    CHECK(aimed_is(-.053) && tfp_pose_ads_probe(&ads) && near(ads.closer, .053, 1e-4) && near(ads.ads_forward, .02, 1e-6));
    CHECK(H->set_ads_placement(H->user, handle, 0, 0, 0, &why) == TFP_OPS_TRUE);
    game_frame(handle, &stats, 16);
    CHECK(aimed_is(-.073));                                           /* the ADS placement is retail's: a step, as the editor drags it */
    /* another zoom level: the game's sight at 0.35 m. Glided, the most (0.25 m) at the end */
    tfp_pose_set_ads_seat(.35f, .10f);
    last = forwarded[1]; worst = 0;
    for (int i = 0; i < 60; i++) { game_frame(handle, &stats, 16); if (fabsf(forwarded[1] - last) > worst) worst = fabsf(forwarded[1] - last); last = forwarded[1]; }
    CHECK(aimed_is(-.25) && worst < .04f);
    /* out of aim: eased back to the hip placement, nothing pulled */
    pose_world_aim(0);
    last = forwarded[1]; worst = 0;
    for (int i = 0; i < 40; i++) { game_frame(handle, &stats, 16); CHECK(forwarded[1] >= last - 1e-6f); if (fabsf(forwarded[1] - last) > worst) worst = fabsf(forwarded[1] - last); last = forwarded[1]; }
    CHECK(near(forwarded[0] - NATIVE[0], .15, 1e-4) && near(forwarded[1] - NATIVE[1], 0, 1e-4) && near(forwarded[2] - NATIVE[2], -.12, 1e-4));
    CHECK(tfp_pose_ads_probe(&ads) && ads.share < 1e-3f && ads.closer < 1e-4f);
    /* 0 cm: the game's own seat, in full aim too */
    tfp_pose_set_ads_seat(.35f, 0);
    pose_world_aim(1);
    for (int i = 0; i < 40; i++) game_frame(handle, &stats, 16);
    CHECK(aimed_is(0) && tfp_pose_ads_probe(&ads) && ads.pull == 0 && ads.closer == 0);
    pose_world_aim(0);
    H->finish(H->user, handle, &has_stats, &stats, &why);
    pose_world_third_person();
    /* a lease that is not a steady entry begins its hip weight at 0 and blends it up at the hip: 1 - weight is then
     * near 1, but that is no aim, and nothing is pulled (the review's I1, mutation #5) */
    TfpOpsHelperConfig blended = c;
    blended.steady_entry = false;
    tfp_pose_set_ads_seat(.173f, .10f);
    CHECK(H->prepare(H->user, &blended, &native, &why) == TFP_OPS_TRUE && H->begin(H->user, &blended, native, &handle, &why) == TFP_OPS_TRUE);
    pose_world_first_person();
    for (int i = 0; i < 12; i++) {
      game_frame(handle, &stats, 16);
      CHECK(forwarded[1] == NATIVE[1] && tfp_pose_ads_probe(&ads) && ads.share == 0 && ads.closer == 0);
    }
    CHECK(near(forwarded[0] - NATIVE[0], .15, 1e-4));                 /* the hip placement did blend in */
    H->finish(H->user, handle, &has_stats, &stats, &why);
    pose_world_third_person();
  }
  if (ads_frames(H, &c)) return 1;
  if (ads_zoom_frames(H, &c)) return 1;
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
