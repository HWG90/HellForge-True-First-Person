// The scripts of tests/diff_blur_helper.cpp, and its runner. Each script is run in its own child process.
#pragma once

namespace dt {

struct Script { std::string name; std::function<void(Run &)> run; bool probed = false; };
static std::vector<Script> scripts;
static void add(const std::string &name, std::function<void(Run &)> run, bool probed = false) { scripts.push_back({name, run, probed}); }
using Change = Run::Change;
static Change B(const char *name, int id, uint64_t offset, uint64_t raw, int length = 4) { return {name, {id, offset}, bytes_value(raw, length)}; }
static Change P(const char *name, int id, uint64_t offset, int target, uint64_t target_offset = 0) { return {name, {id, offset}, address_value(target, target_offset)}; }
static Change S(const char *name, uint64_t which) { return {name, {A_E_TABLE, 0}, slot_value(which)}; }
static const uint64_t RECORD = 0x200 + 0xE8; // the request record at tail 2
static const uint64_t PACKED_A = (static_cast<uint64_t>(DOF) << 32) | ENTITY_A, PACKED_B = (static_cast<uint64_t>(DOF) << 32) | ENTITY_B;

// Every single change of the game's memory the scripts know: each is tried before validate and begin, under
// an armed helper, just before every single read of the probed calls, and at random in the walks.
static std::vector<Change> all_changes() {
  return {
      // controller and avatar flags (the "safe" gate)
      B("controller mask 1", A_CONTROLLER, 0, 1), B("controller mask 2", A_CONTROLLER, 0, 2), B("controller mask 4", A_CONTROLLER, 0, 4),
      B("controller mask 0x40", A_CONTROLLER, 0, 0x40), B("controller mask 0x10002", A_CONTROLLER, 0, 0x10002), B("controller mask 3", A_CONTROLLER, 0, 3),
      B("controller aim byte", A_CONTROLLER, 4, 1, 1), B("controller guard byte", A_CONTROLLER, 5, 1, 1), B("controller guard byte 0x80", A_CONTROLLER, 5, 0x80, 1),
      B("seated flag", A_O_FLAGS0, 14, 4, 1), B("flag byte 14 without bit 4", A_O_FLAGS0, 14, 0xFB, 1), B("flag byte 13", A_O_FLAGS0, 13, 0xFF, 1),
      B("flag byte 15", A_O_FLAGS0, 15, 0xFF, 1), B("flag byte 10 (shoulder)", A_O_FLAGS0, 10, 4, 1), B("seated flag of avatar row 1", A_O_FLAGS1, 14, 4, 1),
      // owner proof
      B("avatar count 0", A_OWNER, 0x6C, 0), B("avatar count 8", A_OWNER, 0x6C, 8), B("avatar count 9", A_OWNER, 0x6C, 9),
      B("descriptor avatar", A_DESCRIPTOR, 8, AVATAR + 1), B("descriptor body", A_DESCRIPTOR, 12, BODY_UNIT + 1), B("descriptor byte 0", A_DESCRIPTOR, 0, 1, 1),
      B("descriptor byte 19", A_DESCRIPTOR, 19, 1, 1), B("descriptor byte 20", A_DESCRIPTOR, 20, 1, 1), P("descriptor row elsewhere", A_OWNER, 0x110, A_SPARE),
      B("descriptor row not a pointer", A_OWNER, 0x110, 1, 8), P("game context elsewhere", A_G_CONTEXT, 0, A_SPARE), B("game context not a pointer", A_G_CONTEXT, 0, 0xFFFF, 8),
      B("game context beyond user space", A_G_CONTEXT, 0, 0x800000000000ull, 8), P("context camera elsewhere", A_CONTEXT, 0, A_SPARE), B("context camera null", A_CONTEXT, 0, 0, 8),
      P("game avatar owner elsewhere", A_G_OWNER, 0, A_SPARE), P("game controller elsewhere", A_G_CONTROLLER, 0, A_SPARE), B("game controller not a pointer", A_G_CONTROLLER, 0, 1, 8),
      P("controller at a page end", A_G_CONTROLLER, 0, A_CONTROLLER, 0xFFB), P("registry elsewhere", A_E_REGISTRY, 0, A_SPARE), B("registry capacity 0", A_REGISTRY, 0x98, 0),
      B("registry capacity 3", A_REGISTRY, 0x98, 3), B("registry capacity 4", A_REGISTRY, 0x98, 4), B("registry capacity 0x400000", A_REGISTRY, 0x98, 0x400000),
      B("registry capacity 0x400001", A_REGISTRY, 0x98, 0x400001), P("generations elsewhere", A_REGISTRY, 0xA0, A_SPARE), B("unit generation", A_GENERATIONS, 3, 2, 1),
      P("units elsewhere", A_REGISTRY, 0x88, A_SPARE), P("body object elsewhere", A_UNITS, 3 * 8, A_SPARE), B("body object null", A_UNITS, 3 * 8, 0, 8),
      // camera request
      B("ring head equals tail", A_CONTEXT, 0x1F8, 2), B("ring head 1", A_CONTEXT, 0x1F8, 1), B("ring head 32", A_CONTEXT, 0x1F8, 32), B("ring tail 32", A_CONTEXT, 0x1FC, 32),
      B("ring tail 3", A_CONTEXT, 0x1FC, 3), B("ring tail 0", A_CONTEXT, 0x1FC, 0), B("request kind 4", A_CONTEXT, RECORD, 4), B("request kind 5", A_CONTEXT, RECORD, 5),
      B("request kind 6", A_CONTEXT, RECORD, 6), B("request kind 0x10006", A_CONTEXT, RECORD, 0x10006), B("request kind 0x10004", A_CONTEXT, RECORD, 0x10004),
      B("request avatar", A_CONTEXT, RECORD + 4, AVATAR + 1), B("request weapon", A_CONTEXT, RECORD + 8, WEAPON + 1), B("request body", A_CONTEXT, RECORD + 12, BODY_UNIT + 1),
      // provider identity and dispatch
      P("game root elsewhere", A_G_ROOT, 0, A_SPARE), P("root system elsewhere", A_ROOT, 0x18, A_SPARE), P("system provider elsewhere", A_SYSTEM, 0xDC8, A_SPARE),
      P("provider vtable", A_PROVIDER, 0, A_E_VTABLE, 8), P("vtable lookup", A_E_VTABLE, 0x38, A_E_LOOKUP, 1), P("vtable apply", A_E_VTABLE, 0x28, A_E_APPLY, 1),
      P("system query", A_SYSTEM, 0xDD8, A_E_QUERY, 8), P("query pack", A_E_QUERY, 0x28, A_E_PACK, 1), P("system table", A_SYSTEM, 0xDE8, A_E_TABLE, 8),
      P("table getter", A_E_TABLE, 8, A_E_GETTER, 1), P("game apis elsewhere", A_G_APIS, 0, A_SPARE), P("apis hash api elsewhere", A_APIS, 0xC0, A_SPARE),
      P("hash api hash", A_HASH_API, 0x28, A_E_HASH, 1), S("slot holds a foreign value", SLOT_FOREIGN), S("slot holds the game's setter", SLOT_NATIVE), S("slot holds the helper", SLOT_HELPER),
      P("provider ids elsewhere", A_PROVIDER, 0xC8, A_SPARE), P("provider classes elsewhere", A_PROVIDER, 0xD0, A_SPARE), P("provider chains elsewhere", A_PROVIDER, 0xD8, A_SPARE),
      P("provider blobs elsewhere", A_PROVIDER, 0xE8, A_SPARE), B("provider blobs null", A_PROVIDER, 0xE8, 0, 8),
      // weapon parameter pool
      P("game pool elsewhere", A_G_POOL, 0, A_SPARE), P("pool buckets elsewhere", A_POOL, 0x278, A_SPARE), B("pool capacity 0", A_POOL, 0x280, 0), B("pool capacity 6", A_POOL, 0x280, 6),
      B("pool capacity 16", A_POOL, 0x280, 16), B("pool capacity 1048576", A_POOL, 0x280, 1048576), B("pool capacity 2097152", A_POOL, 0x280, 2097152),
      B("pool empty key is the weapon", A_POOL, 0x284, WEAPON), B("pool empty key 0", A_POOL, 0x284, 0), B("pool multiplier 2", A_POOL, 0x288, 2), B("pool multiplier 9", A_POOL, 0x288, 9),
      B("pool bucket key", A_PBUCKETS, 4 * 8, WEAPON + 1), B("pool bucket index 0", A_PBUCKETS, 4 * 8 + 4, 0), B("pool bucket index 65536", A_PBUCKETS, 4 * 8 + 4, 65536),
      P("pool descriptors elsewhere", A_POOL, 0x290, A_SPARE), P("parameter descriptor elsewhere", A_PDESCS, 8, A_SPARE), B("parameter descriptor weapon", A_PDESC, 8, WEAPON + 1),
      B("parameter descriptor flag clear", A_PDESC, 20, 0, 1), B("parameter descriptor flag 3", A_PDESC, 20, 3, 1), B("parameter descriptor byte 0", A_PDESC, 0, 1, 1),
      B("parameter descriptor byte 23", A_PDESC, 23, 1, 1), B("parameter descriptor byte 24", A_PDESC, 24, 1, 1), P("pool rows elsewhere", A_POOL, 0x298, A_SPARE),
      B("row index 0 is 4096", A_PROWS, 8, 4096), B("row index 0 is 4095", A_PROWS, 8, 4095), B("row index 1 is 4096", A_PROWS, 12, 4096), B("row index 1 is 16", A_PROWS, 12, 16),
      B("row index 0 is 17", A_PROWS, 8, 17), B("system entity 0", A_SYSTEM, 0x348 + 17 * 0x30, 0), B("system entity all ones", A_SYSTEM, 0x348 + 17 * 0x30, UINT32_MAX),
      B("system entity 109", A_SYSTEM, 0x348 + 17 * 0x30, 109), B("system entity A 0", A_SYSTEM, 0x348 + 16 * 0x30, 0),
      // component map of the provider
      P("provider buckets elsewhere", A_PROVIDER, 0x98, A_SPARE), B("provider count 0", A_PROVIDER, 0x90, 0), B("provider count 40", A_PROVIDER, 0x90, 40), B("provider count 41", A_PROVIDER, 0x90, 41),
      B("provider count 55", A_PROVIDER, 0x90, 55), B("provider capacity 0", A_PROVIDER, 0x94, 0), B("provider capacity 4096", A_PROVIDER, 0x94, 4096), B("provider capacity 4097", A_PROVIDER, 0x94, 4097),
      B("provider capacity 53", A_PROVIDER, 0x94, 53), B("provider active 54", A_PROVIDER, 0xA8, 54), B("provider active 55", A_PROVIDER, 0xA8, 55), B("provider divisor 0", A_PROVIDER, 0xAC, 0),
      B("provider divisor 54", A_PROVIDER, 0xAC, 54), B("provider divisor 55", A_PROVIDER, 0xAC, 55), B("provider divisor 40", A_PROVIDER, 0xAC, 40), B("provider rows 0", A_PROVIDER, 0xB8, 0),
      B("provider rows 5", A_PROVIDER, 0xB8, 5), B("provider rows 6", A_PROVIDER, 0xB8, 6), B("provider rows 256", A_PROVIDER, 0xB8, 256), B("provider rows 257", A_PROVIDER, 0xB8, 257),
      B("provider allocated 4096", A_PROVIDER, 0xBC, 4096), B("provider allocated 4097", A_PROVIDER, 0xBC, 4097), B("provider allocated 91", A_PROVIDER, 0xBC, 91),
      B("bucket 30 next beyond storage", A_BUCKETS, 30 * 12 + 8, 54), B("bucket 30 next 53", A_BUCKETS, 30 * 12 + 8, 53), B("bucket 30 ends the chain", A_BUCKETS, 30 * 12 + 8, 0x7FFFFFFF),
      B("bucket 30 empty", A_BUCKETS, 30 * 12 + 8, 0xFFFFFFFE), B("bucket 30 loops", A_BUCKETS, 30 * 12 + 8, 30), B("bucket 30 is the entity, index 0", A_BUCKETS, 30 * 12, ENTITY_B),
      B("bucket 40 index 4", A_BUCKETS, 40 * 12 + 4, 4), B("bucket 40 index 5", A_BUCKETS, 40 * 12 + 4, 5), B("bucket 40 index 92", A_BUCKETS, 40 * 12 + 4, 92), B("bucket 40 index 0", A_BUCKETS, 40 * 12 + 4, 0),
      B("bucket 40 key", A_BUCKETS, 40 * 12, 1), B("bucket 40 empty", A_BUCKETS, 40 * 12 + 8, 0xFFFFFFFE), B("component id 3", A_IDS, 3 * 4, 1), B("component id 5", A_IDS, 5 * 4, 1),
      B("component class 3 is the optic", A_CLASSES, 3 * 4, DOF), B("component class 5 is not the optic", A_CLASSES, 5 * 4, DOF + 1), B("component chain 3 ends", A_CHAINS, 3 * 4, 0),
      B("component chain 4 ends", A_CHAINS, 4 * 4, 0), B("component chain 4 loops", A_CHAINS, 4 * 4, 4), B("component chain 4 beyond rows", A_CHAINS, 4 * 4, 92),
      B("component A is an optic without a blob", A_CLASSES, 6 * 4, DOF), P("blob pointer elsewhere, same content", A_BLOBS, 5 * 8, A_BLOB1), B("blob pointer null", A_BLOBS, 5 * 8, 0, 8),
      // the optic component's parameter blob
      B("blob count 0", A_BLOB0, 4, 0), B("blob count 3", A_BLOB0, 4, 3), B("blob count 5", A_BLOB0, 4, 5), B("blob count 513", A_BLOB0, 4, 513), B("blob key 0", A_BLOB0, 0x10, 1),
      B("blob key 3 duplicates far", A_BLOB0, 0x1C, H_FAR), B("blob key 3 changes", A_BLOB0, 0x1C, 1), B("blob type of near", A_BLOB0, 0x20, 4), B("blob type of far close", A_BLOB0, 0x40, 3),
      B("blob type of the unrelated key", A_BLOB0, 0x50, 4), B("blob value of near", A_BLOB0, 0x28, 0x40000000),
      // images and code (asked by validate and begin only)
      B("game MZ", A_GAME, 0, 0x5A4C, 2), B("game e_lfanew 63", A_GAME, 0x3c, 63), B("game e_lfanew 4097", A_GAME, 0x3c, 4097), B("game e_lfanew 64", A_GAME, 0x3c, 64),
      B("game PE signature", A_GAME, 128, 0x4551), B("game machine", A_GAME, 132, 0x8665, 2), B("game optional magic", A_GAME, 152, 0x10b, 2), B("game stamp", A_GAME, 136, 0x6ab3b440),
      B("game size", A_GAME, 208, 0x4745000), B("exe MZ", A_EXE, 0, 0x5A4C, 2), B("exe e_lfanew -1", A_EXE, 0x3c, 0xFFFFFFFF), B("exe PE signature", A_EXE, 128, 0),
      B("exe machine", A_EXE, 132, 0x14c, 2), B("exe optional magic", A_EXE, 152, 0, 2), B("exe stamp", A_EXE, 136, 1), B("exe size", A_EXE, 208, 0),
      B("call site first byte", A_G_CALL, 0, 0x40, 1), B("call site last byte", A_G_CALL, 2, 0x12, 1), B("setter first byte", A_E_SETTER, 0, 0x49, 1), B("setter last byte", A_E_SETTER, 45, 0x39, 1),
      B("pack first byte", A_E_PACK, 0, 0x40, 1), B("pack last byte", A_E_PACK, 12, 0xc2, 1), B("lookup first byte", A_E_LOOKUP, 0, 0x41, 1), B("lookup last byte", A_E_LOOKUP, 27, 1, 1),
      B("hash first byte", A_E_HASH, 0, 0xe8, 1), B("hash last byte", A_E_HASH, 4, 1, 1), B("hash body first byte", A_E_HASH_BODY, 0, 0x49, 1), B("hash body last byte", A_E_HASH_BODY, 47, 0x5a, 1),
      // the game's variant
      B("variant tag 4", A_VARIANT, 0, 4), B("variant tag 0x10002", A_VARIANT, 0, 0x10002), B("variant float NaN", A_VARIANT, 8, 0x7fc00000), B("variant float infinity", A_VARIANT, 8, 0x7f800000),
      B("variant float -infinity", A_VARIANT, 8, 0xff800000), B("variant float -0", A_VARIANT, 8, 0x80000000), B("variant float denormal", A_VARIANT, 8, 1), B("variant float largest", A_VARIANT, 8, 0x7f7fffff),
  };
}

static Run *current_run;
static const uint32_t HASH_LIST[] = {H_NEAR, H_FAR, H_FAR_CLOSE, H_OTHER, 0x67414413u, 0x6E4BE2C7u, 0u, H_NEAR + 1, H_FAR_CLOSE - 1};

// ---- the plain story: the retail fixture's main(), compared ------------------------------------------------
static void script_story(Run &r) {
  r.read_stats(0); r.validate(); r.expect(r.result == 1, "the normal request validates");
  r.begin(); r.expect(r.result == 0 && NADSB_Error() == OWNER_MISMATCH && !::reserved.load(), "begin requires the game's own first-person request");
  r.top(6); r.validate(); r.expect(r.result == 1, "kind 6 validates");
  r.begin(); r.expect(r.result == 1 && ::immediate_applies.load() == 3 && ::policy.source_count == 1, "begin zeroes three maxima at once");
  for (uint32_t hash : {H_NEAR, H_FAR, H_FAR_CLOSE}) r.sample(hash);
  r.expect(::replaced.load() == 3 && ::policy.last_native == 1.5f, "the three maxima are replaced");
  for (uint32_t hash : HASH_LIST) r.sample(hash);
  r.expect(::replaced.load() == 6, "other parameters pass");
  Hook h;
  for (int delta : {1, -1, -3, 2, 4096}) { h = Hook(); h.caller_ours = false; h.caller_delta = delta; r.sample(h); }
  h = Hook(); h.object_offset = 8; r.sample(h);
  h = Hook(); h.object_id = A_SPARE; r.sample(h);
  h = Hook(); h.object_id = A_SYSTEM; r.sample(h);
  for (uint64_t packed : std::initializer_list<uint64_t>{PACKED_B + 1, PACKED_A, 0x8D346F4B0000006CULL, ENTITY_B, PACKED_B ^ (1ull << 32), PACKED_B ^ (1ull << 63), PACKED_B | (1ull << 31)}) { h = Hook(); h.packed = packed; r.sample(h); }
  r.expect(::replaced.load() == 6 && ::armed.load() == 1, "wrong caller, provider, entity or class pass untouched");
  r.read_stats(r.gen); r.read_stats(0); r.read_stats(r.gen + 1);
  r.end(r.gen); r.expect(r.result == 1 && !::reserved.load(), "end");
  r.sample(); r.sample(H_OTHER); r.expect(::replaced.load() == 6, "after end the hook only forwards");
  r.read_stats(0); r.read_stats(r.gen);
}
// Hip, shoulder, aim: replaced. Inhibited actions, guard, seat: the game's values pass, the helper stays armed.
static void script_states(Run &r) {
  r.two_sources(); r.arm(); r.expect(::immediate_applies.load() == 6 && ::policy.source_count == 2, "two sources");
  auto three = [&] { for (uint32_t hash : {H_NEAR, H_FAR, H_FAR_CLOSE}) { r.sample(hash); Hook h; h.hash = hash; h.packed = PACKED_A; r.sample(h); } };
  three(); r.expect(::replaced.load() == 6, "both components replaced");
  const size_t m = r.mark();
  r.set(A_O_FLAGS0, 10, bytes_value(4, 1)); r.set(A_O_FLAGS0, 15, bytes_value(32, 1)); three(); r.set(A_CONTROLLER, 4, bytes_value(1, 1)); three(); r.restore(m);
  r.expect(::replaced.load() == 18, "shoulder and aim replaced");
  for (uint32_t mask : {1u, 4u, 8u, 0x20u, 0x40u, 3u, 6u, 0x80000000u, 0x102u}) { r.set(A_CONTROLLER, 0, bytes_value(mask)); three(); }
  r.expect(::replaced.load() == 18 && ::armed.load() == 1, "unsafe masks pass and stay armed");
  r.set(A_CONTROLLER, 0, bytes_value(2)); three(); r.expect(::replaced.load() == 24, "mask 2 replaced");
  r.set(A_CONTROLLER, 5, bytes_value(1, 1)); three(); r.restore(m);
  r.set(A_O_FLAGS0, 14, bytes_value(4, 1)); three(); r.set(A_O_FLAGS0, 14, bytes_value(0xFF, 1)); three(); r.set(A_O_FLAGS0, 14, bytes_value(0xFB, 1)); three();
  r.expect(::replaced.load() == 30 && ::armed.load() == 1, "guard and seat pass, other bits of the flag byte do not matter");
  r.restore(m); three(); r.end(r.gen);
  // The same gates at validate and begin.
  for (int which = 0; which < 6; ++which) {
    const size_t n = r.mark();
    if (which == 0) r.set(A_CONTROLLER, 0, bytes_value(1)); if (which == 1) r.set(A_CONTROLLER, 0, bytes_value(2)); if (which == 2) r.set(A_CONTROLLER, 5, bytes_value(1, 1));
    if (which == 3) r.set(A_O_FLAGS0, 14, bytes_value(4, 1)); if (which == 4) r.set(A_CONTROLLER, 4, bytes_value(1, 1)); if (which == 5) r.set(A_CONTROLLER, 0, bytes_value(0x40));
    r.validate(); r.begin(); if (r.result) { three(); r.end(r.gen); }
    r.restore(n);
  }
}
// The variant: only a finite float (type 2) is replaced; everything else is forwarded as the game gave it.
static void script_variants(Run &r) {
  r.arm();
  const uint32_t floats[] = {0x3fc00000, 0, 0x80000000, 1, 0x007fffff, 0x00800000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000, 0x7f800001, 0xbf000000, 0x42c80000};
  for (uint32_t type : {2u, 4u, 0u, 1u, 3u, 0x102u, 0xFFFFFFFFu})
    for (uint32_t bits : floats) { r.set(A_VARIANT, 0, bytes_value(type)); r.set(A_VARIANT, 8, bytes_value(bits)); r.sample(H_NEAR); r.sample(H_OTHER); }
  r.expect(::replaced.load() == 10 && ::armed.load() == 1, "ten finite floats replaced");
  r.set(A_VARIANT, 0, bytes_value(2)); r.set(A_VARIANT, 8, bytes_value(0x3fc00000)); r.set(A_VARIANT, 4, bytes_value(0xDEADBEEF)); r.set(A_VARIANT, 12, bytes_value(0x01020304, 4)); r.set(A_VARIANT, 16, bytes_value(0x1122334455667788ull, 8));
  r.sample(H_FAR_CLOSE);
  Hook h; h.variant = 2; r.sample(h); h.variant = 1; r.sample(h); h.variant = 3; r.sample(h); r.sample(h);
  r.expect(::armed.load() == 1, "an unreadable variant is forwarded and the helper stays armed");
  // The game's variant page unreadable for one read, and for the whole call.
  r.fail_reads(A_VARIANT, 0); r.sample(); r.fail_reads(A_VARIANT, 0, true); r.sample(); r.sample();
  r.end(r.gen);
}
// Avatar rows 0..7 and the per-row flag bytes.
static void script_rows(Run &r) {
  for (uint32_t index : {1u, 7u, 3u}) {
    r.index = index; r.top(6); r.validate(); r.begin(); r.expect(r.result == 0, "the row is beyond the avatar count");
    const size_t m = r.mark(); r.set(A_OWNER, 0x6C, bytes_value(8)); r.validate(); r.begin(); r.expect(r.result == 1, "row armed");
    r.sample(); r.set(A_O_FLAGS0, 14, bytes_value(4, 1)); r.sample(); r.expect(::replaced.load() == 2, "row 0's seat flag is not this row's");
    r.set(A_O_FLAGS0 + static_cast<int>(index), 14, bytes_value(4, 1)); r.sample(); r.expect(::replaced.load() == 2 && ::armed.load() == 1, "this row's seat flag");
    r.set(A_O_FLAGS0 + static_cast<int>(index), 14, bytes_value(0, 1)); r.sample();
    r.set(A_OWNER, 0x6C, bytes_value(index)); r.sample(); r.expect(::armed.load() == 0, "row beyond the count disarms");
    r.end(r.gen); r.restore(m);
  }
  r.index = 8; r.validate(); r.begin(); r.index = 0xFFFFFFFF; r.validate(); r.begin(); r.index = 0;
  // Another body unit (index 5, generation 2 in its upper bits).
  r.body = (2u << 22) | 5u; r.top(6);
  r.set(A_DESCRIPTOR, 12, bytes_value(r.body)); r.set(A_GENERATIONS, 5, bytes_value(2, 1)); r.set(A_UNITS, 5 * 8, address_value(A_SPARE, 0x100));
  r.validate(); r.begin(); r.expect(r.result == 1, "another body unit"); r.sample(); r.set(A_GENERATIONS, 5, bytes_value(3, 1)); r.sample(); r.end(r.gen);
  r.set(A_GENERATIONS, 5, bytes_value(2, 1)); r.set(A_REGISTRY, 0x98, bytes_value(5)); r.validate(); r.set(A_REGISTRY, 0x98, bytes_value(6)); r.validate();
  r.body = (2u << 22) | 0x3fffffu; r.validate();
}
// Every refused configuration.
static void script_configs(Run &r) {
  r.top(6);
  auto both_calls = [&] { r.validate(); r.begin(); if (r.result) r.end(r.gen); };
  both_calls(); r.expect(r.result == 1, "the plain configuration works");
  r.size = 79; both_calls(); r.size = 81; both_calls(); r.size = 0; both_calls(); r.size = 80;
  r.abi = 0; both_calls(); r.abi = 2; both_calls(); r.abi = 1;
  r.reserved_field = 1; both_calls(); r.reserved_field = 1ull << 40; both_calls(); r.reserved_field = 0;
  r.index = 8; both_calls(); r.index = 0;
  r.avatar = 0; both_calls(); r.avatar = 0xffffffff; both_calls(); r.avatar = AVATAR + 1; both_calls(); r.avatar = AVATAR;
  r.body = 0; both_calls(); r.body = 0xffffffff; both_calls(); r.body = BODY_UNIT + 1; both_calls(); r.body = BODY_UNIT ^ (1u << 22); both_calls(); r.body = BODY_UNIT;
  r.weapon = 0; both_calls(); r.weapon = 0xffffffff; both_calls(); r.weapon = WEAPON + 1; both_calls(); r.weapon = WEAPON;
  for (int64_t small : std::initializer_list<int64_t>{0, 1, 65535, 65536}) {
    r.context_absolute = small; both_calls(); r.context_absolute = -1;
    r.camera_absolute = small; both_calls(); r.camera_absolute = -1;
    r.request_absolute = small; both_calls(); r.request_absolute = -1;
  }
  r.context_delta = 8; both_calls(); r.context_delta = 0; r.camera_delta = 8; both_calls(); r.camera_delta = 0;
  r.owner_delta = 8; both_calls(); r.owner_delta = 0; r.request_delta = 4; both_calls(); r.request_delta = 0xE8; both_calls(); r.request_delta = 0;
  r.game_delta = 0x1000; both_calls(); r.game_delta = 0; r.exe_delta = 0x1000; both_calls(); r.exe_delta = 0;
  r.config_null = true; both_calls(); r.config_null = false; r.config_unreadable = true; both_calls(); r.config_unreadable = false;
  r.generation_null = true; r.begin(); r.expect(r.result == 0 && NADSB_Error() == INVALID_CONFIG, "null generation"); r.generation_null = false;
  r.generation_unwritable = true; r.begin(); r.generation_unwritable = false;
  r.fail_reads(A_IO, IO_CONFIG); r.validate(); r.fail_reads(A_IO, IO_CONFIG); r.begin(); if (r.result) r.end(r.gen);
  both_calls(); r.expect(r.result == 1, "the plain configuration still works");
  // The idle record (head == tail): validate accepts the normal request there, begin never.
  const uint32_t idle[4] = {4, AVATAR, WEAPON, BODY_UNIT};
  for (int i = 0; i < 4; ++i) r.set(A_CONTEXT, 0x1F00 + i * 4, bytes_value(idle[i]));
  r.set(A_CONTEXT, 0x1F8, bytes_value(2)); r.head = 2; r.validate(); r.expect(r.result == 1, "idle record validates"); r.begin(); r.expect(r.result == 0, "idle record does not begin");
  r.set(A_CONTEXT, 0x1F00, bytes_value(6)); r.validate(); r.begin(); r.expect(r.result == 0, "kind 6 in the idle record is not a queued request");
  // Every ring position.
  for (uint32_t tail = 0; tail < 32; ++tail) { r.top(6, tail, (tail + 5) % 32); r.validate(); r.begin(); r.expect(r.result == 1, "ring position"); r.sample(); r.end(r.gen); }
  r.expect(::replaced.load() == 1, "replaced at the last ring position");
}
// Control calls in right and wrong orders; generations.
static void script_control(Run &r) {
  r.end(0); r.end(1); r.read_stats(1); r.read_stats(0, 79); r.read_stats(0, 80, 2); r.read_stats(0, 80, 1, 1); r.read_stats(0, 80, 1, 2); r.read_stats(0);
  r.top(6);
  r.begin(); const uint64_t first = r.gen; r.expect(r.result == 1 && first == 1, "first generation is 1");
  r.begin(); r.expect(r.result == 0 && NADSB_Error() == BUSY, "busy"); r.validate(); r.expect(r.result == 0 && NADSB_Error() == BUSY, "validate while a lease is held");
  r.end(0); r.end(first + 1); r.end(UINT64_MAX); r.read_stats(0); r.read_stats(first + 1); r.read_stats(first, 0); r.read_stats(first, 80, 0); r.read_stats(first, 80, 1, 1); r.read_stats(first);
  r.sample(); r.end(first); r.expect(r.result == 1, "end");
  r.end(first); r.expect(r.result == 1, "the retired generation ends again"); r.end(first + 1); r.end(0); r.read_stats(first); r.read_stats(0);
  r.begin(); const uint64_t second = r.gen; r.expect(second == 2, "second generation is 2");
  r.end(first); r.expect(r.result == 0 && ::armed.load() == 1, "a stale generation cannot end a later lease"); r.read_stats(first);
  r.sample(); r.end(second); r.end(first); r.end(second);
  // A refused begin consumes no generation; a begin refused after the generation was written does.
  r.top(4); r.begin(); r.top(6); r.begin(); r.expect(r.gen == 3, "third generation is 3"); r.end(r.gen);
  r.generation_unwritable = true; r.begin(); r.generation_unwritable = false;
  for (int i = 0; i < 5; ++i) { r.begin(); r.sample(H_NEAR); r.end(r.gen); }
  r.expect(r.gen == 8, "generations count on");
}
// The dispatch slot: conflicts at begin and at end.
static void script_slot(Run &r, int which) {
  r.top(6);
  const Change foreign = S("foreign", SLOT_FOREIGN), native = S("native", SLOT_NATIVE);
  switch (which) {
  case 0: // read-only slot at begin, at end
    fixture_slot_readonly = true; r.validate(); r.begin(); r.expect(r.result == 0 && NADSB_Error() == SLOT_NOT_WRITABLE && !::reserved.load(), "read-only slot refuses");
    fixture_slot_readonly = false; r.begin(); r.expect(r.result == 1, "armed");
    fixture_slot_readonly = true; r.end(r.gen); r.expect(r.result == 0 && NADSB_Error() == CLEANUP_PENDING && ::reserved.load() == 1, "cleanup pending"); r.sample(); r.read_stats(r.gen); r.begin(); r.validate();
    fixture_slot_readonly = false; r.end(r.gen); r.expect(r.result == 1, "end retried"); break;
  case 1: // a foreign value arrives between the proof and the swap
    fixture_foreign_cas_at = fixture_cas_calls + 1; fixture_foreign_value = SLOT_FOREIGN; r.begin();
    r.expect(r.result == 0 && NADSB_Error() == CAS_MISMATCH && !::reserved.load(), "foreign value at the swap");
    r.sample(); r.set(native); r.begin(); r.expect(r.result == 1, "armed after the foreign value left"); r.sample(); r.end(r.gen); break;
  case 2: // the swap faults at begin
    fixture_cas_fail_at = fixture_cas_calls + 1; r.begin(); r.expect(r.result == 0 && NADSB_Error() == CAS_FAULT, "swap fault at begin");
    r.begin(); r.expect(r.result == 1, "armed"); r.sample();
    fixture_cas_fail_at = fixture_cas_calls + 1; r.end(r.gen); r.expect(r.result == 0 && NADSB_Error() == CLEANUP_PENDING, "swap fault at end"); r.sample(); r.end(r.gen); r.expect(r.result == 1, "end retried"); break;
  case 3: // begin answers 2: unstable after the swap and the restoring swap faults; end until it succeeds
    r.change_on_forward(0, B("request weapon", A_CONTEXT, RECORD + 8, WEAPON + 1)); fixture_cas_fail_at = fixture_cas_calls + 2; r.begin();
    r.expect(r.result == 2 && r.gen && ::reserved.load() == 1 && ::armed.load() == 0 && NADSB_Error() == CLEANUP_PENDING, "begin answers 2");
    r.sample(); r.read_stats(r.gen); r.begin(); r.validate();
    r.fail_reads(A_E_TABLE, 0); r.end(r.gen); r.expect(r.result == 0 && ::reserved.load() == 1, "end with an unreadable slot"); r.sample();
    r.end(r.gen); r.expect(r.result == 1 && !::reserved.load(), "end succeeds"); r.restore(); r.top(6); r.begin(); r.expect(r.result == 1, "armed again"); r.sample(); r.end(r.gen); break;
  case 4: // a foreign value in the slot at end is left alone
    r.begin(); r.set(foreign); r.sample(); r.end(r.gen); r.expect(r.result == 1, "end leaves a foreign value"); r.sample(); r.begin(); r.set(native); r.begin(); r.sample(); r.end(r.gen); break;
  case 5: // the game's setter back in the slot under an armed helper
    r.begin(); r.set(native); r.sample(); r.expect(::armed.load() == 0 && NADSB_Error() == API_MISMATCH, "dispatch lost"); r.sample(); r.end(r.gen); r.begin(); r.sample(); r.end(r.gen); break;
  case 6: // pinning fails
    shim_pin_fails = true; r.validate(); r.begin(); r.expect(r.result == 0 && NADSB_Error() == PIN_FAILED && !::reserved.load(), "pin fails"); shim_pin_fails = false;
    r.begin(); r.expect(r.result == 1 && r.gen == 1, "no generation was consumed"); r.end(r.gen); break;
  case 7: { // the slot's target is not executable code
    const uint64_t code = W[0].native_value; W[0].native_value = W[0].a[A_SPARE] + 0x400; fixture_target = reinterpret_cast<Setter>(W[0].native_value);
    World::put<uint64_t>(W[0].slot(), W[0].native_value);
    r.validate(); r.expect(r.result == 0 && NADSB_Error() == API_MISMATCH, "target not executable"); r.begin();
    W[0].native_value = code; fixture_target = reinterpret_cast<Setter>(code); World::put<uint64_t>(W[0].slot(), code); r.begin(); r.expect(r.result == 1, "armed"); r.end(r.gen); break; }
  case 8: // the slot already holds the helper; and a foreign value before begin
    r.set(S("helper", SLOT_HELPER)); r.validate(); r.begin(); r.restore(); r.top(6); r.set(foreign); r.validate(); r.begin(); r.expect(r.result == 0 && NADSB_Error() == API_MISMATCH, "foreign slot"); break;
  case 9: // the slot page really read-only (not the fixture switch): refused at begin; at end, cleanup stays pending
    for (int w = 0; w < 2; ++w) mprotect(reinterpret_cast<void *>(W[w].slot() & ~4095ull), 4096, PROT_READ);
    r.validate(); r.begin(); r.expect(r.result == 0 && NADSB_Error() == SLOT_NOT_WRITABLE, "read-only page");
    for (int w = 0; w < 2; ++w) mprotect(reinterpret_cast<void *>(W[w].slot() & ~4095ull), 4096, PROT_READ | PROT_WRITE);
    r.begin(); r.expect(r.result == 1, "armed");
    for (int w = 0; w < 2; ++w) mprotect(reinterpret_cast<void *>(W[w].slot() & ~4095ull), 4096, PROT_READ);
    r.sample(); r.end(r.gen); r.expect(r.result == 0 && NADSB_Error() == CLEANUP_PENDING, "cleanup pending");
    for (int w = 0; w < 2; ++w) mprotect(reinterpret_cast<void *>(W[w].slot() & ~4095ull), 4096, PROT_READ | PROT_WRITE);
    r.end(r.gen); r.expect(r.result == 1, "ended"); break;
  case 10: // the slot unreadable at begin's verification (the read after the swap), then at the restoring read
    r.begin(); r.end(r.gen);
    for (int number = 0; number < 6; ++number) { r.fail_reads(A_E_TABLE, 0, number & 1, 1 + number / 2); r.begin(); if (r.result) { r.sample(); r.end(r.gen); if (!r.result) r.end(r.gen); } }
    break;
  }
}
// Trouble that arrives while the game's own setter runs (the immediate applies of begin, and frames).
static void script_forward(Run &r) {
  r.two_sources(); r.top(6);
  const size_t m = r.mark();
  const Change kinds[] = {B("request weapon", A_CONTEXT, RECORD + 8, WEAPON + 1), B("controller mask 1", A_CONTROLLER, 0, 1), B("seat", A_O_FLAGS0, 14, 4, 1),
                          B("blob key", A_BLOB0, 0x10, 1), B("unit generation", A_GENERATIONS, 3, 2, 1), S("foreign slot", SLOT_FOREIGN), B("parameter descriptor byte", A_PDESC, 0, 1, 1),
                          P("blob elsewhere", A_BLOBS, 5 * 8, A_BLOB1), B("request kind 4", A_CONTEXT, RECORD, 4)};
  for (const Change &c : kinds)
    for (int forward : {0, 2, 3, 5}) {
      r.change_on_forward(forward, c); r.begin();
      r.expect(r.result == 0 && !::reserved.load(), "a change during the immediate applies rolls begin back");
      r.restore(m); if (c.value.kind == 2) r.set(S("native", SLOT_NATIVE));
      r.sample();
    }
  for (int forward : {0, 1, 4, 5})
    for (int stay = 0; stay < 2; ++stay) {
      r.fail_after_forward(forward, A_CONTROLLER, 0, stay != 0); r.begin();
      r.expect(r.result == 0 && NADSB_Error() == (stay ? OWNER_MISMATCH : READ_FAILED), "an owner read failing after the game's setter rolls begin back with its own cause");
      r.fail_after_forward(forward, A_PDESC, 0, stay != 0); r.begin();
      r.fail_after_forward(forward, A_E_TABLE, 0, stay != 0); r.begin(); if (r.result == 2) r.end(r.gen);
      r.fail_after_forward(forward, A_CONTEXT, 0x1F8, stay != 0); r.begin();
    }
  r.begin(); r.expect(r.result == 1 && ::immediate_applies.load() == 6, "armed");
  // In a frame the game's setter is the last step: a change there is seen by the next frame.
  r.change_on_forward(0, kinds[0]); r.sample(); r.expect(::replaced.load() == 1 && ::armed.load() == 1, "replaced"); r.sample(); r.expect(::armed.load() == 0, "disarmed by the next frame");
  r.end(r.gen);
}
// Reads failing at named places (the retail fixture's owner_read_failures, and more).
static void script_read_failures(Run &r) {
  const Sym owner_places[] = {{A_CONTROLLER, 0}, {A_DESCRIPTOR, 0}, {A_GENERATIONS, 3}, {A_G_CONTEXT, 0}, {A_CONTEXT, 0}, {A_OWNER, 0x6C}, {A_OWNER, 0x110}, {A_O_FLAGS0, 0},
                              {A_E_REGISTRY, 0}, {A_REGISTRY, 0x98}, {A_REGISTRY, 0xA0}, {A_REGISTRY, 0x88}, {A_UNITS, 3 * 8}};
  const Sym other_places[] = {{A_CONTEXT, 0x1F8}, {A_CONTEXT, 0x1FC}, {A_CONTEXT, RECORD}, {A_G_ROOT, 0}, {A_ROOT, 0x18}, {A_SYSTEM, 0xDC8}, {A_PROVIDER, 0}, {A_E_VTABLE, 0x38},
                              {A_E_TABLE, 0}, {A_E_TABLE, 8}, {A_APIS, 0xC0}, {A_HASH_API, 0x28}, {A_G_POOL, 0}, {A_POOL, 0x278}, {A_PBUCKETS, 4 * 8}, {A_PDESCS, 8}, {A_PDESC, 0},
                              {A_PROWS, 8}, {A_SYSTEM, 0x348 + 17 * 0x30}, {A_PROVIDER, 0x98}, {A_PROVIDER, 0x90}, {A_BUCKETS, 30 * 12}, {A_BUCKETS, 40 * 12}, {A_IDS, 3 * 4},
                              {A_CLASSES, 4 * 4}, {A_CHAINS, 4 * 4}, {A_BLOBS, 5 * 8}, {A_BLOB0, 4}, {A_BLOB0, 0x10}, {A_BLOB0, 0x20}, {A_GAME, 0}, {A_GAME, 128}, {A_EXE, 0},
                              {A_G_CALL, 0}, {A_E_SETTER, 0}, {A_E_HASH_BODY, 0}, {A_VARIANT, 0}};
  for (int stay = 0; stay < 2; ++stay) {
    for (const Sym &at : owner_places) {
      r.top(4); r.fail_reads(at.id, at.offset, stay != 0); r.validate();
      r.expect(r.result == 0 && NADSB_Error() == (stay ? OWNER_MISMATCH : READ_FAILED), "owner read failure at validate");
      r.validate(); r.expect(r.result == 1, "healthy again");
      r.top(6); r.fail_reads(at.id, at.offset, stay != 0); r.begin(); r.expect(r.result == 0 && NADSB_Error() == (stay ? OWNER_MISMATCH : READ_FAILED), "owner read failure at begin");
      r.begin(); r.expect(r.result == 1, "armed");
      r.fail_reads(at.id, at.offset, stay != 0); r.sample();
      r.expect(::armed.load() == 0 && ::installed.load() == 1 && NADSB_Error() == (stay ? OWNER_MISMATCH : READ_FAILED), "owner read failure in a frame keeps only cleanup");
      r.sample(); r.end(r.gen); r.expect(r.result == 1, "ended");
    }
    for (const Sym &at : other_places) {
      r.top(4); r.fail_reads(at.id, at.offset, stay != 0); r.validate(); r.expect(NADSB_Error() != READ_FAILED, "only an owner read failure is retryable");
      r.top(6); r.fail_reads(at.id, at.offset, stay != 0); r.begin(); if (r.result) r.end(r.gen);
      r.begin(); r.expect(r.result == 1, "armed");
      r.fail_reads(at.id, at.offset, stay != 0); r.sample(); r.expect(NADSB_Error() != READ_FAILED, "only an owner read failure is retryable"); r.sample(); r.end(r.gen);
      // The second and third read of the same place.
      for (int count = 2; count <= 3; ++count) { r.fail_reads(at.id, at.offset, stay != 0, count); r.begin(); if (r.result) { r.sample(); r.end(r.gen); } }
    }
  }
  // A span crossing into an unreadable page: the descriptor's 20 bytes end 12 bytes into the page after the spare region.
  r.top(6);
  for (int w = 0; w < 2; ++w) std::memcpy(reinterpret_cast<void *>(W[w].a[A_SPARE] + 0x2000 - 12), reinterpret_cast<void *>(W[w].a[A_DESCRIPTOR]), 12);
  r.set(A_OWNER, 0x110, address_value(A_SPARE, 0x2000 - 12)); r.validate(); r.expect(r.result == 0 && NADSB_Error() == OWNER_MISMATCH, "a span into an unreadable page is not retryable"); r.begin();
  r.restore(); r.top(6); r.begin(); r.expect(r.result == 1, "armed"); r.set(A_OWNER, 0x110, address_value(A_SPARE, 0x2000 - 12)); r.sample(); r.expect(NADSB_Error() == OWNER_MISMATCH, "frame"); r.end(r.gen);
}
// The optic component's parameter list in other shapes.
static void script_schema(Run &r) {
  r.top(6);
  auto go = [&](bool works, const char *what) { r.validate(); r.expect((r.result == 1) == works, what); r.begin(); if (r.result) { for (uint32_t hash : {H_NEAR, H_FAR, H_FAR_CLOSE, H_OTHER}) r.sample(hash); r.end(r.gen); } };
  // The 22-key list the retail fixture took from the game.
  static const char *keys = "c7cffea54144b60d56db638ee531af324da6cbf20cdf8eb1c0734c8300a017f63a124c596a74d51e2ec17ff0dcc65cf88f96b7bc90b771ec7426b9a3cf729f200e260ff9e77b335418701472788f10d8770499a73046a41f";
  size_t m = r.mark();
  r.snapshot({A_BLOB0, 0}, 8); for (uint64_t at = 8; at < 0x200; at += 8) r.snapshot({A_BLOB0, at}, 8);
  for (int w = 0; w < 2; ++w) {
    const uint64_t b = W[w].a[A_BLOB0]; World::put<uint32_t>(b + 4, 22); World::literal(b + 0x10, keys);
    World::literal(b + 0x108, "0200000000000000802e073b00000000"); World::literal(b + 0x158, "0200000000000000802e073b00000000"); World::literal(b + 0x1A8, "0200000000000000802e873a00000000");
  }
  go(true, "the 22-key list");
  r.expect(::policy.sources[0].fields[0] == W[0].a[A_BLOB0] + 0x1A8 && ::policy.sources[0].fields[1] == W[0].a[A_BLOB0] + 0x108 && ::policy.sources[0].fields[2] == W[0].a[A_BLOB0] + 0x158, "field places");
  r.restore(m);
  // 512 keys (the most), the three wanted ones last; 513; exactly three.
  for (uint32_t count : {512u, 513u, 3u, 1u, 2u}) {
    m = r.mark();
    for (uint64_t at = 0; at < 0x2810; at += 8) r.snapshot({A_BLOB0, at}, 8);
    for (int w = 0; w < 2; ++w) {
      const uint64_t b = W[w].a[A_BLOB0]; std::memset(reinterpret_cast<void *>(b), 0, 0x2810); World::put<uint32_t>(b + 4, count);
      for (uint32_t i = 0; i < count && i < 512; ++i) { World::put<uint32_t>(b + 0x10 + i * 4, 0x1000 + i); World::put<uint32_t>(b + 0x10 + count * 4 + i * 16, 2); }
      const uint32_t wanted[3] = {H_FAR_CLOSE, H_NEAR, H_FAR};
      for (uint32_t k = 0; k < 3 && k < count; ++k) World::put<uint32_t>(b + 0x10 + (count - 1 - k) * 4, wanted[k]);
    }
    go(count == 512 || count == 3, "key counts");
    if (count == 512) { // under an armed helper: one key, one type, the count
      r.begin(); r.sample(); r.set(A_BLOB0, 0x10 + 100 * 4, bytes_value(7)); r.sample(); r.expect(::armed.load() == 0 && NADSB_Error() == UNIT_MISMATCH, "a changed key retires the helper"); r.end(r.gen);
      r.set(A_BLOB0, 0x10 + 100 * 4, bytes_value(0x1000 + 100)); r.begin(); r.set(A_BLOB0, 0x10 + 512 * 4 + 511 * 16, bytes_value(3)); r.sample(); r.end(r.gen);
    }
    r.restore(m);
  }
  // Wrong types, duplicates, a missing key.
  const Change bad[] = {B("type", A_BLOB0, 0x20, 4), B("type", A_BLOB0, 0x30, 0), B("type", A_BLOB0, 0x40, 1), B("duplicate", A_BLOB0, 0x1C, H_NEAR), B("duplicate", A_BLOB0, 0x1C, H_FAR),
                        B("duplicate", A_BLOB0, 0x1C, H_FAR_CLOSE), B("missing", A_BLOB0, 0x10, 5), B("missing", A_BLOB0, 0x14, 5), B("missing", A_BLOB0, 0x18, 5)};
  for (const Change &c : bad) { m = r.mark(); r.set(c); go(false, "refused"); r.expect(NADSB_Error() == SOURCE_INVALID, "source invalid"); r.restore(m); }
  // No eligible component: an ordinary weapon.
  const Change none[] = {B("chain ends", A_CHAINS, 4 * 4, 0), B("flag", A_PDESC, 20, 0, 1), B("flag", A_PDESC, 20, 0xFE, 1), B("not in the pool", A_PBUCKETS, 4 * 8, UINT32_MAX),
                         B("index", A_PROWS, 12, 4096), B("index", A_PROWS, 12, UINT32_MAX), B("class", A_CLASSES, 5 * 4, 1), B("bucket", A_BUCKETS, 40 * 12 + 8, 0xFFFFFFFE), B("bucket", A_BUCKETS, 30 * 12 + 8, 0x7FFFFFFF)};
  for (const Change &c : none) { m = r.mark(); r.set(c); go(false, "no source"); r.expect(NADSB_Error() == NO_SOURCE && !::reserved.load(), "an ordinary weapon reports no source"); r.restore(m); }
  // Two eligible components; the same component twice is refused; the second alone.
  m = r.mark(); r.two_sources(); go(true, "two sources");
  r.begin(); r.expect(::immediate_applies.load() == 6, "six immediate applies"); Hook h; h.packed = PACKED_A; h.hash = H_NEAR; r.sample(h); r.sample(); r.expect(::replaced.load() == 2, "both");
  r.set(A_BLOB1, 0x10, bytes_value(1)); r.sample(); r.expect(::armed.load() == 0 && NADSB_Error() == UNIT_MISMATCH, "the other component's list changes"); r.end(r.gen); r.restore(m);
  m = r.mark(); r.set(A_PROWS, 8, bytes_value(17)); go(false, "the same component twice"); r.expect(NADSB_Error() == SOURCE_INVALID, "twice"); r.restore(m);
  m = r.mark(); r.two_sources(); r.set(A_PROWS, 12, bytes_value(4096)); go(true, "the first component alone"); r.restore(m);
  m = r.mark(); r.two_sources(); r.set(A_SYSTEM, 0x348 + 17 * 0x30, bytes_value(ENTITY_A)); go(false, "both rows name component A"); r.restore(m);
  m = r.mark(); r.two_sources(); r.set(A_BLOBS, 6 * 8, address_value(A_BLOB0)); go(true, "two components, one list"); r.restore(m);
  // Armed with two; then one of them stops being eligible, or a third way round.
  m = r.mark(); r.two_sources(); r.begin(); r.set(A_CLASSES, 6 * 4, bytes_value(1)); r.sample(); r.expect(::armed.load() == 0, "fewer sources"); r.end(r.gen); r.restore(m);
  r.begin(); r.two_sources(); r.sample(); r.expect(::armed.load() == 0 && NADSB_Error() == UNIT_MISMATCH, "more sources"); r.end(r.gen);
}
// The weapon parameter pool's probing and the component map's bounds.
static void script_pool(Run &r) {
  r.top(6);
  auto go = [&] { r.validate(); r.begin(); if (r.result) { r.sample(); r.end(r.gen); } };
  size_t m = r.mark();
  // Probing wraps: the weapon sits three buckets on, behind other keys.
  r.set(A_PBUCKETS, 4 * 8, bytes_value(7)); r.set(A_PBUCKETS, 5 * 8, bytes_value(8)); r.set(A_PBUCKETS, 6 * 8, bytes_value(9));
  r.set(A_PBUCKETS, 7 * 8, bytes_value(10)); r.set(A_PBUCKETS, 0, bytes_value(WEAPON)); r.set(A_PBUCKETS, 4, bytes_value(1));
  go(); r.expect(r.result == 1, "found after wrapping");
  r.set(A_PBUCKETS, 6 * 8, bytes_value(UINT32_MAX)); go(); r.expect(NADSB_Error() == NO_SOURCE, "an empty bucket ends the search"); r.restore(m);
  // A full table without the weapon; the 4096-probe limit.
  for (unsigned i = 0; i < 8; ++i) r.set(A_PBUCKETS, i * 8, bytes_value(100 + i));
  go(); r.expect(NADSB_Error() == SOURCE_INVALID, "full table"); r.restore(m);
  m = r.mark();
  for (uint64_t at = 0; at < 0x8010; at += 8) r.snapshot({A_PBUCKETS, at}, 8);
  for (int w = 0; w < 2; ++w) { for (unsigned i = 0; i < 4096; ++i) World::put<uint32_t>(W[w].a[A_PBUCKETS] + i * 8, 7); const uint32_t entry[2] = {WEAPON, 1}; World::put(W[w].a[A_PBUCKETS] + 4096 * 8, entry); }
  r.set(A_POOL, 0x280, bytes_value(8192)); r.set(A_POOL, 0x288, bytes_value(0)); r.set(A_POOL, 0x284, bytes_value(0x99999999));
  go(); r.expect(r.result == 0 && NADSB_Error() == SOURCE_INVALID, "the weapon is the 4097th probe");
  for (int w = 0; w < 2; ++w) { const uint32_t entry[2] = {WEAPON, 1}; World::put(W[w].a[A_PBUCKETS] + 4095 * 8, entry); }
  go(); r.expect(r.result == 1, "the weapon is the 4096th probe"); r.restore(m);
  // The multiplier: the product wraps at 32 bits.
  m = r.mark(); r.set(A_POOL, 0x288, bytes_value(0x80000001)); go(); r.expect(r.result == 1, "multiplier wraps"); r.restore(m);
  // Descriptor index 65535 is the last allowed.
  m = r.mark(); r.set(A_PBUCKETS, 4 * 8 + 4, bytes_value(65535)); go(); r.expect(r.result == 0 && NADSB_Error() == SOURCE_INVALID, "descriptor 65535 is not there");
  r.set(A_PDESCS, 65535 * 8, address_value(A_PDESC)); r.set(A_PROWS, 65535 * 8, bytes_value(16)); r.set(A_PROWS, 65535 * 8 + 4, bytes_value(17)); go(); r.expect(r.result == 1, "descriptor 65535 is the last allowed"); r.restore(m);
  // The component map: the entity found at once in its own bucket; a chain of many buckets; a loop bounded by the capacity.
  m = r.mark(); r.set(A_BUCKETS, 30 * 12, bytes_value(ENTITY_B)); r.set(A_BUCKETS, 30 * 12 + 4, bytes_value(5)); go(); r.expect(r.result == 1, "own bucket, row 5 at once"); r.restore(m);
  m = r.mark(); r.set(A_BUCKETS, 30 * 12 + 8, bytes_value(41)); for (uint32_t i = 41; i < 53; ++i) { r.set(A_BUCKETS, i * 12, bytes_value(2000 + i)); r.set(A_BUCKETS, i * 12 + 8, bytes_value(i + 1)); }
  r.set(A_BUCKETS, 53 * 12, bytes_value(ENTITY_B)); r.set(A_BUCKETS, 53 * 12 + 4, bytes_value(3)); r.set(A_BUCKETS, 53 * 12 + 8, bytes_value(0x7FFFFFFF));
  go(); r.expect(r.result == 1, "a long bucket chain");
  r.set(A_PROVIDER, 0x94, bytes_value(54)); r.set(A_PROVIDER, 0x90, bytes_value(53)); go(); r.expect(r.result == 0, "the chain leaves the count"); r.restore(m);
  m = r.mark(); r.set(A_PROVIDER, 0x94, bytes_value(4096)); r.set(A_PROVIDER, 0x90, bytes_value(4096)); r.set(A_PROVIDER, 0xAC, bytes_value(4096)); r.set(A_PROVIDER, 0xB8, bytes_value(4096)); r.set(A_PROVIDER, 0xBC, bytes_value(4096));
  r.set(A_BUCKETS, (ENTITY_B % 4096) * 12, bytes_value(ENTITY_B)); r.set(A_BUCKETS, (ENTITY_B % 4096) * 12 + 4, bytes_value(4095)); r.set(A_BUCKETS, (ENTITY_B % 4096) * 12 + 8, bytes_value(0x7FFFFFFF));
  r.set(A_IDS, 4095 * 4, bytes_value(ENTITY_B)); r.set(A_CLASSES, 4095 * 4, bytes_value(DOF)); r.set(A_BLOBS, 4095 * 8, address_value(A_BLOB1));
  go(); r.expect(r.result == 1, "the largest map, the last row"); r.restore(m);
  // The per-entity chain: eleven rows long, ending at the last row the row count allows, and one beyond it.
  for (uint32_t rows : {92u, 71u, 70u}) {
    m = r.mark(); r.set(A_PROVIDER, 0xB8, bytes_value(rows));
    for (uint32_t i = 60; i < 70; ++i) { r.set(A_IDS, i * 4, bytes_value(ENTITY_B)); r.set(A_CLASSES, i * 4, bytes_value(1)); r.set(A_CHAINS, i * 4, bytes_value(i + 1)); }
    r.set(A_IDS, 70 * 4, bytes_value(ENTITY_B)); r.set(A_CLASSES, 70 * 4, bytes_value(DOF)); r.set(A_BLOBS, 70 * 8, address_value(A_BLOB1)); r.set(A_BUCKETS, 40 * 12 + 4, bytes_value(60));
    go(); r.expect((r.result == 1) == (rows != 70), "chain against the row count"); r.restore(m);
  }
  // Image headers at the smallest and largest allowed place.
  for (int32_t at : {64, 4096, 200}) {
    m = r.mark();
    for (int id : {A_GAME, A_EXE}) { for (uint64_t o = 0; o < 264; o += 8) r.snapshot({id, static_cast<uint64_t>(at) + o}, 8); r.set(id, 0x3c, bytes_value(static_cast<uint32_t>(at))); }
    for (int w = 0; w < 2; ++w) for (int id : {A_GAME, A_EXE}) std::memcpy(reinterpret_cast<void *>(W[w].a[id] + at), reinterpret_cast<void *>(W[w].a[id] + 128), 264 - (at == 64 ? 0 : 0));
    if (at == 64) { // the copy overlapped the header at 128: write the fields again
      for (int w = 0; w < 2; ++w) for (int id : {A_GAME, A_EXE}) { const uint64_t b = W[w].a[id] + 64; std::memset(reinterpret_cast<void *>(b), 0, 264);
        World::put<uint32_t>(b, 0x4550); World::put<uint16_t>(b + 4, 0x8664); World::put<uint32_t>(b + 8, id == A_GAME ? W[w].L->game_stamp : W[w].L->exe_stamp);
        World::put<uint16_t>(b + 24, 0x20b); World::put<uint32_t>(b + 80, id == A_GAME ? W[w].L->game_size : W[w].L->exe_size); }
    }
    go(); r.expect(r.result == 1, "image header elsewhere");
    r.restore(m);
    for (int w = 0; w < 2; ++w) for (int id : {A_GAME, A_EXE}) World::pe_header(W[w].a[id], id == A_GAME ? W[w].L->game_stamp : W[w].L->exe_stamp, id == A_GAME ? W[w].L->game_size : W[w].L->exe_size);
  }
  go(); r.expect(r.result == 1, "the plain state works at the end");
}
// One change of the game's memory: before validate and begin, and under an armed helper.
static void script_change(Run &r, const Change &c) {
  size_t m = r.mark(); r.set(c); r.validate(); r.restore(m);
  r.top(6); m = r.mark(); r.set(c); r.validate(); r.begin();
  if (r.result) { r.sample(H_FAR); r.sample(H_NEAR); r.restore(m); r.end(r.gen); if (!r.result) r.end(r.gen); }
  r.restore(m); if (c.value.kind == 2) r.set(S("native", SLOT_NATIVE));
  r.begin(); r.expect(r.result == 1 && ::armed.load() == 1, "the plain state arms");
  m = r.mark(); r.set(c); r.sample(H_FAR); r.sample(H_NEAR); r.sample(H_OTHER); r.restore(m); if (c.value.kind == 2) r.set(S("helper", ::installed.load() ? SLOT_HELPER : SLOT_NATIVE));
  r.sample(H_FAR_CLOSE); r.end(r.gen); r.validate(); r.begin(); r.sample(); r.end(r.gen);
  // With both components eligible, and the change arriving between two frames of one push.
  r.two_sources(); r.begin(); Hook h; h.packed = PACKED_A; r.sample(h); m = r.mark(); r.set(c); r.sample(h); r.sample(); r.restore(m); r.end(r.gen);
}
// What the fixture build of the original compiles out and the translation has (docs, "Checks only the translation makes").
static void script_translation_only(Run &r) {
  r.top(6); r.validate(); r.expect(r.result == 1, "valid");
  for (int w = 0; w < 2; ++w) r.write_config(w);
  auto config = reinterpret_cast<TfpBlurConfig *>(W[1].a[A_IO] + IO_CONFIG); auto generation = reinterpret_cast<uint64_t *>(W[1].a[A_IO] + IO_GENERATION);
  auto slot_now = [] { uint64_t v; std::memcpy(&v, reinterpret_cast<void *>(W[1].slot()), 8); return v; };
  call_text = "translation only";
  for (int which = 0; which < 3; ++which) {
    answer_executable[which] = false;
    if (tfp_blur_validate(config) != 0 || tfp_blur_error() != TFP_BLUR_API_MISMATCH) fail("engine function %d not executable: validate must refuse with 6, error is %u", which, tfp_blur_error());
    *generation = 77;
    if (tfp_blur_begin(config, generation) != 0 || tfp_blur_error() != TFP_BLUR_API_MISMATCH || *generation != 0 || slot_now() != W[1].native_value)
      fail("engine function %d not executable: begin must refuse with 6 and generation 0, error is %u", which, tfp_blur_error());
    answer_executable[which] = true;
  }
  answer_pin_executable = false; *generation = 77;
  if (tfp_blur_begin(config, generation) != 0 || tfp_blur_error() != TFP_BLUR_PIN_FAILED || *generation != 0 || slot_now() != W[1].native_value)
    fail("pinning the executable fails: begin must refuse with 9 and generation 0, error is %u", tfp_blur_error());
  answer_pin_executable = true;
  for (int w = 0; w < 2; ++w) events[w].clear();
  // Both again: the refused calls left nothing behind (no generation consumed, nothing recorded).
  r.validate(); r.expect(r.result == 1, "valid"); r.begin(); r.expect(r.result == 1 && r.gen == 1, "the first generation is still 1"); r.sample(); r.end(r.gen);
}
// tfp_blur_reset_for_tests: after a lease with frames, the translation is again as the original at start-up.
static void script_reset(Run &r) {
  r.top(6);
  for (int w = 0; w < 2; ++w) r.write_config(w);
  auto config = reinterpret_cast<TfpBlurConfig *>(W[1].a[A_IO] + IO_CONFIG); auto generation = reinterpret_cast<uint64_t *>(W[1].a[A_IO] + IO_GENERATION);
  call_text = "translation only, before the reset";
  for (int i = 0; i < 3; ++i) {
    if (tfp_blur_begin(config, generation) != 1) fail("translation-only begin failed with %u", tfp_blur_error());
    tfp_blur_setter_from(W[1].a[A_GAME] + HERE.call + 3, reinterpret_cast<void *>(W[1].a[A_PROVIDER]), PACKED_B, H_FAR, reinterpret_cast<void *>(W[1].variant()));
    if (i < 2 && tfp_blur_end(*generation) != 1) fail("translation-only end failed");
  }
  tfp_blur_end(99); // leaves an error code; the lease is still held and armed
  tfp_blur_reset_for_tests(); World::put<uint64_t>(W[1].slot(), W[1].native_value); translation_cas_calls = fixture_cas_calls;
  for (int w = 0; w < 2; ++w) events[w].clear();
  if (lock_depth[0] || lock_depth[1]) fail("locks held after translation-only calls");
  expect_null_original = true;
  tfp_blur_setter_from(W[1].a[A_GAME] + HERE.call + 3, reinterpret_cast<void *>(W[1].a[A_PROVIDER]), PACKED_B, H_FAR, reinterpret_cast<void *>(W[1].variant()));
  expect_null_original = false; r.restore();
  if (events[1].size() != 1 || events[1][0].kind != EV_FORWARD || (events[1][0].v[2] >> 32) != 0) fail("after the reset the hook must only forward the game's own variant");
  tfp_blur_reset_for_tests();
  script_story(r);
}

// ---- seeded random walks ----------------------------------------------------------------------------------
static void script_walk(Run &r, uint32_t seed, int steps) {
  std::mt19937 rng(seed);
  const std::vector<Change> changes = all_changes();
  auto pick = [&](uint32_t n) { return static_cast<uint32_t>(rng() % n); };
  const Sym places[] = {{A_CONTROLLER, 0}, {A_DESCRIPTOR, 0}, {A_GENERATIONS, 3}, {A_E_TABLE, 0}, {A_PDESC, 0}, {A_BLOB0, 4}, {A_VARIANT, 0}, {A_IO, IO_CONFIG}, {A_CONTEXT, 0x1F8},
                        {A_PROVIDER, 0}, {A_G_ROOT, 0}, {A_O_FLAGS0, 0}, {A_BLOB1, 0x10}, {A_BUCKETS, 40 * 12}, {A_UNITS, 3 * 8}};
  r.top(pick(4) ? 6 : 4);
  size_t base = r.mark();
  for (int step = 0; step < steps; ++step) {
    const uint32_t u = pick(100);
    if (u < 46) {
      Hook h; h.hash = pick(6) ? HASH_LIST[pick(3)] : HASH_LIST[pick(9)];
      const uint64_t packs[] = {PACKED_B, PACKED_A, PACKED_B + 1, static_cast<uint64_t>(0x8D346F4B0000006CULL)}; h.packed = pick(8) ? packs[pick(2)] : packs[pick(4)];
      if (!pick(12)) { h.caller_ours = false; h.caller_delta = static_cast<int>(pick(7)) - 3; if (!h.caller_delta) h.caller_delta = 5; }
      if (!pick(20)) h.object_offset = 8;
      if (!pick(15)) h.variant = static_cast<int>(pick(4));
      r.sample(h);
    } else if (u < 56) r.set(changes[pick(static_cast<uint32_t>(changes.size()))]);
    else if (u < 66) { r.restore(base); if (!pick(3)) { uint64_t now; std::memcpy(&now, reinterpret_cast<void *>(W[0].slot()), 8); if (slot_class(0, now) != SLOT_NATIVE && !::installed.load()) r.set(S("native", SLOT_NATIVE)); } }
    else if (u < 74) r.begin();
    else if (u < 80) { const uint32_t k = pick(8); r.end(k < 5 ? r.gen : k == 5 ? r.previous_gen : k == 6 ? 0 : r.gen + 1); }
    else if (u < 84) r.validate();
    else if (u < 86) { const uint32_t k = pick(6); r.read_stats(k < 3 ? r.gen : k == 3 ? 0 : r.gen + 1, k == 5 ? 79 : 80); }
    else if (u < 89) { const Sym &at = places[pick(15)]; r.fail_reads(at.id, at.offset, pick(2) != 0, 1 + static_cast<int>(pick(3))); }
    else if (u < 91) r.fail_read_number(static_cast<int>(pick(140)), pick(2) != 0);
    else if (u < 93) r.change_on_forward(static_cast<int>(pick(4)), changes[pick(static_cast<uint32_t>(changes.size()))]);
    else if (u < 94) { const Sym &at = places[pick(15)]; r.fail_after_forward(static_cast<int>(pick(4)), at.id, at.offset, pick(2) != 0); }
    else if (u < 96) { const uint32_t k = pick(4); if (k == 0) r.two_sources(); else if (k == 1) r.top(6); else if (k == 2) r.top(4); else r.top(6, pick(32), pick(32)); }
    else if (u < 98) {
      const uint32_t k = pick(5);
      if (k == 0) fixture_cas_fail_at = fixture_cas_calls + 1 + pick(2); else if (k == 1) { fixture_foreign_cas_at = fixture_cas_calls + 1 + pick(2); fixture_foreign_value = SLOT_FOREIGN; }
      else if (k == 2) fixture_slot_readonly = !fixture_slot_readonly; else if (k == 3) shim_pin_fails = !shim_pin_fails; else { fixture_slot_readonly = false; shim_pin_fails = false; }
    } else {
      const uint32_t k = pick(6);
      if (k == 0) r.set(A_CONTROLLER, 0, bytes_value(pick(2) ? 2 : 1)); else if (k == 1) r.set(A_CONTROLLER, 4, bytes_value(1, 1)); else if (k == 2) r.set(A_O_FLAGS0, 14, bytes_value(4, 1));
      else if (k == 3) r.variant(pick(2) ? 0.25f : NAN, pick(4) ? 2 : 4); else if (k == 4) r.index = pick(3) ? 0 : 1; else r.set(A_OWNER, 0x6C, bytes_value(1 + pick(3)));
    }
  }
}

// ---- probes: every single read of one call fails (and heals, or stays), or a value changes just before it --
static void probe_script(Run &r, int which) {
  r.probe_values = all_changes();
  size_t m = 0;
  switch (which) {
  case 0: r.two_sources(); m = r.mark(); r.probe(); r.validate(); break;
  case 1: r.top(6); m = r.mark(); r.probe(); r.validate(); break;
  case 2: r.top(6); m = r.mark(); r.probe(); r.begin(); break;
  case 3: r.two_sources(); r.top(6); m = r.mark(); r.probe(); r.begin(); break;
  case 4: r.two_sources(); r.arm(); m = r.mark(); r.probe(); r.sample(); break;
  case 5: r.arm(); m = r.mark(); r.probe(); r.end(r.gen); break;
  case 6: r.arm(); r.sample(); m = r.mark(); r.probe(); r.read_stats(r.gen); break;
  case 7: r.top(6); m = r.mark(); r.change_on_forward(1, B("request weapon", A_CONTEXT, RECORD + 8, WEAPON + 1)); r.probe(); r.begin(); break;
  case 8: r.arm(); r.set(A_CONTROLLER, 0, bytes_value(1)); m = r.mark(); r.probe(); r.sample(); break;
  case 9: r.arm(); r.set(S("foreign", SLOT_FOREIGN)); m = r.mark(); r.probe(); r.end(r.gen); break;
  case 10: r.arm(); m = r.mark(); r.probe(); r.sample(H_OTHER); break;
  }
  // Afterwards: frames in the world as the trouble left it, then healed; end; and a full healthy round.
  r.sample(); r.sample(H_NEAR);
  r.restore(m);
  uint64_t now; std::memcpy(&now, reinterpret_cast<void *>(W[0].slot()), 8);
  if (!::installed.load() && slot_class(0, now) != SLOT_NATIVE) r.set(S("native", SLOT_NATIVE));
  r.sample();
  if (::reserved.load()) { r.end(::owned_generation); if (!r.result) r.end(::owned_generation); }
  r.top(6); r.validate(); r.begin(); r.sample(); r.end(r.gen);
}

static void register_scripts() {
  add("story: validate, begin, the three maxima, everything else passes, end", script_story);
  add("states: hip, shoulder, aim replaced; masks, guard, seat pass", script_states);
  add("variants: types and floats", script_variants);
  add("avatar rows and body units", script_rows);
  add("refused configurations and ring positions", script_configs);
  add("control calls in right and wrong orders", script_control);
  const char *slots[] = {"read-only (fixture switch)", "foreign value at the swap", "swap faults", "begin answers 2", "foreign value at end", "game's setter returns under the helper",
                         "pin fails", "target not executable", "helper or foreign value before begin", "read-only page", "slot unreadable"};
  for (int which = 0; which < 11; ++which) add(std::string("slot: ") + slots[which], [=](Run &r) { script_slot(r, which); });
  add("changes while the game's setter runs", script_forward);
  add("reads failing at named places", script_read_failures);
  add("parameter lists: 22 keys, 512 keys, wrong types, duplicates, no source, two sources", script_schema);
  add("pool probing, component map bounds, image headers", script_pool);
  add("checks only the translation makes", script_translation_only);
  add("after tfp_blur_reset_for_tests", script_reset);
  for (const Change &c : all_changes()) add("change: " + c.name + " [" + A_NAMES[c.at.id] + "+" + std::to_string(c.at.offset) + "]", [=](Run &r) { script_change(r, c); });
  const char *probes[] = {"validate, normal request, two sources", "validate, first-person request", "begin", "begin, two sources", "replaced frame, two sources", "end", "read_stats",
                          "begin rolled back by a change during the immediate applies", "frame with an unsafe mask", "end with a foreign value in the slot", "frame of another parameter"};
  for (int which = 0; which < 11; ++which) add(std::string("probe: ") + probes[which], [=](Run &r) { probe_script(r, which); }, true);
  // TFP_DIFF_EXTRA_WALKS=n: n more walks (a soak; not part of the normal run).
  const char *extra = std::getenv("TFP_DIFF_EXTRA_WALKS");
  const uint32_t walks = 16 + (extra ? static_cast<uint32_t>(std::atoi(extra)) : 0u);
  for (uint32_t seed = 1; seed <= walks; ++seed) { char name[64]; std::snprintf(name, sizeof name, "random walk, seed %u", seed); add(name, [=](Run &r) { script_walk(r, 25480438u + seed, 3000); }); }
}

// ---- the runner ----
struct Job { size_t script; int mode, index, value; };
static int running = 0, failed = 0, finished = 0;
static void reap() {
  int status = 0; const pid_t pid = wait(&status);
  if (pid < 0) return;
  --running; ++finished;
  if (WIFSIGNALED(status)) { ++failed; std::fprintf(stderr, "a blur helper script's process ended on signal %d\n", WTERMSIG(status)); }
  else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) ++failed;
}
static bool stop_at_first = false; // TFP_DIFF_STOP: for mutation runs, where one difference is the answer
static void launch(const Job &job, int limit) {
  while (running >= limit) reap();
  if (stop_at_first && failed) return;
  std::fflush(stdout); std::fflush(stderr);
  const pid_t pid = fork();
  if (pid < 0) { std::perror("fork"); std::exit(2); }
  if (pid == 0) {
    shim_self = 0;
    const Script &script = scripts[job.script];
    script_name = script.name;
    Run r; current_run = &r; r.probe_mode = job.mode; r.probe_index = job.index; r.probe_value = job.value;
    if (job.mode != INJECT_NONE) {
      char suffix[160];
      if (job.mode == INJECT_VALUE) std::snprintf(suffix, sizeof suffix, " [\"%s\" just before read %d]", all_changes()[static_cast<size_t>(job.value)].name.c_str(), job.index);
      else std::snprintf(suffix, sizeof suffix, " [read %d fails and %s]", job.index, job.mode == INJECT_FAIL ? "heals" : "stays unreadable");
      script_name += suffix;
    }
    discovering = script.probed && job.mode == INJECT_NONE;
    struct Report { static void crash(int signal_number) {
      char text[512]; const int n = std::snprintf(text, sizeof text, "CRASH (signal %d) in script \"%s\", call %llu: %s\n", signal_number, script_name.c_str(), (unsigned long long)call_index, call_text.c_str());
      if (n > 0) (void)!::write(2, text, static_cast<size_t>(n));
      std::_Exit(4); } };
    signal(SIGSEGV, Report::crash); signal(SIGBUS, Report::crash); signal(SIGFPE, Report::crash); signal(SIGABRT, Report::crash);
    script.run(r);
    __sync_fetch_and_add(&shared->calls, counted.calls); __sync_fetch_and_add(&shared->frames, counted.frames); __sync_fetch_and_add(&shared->events, counted.events);
    __sync_fetch_and_add(&shared->skipped_frames, counted.skipped_frames); __sync_fetch_and_add(&shared->translation_only, counted.translation_only);
    __sync_fetch_and_add(&shared->replaced, counted.replaced);
    std::fflush(stdout);
    std::_Exit(0);
  }
  ++running;
}

} // namespace dt

