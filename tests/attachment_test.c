#include "fake_native.h"

#define RIG_OBJECT UINT64_C(0x1100000000)
#define RIG_POOL UINT64_C(0x1200000000)
#define GUN_OBJECT UINT64_C(0x2300000000)
#define GUN_POOL UINT64_C(0x2400000000)
#define CAMERA_OBJECT UINT64_C(0xe00000000)
#define CAMERA_POOL UINT64_C(0xf00000000)
#define BODY_OBJECT UINT64_C(0xc00000000)
#define BODY_POOL UINT64_C(0xd00000000)
#define REGISTRY UINT64_C(0xb00000000)

static FpMemory attached(FpSnapshot *s) {
  FpMemory m = native_setup();
  n32(UINT64_C(0x300000200), 6);
  p64(GUN_OBJECT + 0x1d0, RIG_OBJECT);
  p64(GUN_OBJECT + 0x78, RIG_POOL + 0x3000);
  n32(GUN_POOL + 0x2000, UINT32_C(0x00080002));
  p64(CAMERA_OBJECT + 0x1d0, RIG_OBJECT);
  p64(CAMERA_OBJECT + 0x78, RIG_POOL + 0x3000);
  n32(CAMERA_POOL + 0x2000, UINT32_C(0x00320002));
  assert(fp_snapshot(&m, true, s, NULL, 0));
  return m;
}
static void unavailable(const FpMemory *m, const FpSnapshot *s) {
  FpGraph out, zero = {0};
  memset(&out, 0xa5, sizeof out);
  assert(!fp_rig_attachments(m, s, &out));
  assert(!memcmp(&out, &zero, sizeof out));
}

/* Mutate backing memory immediately AFTER delivering one read, so an initial
 * check can pass and the closing proof must detect the changed metadata. */
typedef struct Mutation {
  uintptr_t trigger, target;
  size_t trigger_size, target_size;
  uint64_t value;
  unsigned hits, occurrence;
} Mutation;
static bool changing_read(void *user, uintptr_t at, void *out, size_t size) {
  Mutation *change = user;
  bool ok = read_fake(&f, at, out, size);
  if (ok && at == change->trigger && size == change->trigger_size &&
      ++change->hits == change->occurrence)
    put(change->target, &change->value, change->target_size);
  return ok;
}
static void changed(uintptr_t trigger, size_t trigger_size, uintptr_t target,
                    size_t target_size, uint64_t value) {
  FpSnapshot s;
  FpMemory m = attached(&s);
  Mutation change = {trigger, target, trigger_size, target_size, value, 0, 1};
  m.read = changing_read;
  m.user = &change;
  unavailable(&m, &s);
  assert(change.hits >= 1);
}

