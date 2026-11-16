#define TFP_POLICY_TEST 1
/* Exercise the real open/close lease with a private fake dispatch table. */
#define fp_slot_read pose_test_slot_read
#define fp_slot_cas pose_test_slot_cas
#include "../src/pose_policy.c"
#include "fake_native.h"
#include <assert.h>
static float received[4];
static const float *received_pointer;
static uint32_t received_node;
static unsigned calls, getter_calls, checks;
static float getter_value[4];
static bool cas_blocked;
#define CHECK(value)                                                           \
  do {                                                                         \
    checks++;                                                                  \
    assert(value);                                                             \
  } while (0)
static const float *observe(uint32_t u, uint32_t n) {
  (void)u;
  (void)n;
  getter_calls++;
  return getter_value;
}
static void collect(uint32_t u, uint32_t n, const float *input) {
  (void)u;
  received_pointer = input;
  received_node = n;
  memcpy(received, input, 16);
  calls++;
}
bool pose_test_slot_read(void *user, uintptr_t slot, uintptr_t *out) {
  (void)user;
  return read_fake(&f, slot, out, sizeof *out);
}
FpCasResult pose_test_slot_cas(void *user, uintptr_t slot, uintptr_t expected,
                               uintptr_t value, uintptr_t *observed) {
  if (cas_blocked || !pose_test_slot_read(user, slot, observed))
    return FP_CAS_UNCERTAIN;
  if (*observed != expected)
    return FP_CAS_DIFFERENT;
  p64(slot, value);
  return FP_CAS_CHANGED;
}
static void close_float(float a, float b) { CHECK(fabsf(a - b) < .00001f); }
static void hex_code(uintptr_t at, const char *hex) {
  uint8_t bytes[128];
  unsigned n = 0;
  for (; hex[n * 2]; n++) {
    unsigned v;
    assert(sscanf(hex + n * 2, "%2x", &v) == 1);
    bytes[n] = (uint8_t)v;
  }
  put(at, bytes, n);
}
static void attach(bool ready) {
  const uintptr_t rig = UINT64_C(0x1100000000), worlds = UINT64_C(0x1200003000);
  p64(UINT64_C(0x2300000000) + 0x1d0, ready ? rig : UINT64_C(0xc00000000));
  p64(UINT64_C(0x2300000000) + 0x78, worlds);
  n32(UINT64_C(0x2400002000), ready ? 0x00080002 : 0x00540002);
  p64(UINT64_C(0xe00000000) + 0x1d0, ready ? rig : 0);
  p64(UINT64_C(0xe00000000) + 0x78, worlds);
  n32(UINT64_C(0xf00002000), ready ? 0x00320002 : 0);
}
static FpSnapshot snapshot(FpMemory *m) {
  FpSnapshot s;
  CHECK(fp_snapshot(m, true, &s, NULL, 0));
  return s;
}
static FpSnapshot next_request(FpMemory *m, uint32_t kind) {
  FpSnapshot old = snapshot(m);
  uint32_t words[5] = {kind, old.avatar, old.weapon, old.body, 0};
  uintptr_t at = old.context + 0x200 + old.tail * 0xe8;
  uint8_t zero[0xe8] = {0};
  put(at, zero, sizeof zero);
  put(at, words, sizeof words);
  n32(at + 0x18, 1);
  n32(old.context + 0x1f8, old.tail);
  n32(old.context + 0x1fc, (old.tail + 1) % 32);
  return snapshot(m);
}
static FpMemory setup_policy(FpPosePolicy *p, FpSnapshot *s) {
  atomic_store(&active, NULL);
  cas_blocked = false;
  test_now = 1000;
  FpMemory m = native_setup();
  for (unsigned i = 0; i < FP_NATIVE_SITE_COUNT; i++) {
    const FpNativeSite *site = fp_native_sites + i;
    hex_code((site->exe ? m.exe : m.game) + site->rva, site->hex);
  }
  const uintptr_t root = UINT64_C(0x1500000000), api = UINT64_C(0x1500010000);
  p64(m.game + 0x3326308, root);
  p64(root + 0x18, api);
  p64(api, m.exe + 0x1fc450);
  p64(api + 0x38, m.exe + 0x1fc720);
  p64(api + 0x40, m.exe + 0x1fc7a0);
  fp_pose_policy_init(p);
  *s = snapshot(&m);
  char error[160];
  CHECK(fp_pose_policy_open(p, &m, s, NULL, error, sizeof error));
  atomic_store(&original_getter, observe);
  atomic_store(&original_position, collect);
  atomic_store(&original_rotation, collect);
  *s = next_request(&m, 6);
  CHECK(s->request == p->request);
  return m;
}
static void set_ads(const FpSnapshot *s, bool ads) {
  uint8_t controls[6] = {0};
  controls[4] = ads;
  put(s->controller, controls, 6);
}
static void branch(FpPosePolicy *p, uint8_t *scratch, const float native[4]) {
  memcpy(scratch + 0x200, native, 16);
  live_begin = (uintptr_t)scratch;
  live_end = live_begin + 0x300;
  position_call_stack(p->memory.game + 0x1288424, live_begin,
                      p->origin.viewmodel, 5, (const float *)(scratch + 0x200));
}
static void depth_sample(FpPosePolicy *p, uint8_t *scratch) {
  float lens = .5f, distance = .3f;
  uint8_t state[0x24] = {0};
  memcpy(state + 4, &lens, 4);
  memcpy(state + 0x10, &distance, 4);
  put(p->request + 0x80, state, sizeof state);
  p->depth = (FpDepth){.checked = true,
                       .code_ok = true,
                       .signature_set = true,
                       .valid = true,
                       .previous_known = true,
                       .next_check = UINT64_MAX,
                       .current = .3f,
                       .current_reference = .1f};
  memcpy(p->depth.signature, state + 0x10, 4);
  memcpy(scratch + 0x5c, &lens, 4);
  memcpy(scratch + 0x60, &lens, 4);
  live_begin = (uintptr_t)scratch;
  live_end = live_begin + 0x300;
  CHECK(getter_call(p->memory.game + 0x1287a44, live_begin, p->origin.viewmodel,
                    50));
}
static void reopen(FpPosePolicy *p, FpMemory *m, FpSnapshot *s) {
  char error[160];
  *s = snapshot(m);
  CHECK(fp_pose_policy_open(p, m, s, NULL, error, sizeof error));
  atomic_store(&original_getter, observe);
  atomic_store(&original_position, collect);
  atomic_store(&original_rotation, collect);
  *s = next_request(m, 6);
  CHECK(s->request == p->request);
}
static void depth_availability_cases(void) {
  FpPoseDepthResponse r = {0};
  close_float(depth_response(&r, 0, true, -.2f, 0), -.2f);
  close_float(depth_response(&r, 500, false, 0, 1), -.2f);
  float just_expired = depth_response(&r, 501, false, 0, 1);
  CHECK(just_expired > -.2f && just_expired < -.199f);
  close_float(depth_response(&r, 575, false, 0, 1), -.1f);
  close_float(depth_response(&r, 650, false, 0, 1), 0);
  close_float(depth_response(&r, 99999, false, 0, 1), 0);
  /* Recovery from an expired/refused source starts from its current scalar.
   * Subsequent valid changes pass immediately, including while bias decays. */
  close_float(depth_response(&r, 100000, true, -.4f, 1), 0);
  float previous = depth_response(&r, 100075, true, -.4f, 1);
  close_float(previous, -.2f);
  float changed = depth_response(&r, 100075, true, -.5f, 1);
  close_float(changed, -.3f);
  close_float(previous, .1f + changed); /* native +.1, correction -.1 */
  close_float(depth_response(&r, 100150, true, -.5f, 1), -.5f);
  close_float(depth_response(&r, 100151, true, 0, 1), 0);
  CHECK(r.sampled && r.source == 0 && r.phase == FP_POSE_DEPTH_LIVE);
  /* A legitimate zero during recovery is a target, not another refusal. */
  close_float(depth_response(&r, 100152, true, -.3f, 1), -.3f);
  float released = depth_response(&r, 100702, false, 0, 1);
  CHECK(released < 0 && released > -.3f);
  close_float(depth_response(&r, 100702, true, 0, 1), released);
  close_float(depth_response(&r, 100777, true, 0, 1), released * .5f);
  close_float(depth_response(&r, 100852, true, 0, 1), 0);
  /* Scalar-only handoff retains an in-progress release, never extends it. */
  depth_response(&r, 100853, true, -.2f, 1);
  depth_response(&r, 101003, true, -.2f, 1);
  depth_response(&r, 101578, false, 0, 1);
  FpPoseDepthResponse carried = r;
  close_float(depth_response(&carried, 101603, false, 0, 1),
              depth_response(&r, 101603, false, 0, 1));
  close_float(depth_response(&carried, 101653, false, 0, 1), 0);
  /* Long ADS can consume the complete expiry while hip contribution is zero.
   * If data returns partway through release, availability recovery stays
   * smooth. */
  r = (FpPoseDepthResponse){0};
  close_float(depth_response(&r, 0, true, -.2f, 0), -.2f);
  close_float(depth_response(&r, 5000, false, 0, 0), 0);
  close_float(depth_response(&r, 5075, false, 0, .5f), 0);
  close_float(depth_response(&r, 5075, true, -.4f, .5f), 0);
  close_float(depth_response(&r, 5150, true, -.4f, .5f), -.2f);
  close_float(depth_response(&r, 5225, true, -.4f, 1), -.4f);
  CHECK(depth_response_valid(&r, 5225));
}
static FpPosePolicy *delayed_policy;
static FpMemory *delayed_memory;
static FpPosePolicy successor_copy;
static unsigned delayed_family;
static void delayed_reopen(unsigned family) {
  CHECK(family == delayed_family);
  test_before_lock = NULL;
  FpPosePolicy *p = delayed_policy;
  FpSnapshot s = next_request(delayed_memory, 4);
  CHECK(fp_pose_policy_handoff(p));
  test_now += 10;
  reopen(p, delayed_memory, &s);
  attach(true);
  p->placement = (FpPlacement){.17, .08, -.11, 4, 7, 20};
  p->ads = (FpPlacement){.02, .03, .04, 1, 2, 3};
  p->weight = p->start_weight = .375f;
  p->ads_weight = p->start_ads_weight = .625f;
  p->target = 1;
  p->start = test_now;
  p->transition_ready = p->applied_ready = true;
  p->applied_at = p->depth_at = test_now;
  p->applied_owner = s;
  CHECK(fp_rig_attachments(delayed_memory, &s, &p->applied_rig));
  p->depth_response = (FpPoseDepthResponse){.source = -.12f,
                                            .value = -.12f,
                                            .sample_at = test_now,
                                            .start = test_now,
                                            .phase = FP_POSE_DEPTH_LIVE,
                                            .sampled = true};
  p->hip_depth = -.12f;
  p->depth_ready = p->shifted = p->rotated = true;
  p->depth.observation = (FpDepthObservation){.ready = true,
                                              .stack = UINT64_C(0x98760000),
                                              .request = p->request,
                                              .token = p->token,
                                              .thread = GetCurrentThreadId()};
  p->depth.pending = p->depth.observation;
  p->depth.delta = .13f;
  p->depth.carrier_ready = true;
  p->carry.valid = true;
  p->carry.weight = .2f;
  memcpy(&successor_copy, p, sizeof successor_copy);
}
static void delayed_callback_cases(void) {
  for (unsigned family = 0; family < 3; family++) {
    FpPosePolicy p;
    FpSnapshot s;
    FpMemory m = setup_policy(&p, &s);
    attach(true);
    _Alignas(16) uint8_t scratch[0x300] = {0};
    float native[4] = {.2f, .3f, .4f, 77};
    branch(&p, scratch, native);
    test_now += 150;
    branch(&p, scratch, native);
    delayed_policy = &p;
    delayed_memory = &m;
    delayed_family = family;
    test_before_lock = delayed_reopen;
    uint32_t unit_ref = s.viewmodel;
    unsigned before_calls = calls, before_getters = getter_calls;
    const float rotation[4] = {0, 0, 0, 1};
    if (family == 0) {
      CHECK(getter_call(m.game + 0x1287a44, (uintptr_t)scratch, unit_ref, 50) ==
            getter_value);
      CHECK(getter_calls == before_getters + 1 && calls == before_calls);
    } else {
      const float *input =
          family == 1 ? (const float *)(scratch + 0x200) : rotation;
      if (family == 1)
        position_call_stack(m.game + 0x1288424, (uintptr_t)scratch, unit_ref, 5,
                            input);
      else
        rotation_call(m.game + 0x12890df, unit_ref, 5, input);
      CHECK(calls == before_calls + 1 && getter_calls == before_getters);
      CHECK(received_pointer == input && !memcmp(received, input, 16));
    }
    /* Rejection must not clear successor depth readiness, reset its scalar,
     * alter carry/baselines or count an old callback as a fresh pose. */
    CHECK(!memcmp(&p, &successor_copy, sizeof p));
    s = next_request(&m, 4);
    CHECK(fp_pose_policy_close(&p));
  }
}
int main(void) {
  depth_availability_cases();
  delayed_callback_cases();
  FpPosePolicy p;
  FpSnapshot s;
  FpMemory m = setup_policy(&p, &s);
  _Alignas(16) uint8_t scratch[0x300] = {0};
  float native[4] = {.2f, .3f, .4f, 77};
  /* Pending gun/camera links must not touch the branch or start its clock. */
  attach(false);
  branch(&p, scratch, native);
  CHECK(received_pointer == (const float *)(scratch + 0x200));
  CHECK(!memcmp(received, native, 16) && !p.transition_ready &&
        !p.applied_ready);
  test_now += 2000;
  attach(true);
  branch(&p, scratch, native);
  close_float(p.weight, 0);
  CHECK(!memcmp(received, native, 16));
  test_now += 75;
  branch(&p, scratch, native);
  close_float(p.weight, .5f);
  close_float(received[0], .275f);
  close_float(received[2], .34f);
  test_now += 75;
  native[0] = .8f;
  branch(&p, scratch, native);
  close_float(received[0], .95f);
  close_float(received[2], .28f);
  /* Native root0 always keeps its exact input address and all four words. */
  float root[4] = {.1f, .5f, .2f, 99};
  position_call_stack(m.game + 0x12887cd, (uintptr_t)scratch, s.viewmodel, 0,
                      root);
  CHECK(received_pointer == root && received_node == 0 &&
        !memcmp(received, root, 16));
  p.ads = (FpPlacement){.05, .04, .03, 0, 0, 0};
  depth_sample(&p, scratch);
  branch(&p, scratch, native);
  close_float(received[1], .3f); /* first availability fades in from zero */
  test_now += 150;
  depth_sample(&p, scratch);
  branch(&p, scratch, native);
  close_float(received[1], .1f);
  CHECK(p.depth_ready && !p.depth.carrier_ready);
  float before[4];
  memcpy(before, received, 16);
  /* An instantaneous ADS flag must not instantly discard hip depth. */
  set_ads(&s, true);
  branch(&p, scratch, native);
  CHECK(!memcmp(before, received, 16));
  close_float(p.weight, 1);
  test_now += 75;
  branch(&p, scratch, native);
  close_float(p.weight, .5f);
  close_float(received[0], .9f);
  close_float(received[1], .22f);
  close_float(received[2], .355f);
  /* Reversals continue at the last applied point, including repeated reversals.
   */
  memcpy(before, received, 16);
  set_ads(&s, false);
  branch(&p, scratch, native);
  CHECK(!memcmp(before, received, 16));
  test_now += 75;
  branch(&p, scratch, native);
  close_float(p.weight, .75f);
  memcpy(before, received, 16);
  set_ads(&s, true);
  branch(&p, scratch, native);
  CHECK(!memcmp(before, received, 16));
  test_now += 150;
  branch(&p, scratch, native);
  close_float(p.weight, 0);
  close_float(received[0], .85f);
  close_float(received[1], .34f);
  close_float(received[2], .43f);
  /* Stale depth expires invisibly during a long native ADS. Returning data
   * halfway through release must not inject its complete correction at once. */
  test_now += 1000;
  branch(&p, scratch, native);
  CHECK(!p.depth_ready);
  close_float(p.depth_response.value, 0);
  set_ads(&s, false);
  branch(&p, scratch, native);
  test_now += 75;
  branch(&p, scratch, native);
  close_float(p.weight, .5f);
  memcpy(before, received, 16);
  depth_sample(&p, scratch);
  branch(&p, scratch, native);
  CHECK(!memcmp(before, received, 16));
  set_ads(&s, true);
  branch(&p, scratch, native);
  CHECK(!memcmp(before, received, 16));
  test_now += 75;
  branch(&p, scratch, native);
  close_float(p.weight, .25f);
  close_float(p.depth_response.value, -.1f);
  close_float(received[1], .305f);
  memcpy(before, received, 16);
  set_ads(&s, false);
  branch(&p, scratch, native);
  CHECK(!memcmp(before, received, 16));
  test_now += 75;
  branch(&p, scratch, native);
  close_float(p.weight, .625f);
  close_float(received[1], .19f);
  set_ads(&s, true);
  branch(&p, scratch, native);
  test_now += 150;
  branch(&p, scratch, native);
  close_float(p.weight, 0);
  /* Authored angular offsets use the same branch scalar, atop native rotation.
   */
  p.placement.roll = 30;
  p.ads.roll = 10;
  float rotation[4] = {0, 0, 0, 1};
  live_begin = (uintptr_t)rotation;
  live_end = live_begin + 16;
  rotation_call(m.game + 0x12890df, s.viewmodel, 5, rotation);
  close_float(received[1], sinf(5 * .01745329251994329577f));
  float coupling = 1;
  put(p.request + 0x98, &coupling, 4);
  rotation_call(m.game + 0x12890df, s.viewmodel, 5, rotation);
  CHECK(received_pointer == rotation && !memcmp(received, rotation, 16));
  /* ADS exit creates a new native request. Handoff must neither restore the
   * retired branch nor seed the new one at full hip displacement. */
  set_ads(&s, false);
  s = next_request(&m, 4);
  unsigned previous_calls = calls;
  CHECK(fp_pose_policy_handoff(&p));
  CHECK(calls == previous_calls && p.carry.valid);
  CHECK(!p.token && !p.shifted && !p.rotated);
  close_float(p.carry.weight, 0);
  test_now += 20;
  reopen(&p, &m, &s);
  attach(true);
  float new_native[4] = {3, 4, 5, 88};
  branch(&p, scratch, new_native);
  close_float(received[0], 3.05f);
  close_float(received[1], 4.04f);
  close_float(received[2], 5.03f);
  close_float(p.weight, 0);
  test_now += 75;
  branch(&p, scratch, new_native);
  close_float(p.weight, .5f);
  close_float(received[0], 3.1f); /* .15 hip + .05 ADS, mixed at .5 */
  /* Mid-transition reentry preserves its scalar, not the old native pose. */
  s = next_request(&m, 4);
  previous_calls = calls;
  cas_blocked = true;
  CHECK(!fp_pose_policy_handoff(&p));
  CHECK(p.carry.valid);
  cas_blocked = false;
  CHECK(fp_pose_policy_handoff(&p));
  CHECK(calls == previous_calls && p.carry.valid);
  close_float(p.carry.weight, .5f);
  test_now += 10;
  reopen(&p, &m, &s);
  attach(true);
  new_native[0] = 6;
  new_native[1] = 7;
  branch(&p, scratch, new_native);
  close_float(p.weight, .5f);
  close_float(received[0], 6.1f);
  /* A stale seed is refused even if all owner metadata remains identical. */
  s = next_request(&m, 4);
  CHECK(fp_pose_policy_handoff(&p));
  test_now += 501;
  reopen(&p, &m, &s);
  attach(true);
  branch(&p, scratch, new_native);
  close_float(p.weight, 0);
  CHECK(!memcmp(received, new_native, 16));
  /* Changed descriptor bytes invalidate a seed even when the entity/Unit
   * IDs remain equal and the current pool/world records agree. */
  test_now += 75;
  branch(&p, scratch, new_native);
  close_float(p.weight, .5f);
  s = next_request(&m, 4);
  CHECK(fp_pose_policy_handoff(&p));
  CHECK(p.carry.valid);
  n32(UINT64_C(0x400000000) + FP_ENTITY_ROWS + 24, 34);
  n32(UINT64_C(0xa00000400), 34);
  test_now += 10;
  reopen(&p, &m, &s);
  branch(&p, scratch, new_native);
  close_float(p.weight, 0);
  CHECK(!memcmp(received, new_native, 16));
  /* A changed gun Unit object/registry generation also invalidates a fresh
   * seed. */
  test_now += 75;
  branch(&p, scratch, new_native);
  close_float(p.weight, .5f);
  s = next_request(&m, 4);
  CHECK(fp_pose_policy_handoff(&p));
  CHECK(p.carry.valid);
  const uintptr_t old_gun = UINT64_C(0x2300000000),
                  replacement = UINT64_C(0x2300010000);
  uint8_t gun_copy[0x360] = {0};
  /* Preserve just the fields needed by graph/Unit validation in the fake. */
  const size_t fields[][2] = {{0, 8},    {8, 4},    {0x18, 8}, {0x70, 4},
                              {0x78, 8}, {0x80, 8}, {0x88, 8}, {0x98, 8},
                              {0xa0, 8}, {0x1d0, 8}};
  for (unsigned i = 0; i < sizeof fields / sizeof fields[0]; i++)
    CHECK(read_fake(&f, old_gun + fields[i][0], gun_copy + fields[i][0],
                    fields[i][1]));
  put(replacement, gun_copy, sizeof gun_copy);
  p64(UINT64_C(0xb00002000) + 501 * 8, replacement);
  test_now += 10;
  reopen(&p, &m, &s);
  branch(&p, scratch, new_native);
  close_float(p.weight, 0);
  CHECK(!memcmp(received, new_native, 16));
  /* Explicit disable destroys carry; a wrong caller never changes its pointer.
   */
  s = next_request(&m, 4);
  CHECK(fp_pose_policy_handoff(&p));
  CHECK(p.carry.valid);
  CHECK(fp_pose_policy_close(&p));
  CHECK(!p.carry.valid);
  position_call(m.game + 0x1288423, s.viewmodel, 5, root);
  CHECK(received_pointer == root && !memcmp(received, root, 16));
  /* Hard close after native retirement owns no old branch/root transform. */
  m = setup_policy(&p, &s);
  attach(true);
  branch(&p, scratch, native);
  test_now += 150;
  branch(&p, scratch, native);
  CHECK(p.shifted);
  s = next_request(&m, 4);
  previous_calls = calls;
  CHECK(fp_pose_policy_close(&p));
  CHECK(calls == previous_calls && !p.carry.valid);
  atomic_store(&active, NULL);
  printf("C pose policy: %u checks passed; root pointer exact, pending links, "
         "shared ADS depth/placement, reversals, fresh scalar-only reentry, "
         "stale/changed gun refusal and handoff cleanup\n",
         checks);
  return 0;
}
