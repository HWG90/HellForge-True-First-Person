/* Real policy and dispatch lease; private fake native memory and original
 * setter collector only. No game process, game call or installed DLL. */
#define TFP_VISIBILITY_TEST 1
#define fp_slot_read visibility_test_slot_read
#define fp_slot_cas visibility_test_slot_cas
#include "../src/visibility_policy.c"
#include "fake_native.h"
#include <assert.h>

#define GUN UINT64_C(0x2300000000)
#define GUN_POOL UINT64_C(0x2400000000)
#define RIG UINT64_C(0x1100000000)
#define RIG_POOL UINT64_C(0x1200000000)
#define CAMERA UINT64_C(0xe00000000)
#define CAMERA_POOL UINT64_C(0xf00000000)
#define CHILD UINT64_C(0x2600000000)
#define GRANDCHILD UINT64_C(0x2700000000)
#define REGISTRY UINT64_C(0xb00000000)
static unsigned checks, calls;
static uint32_t received_ref;
static int received_visible;
static bool received_extra;
static unsigned cas_mode;
static bool delay_reopen;
static FpVisibilityPolicy *delayed_policy;
static FpMemory delayed_memory;
#define CHECK(v) do { checks++; assert(v); } while (0)

static void collect(uint32_t ref, int visible, bool extra) {
  calls++;
  received_ref = ref;
  received_visible = visible;
  received_extra = extra;
}
bool visibility_test_slot_read(void *user, uintptr_t slot, uintptr_t *out) {
  (void)user;
  return read_fake(&f, slot, out, sizeof *out);
}
FpCasResult visibility_test_slot_cas(void *user, uintptr_t slot,
                                    uintptr_t expected, uintptr_t value,
                                    uintptr_t *observed) {
  if (!visibility_test_slot_read(user, slot, observed) || cas_mode == 1)
    return FP_CAS_UNCERTAIN;
  if (*observed != expected)
    return FP_CAS_DIFFERENT;
  p64(slot, value);
  if (cas_mode == 2) {
    cas_mode = 1;
    return FP_CAS_UNCERTAIN;
  }
  return FP_CAS_CHANGED;
}
static void code(uintptr_t at, const char *hex) {
  uint8_t bytes[128];
  unsigned count = 0;
  while (hex[count * 2]) {
    unsigned value;
    assert(count < sizeof bytes);
    assert(sscanf(hex + count * 2, "%2x", &value) == 1);
    bytes[count++] = (uint8_t)value;
  }
  put(at, bytes, count);
}
static void links(uintptr_t object) {
  p64(object + 0x1d8, 0);
  p64(object + 0x1e0, 0);
}
/* Child units need registry/Unit proofs but no fabricated Lua handles. */
static void child(const FpMemory *m, uint32_t ref, uintptr_t object,
                   uintptr_t parent, uintptr_t sibling) {
  uint8_t data[0x1e8] = {0};
  uintptr_t vt = GUN_POOL + 0x100, world = UINT64_C(0x400000000);
  memcpy(data, &vt, 8);
  memcpy(data + 8, &ref, 4);
  memcpy(data + 0x18, &world, 8);
  memcpy(data + 0x1d0, &parent, 8);
  memcpy(data + 0x1e0, &sibling, 8);
  put(object, data, sizeof data);
  p64(REGISTRY + 0x2000 + (uintptr_t)(ref & 0x3fffff) * 8, object);
  uint8_t generation = (uint8_t)(ref >> 22);
  put(REGISTRY + 0x1000 + (ref & 0x3fffff), &generation, 1);
  CHECK(fp_unit_object(m, ref) == object);
}
static FpMemory setup_policy(FpVisibilityPolicy *p, FpSnapshot *normal) {
  atomic_store(&active, NULL);
  cas_mode = 0;
  delay_reopen = false;
  FpMemory m = native_setup();
  p64(GUN + 0x1d0, RIG);
  p64(GUN + 0x78, RIG_POOL + 0x3000);
  n32(GUN_POOL + 0x2000, UINT32_C(0x00080002));
  p64(CAMERA + 0x1d0, RIG);
  p64(CAMERA + 0x78, RIG_POOL + 0x3000);
  n32(CAMERA_POOL + 0x2000, UINT32_C(0x00320002));
  links(GUN);
  const uintptr_t root = UINT64_C(0x1500000000), api = UINT64_C(0x1500010000);
  p64(m.game + 0x3326308, root);
  p64(root + 0x18, api);
  p64(api + 0x658, m.exe + 0x205680);
  code(m.exe + 0x205680,
       "48895c24084889742410574883ec20410fb6f08bfa8bd9e82482e9ff"
       "4c8b5018b801000000f0410fc182380017008bc04c6bc858"
       "43c784118800010015000000");
  code(m.exe + 0x12f64c,
       "8b0fe86de2f6ff488bc84885c00f8417f9ffff837f0800488b00"
       "440fb64f040f95c2440fb6470cff90d0000000");
  CHECK(fp_snapshot(&m, true, normal, NULL, 0));
  CHECK(normal->kind == 4);
  fp_visibility_policy_init(p);
  char error[160];
  CHECK(fp_visibility_policy_open(p, &m, normal, error, sizeof error));
  atomic_store(&native_setter, collect);
  CHECK(p->token && !p->wanted && !atomic_load(&p->candidate_count));
  return m;
}
static void publish(FpVisibilityPolicy *p, FpSnapshot *s, bool wanted) {
  CHECK(fp_snapshot(&p->memory, true, s, NULL, 0));
  CHECK(fp_visibility_policy_update(p, p->token, s, wanted));
}
static void enable(FpVisibilityPolicy *p, FpSnapshot *s) {
  n32(p->origin.request, 6);
  publish(p, s, true);
  CHECK(p->wanted && atomic_load(&p->candidate_count));
}
static void invoke(FpVisibilityPolicy *p, uint32_t ref, int visible, bool extra) {
  uint64_t epoch = atomic_load(&callback_generation);
  visibility_call(epoch, p->memory.game + 0x9aa0c6, ref, visible, extra);
}
static void forwarded(FpVisibilityPolicy *p, uint32_t ref, int visible,
                       bool extra) {
  unsigned before = calls;
  invoke(p, ref, visible, extra);
  CHECK(calls == before + 1);
  CHECK(received_ref == ref && received_visible == visible &&
        received_extra == extra);
}
static void suppressed(FpVisibilityPolicy *p, uint32_t ref, bool extra) {
  unsigned before = calls;
  uint64_t counter = atomic_load(&p->suppressed);
  invoke(p, ref, 0, extra);
  CHECK(calls == before && atomic_load(&p->suppressed) == counter + 1);
}
void fp_visibility_before_lock(void) {
  if (!delay_reopen)
    return;
  delay_reopen = false;
  FpVisibilityPolicy *p = delayed_policy;
  CHECK(fp_visibility_policy_close(p));
  n32(p->origin.request, 4);
  FpSnapshot normal, current;
  CHECK(fp_snapshot(&delayed_memory, true, &normal, NULL, 0));
  char error[160];
  CHECK(fp_visibility_policy_open(p, &delayed_memory, &normal, error,
                                   sizeof error));
  atomic_store(&native_setter, collect);
  enable(p, &current);
}
typedef struct Mutation {
  uintptr_t trigger, target;
  uint64_t value;
  size_t size;
  bool changed;
} Mutation;
static bool changing_read(void *user, uintptr_t at, void *out, size_t size) {
  Mutation *change = user;
  bool ok = read_fake(&f, at, out, size);
  if (ok && at == change->trigger && !change->changed) {
    change->changed = true;
    put(change->target, &change->value, change->size);
  }
  return ok;
}

