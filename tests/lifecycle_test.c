#include "fake_native.h"
#include "lifecycle.h"

static const uintptr_t context = UINT64_C(0x300000000),
                       world = UINT64_C(0x400000000);
static const uintptr_t controller = UINT64_C(0x500000000),
                       avatar = UINT64_C(0x700000000);
static const uintptr_t wield = UINT64_C(0x900000000),
                       weapons = UINT64_C(0xa00000000);
static const uintptr_t registry = UINT64_C(0xb00000000),
                       old_object = UINT64_C(0x2300000000);
static unsigned enters, exits, checks, old_descriptor_reads;
static bool interrupt_entry, interrupt_exit;
typedef enum Mutation {
  MUTATE_NONE,
  MUTATE_DURING_OLD_RECORD,
  MUTATE_AFTER_OLD_PROOF,
  MUTATE_CLOSING_BODY
} Mutation;
static Mutation mutation;
#define CHECK(v)                                                               \
  do {                                                                         \
    checks++;                                                                  \
    assert(v);                                                                 \
  } while (0)

static void change_old_descriptor(void) {
  /* Both records stay internally coherent, but differ from the lease's bytes.
   */
  n32(world + FP_ENTITY_ROWS + 24, 99);
  n32(weapons + 0x400, 99);
}
static bool observed_read(void *user, uintptr_t at, void *out, size_t size) {
  bool ok = native_fake_read(user, at, out, size);
  if (!ok)
    return false;
  if (at == weapons + 0x400 && size == 20) {
    old_descriptor_reads++;
    if (mutation == MUTATE_DURING_OLD_RECORD) {
      mutation = MUTATE_NONE;
      change_old_descriptor();
    }
  }
  /* Mutate after initial outgoing-record open/close, during the fresh basic
   * ownership snapshot. This exercises the final native-call boundary. */
  if (old_descriptor_reads >= 2 && at == avatar + FP_AVATAR_FLAGS &&
      size == 24) {
    if (mutation == MUTATE_AFTER_OLD_PROOF) {
      mutation = MUTATE_NONE;
      change_old_descriptor();
    } else if (mutation == MUTATE_CLOSING_BODY) {
      mutation = MUTATE_NONE;
      n32(context + 0x200 + 0xe8 + 12, 999);
    }
  }
  return true;
}
static bool invoke(void *user, FpNativeOperation op, const FpMemory *m,
                   const FpSnapshot *s, bool *allowed) {
  (void)user;
  if (op == FP_NATIVE_ALLOWED) {
    *allowed = true;
    return true;
  }
  if (op == FP_NATIVE_ENTER) {
    enters++;
    uint8_t record[0xe8] = {0};
    uint32_t header[5] = {6, s->avatar, s->weapon, s->body, 0};
    memcpy(record, header, sizeof header);
    put(s->context + 0x200 + s->tail * 0xe8, record, sizeof record);
    n32(s->context + 0x1fc, (s->tail + 1) % 32);
    return !interrupt_entry;
  }
  exits++;
  if (interrupt_exit)
    return false;
  /* Fake native producer only: retire old kind6, append saved fallback and
   * advance the ring. Product cleanup must invoke context-only native EXIT. */
  uint32_t top_kind = 0;
  CHECK(fp_u32(m, s->request, &top_kind) && (top_kind & 65535) == 6);
  uint16_t retired = 0;
  put(s->request, &retired, sizeof retired);
  uint8_t fallback[0xe8] = {0};
  uint32_t header[5] = {4, s->avatar, s->weapon, s->body, 0};
  memcpy(fallback, header, sizeof header);
  put(s->context + 0x200 + s->tail * 0xe8, fallback, sizeof fallback);
  uint32_t tail = (s->tail + 1) % 32;
  if (tail == s->head)
    n32(s->context + 0x1f8, (s->head + 1) % 32);
  n32(s->context + 0x1fc, tail);
  return true;
}
static FpMemory fresh(void) {
  FpMemory m = native_setup();
  m.read = observed_read;
  enters = exits = old_descriptor_reads = 0;
  interrupt_entry = interrupt_exit = false;
  mutation = MUTATE_NONE;
  for (unsigned i = 0; i < FP_WITNESS_COUNT; i++) {
    const FpWitness *w = fp_witnesses + i;
    uint8_t b[128];
    unsigned j = 0;
    for (; w->hex[j * 2]; j++) {
      unsigned n;
      assert(sscanf(w->hex + j * 2, "%2x", &n) == 1);
      b[j] = (uint8_t)n;
    }
    put(m.game + w->rva, b, j);
  }
  return m;
}
static FpNativeLease begin(const FpMemory *m) {
  FpSnapshot s;
  FpNativeLease lease = {0};
  CHECK(fp_snapshot(m, true, &s, NULL, 0));
  CHECK(fp_native_begin(m, &s, &lease, invoke, NULL, NULL, 0));
  CHECK(enters == 1 && lease.held);
  old_descriptor_reads = 0;
  return lease;
}
static void select_new_weapon(const FpMemory *m) {
  uint32_t descriptor[6] = {55, 66, 202, 502, 602, 1};
  /* Add the new weapon without removing the outgoing weapon from either map. */
  unsigned bucket = (202 * 3) & 7;
  n32(world + 0x200 + bucket * 8, 202);
  n32(world + 0x200 + bucket * 8 + 4, 2);
  put(world + FP_ENTITY_ROWS + 48, descriptor, sizeof descriptor);
  n32(weapons + 0x200 + bucket * 8, 202);
  n32(weapons + 0x200 + bucket * 8 + 4, 1);
  p64(weapons + 0x308, weapons + 0x500);
  put(weapons + 0x500, descriptor, 20);
  unit(m, 502, UINT64_C(0x2500000000), 1, UINT64_C(0x2600000000));
  p64(UINT64_C(0x2500000000) + 0x358, UINT64_C(0x1112131415161718));
  n32(wield + 0x400, 202);
}
static void refused(const FpMemory *m, FpNativeLease *lease) {
  CHECK(!fp_native_release(m, lease, invoke, NULL, NULL, 0));
  CHECK(lease->held && exits == 0);
}
static void swap_retirement_and_new_entry(void) {
  FpMemory m = fresh();
  FpNativeLease lease = begin(&m);
  select_new_weapon(&m);
  FpSnapshot current;
  CHECK(!fp_snapshot(&m, true, &current, NULL, 0));
  CHECK(fp_snapshot(&m, false, &current, NULL, 0) && current.kind == 6 &&
        current.request_weapon == 201);
  FpWeaponRecord outgoing;
  CHECK(fp_weapon_record(&m, current.world, 201, &outgoing) &&
        outgoing.unit == 501 && outgoing.object == old_object);
  CHECK(fp_native_release(&m, &lease, invoke, NULL, NULL, 0));
  CHECK(exits == 1 && !lease.held);
  CHECK(fp_snapshot(&m, true, &current, NULL, 0) && current.kind == 4 &&
        current.weapon == 202 && current.weapon_unit == 502);
  CHECK(fp_native_begin(&m, &current, &lease, invoke, NULL, NULL, 0));
  CHECK(enters == 2 && lease.held && lease.origin.weapon == 202);
  CHECK(fp_snapshot(&m, true, &current, NULL, 0) && current.kind == 6 &&
        current.request == lease.request && current.request_weapon == 202);
}
static void holster_gap_cleanup(void) {
  FpMemory m = fresh();
  FpNativeLease lease = begin(&m);
  /* A draw/holster transition can temporarily clear current wield while the
   * outgoing descriptor and Unit still exist. Retire the owned old request in
   * that window; discovering a replacement firearm is a separate operation. */
  n32(wield + 0x400, 0);
  FpSnapshot current;
  CHECK(!fp_snapshot(&m, true, &current, NULL, 0));
  CHECK(fp_snapshot(&m, false, &current, NULL, 0) && current.kind == 6 &&
        current.request == lease.request && current.request_weapon == 201);
  FpWeaponRecord outgoing;
  CHECK(fp_weapon_record(&m, current.world, 201, &outgoing) &&
        outgoing.unit == 501 && outgoing.object == old_object);
  CHECK(fp_native_release(&m, &lease, invoke, NULL, NULL, 0));
  CHECK(exits == 1 && !lease.held);
  CHECK(fp_snapshot(&m, false, &current, NULL, 0) && current.kind == 4);
  CHECK(!fp_snapshot(&m, true, &current, NULL, 0));
}
static void invalid_outgoing_resources(void) {
  for (unsigned scenario = 0; scenario < 7; scenario++) {
    FpMemory m = fresh();
    FpNativeLease lease = begin(&m);
    select_new_weapon(&m);
    switch (scenario) {
    case 0:
      n32(world + 0x200 + ((201 * 3) & 7) * 8, UINT32_MAX);
      break;
    case 1:
      p64(registry + 0x2000 + 501 * 8, 0);
      break;
    case 2: {
      uint8_t generation = 1;
      put(registry + 0x1000 + 501, &generation, 1);
      break;
    }
    case 3:
      change_old_descriptor();
      break;
    case 4:
      mutation = MUTATE_DURING_OLD_RECORD;
      break;
    case 5:
      mutation = MUTATE_AFTER_OLD_PROOF;
      break;
    case 6:
      mutation = MUTATE_CLOSING_BODY;
      break;
    }
    if (scenario < 3) {
      FpWeaponRecord record, zero = {0};
      memset(&record, 0xa5, sizeof record);
      CHECK(!fp_weapon_record(&m, world, 201, &record));
      CHECK(!memcmp(&record, &zero, sizeof record));
    }
    refused(&m, &lease);
    if (scenario >= 4)
      CHECK(mutation == MUTATE_NONE);
  }
}
static void foreign_requests_and_controls(void) {
  FpMemory m = fresh();
  FpNativeLease lease = begin(&m);
  uint32_t successor[5] = {6, 101, 202, 401, 0};
  put(context + 0x200 + 2 * 0xe8, successor, sizeof successor);
  n32(context + 0x1fc, 3);
  CHECK(fp_native_release(&m, &lease, invoke, NULL, NULL, 0));
  CHECK(!lease.held && exits == 0);
  m = fresh();
  lease = begin(&m);
  n32(lease.request + 4, 999);
  CHECK(fp_native_release(&m, &lease, invoke, NULL, NULL, 0));
  CHECK(!lease.held && exits == 0);
  m = fresh();
  lease = begin(&m);
  n32(lease.request + 12, 999);
  refused(&m, &lease);
  for (unsigned aiming = 0; aiming < 2; aiming++)
    for (unsigned changed = 0; changed < 2; changed++) {
      m = fresh();
      lease = begin(&m);
      lease.aim_changed = lease.control_changed = changed != 0;
      uint8_t active = 1;
      if (aiming)
        put(avatar + FP_AVATAR_FLAGS + 6, &active, 1);
      else
        put(controller + 4, &active, 1);
      /* Native aim adopts the request even if entry changed some inputs. The
       * ledger records observations; it does not confer ownership of real ADS.
       */
      CHECK(fp_native_release(&m, &lease, invoke, NULL, NULL, 0));
      CHECK(exits == 0 && !lease.held);
    }
  for (unsigned guard = 0; guard < 3; guard++) {
    m = fresh();
    lease = begin(&m);
    uint8_t value = guard == 2 ? 4 : 1;
    if (guard == 0)
      n32(controller, 1);
    if (guard == 1)
      put(controller + 5, &value, 1);
    if (guard == 2)
      put(avatar + FP_AVATAR_FLAGS + 14, &value, 1);
    refused(&m, &lease);
  }
}
static void interrupted_calls(void) {
  FpMemory m = fresh();
  FpSnapshot s;
  FpNativeLease lease = {0};
  CHECK(fp_snapshot(&m, true, &s, NULL, 0));
  interrupt_entry = true;
  CHECK(!fp_native_begin(&m, &s, &lease, invoke, NULL, NULL, 0) && lease.held);
  CHECK(fp_native_release(&m, &lease, invoke, NULL, NULL, 0) && !lease.held);
  CHECK(enters == 1 && exits == 1);
  m = fresh();
  lease = begin(&m);
  interrupt_exit = true;
  CHECK(!fp_native_release(&m, &lease, invoke, NULL, NULL, 0) && lease.held);
  CHECK(exits == 1);
  interrupt_exit = false;
  CHECK(fp_native_release(&m, &lease, invoke, NULL, NULL, 0) && !lease.held);
  CHECK(exits == 2);
}
int main(void) {
  swap_retirement_and_new_entry();
  holster_gap_cleanup();
  invalid_outgoing_resources();
  foreign_requests_and_controls();
  interrupted_calls();
  printf("C native lifecycle: %u assertions passed; old/current weapon "
         "separation, "
         "generation/mutation refusal, producer handoff, ADS/action/foreign "
         "ownership "
         "and interrupted cleanup\n",
         checks);
  return 0;
}
