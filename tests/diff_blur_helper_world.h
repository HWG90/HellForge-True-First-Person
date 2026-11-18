// Fake game memory for tests/diff_blur_helper.cpp: one logical state, written into real pages twice, once
// with the retail build's addresses and offsets (26568081, from native_ads_blur.cpp) and once with this
// build's (25480438, from the table in docs/translation/BLUR-HELPER-TRANSLATION.md, not from the #define block of
// src/blur_helper.c). The structure layout numbers both builds share are written here once, as the retail
// source and its fixture (blur_fixture.cpp) have them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>

namespace dt {

struct Layout {
  uint32_t game_stamp, game_size, exe_stamp, exe_size;
  uint32_t setter, call, pack, lookup, hash, hash_body, vtable, apply, query, table, getter, registry;
  uint32_t g_context, g_owner, g_controller, g_pool, g_root, g_api_root;
  uint32_t avatar_flags, avatar_stride;
  const char *hex_hash, *hex_hash_body;
};
// native_ads_blur.cpp:20, 137-143, 183, 305-325.
static const Layout RETAIL = {
    0x6af322c1, 0x3382000, 0x6af2d5e6, 0x29fe000,
    0x2df740, 0xe9ac4b, 0x2df6b0, 0x66a170, 0x2df830, 0x669480, 0x1715c40, 0x2ad330, 0x2873ac0, 0x2873b18, 0x2df7b0, 0x1ab76f8,
    0x2e287d0, 0x2ce1ee0, 0x2e381a8, 0x2ce1bf8, 0x2ce1618, 0x2ce14c8,
    0x53e888, 0x1240,
    "e94b9c3800",
    "48895c2408574883ec200fb60133ff488bd984c0743e3c2e752f8bd34533c02bd1e89abcfaff48c1e82069c895e9d15b"};
// docs/translation/BLUR-HELPER-TRANSLATION.md section 2, column "here".
static const Layout HERE = {
    0x6ab3b43f, 0x4744000, 0x6ab382e4, 0x39e8000,
    0x310050, 0x100a80b, 0x30ffc0, 0x62adb0, 0x310140, 0x62a0c0, 0x1677dc8, 0x2dd5e0, 0x27cc5e0, 0x27cc638, 0x3100c0, 0x1a100f0,
    0x346d560, 0x3326d20, 0x347ce70, 0x3326a38, 0x3326468, 0x3326308,
    0x53e880, 0x1238,
    "e97b9f3100",
    "48895c2408574883ec200fb60133ff488bd984c0743e3c2e752f8bd34533c02bd1e85ab7faff48c1e82069c895e9d15b"};
// The code bytes both builds share (native_ads_blur.cpp:306-309).
static const char *const HEX_CALL = "41ff13";
static const char *const HEX_SETTER = "48895c24084889742410574883ec404c8b11498bd94c8bca418bf8448bc249c1e920488d542460488bf141ff5238";
static const char *const HEX_PACK = "418bc048c1e0208bca480bc1c3";
static const char *const HEX_LOOKUP = "405383b9a8000000004c8bda48897c2418488bd9c744241000000000";

// Every allocation, and the build-specific places inside the images, by name. An address in one world is
// compared with an address in the other as (name, bytes after it).
enum A {
  A_GAME, A_EXE, A_CONTEXT, A_CAMERA, A_OWNER, A_CONTROLLER, A_DESCRIPTOR, A_REGISTRY, A_UNITS, A_GENERATIONS,
  A_BODY, A_ROOT, A_SYSTEM, A_PROVIDER, A_APIS, A_HASH_API, A_POOL, A_PBUCKETS, A_PDESCS, A_PDESC, A_PROWS,
  A_BUCKETS, A_IDS, A_CLASSES, A_CHAINS, A_BLOBS, A_BLOB0, A_BLOB1, A_IO, A_VARIANT, A_SPARE,
  A_REGIONS,
  A_G_CONTEXT = A_REGIONS, A_G_OWNER, A_G_CONTROLLER, A_G_POOL, A_G_ROOT, A_G_APIS, A_G_CALL,
  A_E_SETTER, A_E_PACK, A_E_LOOKUP, A_E_HASH, A_E_HASH_BODY, A_E_VTABLE, A_E_APPLY, A_E_QUERY, A_E_TABLE, A_E_GETTER, A_E_REGISTRY,
  A_O_FLAGS0, A_O_FLAGS1, A_O_FLAGS2, A_O_FLAGS3, A_O_FLAGS4, A_O_FLAGS5, A_O_FLAGS6, A_O_FLAGS7,
  A_COUNT
};
static const char *const A_NAMES[A_COUNT] = {
    "game", "exe", "context", "camera", "avatar_owner", "controller", "descriptor", "registry", "units", "generations",
    "body", "root", "system", "provider", "apis", "hash_api", "param_pool", "param_buckets", "param_descriptors",
    "param_descriptor", "param_rows", "buckets", "ids", "classes", "chains", "blobs", "blob0", "blob1", "io", "variant", "spare",
    "game:context", "game:avatar_owner", "game:controller", "game:param_pool", "game:root", "game:api_root", "game:call",
    "exe:setter", "exe:pack", "exe:lookup", "exe:hash", "exe:hash_body", "exe:vtable", "exe:apply", "exe:query", "exe:table",
    "exe:getter", "exe:registry",
    "owner:flags[0]", "owner:flags[1]", "owner:flags[2]", "owner:flags[3]", "owner:flags[4]", "owner:flags[5]", "owner:flags[6]", "owner:flags[7]"};
struct Sym { int id; uint64_t offset; };
// Places in the io page.
enum : uint64_t { IO_CONFIG = 0, IO_GENERATION = 0x100, IO_STATS = 0x200, IO_STATS2 = 0x800 };

enum : uint32_t { AVATAR = 0x333, BODY_UNIT = 0x400003, WEAPON = 0x444, ENTITY_A = 107, ENTITY_B = 108, DOF = 0xF7A3101F,
                  H_NEAR = 0xA7990477, H_FAR = 0xF07FC12E, H_FAR_CLOSE = 0x209F72CF, H_OTHER = 0x0DB64441 };

struct World {
  const Layout *L = nullptr;
  uint64_t a[A_COUNT] = {};
  uint64_t size[A_REGIONS] = {};
  struct Region { uint64_t base, size; int id; };
  std::vector<Region> regions;
  std::vector<int> inside[A_REGIONS];
  uint64_t native_value = 0, hook_value = 0;      // what the dispatch slot holds untouched, and with the helper in it

