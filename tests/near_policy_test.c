/* Real policy/open/close code with a private fake native dispatch table. */
#define TFP_NEAR_TEST 1
#define fp_slot_read near_test_slot_read
#define fp_slot_cas near_test_slot_cas
#include "../src/near_policy.c"
#include "fake_native.h"
#include <assert.h>

static float received;
static unsigned calls, checks;
static bool cas_blocked, newer_on_restore;
static bool reopen_before_lock;
static FpNearPolicy *delayed_policy;
static FpMemory delayed_memory;
#define CHECK(v) do { checks++; assert(v); } while (0)
static void collect(void *camera, float value) {
  received = value;
  calls++;
  put((uintptr_t)camera + 0x28, &value, 4);
  if (newer_on_restore && value == .28111f) {
    float newer = .07f;
    put((uintptr_t)camera + 0x28, &newer, 4);
  }
}
bool near_test_slot_read(void *user, uintptr_t slot, uintptr_t *out) {
  (void)user;
  return read_fake(&f, slot, out, sizeof *out);
}
FpCasResult near_test_slot_cas(void *user, uintptr_t slot, uintptr_t expected,
                               uintptr_t value, uintptr_t *observed) {
  if (cas_blocked || !near_test_slot_read(user, slot, observed))
    return FP_CAS_UNCERTAIN;
  if (*observed != expected)
    return FP_CAS_DIFFERENT;
  p64(slot, value);
  return FP_CAS_CHANGED;
}
static void code(uintptr_t at, const char *hex) {
  uint8_t bytes[128];
  unsigned n = 0;
  for (; hex[n * 2]; n++) {
    unsigned v;
    assert(sscanf(hex + n * 2, "%2x", &v) == 1);
    bytes[n] = (uint8_t)v;
  }
  put(at, bytes, n);
}
static FpMemory setup_policy(FpNearPolicy *p, FpSnapshot *s) {
  atomic_store(&active, NULL);
  cas_blocked = newer_on_restore = false;
  FpMemory m = native_setup();
  for (unsigned i = 0; i < FP_NATIVE_SITE_COUNT; i++) {
    const FpNativeSite *site = fp_native_sites + i;
    code((site->exe ? m.exe : m.game) + site->rva, site->hex);
  }
  const uintptr_t root = UINT64_C(0x1500000000), api = UINT64_C(0x1500010000);
  p64(m.game + 0x3326308, root);
  p64(root + 0x20, api);
  p64(api + 0x68, m.exe + 0x30d960);
  p64(api + 0x58, m.exe + 0x30d940);
  CHECK(fp_snapshot(&m, true, s, NULL, 0));
  fp_near_policy_init(p);
  char error[160];
  CHECK(fp_near_policy_open(p, &m, s, error, sizeof error));
  atomic_store(&native_setter, collect);
  n32(s->request, 6);
  return m;
}
static void native(FpNearPolicy *p, float value) {
  near_call(p->memory.game + 0x127bf60, (void *)p->origin.camera, value);
}
static float property(FpNearPolicy *p) {
  float value = 0;
  CHECK(fp_read(&p->memory, p->origin.camera + 0x28, &value, 4));
  return value;
}
static void flags(const FpSnapshot *s, bool shoulder, bool seated) {
  uint8_t value[24] = {0};
  value[10] = shoulder ? 4 : 0;
  value[15] = shoulder ? 32 : 0;
  value[14] = seated ? 4 : 0;
  put(s->avatar_owner + FP_AVATAR_FLAGS, value, sizeof value);
}
static void controls(const FpSnapshot *s, uint32_t mask, bool ads, bool action) {
  uint8_t value[6] = {0};
  memcpy(value, &mask, 4);
  value[4] = ads;
  value[5] = action;
  put(s->controller, value, sizeof value);
}
static void unchanged(FpNearPolicy *p, float value) {
  native(p, value);
  CHECK(!memcmp(&received, &value, sizeof value));
  CHECK(!p->property_owned);
}
void fp_near_before_lock(void) {
  if (!reopen_before_lock)
    return;
  reopen_before_lock = false;
  FpNearPolicy *p = delayed_policy;
  CHECK(fp_near_policy_close(p));
  n32(p->origin.request, 4);
  FpSnapshot s;
  CHECK(fp_snapshot(&delayed_memory, false, &s, NULL, 0));
  char error[160];
  CHECK(fp_near_policy_open(p, &delayed_memory, &s, error, sizeof error));
  atomic_store(&native_setter, collect);
  CHECK(fp_near_policy_set(p, p->token, .1f, 0));
  n32(s.request, 6);
  native(p, .36f);
  CHECK(p->property_owned && p->baseline == .36f && p->written == .1f);
}

