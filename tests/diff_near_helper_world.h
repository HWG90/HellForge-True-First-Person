// Fake game memory for tests/diff_near_helper.cpp: one logical state, written into real pages twice, once
// with the retail build's addresses and offsets (26568081, from native_ads_near.cpp) and once with this
// build's (25480438, from the table in docs/translation/NEAR-HELPER-TRANSLATION.md, not from the #define block). The
// structure layout numbers both builds share are written here once, as the retail source has them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>

namespace dt {

struct Layout {
  uint32_t game_stamp, game_size, exe_stamp, exe_size;
  uint32_t setter, getter, site, native_near, text, text_end;
  const char *hex_site, *hex_setter;
  uint32_t g_context, g_owner, g_controller, g_api_root;
  uint32_t avatar_flags, avatar_stride;
};
// native_ads_near.cpp:31, 130-138, 168-172, 178.
static const Layout RETAIL = {
    0x6af322c1, 0x3382000, 0x6af2d5e6, 0x29fe000,
    0x2dd050, 0x2dd030, 0x110ddec, 0x2245efc, 0x1000, 0x1f99000,
    "488b05d536bd01f30f100d01811301498b0f440f29bc2490010000488b5020ff5268", "f30f114928e9c66b1f00",
    0x2e287d0, 0x2ce1ee0, 0x2e381a8, 0x2ce14c8,
    0x53e888, 0x1240};
// docs/translation/NEAR-HELPER-TRANSLATION.md section 2, column "here".
static const Layout HERE = {
    0x6ab3b43f, 0x4744000, 0x6ab382e4, 0x39e8000,
    0x30d960, 0x30d940, 0x127bf2c, 0x23c66d4, 0x1000, 0x2111000,
    "488b05d5a30a02f30f100d99a71401498b0f440f29bc2490010000488b5020ff5268", "f30f114928e9a6201c00",
    0x346d560, 0x3326d20, 0x347ce70, 0x3326308,
    0x53e880, 0x1238};

// Every allocation, and the build-specific places inside the images, by name. An address in one world is
// compared with an address in the other as (name, bytes after it).
enum A {
  A_GAME, A_EXE, A_EXE2, A_CONTEXT, A_CONTEXT2, A_CAMERA, A_CAMERA2, A_OWNER, A_OWNER2, A_CONTROLLER, A_CONTROLLER2,
  A_API_ROOT, A_API_ROOT2, A_API, A_API2, A_IO, A_GEN, A_RO,
  A_REGIONS,
  A_G_CONTEXT = A_REGIONS, A_G_OWNER, A_G_CONTROLLER, A_G_API_ROOT, A_G_SITE, A_G_NEAR,
  A_E_SETTER, A_E_GETTER, A_E2_SETTER, A_E2_GETTER,
  A_O_FLAGS, A_O2_FLAGS = A_O_FLAGS + 8,
  A_COUNT = A_O2_FLAGS + 8
};
static const char *const A_NAMES[A_COUNT] = {
    "game", "exe", "exe2", "context", "context2", "camera", "camera2", "avatar_owner", "avatar_owner2", "controller", "controller2",
    "api_root", "api_root2", "api", "api2", "io", "generation", "readonly",
    "game:context", "game:avatar_owner", "game:controller", "game:api_root", "game:site", "game:near_constant",
    "exe:setter", "exe:getter", "exe2:setter", "exe2:getter",
    "owner:flags[0]", "owner:flags[1]", "owner:flags[2]", "owner:flags[3]", "owner:flags[4]", "owner:flags[5]", "owner:flags[6]", "owner:flags[7]",
    "owner2:flags[0]", "owner2:flags[1]", "owner2:flags[2]", "owner2:flags[3]", "owner2:flags[4]", "owner2:flags[5]", "owner2:flags[6]", "owner2:flags[7]"};
struct Sym { int id; uint64_t offset; };
// Places in the io page.
enum : uint64_t { IO_CONFIG = 0, IO_STATS = 0x200, IO_SITES = 0x400, IO_SCRIPT_STATS = 0x800, IO_SCRIPT_SITES = 0xA00, IO_FORWARD_SITES = 0xD00 };

struct Scene {
  uint32_t head = 0, tail = 2;       // the request ring
  uint32_t kind = 6, idle = 4;       // the newest request record's kind word, and the idle record's
  uint8_t controls[6] = {};          // the controller's first six bytes; [4] is full ADS
  uint8_t flags[24] = {};            // the avatar row's flag bytes; [10]&4 with [15]&32 is normal aim
  uint32_t count = 1;                // avatar rows in use
};

struct World {
  const Layout *L = nullptr;
  uint64_t a[A_COUNT] = {};
  uint64_t size[A_REGIONS] = {};
  struct Region { uint64_t base, size; int id; };
  std::vector<Region> regions;
  std::vector<int> inside[A_REGIONS];
  uint64_t native_value = 0, native2_value = 0, hook_value = 0; // what the dispatch slot holds untouched (exe, exe2), and with the helper in it

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
  uint64_t slot() const { return a[A_API] + 0x68; }
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
  // Callers of the slot (return addresses), by logical number.
  enum { CALL_SITE = 0, CALL_TEXT_FIRST, CALL_BELOW_TEXT, CALL_TEXT_LAST, CALL_TEXT_END, CALL_FOREIGN, CALL_GAME_BASE, CALL_ZERO_PAGE, CALL_MANY = 10, CALL_MANY_COUNT = 40 };
  uint64_t caller(int id) const {
    switch (id) {
    case CALL_SITE: return a[A_GAME] + L->site + 34;                  // the return address of the pinned call [rdx+0x68]
    case CALL_TEXT_FIRST: return a[A_GAME] + L->text;
    case CALL_BELOW_TEXT: return a[A_GAME] + L->text - 1;
    case CALL_TEXT_LAST: return a[A_GAME] + L->text_end - 1;
    case CALL_TEXT_END: return a[A_GAME] + L->text_end;
    case CALL_FOREIGN: return 0x5550000;
    case CALL_GAME_BASE: return a[A_GAME];
    case CALL_ZERO_PAGE: return 1;
    default: return a[A_GAME] + 0x2000 + static_cast<uint64_t>(id - CALL_MANY) * 16;
    }
  }
  int caller_id(uint64_t address) const {
    for (int id = 0; id < CALL_MANY + CALL_MANY_COUNT; ++id) if ((id < 8 || id >= CALL_MANY) && caller(id) == address) return id;
    return -1;
  }