  template <class V> static void put(uint64_t at, const V &v) { std::memcpy(reinterpret_cast<void *>(at), &v, sizeof v); }
  static void literal(uint64_t at, const char *hex) {
    auto digit = [](char c) -> uint8_t { return static_cast<uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10); };
    for (size_t i = 0; i < std::strlen(hex) / 2; ++i) put<uint8_t>(at + i, static_cast<uint8_t>((digit(hex[i * 2]) << 4) | digit(hex[i * 2 + 1])));
  }
  static void pe_header(uint64_t base, uint32_t stamp, uint32_t length) {
    put<uint16_t>(base, 0x5A4D); put<int32_t>(base + 0x3c, 128);
    put<uint32_t>(base + 128, 0x00004550); put<uint16_t>(base + 128 + 4, 0x8664); put<uint32_t>(base + 128 + 8, stamp);
    put<uint16_t>(base + 128 + 24, 0x20b); put<uint32_t>(base + 128 + 80, length);
  }
  void region(int id, uint64_t bytes) {
    bytes = (bytes + 4095) & ~4095ull;
    // An unreadable page on each side: a read that runs off a region fails the same way in both worlds.
    void *p = mmap(nullptr, bytes + 8192, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) { std::perror("mmap"); std::_Exit(3); }
    p = static_cast<char *>(p) + 4096;
    if (mprotect(p, bytes, PROT_READ | PROT_WRITE)) { std::perror("mprotect"); std::_Exit(3); }
    a[id] = reinterpret_cast<uint64_t>(p); size[id] = bytes;
    regions.push_back({a[id], bytes, id});
  }
  void inner(int id, int parent, uint64_t offset) { a[id] = a[parent] + offset; inside[parent].push_back(id); }
  uint64_t at(Sym s) const { return a[s.id] + s.offset; }
  uint64_t slot() const { return a[A_E_TABLE]; }
  uint64_t variant() const { return a[A_VARIANT]; }
  // (name, offset) of an address; name 0xffff = in none of this world's regions.
  Sym name(uint64_t address) const {
    auto next = std::upper_bound(regions.begin(), regions.end(), address, [](uint64_t v, const Region &r) { return v < r.base; });
    if (next == regions.begin()) return {0xffff, address};
    const Region &r = *(next - 1);
    if (address - r.base >= r.size + 4096) return {0xffff, address}; // the unreadable page after a region still names it
    int best = r.id;
    for (int id : inside[r.id]) if (a[id] <= address && a[id] >= a[best]) best = id;
    return {best, address - a[best]};
  }