int main(void) {
  FpNearPolicy p;
  FpSnapshot s;
  FpMemory m = setup_policy(&p, &s);
  CHECK(fp_near_policy_set(&p, p.token, .05f, 0));
  native(&p, .28111f);
  CHECK(received == .05f && p.property_owned && p.baseline == .28111f);
  CHECK(property(&p) == .05f);
  native(&p, .33f);
  CHECK(received == .05f && p.baseline == .33f);
  unsigned before = calls;
  CHECK(fp_near_policy_close(&p));
  CHECK(calls == before + 1 && property(&p) == .33f && !p.token);

  /* A newer native .05 write owns the property even if it equals our target.
   * Neither native full ADS nor an equal hip input may leave stale ownership. */
  for (unsigned ads = 0; ads < 2; ads++) {
    m = setup_policy(&p, &s);
    native(&p, .28111f);
    CHECK(p.property_owned);
    controls(&s, 0, ads != 0, false);
    unchanged(&p, .05f);
    before = calls;
    CHECK(fp_near_policy_close(&p));
    CHECK(calls == before && property(&p) == .05f && !p.token);
  }

  m = setup_policy(&p, &s);
  CHECK(fp_near_policy_set(&p, p.token, .4f, 0));
  native(&p, .28111f);
  CHECK(received == .4f && p.baseline == .28111f);
  CHECK(!fp_near_policy_set(&p, p.token, NAN, .05f));
  CHECK(!fp_near_policy_set(&p, p.token, -.01f, .05f));
  CHECK(!fp_near_policy_set(&p, p.token, 1.01f, .05f));
  CHECK(!fp_near_policy_set(&p, p.token, .05f, INFINITY));
  CHECK(!fp_near_policy_set(&p, p.token + 1, .05f, .05f));
  CHECK(p.hip == .4f && p.aim == 0);
  flags(&s, true, false);
  unchanged(&p, .28111f);
  CHECK(fp_near_policy_set(&p, p.token, .05f, .05f));
  native(&p, .28111f);
  CHECK(received == .05f && p.property_owned);
  controls(&s, 0, true, false);
  unchanged(&p, .42f);
  controls(&s, 0, false, false);
  flags(&s, false, true);
  unchanged(&p, .28111f);
  flags(&s, false, false);
  controls(&s, 2, false, false);
  unchanged(&p, .28111f);
  controls(&s, 0, false, true);
  unchanged(&p, .28111f);
  controls(&s, 0, false, false);
  unchanged(&p, NAN);
  unchanged(&p, INFINITY);
  unchanged(&p, -.1f);
  unchanged(&p, 0);
  unchanged(&p, 1.1f);
  CHECK(fp_near_policy_set(&p, p.token, 0, 0));
  unchanged(&p, .28111f);
  CHECK(fp_near_policy_set(&p, p.token, .05f, .05f));
  unchanged(&p, .05f);
  near_call(m.exe + 0x1000, (void *)s.camera, .28111f);
  CHECK(received == .28111f && !p.property_owned);
  near_call(m.game + FP_GAME_SIZE, (void *)s.camera, .28111f);
  CHECK(received == .28111f && !p.property_owned);
  near_call(m.game + 0xfff, (void *)s.camera, .28111f);
  CHECK(received == .28111f && !p.property_owned);
  native(&p, .28111f);
  near_call(m.game + 0x127bf60, (void *)(s.camera + 0x100), .2f);
  CHECK(received == .2f && p.property_owned);
  CHECK(fp_near_policy_close(&p));

  m = setup_policy(&p, &s);
  n32(s.request, 4);
  unchanged(&p, .28111f);
  n32(s.request, 7);
  unchanged(&p, .28111f);
  n32(s.request, 6);
  n32(s.request + 4, s.avatar + 1);
  unchanged(&p, .28111f);
  n32(s.request + 4, s.avatar);
  n32(s.context + 8, s.camera_unit + 1);
  unchanged(&p, .28111f);
  n32(s.context + 8, s.camera_unit);
  uint8_t retired = 1;
  put(UINT64_C(0xb00001000) + s.body, &retired, 1);
  unchanged(&p, .28111f);
  CHECK(fp_near_policy_close(&p));

  for (unsigned change = 0; change < 6; change++) {
    m = setup_policy(&p, &s);
    native(&p, .28111f);
    before = calls;
    const uintptr_t successor = UINT64_C(0x290000f000);
    if (change == 0) {
      float newer = .07f;
      put(s.camera + 0x28, &newer, 4);
    } else if (change == 1)
      p64(p.slot, successor);
    else if (change == 2) {
      n32(UINT64_C(0x400000000) + FP_ENTITY_ROWS, 12);
      n32(s.avatar_descriptor, 12);
    } else if (change == 3)
      n32(s.context + 8, s.camera_unit + 1);
    else if (change == 4)
      n32(s.request, 4);
    else
      p64(s.camera + 0x18, UINT64_C(0xc00000060));
    CHECK(fp_near_policy_close(&p));
    CHECK(calls == before && !p.token);
    if (change == 0)
      CHECK(property(&p) == .07f);
    if (change == 1) {
      uintptr_t actual = 0;
      CHECK(read_fake(&f, p.slot, &actual, 8) && actual == successor);
    }
  }

  for (unsigned missing = 0; missing < 3; missing++) {
    m = setup_policy(&p, &s);
    native(&p, .28111f);
    before = calls;
    f.denied = missing == 0 ? s.camera + 0x28
               : missing == 1 ? m.game + FP_CAMERA : p.slot;
    CHECK(!fp_near_policy_close(&p));
    CHECK(p.token && p.property_owned && calls == before);
    f.denied = 0;
    CHECK(fp_near_policy_close(&p));
    CHECK(property(&p) == .28111f && !p.token);
  }
  m = setup_policy(&p, &s);
  native(&p, .28111f);
  before = calls;
  cas_blocked = true;
  CHECK(!fp_near_policy_close(&p));
  CHECK(p.token && !p.property_owned && calls == before + 1);
  cas_blocked = false;
  CHECK(fp_near_policy_close(&p));
  CHECK(!p.token && calls == before + 1);

  m = setup_policy(&p, &s);
  native(&p, .28111f);
  newer_on_restore = true;
  CHECK(fp_near_policy_close(&p));
  CHECK(property(&p) == .07f && !p.token);

  m = setup_policy(&p, &s);
  native(&p, .28111f);
  p64(p.slot, UINT64_C(0x290000f000));
  unchanged(&p, .28111f);
  CHECK(fp_near_policy_close(&p));

  /* The callback captured its epoch before a close/reopen of the same p and
   * Camera. It must forward its old input without publishing into the new
   * lease, and that actual passthrough relinquishes the new property write. */
  m = setup_policy(&p, &s);
  native(&p, .28111f);
  uint64_t previous_token = p.token;
  delayed_policy = &p;
  delayed_memory = m;
  reopen_before_lock = true;
  native(&p, .28111f);
  CHECK(!reopen_before_lock && p.token != previous_token);
  CHECK(received == .28111f && p.baseline == .36f && !p.property_owned);
  before = calls;
  CHECK(fp_near_policy_close(&p));
  CHECK(calls == before && property(&p) == .28111f);
  atomic_store(&active, NULL);
  printf("C near clip: %u checks passed; dynamic .28111->.05 and higher clips, "
         "aim opt-in, ADS/actions/foreign pass-through, exact native restore "
         "and conditional retry cleanup\n", checks);
  return 0;
}