  void build(const Layout &layout) {
    L = &layout;
    region(A_GAME, L->game_size); region(A_EXE, L->exe_size); region(A_EXE2, L->exe_size);
    region(A_CONTEXT, 0x3000); region(A_CONTEXT2, 0x3000); region(A_CAMERA, 0x1000); region(A_CAMERA2, 0x1000);
    region(A_OWNER, 0x548000); region(A_OWNER2, 0x548000); region(A_CONTROLLER, 0x1000); region(A_CONTROLLER2, 0x1000);
    region(A_API_ROOT, 0x1000); region(A_API_ROOT2, 0x1000); region(A_API, 0x1000); region(A_API2, 0x1000);
    region(A_IO, 0x1000); region(A_GEN, 0x1000); region(A_RO, 0x1000);
    std::sort(regions.begin(), regions.end(), [](const Region &x, const Region &y) { return x.base < y.base; });
    inner(A_G_CONTEXT, A_GAME, L->g_context); inner(A_G_OWNER, A_GAME, L->g_owner); inner(A_G_CONTROLLER, A_GAME, L->g_controller);
    inner(A_G_API_ROOT, A_GAME, L->g_api_root); inner(A_G_SITE, A_GAME, L->site); inner(A_G_NEAR, A_GAME, L->native_near);
    inner(A_E_SETTER, A_EXE, L->setter); inner(A_E_GETTER, A_EXE, L->getter);
    inner(A_E2_SETTER, A_EXE2, L->setter); inner(A_E2_GETTER, A_EXE2, L->getter);
    for (int row = 0; row < 8; ++row) {
      inner(A_O_FLAGS + row, A_OWNER, L->avatar_flags + static_cast<uint64_t>(row) * L->avatar_stride);
      inner(A_O2_FLAGS + row, A_OWNER2, L->avatar_flags + static_cast<uint64_t>(row) * L->avatar_stride);
    }
    base();
    // The read-only page: a statistics header a helper can read, and nothing it can write.
    put<uint32_t>(a[A_RO], 88); put<uint32_t>(a[A_RO] + 4, 1);
    mprotect(reinterpret_cast<void *>(a[A_RO]), 0x1000, PROT_READ);
  }
  // Everything that does not change with the scene: images, code bytes and the pointer chains. Scripts damage
  // these and call base() again to heal them. The dispatch slot is not touched here.
  void base() {
    const uint64_t game = a[A_GAME];
    pe_header(game, L->game_stamp, L->game_size);
    literal(game + L->site, L->hex_site); literal(game + L->native_near, "cdcc4c3d");
    for (int e : {A_EXE, A_EXE2}) {
      pe_header(a[e], L->exe_stamp, L->exe_size);
      literal(a[e] + L->setter, L->hex_setter); literal(a[e] + L->getter, "f30f104128c3");
    }
    put<uint64_t>(game + L->g_context, a[A_CONTEXT]); put<uint64_t>(game + L->g_owner, a[A_OWNER]);
    put<uint64_t>(game + L->g_controller, a[A_CONTROLLER]); put<uint64_t>(game + L->g_api_root, a[A_API_ROOT]);
    put<uint64_t>(a[A_CONTEXT], a[A_CAMERA]); put<uint64_t>(a[A_CONTEXT2], a[A_CAMERA2]);
    put<uint64_t>(a[A_API_ROOT] + 0x20, a[A_API]); put<uint64_t>(a[A_API_ROOT2] + 0x20, a[A_API2]);
    put<uint64_t>(a[A_API] + 0x58, a[A_EXE] + L->getter); put<uint64_t>(a[A_API2] + 0x58, a[A_EXE] + L->getter);
    put<uint32_t>(a[A_OWNER2] + 0x6C, 1);
  }
  // The scene, as the game would have it in memory before it calls the near-distance setter.
  void apply(const Scene &s, uint32_t avatar_index) {
    const uint64_t context = a[A_CONTEXT];
    put<uint32_t>(context + 0x1F8, s.head); put<uint32_t>(context + 0x1FC, s.tail);
    put<uint32_t>(context + 0x200 + ((s.tail + 31) % 32) * 0xE8, s.kind); put<uint32_t>(context + 0x1F00, s.idle);
    std::memcpy(reinterpret_cast<void *>(a[A_CONTROLLER]), s.controls, 6);
    std::memcpy(reinterpret_cast<void *>(a[A_OWNER] + L->avatar_flags + static_cast<uint64_t>(avatar_index & 7) * L->avatar_stride), s.flags, 24);
    put<uint32_t>(a[A_OWNER] + 0x6C, s.count);
  }
};

} // namespace dt
