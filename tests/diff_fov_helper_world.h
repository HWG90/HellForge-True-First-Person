// Fake game memory for tests/diff_fov_helper.cpp: the same logical state in real pages twice, once with the
// retail build's addresses and offsets (26568081, from native_ads_fov.cpp) and once with this build's
// (25480438, from the table in docs/FOV-HELPER-TRANSLATION.md, not from the #define block of the C file).
// The structure layout numbers both builds share are written here once, as the retail source has them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>

namespace dt {

struct Layout {
  uint32_t game_stamp, game_size, exe_stamp, exe_size;
  uint32_t setter, getter, call;
  const char *hex_setter;
  uint32_t g_context, g_owner, g_controller, e_registry, g_state, state_fov, g_api_root;
  uint32_t avatar_flags, avatar_stride;
  uint32_t fird[6];
  const char *hex_fird[6];
};
// native_ads_fov.cpp:21, 269-275, 431, 467-469, 476-481, 488.
static const Layout RETAIL = {
    0x6af322c1, 0x3382000, 0x6af2d5e6, 0x29fe000,
    0x2dd080, 0x2dd070, 0x110bfda,
    "f30f114934c7415000000000e92f6c1f00",
    0x2e287d0, 0x2ce1ee0, 0x2e381a8, 0x1ab76f8, 0x2ce1508, 0xac488, 0x2ce14c8,
    0x53e888, 0x1240,
    {0x110b4db, 0x111e3ce, 0x111ba5e, 0x111ba70, 0x111ba91, 0x111baa2},
    {"e8e0fc0000", "f3450f119c24c4000000f3450f115c243c", "f3410f1044243c",
     "448bc14969c0e8000000664283bc300002000004740bffc183e11f3bca75e1eb11", "4969c0e8000000f3420f1084303c020000",
     "f30f590506b51201488d55a0488b05535abc01f30f5e8088c40a00f30f59d8f30f59d0f30f59c8"}};
// docs/FOV-HELPER-TRANSLATION.md section 2, column "here".
static const Layout HERE = {
    0x6ab3b43f, 0x4744000, 0x6ab382e4, 0x39e8000,
    0x30d990, 0x30d980, 0x127a11a,
    "f30f114934c7415000000000e90f211c00",
    0x346d560, 0x3326d20, 0x347ce70, 0x1a100f0, 0x3326340, 0xac408, 0x3326308,
    0x53e880, 0x1238,
    {0x127961b, 0x128c38e, 0x1289a1e, 0x1289a30, 0x1289a51, 0x1289a62},
    {"e860fb0000", "f3450f119c24c4000000f3450f115c243c", "f3410f1044243c",
     "448bc14969c0e8000000664283bc300002000004740bffc183e11f3bca75e1eb11", "4969c0e8000000f3420f1084303c020000",
     "f30f5905c6dc1301488d55a0488b05cbc80902f30f5e8008c40a00f30f59d8f30f59d0f30f59c8"}};

// Every allocation, and the build-specific places inside them, by name. An address in one world is compared
// with an address in the other as (name, bytes after it).
enum A {
  A_GAME, A_EXE, A_CONTEXT, A_CAMERA, A_OWNER, A_CONTROLLER, A_DESCRIPTOR, A_DESCRIPTOR1, A_REGISTRY, A_UNITS,
  A_GENERATIONS, A_BODY, A_BODY1, A_SETTINGS, A_API_ROOT, A_API, A_API_OTHER, A_SPARE, A_IO,
  A_REGIONS,
  A_G_CONTEXT = A_REGIONS, A_G_OWNER, A_G_CONTROLLER, A_G_STATE, A_G_API_ROOT, A_G_CALL,
  A_G_FIRD0, A_G_FIRD1, A_G_FIRD2, A_G_FIRD3, A_G_FIRD4, A_G_FIRD5,
  A_E_SETTER, A_E_GETTER, A_E_REGISTRY, A_O_FLAGS0, A_O_FLAGS1, A_S_FOV,
  A_COUNT
};
static const char *const A_NAMES[A_COUNT] = {
    "game", "exe", "context", "camera", "avatar_owner", "controller", "descriptor[0]", "descriptor[1]", "registry", "units",
    "generations", "body[0]", "body[1]", "settings", "api_root", "api", "api_other", "spare", "io",
    "game:context", "game:avatar_owner", "game:controller", "game:state", "game:api_root", "game:call",
    "game:fird_dispatch", "game:fird_publish", "game:fird_input", "game:fird_select", "game:fird_row", "game:fird_scale",
    "exe:setter", "exe:getter", "exe:registry", "owner:flags[0]", "owner:flags[1]", "settings:fov"};
struct Sym { int id; uint64_t offset; };
// Places in the io page.
enum : uint64_t { IO_CONFIG = 0, IO_GENERATION = 0x100, IO_STATS = 0x200, IO_STATS_SCRIPT = 0x300 };
// The request records of the camera context: 32 ring rows and the idle record (row 32).
enum { IDLE_ROW = 32 };
static uint64_t row_offset(int row) { return row == IDLE_ROW ? 0x1F00 : 0x200 + static_cast<uint64_t>(row) * 0xE8; }
enum : uint32_t { AVATAR = 0x333, BODY = 0x400003, AVATAR1 = 0x334, BODY1 = 0x400005, WEAPON = 0x444, WEAPON_B = 0x555 };

struct World {
  const Layout *L = nullptr;
  uint64_t a[A_COUNT] = {};
  uint64_t size[A_REGIONS] = {};
  struct Region { uint64_t base, size; int id; };
  std::vector<Region> regions;
  std::vector<int> inside[A_REGIONS];
  uint64_t native_value = 0, hook_value = 0; // what the dispatch slot holds untouched, and with the helper in it

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
  uint64_t slot() const { return a[A_API] + 0x80; }
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