int main(void) {
  FpVisibilityPolicy p;
  FpSnapshot s;
  FpMemory m = setup_policy(&p, &s);
  forwarded(&p, s.weapon_unit, 0, false); /* open in kind4 never freezes TP */
  n32(s.request, 6);
  forwarded(&p, s.weapon_unit, 0, true); /* no publication yet */
  publish(&p, &s, true);
  CHECK(p.count == 1);
  for (unsigned i = 0; i < 8; i++)
    suppressed(&p, s.weapon_unit, (i & 1) != 0);
  forwarded(&p, s.weapon_unit, 1, true);
  forwarded(&p, s.weapon_unit, -17, false);
  forwarded(&p, 901, 0, true);
  forwarded(&p, s.body, 0, true);
  forwarded(&p, s.viewmodel, 0, false);
  forwarded(&p, s.camera_unit, 0, true);
  unsigned reads = f.reads;
  forwarded(&p, 999, 0, true);
  CHECK(f.reads == reads); /* cheap unrelated Unit filter performs no reads */
  CHECK(!fp_visibility_policy_update(&p, p.token + 1, &s, false));
  suppressed(&p, s.weapon_unit, false);
  uint8_t firing_flags[24] = {0};
  firing_flags[15] = 2; /* legitimate reload animation is still current FP */
  put(s.avatar_owner + FP_AVATAR_FLAGS, firing_flags, sizeof firing_flags);
  suppressed(&p, s.weapon_unit, false);
  publish(&p, &s, false);
  CHECK(!p.wanted && !atomic_load(&p.candidate_count));
  forwarded(&p, s.weapon_unit, 0, false);
  publish(&p, &s, true);
  CHECK(fp_visibility_policy_close(&p));
  CHECK(!p.token && !atomic_load(&active));
  forwarded(&p, s.weapon_unit, 0, true);

  /* Every linked Unit descendant is eligible, without a weapon-name list. */
  m = setup_policy(&p, &s);
  child(&m, 901, CHILD, GUN, 0);
  child(&m, 902, GRANDCHILD, CHILD, 0);
  p64(GUN + 0x1d8, CHILD);
  p64(CHILD + 0x1d8, GRANDCHILD);
  enable(&p, &s);
  CHECK(p.count == 3);
  suppressed(&p, 501, false);
  suppressed(&p, 901, true);
  suppressed(&p, 902, false);
  p64(CHILD + 0x1d0, RIG); /* published ref alone cannot confer ownership */
  forwarded(&p, 901, 0, true);
  p64(CHILD + 0x1d0, GUN);
  p64(GUN + 0x1d8, 0); /* orphan parent pointer is insufficient */
  forwarded(&p, 901, 0, false);
  p64(GUN + 0x1d8, CHILD);
  p64(CHILD + 0x18, UINT64_C(0x450000000));
  forwarded(&p, 901, 0, true);
  p64(CHILD + 0x18, UINT64_C(0x400000000));
  uint8_t retired = 1;
  put(REGISTRY + 0x1000 + 901, &retired, 1);
  forwarded(&p, 901, 0, true);
  retired = 0;
  put(REGISTRY + 0x1000 + 901, &retired, 1);
  suppressed(&p, 901, false);
  CHECK(fp_visibility_policy_close(&p));

  /* Same registry index, new generation and object cannot reuse old authority. */
  m = setup_policy(&p, &s);
  child(&m, 901, CHILD, GUN, 0);
  p64(GUN + 0x1d8, CHILD);
  enable(&p, &s);
  const uint32_t successor_ref = (1u << 22) | 901u;
  child(&m, successor_ref, GRANDCHILD, GUN, 0);
  p64(GUN + 0x1d8, GRANDCHILD);
  forwarded(&p, 901, 0, true);
  forwarded(&p, successor_ref, 0, false);
  publish(&p, &s, true);
  suppressed(&p, successor_ref, true);
  forwarded(&p, 901, 0, false);
  CHECK(fp_visibility_policy_close(&p));

  /* Attached native ADS and input masks retain protection. Action camera,
   * vehicle, foreign request and incomplete native links still forward. */
  for (unsigned mode = 0; mode < 11; mode++) {
    m = setup_policy(&p, &s);
    enable(&p, &s);
    if (mode == 0)
      n32(s.request, 4);
    else if (mode == 1)
      n32(s.request, 7);
    else if (mode == 2) {
      uint8_t controls[6] = {0, 0, 0, 0, 1, 0};
      put(s.controller, controls, sizeof controls);
    } else if (mode == 3) {
      uint8_t controls[6] = {0, 0, 0, 0, 0, 1};
      put(s.controller, controls, sizeof controls);
    } else if (mode == 4)
      n32(s.controller, 2);
    else if (mode == 5) {
      uint8_t flags[24] = {0};
      flags[14] = 4;
      put(s.avatar_owner + FP_AVATAR_FLAGS, flags, sizeof flags);
    } else if (mode == 6)
      n32(s.request + 4, s.avatar + 1);
    else if (mode == 7)
      n32(GUN_POOL + 0x2000, UINT32_C(0x00070002));
    else if (mode == 8)
      p64(CAMERA + 0x1d0, 0);
    else if (mode == 9)
      f.denied = s.weapon_descriptor;
    else
      n32(s.controller, 64);
    if (mode == 2 || mode == 4 || mode == 10)
      suppressed(&p, 501, (mode & 1) != 0);
    else
      forwarded(&p, 501, 0, (mode & 1) != 0);
    CHECK(fp_visibility_policy_close(&p));
  }

  m = setup_policy(&p, &s);
  enable(&p, &s);
  p64(m.game + FP_PLAYER, UINT64_C(0x650000000));
  n32(UINT64_C(0x650000088), 1);
  n32(UINT64_C(0x6500003a8), 301);
  forwarded(&p, 501, 0, true); /* new local player with otherwise same body */
  CHECK(fp_visibility_policy_close(&p));

  /* Root plus 32 linked Units fits; one more refuses, without truncating a
   * partially proved tree into an apparently complete candidate list. */
  m = setup_policy(&p, &s);
  for (unsigned i = 0; i < 32; i++) {
    uintptr_t object = CHILD + (uintptr_t)i * 0x1000;
    uintptr_t next = i == 31 ? 0 : object + 0x1000;
    child(&m, 901 + i, object, GUN, next);
  }
  p64(GUN + 0x1d8, CHILD);
  enable(&p, &s);
  CHECK(p.count == FP_VISIBILITY_UNITS);
  suppressed(&p, 932, true);
  child(&m, 933, CHILD + 32 * 0x1000, GUN, 0);
  p64(CHILD + 31 * 0x1000 + 0x1e0, CHILD + 32 * 0x1000);
  publish(&p, &s, true);
  CHECK(!p.wanted && !atomic_load(&p.candidate_count));
  forwarded(&p, 501, 0, true);
  CHECK(fp_visibility_policy_close(&p));

  /* Replacing a held firearm invalidates old candidates immediately, then
   * publishes the newly qualified firearm after update. */
  m = setup_policy(&p, &s);
  enable(&p, &s);
  unit(&m, 502, CHILD, 1, GRANDCHILD);
  links(CHILD);
  p64(CHILD + 0x1d0, RIG);
  p64(CHILD + 0x78, RIG_POOL + 0x3000);
  n32(GRANDCHILD + 0x2000, UINT32_C(0x00080002));
  uint32_t weapon[6] = {33, 44, 202, 502, 602, 1};
  map(s.world + FP_ENTITY_MAP, s.world + 0x200, 202, 1);
  put(s.world + FP_ENTITY_ROWS + 24, weapon, sizeof weapon);
  map(UINT64_C(0xa00000030), UINT64_C(0xa00000200), 202, 0);
  put(UINT64_C(0xa00000400), weapon, 20);
  n32(UINT64_C(0x900000400), 202);
  n32(s.request + 8, 202);
  forwarded(&p, 501, 0, true);
  forwarded(&p, 502, 0, false);
  publish(&p, &s, true);
  CHECK(p.count == 1 && p.units[0].ref == 502);
  suppressed(&p, 502, false);
  forwarded(&p, 501, 0, true);
  CHECK(fp_visibility_policy_close(&p));

  /* A read that initially succeeds cannot authorize a changed parent. */
  m = setup_policy(&p, &s);
  child(&m, 901, CHILD, GUN, 0);
  p64(GUN + 0x1d8, CHILD);
  enable(&p, &s);
  Mutation change = {CHILD + 0x1d0, CHILD + 0x1d0, RIG, 8, false};
  p.memory.read = changing_read;
  p.memory.user = &change;
  forwarded(&p, 901, 0, true);
  CHECK(change.changed);
  CHECK(fp_visibility_policy_close(&p));

  /* Cycles and excessive depth refuse the entire candidate publication. */
  m = setup_policy(&p, &s);
  child(&m, 901, CHILD, GUN, CHILD);
  p64(GUN + 0x1d8, CHILD);
  n32(s.request, 6);
  publish(&p, &s, true);
  CHECK(!p.wanted && !atomic_load(&p.candidate_count));
  forwarded(&p, 501, 0, true);
  CHECK(fp_visibility_policy_close(&p));
  m = setup_policy(&p, &s);
  uintptr_t parent = GUN;
  for (unsigned i = 0; i < 9; i++) {
    uintptr_t object = CHILD + (uintptr_t)i * 0x1000;
    child(&m, 901 + i, object, parent, 0);
    p64(parent + 0x1d8, object);
    parent = object;
  }
  n32(s.request, 6);
  publish(&p, &s, true);
  CHECK(!p.wanted && !atomic_load(&p.candidate_count));
  CHECK(fp_visibility_policy_close(&p));

  /* Cached callback after foreign slot successor never suppresses or restores
   * the successor. A foreign caller also forwards every original argument. */
  m = setup_policy(&p, &s);
  enable(&p, &s);
  unsigned before = calls;
  visibility_call(atomic_load(&callback_generation), UINT64_C(0x290000f000),
                  501, 0, true);
  CHECK(calls == before + 1 && received_ref == 501 && !received_visible &&
        received_extra);
  before = calls;
  visibility_call(atomic_load(&callback_generation), m.exe + 0x4129eb,
                  501, 0, true);
  /* Synthetic EXE-range acceptance. The real Lua binding calls the native
   * function directly, bypassing this slot; cached model repair covers it. */
  CHECK(calls == before);
  p64(p.slot, UINT64_C(0x290000f000));
  forwarded(&p, 501, 0, false);
  CHECK(!fp_visibility_policy_update(&p, p.token, &s, true));
  CHECK(fp_visibility_policy_close(&p));
  uintptr_t actual = 0;
  CHECK(fp_pointer(&m, p.slot, &actual) && actual == UINT64_C(0x290000f000));

  m = setup_policy(&p, &s);
  enable(&p, &s);
  cas_mode = 1;
  CHECK(!fp_visibility_policy_close(&p) && p.token && !p.wanted);
  forwarded(&p, 501, 0, true);
  cas_mode = 0;
  CHECK(fp_visibility_policy_close(&p) && !p.token);

  /* Open also supports a qualified pre-existing kind6 view, but rejects a
   * foreign slot, wrong native witness, foreign snapshot or action domain. */
  m = setup_policy(&p, &s);
  CHECK(fp_visibility_policy_close(&p));
  n32(s.request, 6);
  CHECK(fp_snapshot(&m, true, &s, NULL, 0));
  char open_error[160];
  CHECK(fp_visibility_policy_open(&p, &m, &s, open_error, sizeof open_error));
  atomic_store(&native_setter, collect);
  publish(&p, &s, true);
  suppressed(&p, 501, true);
  CHECK(fp_visibility_policy_close(&p));
  for (unsigned mode = 0; mode < 4; mode++) {
    m = setup_policy(&p, &s);
    CHECK(fp_visibility_policy_close(&p));
    if (mode == 0)
      p64(p.slot, UINT64_C(0x290000f000));
    else if (mode == 1) {
      uint8_t wrong = 0x90;
      put(m.exe + 0x205680, &wrong, 1);
    } else if (mode == 2)
      s.player++;
    else {
      uint8_t action[6] = {0, 0, 0, 0, 0, 1};
      put(s.controller, action, sizeof action);
    }
    CHECK(!fp_visibility_policy_open(&p, &m, &s, open_error,
                                     sizeof open_error));
    CHECK(!p.token && !atomic_load(&active));
  }

  m = setup_policy(&p, &s);
  CHECK(fp_visibility_policy_close(&p));
  cas_mode = 2;
  char error[160];
  CHECK(!fp_visibility_policy_open(&p, &m, &s, error, sizeof error));
  CHECK(p.token && !atomic_load(&p.lease.armed));
  atomic_store(&native_setter, collect);
  forwarded(&p, 501, 0, true);
  cas_mode = 0;
  CHECK(fp_visibility_policy_close(&p) && !p.token);

  /* Actual wrapper entry epoch is captured before the active/original loads.
   * Delay across close/reopen same p, same camera, same Unit must forward. */
  m = setup_policy(&p, &s);
  enable(&p, &s);
  uint64_t token = p.token;
  delayed_policy = &p;
  delayed_memory = m;
  delay_reopen = true;
  forwarded(&p, 501, 0, true);
  CHECK(!delay_reopen && p.token != token && p.wanted);
  suppressed(&p, 501, false);
  CHECK(fp_visibility_policy_close(&p));
  printf("C visibility: %u checks passed; native +658 whole-hide protection, "
         "current gun/descendants, attached ADS/input masks, action refusal, "
         "switch/retirement/closing proofs, epoch and CAS cleanup\n", checks);
  return 0;
}
