// Fake game memory for tests/diff_camera_helper.cpp: one logical scene, written into real pages twice,
// once with the retail build's addresses and offsets (26568081, from native_ads_camera.cpp) and once with
// this build's (25480438, from the table in docs/translation/CAMERA-HELPER-TRANSLATION.md). The structure layout
// numbers (the ones both builds share) are written here once, as the retail source has them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>

namespace dt {

struct Layout {
  uint32_t game_stamp, game_size, exe_stamp, exe_size;
  uint32_t setter, call, graph_getter, anim_getter, unit_world, call_pair;
  const char *hex_setter, *hex_unit_world, *hex_call_pair;
  uint32_t g_context, g_owner, g_controller, e_registry, g_state, state_scene, g_api_root;
  uint32_t avatar_flags, avatar_rig, avatar_stride;
};
// native_ads_camera.cpp:23, 262-270, 360, 377, 468, 485, 1029-1032, 1220.
static const Layout RETAIL = {
    0x6af322c1, 0x3382000, 0x6af2d5e6, 0x29fe000,
    0x2dd5a0, 0x110bff5, 0x28d3c0, 0x28d510, 0x1f30e0, 0x110bfe7,
    "48895c2408574883ec60488bd9498bf88bcae81931dcff4c8b4b184c8bd04c6343208b4708498bd048c1e206498b4920",
    "40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3",
    "4c8d45c08b5308488b0b4c8b482041ff5120488b05c854bd014c8d45008b5308488b0b4c8b482041ff5128",
    0x2e287d0, 0x2ce1ee0, 0x2e381a8, 0x1ab76f8, 0x2ce1508, 0xac29c, 0x2ce14c8,
    0x53e888, 0x53e9c0, 0x1240};
// docs/translation/CAMERA-HELPER-TRANSLATION.md section 2, column "here". The three long byte strings are retail's with
// the one displacement the table names replaced.
static const Layout HERE = {
    0x6ab3b43f, 0x4744000, 0x6ab382e4, 0x39e8000,
    0x30deb0, 0x127a135, 0x2bd870, 0x2bd9c0, 0x1fd220, 0x127a127,
    "48895c2408574883ec60488bd9498bf88bcae8f9f9d8ff4c8b4b184c8bd04c6343208b4708498bd048c1e206498b4920",
    "40534883ec204863dae89206eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3",
    "4c8d45c08b5308488b0b4c8b482041ff5120488b05c8c10a024c8d45008b5308488b0b4c8b482041ff5128",
    0x346d560, 0x3326d20, 0x347ce70, 0x1a100f0, 0x3326340, 0xac21c, 0x3326308,
    0x53e880, 0x53e9b8, 0x1238};

// Every allocation, and the build-specific places inside the images, by name. An address in one world is
// compared with an address in the other as (name, bytes after it).
enum A {
  A_GAME, A_EXE, A_CONTEXT, A_CAMERA, A_OWNER, A_CONTROLLER, A_DESCRIPTOR, A_REGISTRY, A_UNITS, A_GENERATIONS,
  A_BODY, A_CAMOBJ, A_BODY_VT, A_HASHES, A_LINKS, A_BODY_WORLD, A_ANIM_INSTANCE, A_ANIM_GRAPH, A_ANIM_CURRENT,
  A_SETTINGS, A_CAM_LOCALS, A_CAM_WORLDS, A_API_ROOT, A_API, A_FPS, A_FPS_WORLD, A_FPS_HASHES, A_FPS_LINKS,
  A_CAM_LINKS, A_W0, A_W0_WORLD, A_W0_LINKS, A_W1, A_W1_WORLD, A_W1_LINKS, A_IO, A_NATIVE,
  A_REGIONS,
  A_G_CONTEXT = A_REGIONS, A_G_OWNER, A_G_CONTROLLER, A_G_STATE, A_G_API_ROOT, A_G_CALL_PAIR,
  A_E_SETTER, A_E_GRAPH_GETTER, A_E_ANIM_GETTER, A_E_UNIT_WORLD, A_E_REGISTRY,
  A_O_FLAGS0, A_O_RIG0, A_O_FLAGS1, A_O_RIG1, A_S_SCENE,
  A_COUNT
};
static const char *const A_NAMES[A_COUNT] = {
    "game", "exe", "context", "camera", "avatar_owner", "controller", "descriptor", "registry", "units", "generations",
    "body", "camera_object", "body_vtable", "body_hashes", "body_links", "body_world", "anim_instance", "anim_graph",
    "anim_current", "settings", "camera_locals", "camera_worlds", "api_root", "api", "rig", "rig_world", "rig_hashes",
    "rig_links", "camera_links", "weapon0", "weapon0_world", "weapon0_links", "weapon1", "weapon1_world",
    "weapon1_links", "io", "argument",
    "game:context", "game:avatar_owner", "game:controller", "game:state", "game:api_root", "game:call_pair",
    "exe:setter", "exe:graph_getter", "exe:anim_getter", "exe:unit_world", "exe:registry",
    "owner:flags[0]", "owner:rig[0]", "owner:flags[1]", "owner:rig[1]", "settings:scene"};
struct Sym { int id; uint64_t offset; };
// Places in the io page.
enum : uint64_t { IO_CONFIG = 0, IO_GENERATION = 0x100, IO_STATS = 0x200, IO_HANDOFF = 0x300, IO_TRACE = 0x400, IO_ANCHOR = 0x600 };

enum { HEAD_NODE = 90, HEAD_PARENT = 89, WEAPON_NODE = 42, BODY_NODES = 107 };

struct Scene {
  uint32_t mask = 0; uint8_t aim = 0, guard = 0;
  uint8_t flags[24] = {};                         // the avatar's flag bytes; the first four are the role word
  uint32_t ring[2] = {0, 1};
  uint32_t request[4] = {4, 0x333, 0x444, 0x400003}; // kind, avatar, weapon, body: the newest request record
  uint32_t idle[4] = {4, 0x333, 0, 0};
  float root[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  float head[3] = {0, 0, 1.5f};
  float node42[3] = {.25f, .36f, 1.09f};
  float context[11] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  bool linked = false;                            // the camera's parent is the first-person rig
  uint64_t parent_raw = 0;                        // the parent value when not linked
  uint32_t camera_index = 0, camera_count = 1, camera_link = 0x00320002;
  // The camera's world rotation rows. Not given: left as the game's rotation setter wrote it after the last call.
  bool camera_rotation_given = true; float camera_rotation[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
  bool camera_split = false; float camera_local[3] = {}, camera_world[3] = {};
  float rig[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  uint32_t rig_reference = 0x400005;
  uint8_t generations[8] = {0, 0, 0, 1, 1, 1, 1, 1};
  struct Weapon { float position[3] = {0, .32f, 1.28f}; int parent = 0; uint32_t link = (WEAPON_NODE << 16) | 2u; } weapon[2];
  uint32_t state = 36, scene = 0;
  uint32_t graph_count = BODY_NODES, head_hash = 0x8C5570C9, head_link = (HEAD_PARENT << 16) | 1u, parent_hash = 0xE9ECCA21;
  float argument[4] = {-2, 0, 1, 0};
  float argument_rotation[4] = {0, 0, 0, 1};
  uint32_t avatar_count = 2;
};

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
  uint64_t slot() const { return a[A_API] + 0x20; }
  uint64_t native() const { return a[A_NATIVE]; }
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
    region(A_OWNER, 0x548000); region(A_CONTROLLER, 0x1000); region(A_DESCRIPTOR, 0x1000); region(A_REGISTRY, 0x1000);
    region(A_UNITS, 0x1000); region(A_GENERATIONS, 0x1000); region(A_BODY, 0x1000); region(A_CAMOBJ, 0x1000);
    region(A_BODY_VT, 0x1000); region(A_HASHES, 0x1000); region(A_LINKS, 0x1000); region(A_BODY_WORLD, BODY_NODES * 64);
    region(A_ANIM_INSTANCE, 0x1000); region(A_ANIM_GRAPH, 0x20000); region(A_ANIM_CURRENT, 0x1000); region(A_SETTINGS, 0xAD000);
    region(A_CAM_LOCALS, 0x1000); region(A_CAM_WORLDS, 0x1000); region(A_API_ROOT, 0x1000); region(A_API, 0x1000);
    region(A_FPS, 0x1000); region(A_FPS_WORLD, 63 * 64); region(A_FPS_HASHES, 0x1000); region(A_FPS_LINKS, 0x1000);
    region(A_CAM_LINKS, 0x1000);
    region(A_W0, 0x1000); region(A_W0_WORLD, 0x1000); region(A_W0_LINKS, 0x1000);
    region(A_W1, 0x1000); region(A_W1_WORLD, 0x1000); region(A_W1_LINKS, 0x1000);
    region(A_IO, 0x1000); region(A_NATIVE, 0x1000);
    std::sort(regions.begin(), regions.end(), [](const Region &x, const Region &y) { return x.base < y.base; });
    inner(A_G_CONTEXT, A_GAME, L->g_context); inner(A_G_OWNER, A_GAME, L->g_owner); inner(A_G_CONTROLLER, A_GAME, L->g_controller);
    inner(A_G_STATE, A_GAME, L->g_state); inner(A_G_API_ROOT, A_GAME, L->g_api_root); inner(A_G_CALL_PAIR, A_GAME, L->call_pair);
    inner(A_E_SETTER, A_EXE, L->setter); inner(A_E_GRAPH_GETTER, A_EXE, L->graph_getter); inner(A_E_ANIM_GETTER, A_EXE, L->anim_getter);
    inner(A_E_UNIT_WORLD, A_EXE, L->unit_world); inner(A_E_REGISTRY, A_EXE, L->e_registry);
    inner(A_O_FLAGS0, A_OWNER, L->avatar_flags); inner(A_O_RIG0, A_OWNER, L->avatar_rig);
    inner(A_O_FLAGS1, A_OWNER, L->avatar_flags + L->avatar_stride); inner(A_O_RIG1, A_OWNER, L->avatar_rig + L->avatar_stride);
    inner(A_S_SCENE, A_SETTINGS, L->state_scene);

    const uint64_t game = a[A_GAME], exe = a[A_EXE];
    pe_header(game, L->game_stamp, L->game_size); pe_header(exe, L->exe_stamp, L->exe_size);
    literal(game + L->call_pair, L->hex_call_pair); // holds the call itself ("41ff5120") 14 bytes in
    literal(exe + L->setter, L->hex_setter); literal(exe + L->unit_world, L->hex_unit_world);
    literal(exe + L->graph_getter, "488d4160c3"); literal(exe + L->anim_getter, "488b8178010000c3");
    put<uint64_t>(game + L->g_context, a[A_CONTEXT]); put<uint64_t>(game + L->g_owner, a[A_OWNER]);
    put<uint64_t>(game + L->g_controller, a[A_CONTROLLER]); put<uint64_t>(game + L->g_state, a[A_SETTINGS]);
    put<uint64_t>(game + L->g_api_root, a[A_API_ROOT]); put<uint64_t>(exe + L->e_registry, a[A_REGISTRY]);
    put<uint64_t>(a[A_CONTEXT], a[A_CAMERA]); put<uint32_t>(a[A_CONTEXT] + 8, 0x400004);
    put<uint64_t>(a[A_OWNER] + 0x110, a[A_DESCRIPTOR]); put<uint64_t>(a[A_OWNER] + 0x118, a[A_DESCRIPTOR]);
    put<uint32_t>(a[A_DESCRIPTOR] + 8, 0x333); put<uint32_t>(a[A_DESCRIPTOR] + 12, 0x400003);
    put<uint64_t>(a[A_REGISTRY] + 0x88, a[A_UNITS]); put<uint32_t>(a[A_REGISTRY] + 0x98, 16); put<uint64_t>(a[A_REGISTRY] + 0xA0, a[A_GENERATIONS]);
    put<uint64_t>(a[A_UNITS] + 3 * 8, a[A_BODY]); put<uint64_t>(a[A_UNITS] + 4 * 8, a[A_CAMOBJ]); put<uint64_t>(a[A_UNITS] + 5 * 8, a[A_FPS]);
    put<uint64_t>(a[A_UNITS] + 6 * 8, a[A_W0]); put<uint64_t>(a[A_UNITS] + 7 * 8, a[A_W1]);
    put<uint64_t>(a[A_BODY], a[A_BODY_VT]); put<uint64_t>(a[A_BODY_VT] + 0xE8, exe + L->graph_getter); put<uint64_t>(a[A_BODY_VT] + 0x1B0, exe + L->anim_getter);
    put<uint64_t>(a[A_BODY] + 0x88, a[A_BODY_WORLD]); put<uint64_t>(a[A_BODY] + 0x98, a[A_LINKS]); put<uint64_t>(a[A_BODY] + 0xA0, a[A_HASHES]);
    put<uint64_t>(a[A_BODY] + 0x178, a[A_ANIM_INSTANCE]); put<uint64_t>(a[A_BODY] + 0x358, 0x4D1C334D294DFA97ULL);
    for (unsigned i = 0; i < BODY_NODES; ++i) put<uint32_t>(a[A_HASHES] + i * 4, 0x1000 + i);
    put<uint64_t>(a[A_ANIM_INSTANCE] + 0x28, a[A_ANIM_GRAPH]); put<uint64_t>(a[A_ANIM_INSTANCE] + 0x50, a[A_ANIM_CURRENT]);
    put<uint32_t>(a[A_ANIM_GRAPH] + 4, 31); put<uint32_t>(a[A_ANIM_GRAPH] + 8, 0x100);
    put<uint32_t>(a[A_ANIM_GRAPH] + 0x100 + 4 + 13 * 4, 0x100); put<uint32_t>(definition() + 8, 128);
    for (uint32_t i = 0; i < 128; ++i) put<uint32_t>(definition() + 12 + i * 4, 0x1000 + i * 16);
    put<uint64_t>(a[A_CAMERA] + 0x18, a[A_CAMOBJ] + 0x60);
    put<uint64_t>(a[A_CAMOBJ] + 0x80, a[A_CAM_LOCALS]); put<uint64_t>(a[A_CAMOBJ] + 0x88, a[A_CAM_WORLDS]);
    put<uint64_t>(a[A_CAMOBJ] + 0x78, a[A_FPS_WORLD]); put<uint64_t>(a[A_CAMOBJ] + 0x98, a[A_CAM_LINKS]);
    put<uint64_t>(a[A_API_ROOT] + 0x20, a[A_API]);
    put<uint32_t>(a[A_FPS] + 0x70, 63); put<uint64_t>(a[A_FPS] + 0x88, a[A_FPS_WORLD]); put<uint64_t>(a[A_FPS] + 0x98, a[A_FPS_LINKS]);
    put<uint64_t>(a[A_FPS] + 0xA0, a[A_FPS_HASHES]); put<uint32_t>(a[A_FPS_HASHES] + 50 * 4, 0x60ED8C39); put<uint32_t>(a[A_FPS_LINKS] + 50 * 4, 0x40001);
    for (int w = 0; w < 2; ++w) {
      const uint64_t object = a[A_W0 + w * 3];
      put<uint64_t>(object + 0x88, a[A_W0_WORLD + w * 3]); put<uint64_t>(object + 0x98, a[A_W0_LINKS + w * 3]);
    }
  }
  uint64_t definition() const { return a[A_ANIM_GRAPH] + 0x200; }

  // The scene, as the game would have it in memory before it calls the camera position setter.
  void apply(const Scene &s, uint32_t avatar_index) {
    put<uint32_t>(a[A_CONTROLLER], s.mask); put<uint8_t>(a[A_CONTROLLER] + 4, s.aim); put<uint8_t>(a[A_CONTROLLER] + 5, s.guard);
    const uint64_t row = static_cast<uint64_t>(avatar_index) * L->avatar_stride;
    std::memcpy(reinterpret_cast<void *>(a[A_OWNER] + L->avatar_flags + row), s.flags, 24);
    put<uint32_t>(a[A_OWNER] + L->avatar_rig + row, s.rig_reference);
    put<uint32_t>(a[A_OWNER] + 0x6C, s.avatar_count);
    const uint64_t context = a[A_CONTEXT];
    put(context + 0x1F8, s.ring);
    put(context + 0x200 + ((s.ring[1] + 31) % 32) * 0xE8, s.request);
    put(context + 0x1F00, s.idle);
    put(context + 0x0C, s.context);
    put(a[A_BODY_WORLD], s.root);
    put(a[A_BODY_WORLD] + HEAD_NODE * 64 + 0x30, s.head);
    put(a[A_BODY_WORLD] + WEAPON_NODE * 64 + 0x30, s.node42);
    put<uint64_t>(a[A_CAMOBJ] + 0x1D0, s.linked ? a[A_FPS] : s.parent_raw);
    put<uint32_t>(a[A_CAMERA] + 0x20, s.camera_index); put<uint32_t>(a[A_CAMOBJ] + 0x70, s.camera_count);
    put<uint32_t>(a[A_CAM_LINKS], s.camera_link);
    if (s.camera_rotation_given) put(a[A_CAM_WORLDS], s.camera_rotation);
    if (s.camera_split) { put(a[A_CAM_LOCALS] + 0x24, s.camera_local); put(a[A_CAM_WORLDS] + 0x30, s.camera_world); }
    put(a[A_FPS_WORLD] + 50 * 64, s.rig);
    std::memcpy(reinterpret_cast<void *>(a[A_GENERATIONS]), s.generations, 8);
    for (int w = 0; w < 2; ++w) {
      const uint64_t object = a[A_W0 + w * 3];
      put(a[A_W0_WORLD + w * 3] + 0x30, s.weapon[w].position);
      put<uint64_t>(object + 0x1D0, s.weapon[w].parent == 1 ? a[A_BODY] : s.weapon[w].parent == 2 ? a[A_FPS] : 0);
      put<uint32_t>(a[A_W0_LINKS + w * 3], s.weapon[w].link);
    }
    put<uint64_t>(a[A_ANIM_CURRENT] + 13 * 8, definition() + 0x1000 + static_cast<uint64_t>(s.state) * 16);
    put<uint32_t>(a[A_SETTINGS] + L->state_scene, s.scene);
    put<uint32_t>(a[A_BODY] + 0x70, s.graph_count);
    put<uint32_t>(a[A_HASHES] + HEAD_NODE * 4, s.head_hash); put<uint32_t>(a[A_LINKS] + HEAD_NODE * 4, s.head_link);
    put<uint32_t>(a[A_HASHES] + HEAD_PARENT * 4, s.parent_hash);
    put(native(), s.argument); put(native() + 64, s.argument_rotation);
  }
};

} // namespace dt
