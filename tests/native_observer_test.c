#include "fake_native.h"
#include "native_observer.h"
#include <assert.h>
#include <math.h>

enum Change {
  CHANGE_NONE,
  CHANGE_RIG_GRAPH,
  CHANGE_RIG_GENERATION,
  CHANGE_BODY_GRAPH,
  CHANGE_CAMERA_GENERATION,
  CHANGE_WEAPON_GENERATION,
  CHANGE_WEAPON_DESCRIPTOR,
  CHANGE_REQUEST,
  CHANGE_REQUEST_HIGH_BITS,
  CHANGE_CONTROLLER,
  CHANGE_LENS
};
static enum Change change;
static bool triggered;
static const uintptr_t final_rig_matrix =
    UINT64_C(0x1200000000) + 0x3000 + 50 * 64;
static uintptr_t trigger_address;
static bool observing_read(void *user, uintptr_t at, void *out, size_t size) {
  bool ok = native_fake_read(user, at, out, size);
  if (ok && !triggered && change && at == trigger_address && size == 64) {
    triggered = true;
    uint8_t generation = 1;
    switch (change) {
    case CHANGE_RIG_GRAPH:
      p64(UINT64_C(0x1100000000) + 0x88, UINT64_C(0x1200090000));
      break;
    case CHANGE_RIG_GENERATION:
      put(UINT64_C(0xb00001000) + 801, &generation, 1);
      break;
    case CHANGE_BODY_GRAPH:
      p64(UINT64_C(0xc00000000) + 0x88, UINT64_C(0xd00090000));
      break;
    case CHANGE_CAMERA_GENERATION:
      put(UINT64_C(0xb00001000) + 701, &generation, 1);
      break;
    case CHANGE_WEAPON_GENERATION:
      put(UINT64_C(0xb00001000) + 501, &generation, 1);
      break;
    case CHANGE_WEAPON_DESCRIPTOR:
      n32(UINT64_C(0xa00000000) + 0x400, 99);
      break;
    case CHANGE_REQUEST:
      n32(UINT64_C(0x300000000) + 0x200, 6);
      break;
    case CHANGE_REQUEST_HIGH_BITS:
      n32(UINT64_C(0x300000000) + 0x200, 0x10004);
      break;
    case CHANGE_CONTROLLER:
      put(UINT64_C(0x500000000) + 4, &generation, 1);
      break;
    case CHANGE_LENS: {
      float fov = 1.1f;
      put(UINT64_C(0x300003000) + 0x34, &fov, 4);
      break;
    }
    default:
      break;
    }
  }
  return ok;
}
static FpMemory fixture(void) {
  FpMemory m = native_setup();
  float native_pose[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  put(UINT64_C(0x1200000000) + 0x3000 + 13 * 64, native_pose, 64);
  put(UINT64_C(0x1200000000) + 0x3000 + 34 * 64, native_pose, 64);
  put(UINT64_C(0x1200000000) + 0x3000 + 8 * 64, native_pose, 64);
  m.read = observing_read;
  change = CHANGE_NONE;
  triggered = false;
  trigger_address = final_rig_matrix;
  p64(UINT64_C(0x2300000000) + 0x1d0, 0);
  uint8_t near_getter[] = {0xf3, 0x0f, 0x10, 0x41, 0x28, 0xc3};
  uint8_t fov_getter[] = {0xf3, 0x0f, 0x10, 0x41, 0x34, 0xc3};
  put(m.exe + 0x30d940, near_getter, sizeof near_getter);
  put(m.exe + 0x30d980, fov_getter, sizeof fov_getter);
  uintptr_t camera = UINT64_C(0x300003000);
  n32(camera + 4, 73);
  uint8_t lens[32] = {0};
  float near_value = .05f, far_value = 1000, fov = 1.57f,
        frustum[] = {-.2f, .2f, -.1f, .1f};
  uint32_t projection = 1;
  memcpy(lens, &near_value, 4);
  memcpy(lens + 4, &far_value, 4);
  memcpy(lens + 8, &projection, 4);
  memcpy(lens + 12, &fov, 4);
  memcpy(lens + 16, frustum, 16);
  put(camera + 0x28, lens, sizeof lens);
  n32(camera + 0x50, 2);
  float pose[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const unsigned nodes[] = {0, 5, 50};
  for (unsigned i = 0; i < 3; i++) {
    pose[12] = (float)i + 1;
    pose[13] = (float)i + 2;
    pose[14] = (float)i + 3;
    put(UINT64_C(0x1200000000) + 0x3000 + nodes[i] * 64, pose, 64);
  }
  return m;
}
int main(void) {
  FpNativeObservation o;
  FpMemory m = fixture();
  assert(fp_native_observe(&m, &o));
  assert(o.available == FP_OBSERVE_ALL && !o.unavailable &&
         o.failure == FP_OBSERVE_OK);
  assert(o.owner.kind == 4 && o.owner.head == 0 && o.owner.tail == 1 &&
         o.owner.camera_unit == 701 && o.camera_render_id == 73 &&
         o.request_words[3] == 401);
  assert(o.camera_graph.ref == 701 && o.rig_graph.ref == 801 &&
         o.body_sample.graph.ref == 401 && o.body_sample.head == 90);
  assert(fabsf(o.body_head[2] - 1.7f) < .000001f);
  assert(o.rig_world[0][12] == 1 && o.rig_world[1][12] == 2 &&
         o.rig_world[2][12] == 3);
  assert(o.lens.near_range == .05f && o.lens.far_range == 1000 &&
         o.lens.vertical_fov == 1.57f && o.lens.projection_type == 1 &&
         o.lens.frustum_mode == 2);
  assert(o.weapon_owner.weapon == 201 && o.weapon_owner.weapon_unit == 501 &&
         o.weapon_graph.object == UINT64_C(0x2300000000) &&
         o.weapon_world[15] == 1 && !o.weapon_parent);

  /* A valid FPS parent is recorded without changing the camera pose. */
  p64(UINT64_C(0xe00000000) + 0x1d0, UINT64_C(0x1100000000));
  p64(UINT64_C(0xe00000000) + 0x78, UINT64_C(0x1200000000) + 0x3000);
  n32(UINT64_C(0xf00000000) + 0x2000, 50 * 65536 + 2);
  assert(fp_native_observe(&m, &o) && o.camera_parent_graph.ref == 801 &&
         o.camera_parent == o.rig_graph.object);
  p64(UINT64_C(0x2300000000) + 0x1d0, UINT64_C(0x1100000000));
  p64(UINT64_C(0x2300000000) + 0x78, UINT64_C(0x1200000000) + 0x3000);
  n32(UINT64_C(0x2400000000) + 0x2000, 8 * 65536 + 2);
  float attachment[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 9, 10, 11, 1};
  put(UINT64_C(0x1200000000) + 0x3000 + 8 * 64, attachment, 64);
  assert(fp_native_observe(&m, &o) && (o.available & FP_OBSERVE_WEAPON) &&
         o.weapon_parent_graph.ref == 801 && (o.weapon_link >> 16) == 8 &&
         o.weapon_parent_world[12] == 9);

  m = fixture();
  n32(UINT64_C(0x700000000) + FP_AVATAR_RIG, 0);
  assert(fp_native_observe(&m, &o) && !(o.available & FP_OBSERVE_RIG) &&
         (o.unavailable & FP_OBSERVE_RIG) && !o.rig_graph.object &&
         o.rig_world[0][15] == 0);
  m = fixture();
  f.denied = UINT64_C(0x300003000) + 0x28;
  assert(fp_native_observe(&m, &o) && !(o.available & FP_OBSERVE_LENS) &&
         (o.available & FP_OBSERVE_CAMERA) &&
         (o.unavailable & FP_OBSERVE_LENS) && !o.lens.vertical_fov);
  m = fixture();
  n32(UINT64_C(0x900000000) + 0x400, UINT32_MAX);
  assert(fp_native_observe(&m, &o) && !(o.available & FP_OBSERVE_WEAPON) &&
         (o.unavailable & FP_OBSERVE_WEAPON) && !o.weapon_graph.object &&
         !o.weapon_owner.weapon);

  const enum Change changes[] = {
      CHANGE_RIG_GRAPH,         CHANGE_RIG_GENERATION,
      CHANGE_BODY_GRAPH,        CHANGE_CAMERA_GENERATION,
      CHANGE_WEAPON_GENERATION, CHANGE_WEAPON_DESCRIPTOR,
      CHANGE_REQUEST,           CHANGE_REQUEST_HIGH_BITS,
      CHANGE_CONTROLLER,        CHANGE_LENS};
  for (unsigned i = 0; i < sizeof changes / sizeof *changes; i++) {
    m = fixture();
    change = changes[i];
    if (change == CHANGE_WEAPON_GENERATION ||
        change == CHANGE_WEAPON_DESCRIPTOR)
      trigger_address = UINT64_C(0x2400000000) + 0x3000;
    assert(!fp_native_observe(&m, &o) && triggered);
    assert(!o.available && o.unavailable == FP_OBSERVE_ALL);
    assert(o.failure == (changes[i] == CHANGE_LENS ? FP_OBSERVE_FIELDS_CHANGED
                         : changes[i] >= CHANGE_REQUEST
                             ? FP_OBSERVE_REQUEST_CHANGED
                             : FP_OBSERVE_GRAPH_CHANGED));
  }
  m = fixture();
  image(m.exe, 0, FP_EXE_SIZE);
  assert(!fp_native_observe(&m, &o) && o.failure == FP_OBSERVE_WRONG_BUILD);
  assert(!fp_native_observe(NULL, &o) && o.failure == FP_OBSERVE_BAD_ARGUMENT);
  assert(!fp_native_observe(&m, NULL));
  puts("C native observer: exact request/controller, camera/lens, body and FPS "
       "0/5/50 copies, weapon/attachment poses, explicit unavailable parts, "
       "parent proof, graph/Unit/descriptor retirement, hidden request "
       "changes and wrong-build refusal passed");
  return 0;
}
