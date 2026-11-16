#define TFP_CAMERA_TEST 1
#include "../src/camera_policy.c"
#include "fake_native.h"
#include <assert.h>
double fp_camera_test_time = 1000.125;
static bool retire_before_lock;
void fp_camera_before_lock(void) {
  if (retire_before_lock) {
    retire_before_lock = false;
    atomic_fetch_add(&callback_generation, 1);
  }
}
static unsigned mutation;
void fp_camera_before_commit(bool native) {
  if (native)
    return;
  if (mutation == 1)
    p64(0xb00000000ULL + 0x2000 + 501 * 8, 0x2600000000ULL);
  if (mutation == 2)
    n32(0xd00000000ULL + 0x2000 + 90 * 4, 88 * 65536 + 1);
  mutation = 0;
}
static float received[4];
static const float *received_pointer;
static unsigned calls;
static void collect(void *camera, uint32_t ref, const float *pose) {
  (void)camera;
  (void)ref;
  received_pointer = pose;
  memcpy(received, pose, 16);
  calls++;
}
static void approximately(float a, float b) { assert(fabsf(a - b) < .00001f); }
static void action_setup(const FpMemory *m, const FpSnapshot *s) {
  uintptr_t body = fp_unit_object(m, s->body), vt = 0;
  assert(fp_pointer(m, body, &vt));
  p64(vt + 0x1b0, m->exe + 0x2bd9c0);
  const uint8_t code[] = {0x48, 0x8b, 0x81, 0x78, 1, 0, 0, 0xc3};
  put(m->exe + 0x2bd9c0, code, 8);
  p64(body + 0x358, UINT64_C(0x4d1c334d294dfa97));
  uintptr_t instance = 0x1700000000ULL, graph = 0x1700010000ULL,
            act = 0x1700020000ULL;
  p64(body + 0x178, instance);
  p64(instance + 0x28, graph);
  p64(instance + 0x50, act);
  uint32_t header[] = {0, 31, 12};
  put(graph, header, 12);
  uint32_t defs[31] = {0};
  defs[0] = 0x1000;
  defs[2] = 0x2000;
  defs[13] = 0x3000;
  put(graph + 16, defs, sizeof defs);
  unsigned layers[] = {1, 3, 14}, states[] = {0, 0, 36};
  for (unsigned n = 0; n < 3; n++) {
    uintptr_t def = graph + 12 + defs[layers[n] - 1];
    n32(def + 8, 100);
    uint32_t rows[100];
    for (unsigned i = 0; i < 100; i++)
      rows[i] = 0x400 + i * 16;
    put(def + 12, rows, sizeof rows);
    p64(act + (layers[n] - 1) * 8, def + rows[states[n]]);
  }
}
int main(void) {
  FpMemory m = native_setup();
  FpSnapshot s;
  assert(fp_snapshot(&m, true, &s, NULL, 0));
  FpCameraPolicy p;
  fp_camera_policy_init(&p);
  p.memory = m;
  p.origin = s;
  p.sequence = 1;
  p.body_object = fp_unit_object(&m, s.body);
  p.camera_object = fp_unit_object(&m, s.camera_unit);
  fake_dispatch(&p.lease);
  atomic_store(&p.lease.armed, true);
  atomic_store(&active, &p);
  atomic_store(&native_setter, collect);
  float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  put(s.context + 12, identity, 64);
  float input[20] = {.1f, .2f, .3f, 57};
  input[19] = 1;
  live_begin = (uintptr_t)input;
  live_end = live_begin + sizeof input;
  n32(s.request, 6);
  p64(0xe00000000ULL + 0x1d0, 0x1100000000ULL);
  p64(0xe00000000ULL + 0x78, 0x1200000000ULL + 0x3000);
  n32(0xf00000000ULL + 0x2000, 50 * 65536 + 2);
  float before[20];
  for (unsigned state = 0; state < 4; state++) {
    uint8_t controls[6] = {0};
    controls[4] = (state & 1) != 0;
    put(s.controller, controls, 6);
    FpQuat q = fp_q_look(.6f, state < 2 ? .25f : 1.55f);
    memcpy(input + 16, &q, 16);
    memcpy(before, input, sizeof before);
    fp_camera_test_time += 1000.0 / 240;
    p.sequence++;
    camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
    assert(received_pointer == input && !memcmp(received, input, 16) &&
           !memcmp(input, before, sizeof before));
    float movement[6];
    FpQuat observed;
    assert(fp_camera_policy_read_frame(&p, movement, &observed));
    approximately(observed.x, q.x);
    approximately(observed.y, q.y);
    approximately(observed.z, q.z);
    approximately(observed.w, q.w);
  }
  assert(p.native_forwarded == 4 && p.adjusted == 0 && p.correction.remembered);
  /* Read-only native buffers must never become an implicit write target. */
  float *readonly =
      VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  assert(readonly);
  memcpy(readonly, input, sizeof before);
  DWORD previous;
  assert(VirtualProtect(readonly, 4096, PAGE_READONLY, &previous));
  live_begin = (uintptr_t)readonly;
  live_end = live_begin + 4096;
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, readonly);
  assert(received_pointer == readonly && !memcmp(received, readonly, 16));
  VirtualFree(readonly, 0, MEM_RELEASE);
  live_begin = (uintptr_t)input;
  live_end = live_begin + sizeof input;
  unsigned native = p.native_forwarded;
  retire_before_lock = true;
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
  assert(received_pointer == input && p.native_forwarded == native);
  /* No independent vehicle/action pin or rotation override. */
  n32(s.request, 17);
  n32(s.request + 4, 999);
  memcpy(before, input, sizeof before);
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
  assert(received_pointer == input && !memcmp(input, before, sizeof before) &&
         !p.correction.remembered);
  float movement[6];
  FpQuat observed;
  assert(!fp_camera_policy_read_frame(&p, movement, &observed));
  n32(s.request, 4);
  n32(s.request + 4, s.avatar);
  uint8_t controls[6] = {0};
  put(s.controller, controls, 6);
  action_setup(&m, &s);
  p64(0xe00000000ULL + 0x1d0, 0);
  n32(0xf00000000ULL + 0x2000, 0);
  float zero[3] = {0};
  put(0xf00000000ULL + 0x1000 + 36, zero, 12);
  p.native_seen = true;
  p.native_at = fp_camera_test_time - 251;
  fp_correction_reset(&p.correction);
  memset(input, 0, 12);
  input[3] = 57;
  memcpy(before, input, sizeof before);
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
  fp_camera_test_time += 120;
  assert(p.blend == 0 && !p.native_seen);
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
  assert(received_pointer != input);
  approximately(received[0], 0);
  approximately(received[1], .18f);
  approximately(received[2], 1.74f);
  approximately(received[3], 57);
  assert(!memcmp(input, before, sizeof before));
  for (unsigned change = 1; change <= 2; change++) {
    n32(s.request, 6);
    p64(0xe00000000ULL + 0x1d0, 0x1100000000ULL);
    p64(0xe00000000ULL + 0x78, 0x1200000000ULL + 0x3000);
    n32(0xf00000000ULL + 0x2000, 50 * 65536 + 2);
    fp_camera_test_time += 10;
    p.sequence++;
    camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
    assert(p.history_valid && p.correction.remembered);
    n32(s.request, 4);
    p64(0xe00000000ULL + 0x1d0, 0);
    n32(0xf00000000ULL + 0x2000, 0);
    mutation = change;
    fp_camera_test_time += 1;
    camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
    assert(received_pointer == input && !p.history_valid);
    p64(0xb00000000ULL + 0x2000 + 501 * 8, 0x2300000000ULL);
    n32(0xd00000000ULL + 0x2000 + 90 * 4, 89 * 65536 + 1);
  }
  unsigned adjusted = p.adjusted;
  camera_call(m.game + 0x127a138, (void *)s.camera, s.camera_unit, input);
  assert(received_pointer == input && p.adjusted == adjusted);
  /* A parented normal camera is native-owned even with a valid body graph. */
  p64(0xe00000000ULL + 0x1d0, 0x1100000000ULL);
  n32(0xf00000000ULL + 0x2000, 50 * 65536 + 2);
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
  assert(received_pointer == input && p.adjusted == adjusted);
  p64(0xe00000000ULL + 0x1d0, 0);
  n32(0xf00000000ULL + 0x2000, 0);
  float shifted[3] = {1, 0, 0};
  put(0xf00000000ULL + 0x1000 + 36, shifted, 12);
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
  assert(received_pointer == input && p.adjusted == adjusted);
  put(0xf00000000ULL + 0x1000 + 36, zero, 12);
  p64(0x1700020000ULL + 13 * 8, 0x777777777ULL);
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
  assert(received_pointer == input && p.adjusted == adjusted);
  n32(0xd00000000ULL + 0x2800 + 90 * 4, 0);
  camera_call(m.game + 0x127a139, (void *)s.camera, s.camera_unit, input);
  assert(received_pointer == input && p.adjusted == adjusted);
  FpEyeOptions options = p.options;
  p.motion.phase = 3.25;
  assert(fp_camera_policy_options(&p, &options) && p.motion.phase == 3.25);
  options.motion.bob_strength = .5;
  assert(fp_camera_policy_options(&p, &options) && p.motion.phase == 0);
  assert(!fp_camera_policy_free_aim(&p, 30000));
  atomic_store(&active, NULL);
  puts("C camera: exact native FP/ADS pointer and full-frame preservation, "
       "vertical native look, read-only input, retired callback generation, "
       "native vehicles/actions, guarded on-foot positional fallback, "
       "invalid-space/action/graph refusal and unchanged motion phase passed");
  return 0;
}