  // The state of blur_fixture.cpp's Fixture (one eligible optic component: entity 108, row 5, blob0), in
  // this world's layout. blob1 is ready for a second component (entity 107, row 6) but not linked.
  void build(const Layout &layout) {
    L = &layout;
    region(A_GAME, L->game_size); region(A_EXE, L->exe_size); region(A_CONTEXT, 0x3000); region(A_CAMERA, 0x1000);
    region(A_OWNER, 0x548000); region(A_CONTROLLER, 0x1000); region(A_DESCRIPTOR, 0x1000); region(A_REGISTRY, 0x1000);
    region(A_UNITS, 0x1000); region(A_GENERATIONS, 0x1000); region(A_BODY, 0x1000); region(A_ROOT, 0x1000);
    region(A_SYSTEM, 0x32000); region(A_PROVIDER, 0x1000); region(A_APIS, 0x1000); region(A_HASH_API, 0x1000);
    region(A_POOL, 0x1000); region(A_PBUCKETS, 0x9000); region(A_PDESCS, 0x80000); region(A_PDESC, 0x1000); region(A_PROWS, 0x80000);
    region(A_BUCKETS, 0xD000); region(A_IDS, 0x4000); region(A_CLASSES, 0x4000); region(A_CHAINS, 0x4000); region(A_BLOBS, 0x8000);
    region(A_BLOB0, 0x3000); region(A_BLOB1, 0x3000); region(A_IO, 0x1000); region(A_VARIANT, 0x1000); region(A_SPARE, 0x2000);
    std::sort(regions.begin(), regions.end(), [](const Region &x, const Region &y) { return x.base < y.base; });
    inner(A_G_CONTEXT, A_GAME, L->g_context); inner(A_G_OWNER, A_GAME, L->g_owner); inner(A_G_CONTROLLER, A_GAME, L->g_controller);
    inner(A_G_POOL, A_GAME, L->g_pool); inner(A_G_ROOT, A_GAME, L->g_root); inner(A_G_APIS, A_GAME, L->g_api_root); inner(A_G_CALL, A_GAME, L->call);
    inner(A_E_SETTER, A_EXE, L->setter); inner(A_E_PACK, A_EXE, L->pack); inner(A_E_LOOKUP, A_EXE, L->lookup); inner(A_E_HASH, A_EXE, L->hash);
    inner(A_E_HASH_BODY, A_EXE, L->hash_body); inner(A_E_VTABLE, A_EXE, L->vtable); inner(A_E_APPLY, A_EXE, L->apply); inner(A_E_QUERY, A_EXE, L->query);
    inner(A_E_TABLE, A_EXE, L->table); inner(A_E_GETTER, A_EXE, L->getter); inner(A_E_REGISTRY, A_EXE, L->registry);
    for (int i = 0; i < 8; ++i) inner(A_O_FLAGS0 + i, A_OWNER, L->avatar_flags + static_cast<uint64_t>(i) * L->avatar_stride);

    const uint64_t game = a[A_GAME], exe = a[A_EXE];
    pe_header(game, L->game_stamp, L->game_size); pe_header(exe, L->exe_stamp, L->exe_size);
    literal(game + L->call, HEX_CALL); literal(exe + L->setter, HEX_SETTER); literal(exe + L->pack, HEX_PACK);
    literal(exe + L->lookup, HEX_LOOKUP); literal(exe + L->hash, L->hex_hash); literal(exe + L->hash_body, L->hex_hash_body);
    // owner
    put<uint64_t>(game + L->g_context, a[A_CONTEXT]); put<uint64_t>(a[A_CONTEXT], a[A_CAMERA]);
    put<uint64_t>(game + L->g_owner, a[A_OWNER]); put<uint64_t>(game + L->g_controller, a[A_CONTROLLER]);
    put<uint32_t>(a[A_OWNER] + 0x6C, 1);
    for (int i = 0; i < 8; ++i) put<uint64_t>(a[A_OWNER] + 0x110 + i * 8, a[A_DESCRIPTOR]);
    put<uint32_t>(a[A_DESCRIPTOR] + 8, AVATAR); put<uint32_t>(a[A_DESCRIPTOR] + 12, BODY_UNIT);
    put<uint64_t>(exe + L->registry, a[A_REGISTRY]); put<uint32_t>(a[A_REGISTRY] + 0x98, 16);
    put<uint64_t>(a[A_REGISTRY] + 0x88, a[A_UNITS]); put<uint64_t>(a[A_REGISTRY] + 0xA0, a[A_GENERATIONS]);
    put<uint8_t>(a[A_GENERATIONS] + 3, 1); put<uint64_t>(a[A_UNITS] + 3 * 8, a[A_BODY]);
    // provider identity
    put<uint64_t>(game + L->g_root, a[A_ROOT]); put<uint64_t>(a[A_ROOT] + 0x18, a[A_SYSTEM]); put<uint64_t>(a[A_SYSTEM] + 0xDC8, a[A_PROVIDER]);
    put<uint64_t>(game + L->g_api_root, a[A_APIS]); put<uint64_t>(a[A_APIS] + 0xC0, a[A_HASH_API]); put<uint64_t>(a[A_HASH_API] + 0x28, exe + L->hash);
    put<uint64_t>(a[A_PROVIDER], exe + L->vtable); put<uint64_t>(exe + L->vtable + 0x38, exe + L->lookup);
    put<uint64_t>(exe + L->vtable + 0x28, exe + L->apply);
    put<uint64_t>(a[A_SYSTEM] + 0xDD8, exe + L->query); put<uint64_t>(exe + L->query + 0x28, exe + L->pack);
    put<uint64_t>(a[A_SYSTEM] + 0xDE8, exe + L->table); put<uint64_t>(exe + L->table + 8, exe + L->getter);
    // weapon parameters
    put<uint64_t>(game + L->g_pool, a[A_POOL]); put<uint64_t>(a[A_POOL] + 0x278, a[A_PBUCKETS]); put<uint32_t>(a[A_POOL] + 0x280, 8);
    put<uint32_t>(a[A_POOL] + 0x284, UINT32_MAX); put<uint32_t>(a[A_POOL] + 0x288, 1);
    for (unsigned i = 0; i < 0x9000 / 8; ++i) put<uint32_t>(a[A_PBUCKETS] + i * 8, UINT32_MAX);
    put<uint32_t>(a[A_PBUCKETS] + (WEAPON % 8) * 8, WEAPON); put<uint32_t>(a[A_PBUCKETS] + (WEAPON % 8) * 8 + 4, 1);
    put<uint64_t>(a[A_POOL] + 0x290, a[A_PDESCS]); put<uint64_t>(a[A_PDESCS] + 8, a[A_PDESC]);
    put<uint64_t>(a[A_POOL] + 0x298, a[A_PROWS]); put<uint32_t>(a[A_PDESC] + 8, WEAPON); put<uint8_t>(a[A_PDESC] + 20, 1);
    put<uint32_t>(a[A_PROWS] + 8, 16); put<uint32_t>(a[A_PROWS] + 12, 17);
    put<uint32_t>(a[A_SYSTEM] + 0x348 + 16 * 0x30, ENTITY_A); put<uint32_t>(a[A_SYSTEM] + 0x348 + 17 * 0x30, ENTITY_B);
    // components
    put<uint64_t>(a[A_PROVIDER] + 0x98, a[A_BUCKETS]); put<uint32_t>(a[A_PROVIDER] + 0x90, 54); put<uint32_t>(a[A_PROVIDER] + 0x94, 54);
    put<uint32_t>(a[A_PROVIDER] + 0xA8, 38); put<uint32_t>(a[A_PROVIDER] + 0xAC, 39); put<uint32_t>(a[A_PROVIDER] + 0xB8, 92); put<uint32_t>(a[A_PROVIDER] + 0xBC, 256);
    put<uint64_t>(a[A_PROVIDER] + 0xC8, a[A_IDS]); put<uint64_t>(a[A_PROVIDER] + 0xD0, a[A_CLASSES]); put<uint64_t>(a[A_PROVIDER] + 0xD8, a[A_CHAINS]); put<uint64_t>(a[A_PROVIDER] + 0xE8, a[A_BLOBS]);
    for (unsigned i = 0; i < 0xD000 / 12; ++i) put<uint32_t>(a[A_BUCKETS] + i * 12 + 8, 0xFFFFFFFE);
    put<uint32_t>(a[A_BUCKETS] + (107 % 39) * 12, 107); put<uint32_t>(a[A_BUCKETS] + (107 % 39) * 12 + 4, 6); put<uint32_t>(a[A_BUCKETS] + (107 % 39) * 12 + 8, 0x7FFFFFFF);
    put<uint32_t>(a[A_IDS] + 6 * 4, 107); put<uint32_t>(a[A_CLASSES] + 6 * 4, 0xC823B21C);
    // The entity 108 bucket collides into overflow 40 (> modulus 39).
    put<uint32_t>(a[A_BUCKETS] + (108 % 39) * 12, 999); put<uint32_t>(a[A_BUCKETS] + (108 % 39) * 12 + 8, 40);
    put<uint32_t>(a[A_BUCKETS] + 40 * 12, 108); put<uint32_t>(a[A_BUCKETS] + 40 * 12 + 4, 3); put<uint32_t>(a[A_BUCKETS] + 40 * 12 + 8, 0x7FFFFFFF);
    put<uint32_t>(a[A_IDS] + 3 * 4, 108); put<uint32_t>(a[A_CLASSES] + 3 * 4, 0xC823B21C); put<uint32_t>(a[A_CHAINS] + 3 * 4, 4);
    put<uint32_t>(a[A_IDS] + 4 * 4, 108); put<uint32_t>(a[A_CLASSES] + 4 * 4, 0x35B0F6C2); put<uint32_t>(a[A_CHAINS] + 4 * 4, 5);
    put<uint32_t>(a[A_IDS] + 5 * 4, 108); put<uint32_t>(a[A_CLASSES] + 5 * 4, DOF);
    put<uint64_t>(a[A_BLOBS] + 5 * 8, a[A_BLOB0]);
    for (uint64_t b : {a[A_BLOB0], a[A_BLOB1]}) {
      put<uint32_t>(b + 4, 4); put<uint32_t>(b + 0x10, H_NEAR); put<uint32_t>(b + 0x14, H_FAR);
      put<uint32_t>(b + 0x18, H_FAR_CLOSE); put<uint32_t>(b + 0x1C, H_OTHER);
      for (unsigned i = 0; i < 4; ++i) { put<uint32_t>(b + 0x20 + i * 16, 2); put<float>(b + 0x28 + i * 16, .5f); }
    }
    // controls, flags, request ring (the normal request, kind 4, at record 1), the game's variant
    put<uint32_t>(a[A_CONTEXT] + 0x1F8, 0); put<uint32_t>(a[A_CONTEXT] + 0x1FC, 2);
    const uint32_t record[4] = {4, AVATAR, WEAPON, BODY_UNIT}; put(a[A_CONTEXT] + 0x200 + 0xE8, record);
    uint8_t input[24]; for (unsigned i = 0; i < 24; ++i) input[i] = static_cast<uint8_t>(i + 0x80);
    const uint32_t type = 2; const float f = 1.5f; std::memcpy(input, &type, 4); std::memcpy(input + 8, &f, 4);
    put(a[A_VARIANT], input);
  }
};

} // namespace dt