int main(void) {
  FpSnapshot s;
  FpMemory m = attached(&s);
  FpGraph out;
  assert(fp_rig_attachments(&m, &s, &out));
  assert(out.object == RIG_OBJECT && out.ref == s.viewmodel && out.count == 63);
  assert(out.worlds == RIG_POOL + 0x3000);
  assert(f.reads < 1500); /* A bounded local proof, not a discovery loop. */

  /* Attachment readiness does not invent an ownership/ADS policy. A kind4
   * handoff may still have completed FPS links, while ordinary kind4 fails. */
  n32(s.request, 4);
  assert(fp_snapshot(&m, true, &s, NULL, 0));
  assert(fp_rig_attachments(&m, &s, &out));
  m = attached(&s);
  s.kind = 7;
  unavailable(&m, &s);
  m = attached(&s);
  s.weapon_ready = false;
  unavailable(&m, &s);
  m = attached(&s);
  s.local_request = false;
  unavailable(&m, &s);
  unavailable(NULL, &s);
  unavailable(&m, NULL);
  assert(!fp_rig_attachments(&m, &s, NULL));

  /* Engine Unit worlds can share an owner distinct from game World. */
  m = attached(&s);
  const uintptr_t objects[] = {RIG_OBJECT, GUN_OBJECT, CAMERA_OBJECT, BODY_OBJECT};
  for (unsigned i = 0; i < sizeof objects / sizeof objects[0]; i++)
    p64(objects[i] + 0x18, UINT64_C(0x450000000));
  assert(fp_rig_attachments(&m, &s, &out));

  /* Observed first-kind6 state: old rig exists, gun is still on body node84,
   * camera is unparented. Correct rig topology alone is insufficient. */
  m = attached(&s);
  p64(GUN_OBJECT + 0x1d0, BODY_OBJECT);
  p64(GUN_OBJECT + 0x78, BODY_POOL + 0x3000);
  n32(GUN_POOL + 0x2000, UINT32_C(0x00540002));
  p64(CAMERA_OBJECT + 0x1d0, 0);
  n32(CAMERA_POOL + 0x2000, 0);
  unavailable(&m, &s);
  m = attached(&s);
  p64(CAMERA_OBJECT + 0x1d0, 0);
  n32(CAMERA_POOL + 0x2000, 0);
  unavailable(&m, &s);
  m = attached(&s);
  n32(GUN_POOL + 0x2000, UINT32_C(0x00070002));
  unavailable(&m, &s);
  m = attached(&s);
  p64(GUN_OBJECT + 0x1d0, BODY_OBJECT);
  unavailable(&m, &s);
  m = attached(&s);
  p64(GUN_OBJECT + 0x78, BODY_POOL + 0x3000);
  unavailable(&m, &s);
  m = attached(&s);
  p64(GUN_OBJECT + 0x18, UINT64_C(0x450000000));
  unavailable(&m, &s);
  m = attached(&s);
  p64(BODY_OBJECT + 0x18, UINT64_C(0x450000000));
  unavailable(&m, &s);
  m = attached(&s);
  n32(CAMERA_POOL + 0x2000, UINT32_C(0x00310002));
  unavailable(&m, &s);
  m = attached(&s);
  p64(CAMERA_OBJECT + 0x1d0, BODY_OBJECT);
  unavailable(&m, &s);
  m = attached(&s);
  p64(CAMERA_OBJECT + 0x78, BODY_POOL + 0x3000);
  unavailable(&m, &s);
  m = attached(&s);
  p64(CAMERA_OBJECT + 0x18, UINT64_C(0x450000000));
  unavailable(&m, &s);
  m = attached(&s);
  p64(s.camera + 0x18, BODY_OBJECT + 0x60);
  unavailable(&m, &s);
  m = attached(&s);
  n32(s.camera + 0x20, 1);
  unavailable(&m, &s);

  const uint32_t refs[] = {401, 501, 701, 801};
  for (unsigned i = 0; i < sizeof refs / sizeof refs[0]; i++) {
    m = attached(&s);
    uint8_t retired = 1;
    put(REGISTRY + 0x1000 + refs[i], &retired, 1);
    unavailable(&m, &s);
  }
  m = attached(&s);
  n32(RIG_POOL + 0x2800 + 8 * 4, 0);
  unavailable(&m, &s);
  m = attached(&s);
  n32(s.weapon_descriptor, 0);
  unavailable(&m, &s);
  m = attached(&s);
  n32(UINT64_C(0x900000400), 202);
  unavailable(&m, &s);
  m = attached(&s);
  n32(s.request, 4);
  unavailable(&m, &s);

  changed(GUN_OBJECT + 0x1d0, 8, GUN_OBJECT + 0x1d0, 8, BODY_OBJECT);
  changed(GUN_OBJECT + 0x78, 8, GUN_OBJECT + 0x78, 8, BODY_POOL + 0x3000);
  changed(GUN_POOL + 0x2000, 4, GUN_POOL + 0x2000, 4, UINT32_C(0x00070002));
  changed(GUN_POOL + 0x2000, 4, REGISTRY + 0x1000 + 501, 1, 1);
  changed(GUN_OBJECT + 0x80, 8, GUN_OBJECT + 0x80, 8, GUN_POOL + 0x1100);
  changed(CAMERA_OBJECT + 0x1d0, 8, CAMERA_OBJECT + 0x1d0, 8, BODY_OBJECT);
  changed(CAMERA_POOL + 0x2000, 4, CAMERA_POOL + 0x2000, 4,
          UINT32_C(0x00310002));
  changed(UINT64_C(0x300003018), 8, UINT64_C(0x300003020), 4, 1);
  changed(RIG_POOL + 0x2800, 63 * 4, RIG_POOL + 0x2800 + 8 * 4, 4, 0);
  changed(BODY_OBJECT + 0x18, 8, BODY_OBJECT + 0x18, 8, UINT64_C(0x450000000));
  changed(UINT64_C(0xa00000400), 20, UINT64_C(0xa00000400), 4, 0);
  changed(UINT64_C(0x300000200), 20, UINT64_C(0x300000210), 4, 1);
  changed(GUN_POOL + 0x2000, 4, UINT64_C(0x3000001f8), 4, 1);
  puts("C attachments: completed gun8/camera50, pending links, strict owners, "
       "worlds, retirement and after-read metadata changes passed");
  return 0;
}