int main(int argc, char **argv) {
  using namespace dt;
  shared = static_cast<Shared *>(mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  register_scripts();
  stop_at_first = std::getenv("TFP_DIFF_STOP") != nullptr;
  build(); // the two worlds, untouched by any helper call; every child starts from a copy of them
  const char *only = argc > 1 ? argv[1] : nullptr; // a part of a script's name: run just those
  if (only && !std::strcmp(only, "--list")) { for (const Script &s : scripts) std::printf("%s\n", s.name.c_str()); return 0; }
  const int limit = std::max(1L, sysconf(_SC_NPROCESSORS_ONLN));
  size_t plain = 0, probe_runs = 0;
  for (size_t i = 0; i < scripts.size(); ++i) {
    if (only && scripts[i].name.find(only) == std::string::npos) continue;
    if (scripts[i].probed) continue;
    launch({i, INJECT_NONE, -1, 0}, limit); ++plain;
  }
  while (running) reap();
  // Probes: one run counts the probed call's reads; then one run per read and kind of trouble.
  for (size_t i = 0; i < scripts.size() && failed < 20; ++i) {
    if (!scripts[i].probed || (only && scripts[i].name.find(only) == std::string::npos)) continue;
    if (stop_at_first && failed) break;
    shared->probe_reads = shared->probe_values = 0;
    const int before = failed;
    launch({i, INJECT_NONE, -1, 0}, 1); while (running) reap(); ++probe_runs;
    if (failed != before) continue;
    const int reads = static_cast<int>(shared->probe_reads), values = static_cast<int>(shared->probe_values);
    for (int index = 0; index < reads && failed < 20; ++index) {
      launch({i, INJECT_FAIL, index, 0}, limit); launch({i, INJECT_FAIL_STAY, index, 0}, limit); probe_runs += 2;
      for (int value = 0; value < values; ++value) { launch({i, INJECT_VALUE, index, value}, limit); ++probe_runs; }
    }
    while (running) reap();
  }
  while (running) reap();
  std::printf("Blur helper: %zu scripts and %zu probe runs; %llu calls compared (%llu of them hook frames, %llu replaced), %llu reads, writes, swaps, lock steps and forwarded calls matched; "
              "%llu translation-only host answers; %llu frames skipped (hook before any begin)\n",
              plain, probe_runs, (unsigned long long)shared->calls, (unsigned long long)shared->frames, (unsigned long long)shared->replaced, (unsigned long long)shared->events,
              (unsigned long long)shared->translation_only, (unsigned long long)shared->skipped_frames);
  if (failed) { std::printf("Blur helper: %d script run(s) FAILED\n", failed); return 1; }
  std::printf("Blur helper: the translation behaves as the retail helper in every compared call\n");
  return 0;
}