  void build(const Layout &layout) {
    L = &layout;
    region(A_GAME, L->game_size); region(A_EXE, L->exe_size); region(A_CONTEXT, 0x3000); region(A_CAMERA, 0x1000);
    region(A_OWNER, 0x548000); region(A_CONTROLLER, 0x1000); region(A_DESCRIPTOR, 0x1000); region(A_DESCRIPTOR1, 0x1000);
    region(A_REGISTRY, 0x1000); region(A_UNITS, 0x1000); region(A_GENERATIONS, 0x1000); region(A_BODY, 0x1000); region(A_BODY1, 0x1000);
    region(A_SETTINGS, 0xAD000); region(A_API_ROOT, 0x1000); region(A_API, 0x1000); region(A_API_OTHER, 0x1000);
    region(A_SPARE, 0x3000); region(A_IO, 0x1000);
    std::sort(regions.begin(), regions.end(), [](const Region &x, const Region &y) { return x.base < y.base; });
    inner(A_G_CONTEXT, A_GAME, L->g_context); inner(A_G_OWNER, A_GAME, L->g_owner); inner(A_G_CONTROLLER, A_GAME, L->g_controller);
    inner(A_G_STATE, A_GAME, L->g_state); inner(A_G_API_ROOT, A_GAME, L->g_api_root); inner(A_G_CALL, A_GAME, L->call);
    for (int i = 0; i < 6; ++i) inner(A_G_FIRD0 + i, A_GAME, L->fird[i]);
    inner(A_E_SETTER, A_EXE, L->setter); inner(A_E_GETTER, A_EXE, L->getter); inner(A_E_REGISTRY, A_EXE, L->e_registry);
    inner(A_O_FLAGS0, A_OWNER, L->avatar_flags); inner(A_O_FLAGS1, A_OWNER, L->avatar_flags + L->avatar_stride);
    inner(A_S_FOV, A_SETTINGS, L->state_fov);
    write_static();
  }
  // Everything a lease does not change: images, code bytes, the pointer chains, the units, the setting.
  // (Not the dispatch slot, the request records, the controller or the avatar's flag bytes.)
  void write_static() {
    const uint64_t game = a[A_GAME], exe = a[A_EXE];
    pe_header(game, L->game_stamp, L->game_size); pe_header(exe, L->exe_stamp, L->exe_size);
    literal(game + L->call, "ff9280000000"); literal(exe + L->setter, L->hex_setter); literal(exe + L->getter, "f30f104134c3");
    for (int i = 0; i < 6; ++i) literal(game + L->fird[i], L->hex_fird[i]);
    put<uint64_t>(game + L->g_context, a[A_CONTEXT]); put<uint64_t>(a[A_CONTEXT], a[A_CAMERA]);
    put<uint64_t>(game + L->g_owner, a[A_OWNER]); put<uint64_t>(game + L->g_controller, a[A_CONTROLLER]);
    put<uint32_t>(a[A_OWNER] + 0x6C, 2); put<uint64_t>(a[A_DESCRIPTOR], 0);
    put<uint64_t>(a[A_OWNER] + 0x110, a[A_DESCRIPTOR]); put<uint64_t>(a[A_OWNER] + 0x118, a[A_DESCRIPTOR1]);
    put<uint32_t>(a[A_DESCRIPTOR] + 8, AVATAR); put<uint32_t>(a[A_DESCRIPTOR] + 12, BODY);
    put<uint32_t>(a[A_DESCRIPTOR1] + 8, AVATAR1); put<uint32_t>(a[A_DESCRIPTOR1] + 12, BODY1);
    put<uint64_t>(exe + L->e_registry, a[A_REGISTRY]); put<uint32_t>(a[A_REGISTRY] + 0x98, 16);
    put<uint64_t>(a[A_REGISTRY] + 0x88, a[A_UNITS]); put<uint64_t>(a[A_REGISTRY] + 0xA0, a[A_GENERATIONS]);
    put<uint64_t>(a[A_GENERATIONS], 0); put<uint8_t>(a[A_GENERATIONS] + 3, 1); put<uint64_t>(a[A_UNITS] + 3 * 8, a[A_BODY]);
    put<uint8_t>(a[A_GENERATIONS] + 5, 1); put<uint64_t>(a[A_UNITS] + 5 * 8, a[A_BODY1]);
    put<uint64_t>(game + L->g_state, a[A_SETTINGS]); put<float>(a[A_SETTINGS] + L->state_fov, 90.0f);
    put<uint64_t>(game + L->g_api_root, a[A_API_ROOT]); put<uint64_t>(a[A_API_ROOT] + 0x20, a[A_API]);
    put<uint64_t>(a[A_API] + 0x78, exe + L->getter); put<uint64_t>(a[A_API_OTHER] + 0x78, exe + L->getter);
    // A second controller and context for "the pointer changed" cases: valid contents at another address.
    put<uint64_t>(a[A_SPARE], a[A_CAMERA]);
  }
};

} // namespace dt
