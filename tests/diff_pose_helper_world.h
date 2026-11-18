// Fake game memory for tests/diff_pose_helper.cpp: one logical scene, written into real pages twice, once
// with the retail build's addresses and offsets (26568081, from native_ads_editor.cpp) and once with this
// build's (25480438, from the tables of docs/translation/POSE-HELPER-TRANSLATION.md section 2; the names that document
// lists as open take the test's own values from tests/diff_pose_helper_open.h). The structure layout
// numbers both builds share are written here once, as the retail source has them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>
#include <sys/mman.h>
#include "diff_pose_helper_open.h"

namespace dp {

struct Pin { uint32_t offset, length; bool exe, real; }; // real: this build's own bytes exist for it
struct Layout {
  uint32_t game_stamp, game_size, exe_stamp, exe_size;
  uint32_t wrappers[4]; const char *hex_wrappers[4];
  uint32_t graph_getter;
  uint32_t carrier_call, carrier_pair, rig_call, rotation_call, observe_call;
  uint32_t g_context, g_owner, g_controller, g_api_root, e_registry, g_pool, g_world, g_settings, s_degrees;
  uint32_t avatar_flags, avatar_rig, avatar_stride;
  uint32_t w_resources, buckets, records, definition;
  uint32_t depth_pin[9]; const char *depth_hex[9];
  uint32_t carrier_pin[3];
  uint32_t world_matrix; const char *hex_world_matrix;
  uint32_t input_return, exp, presets, g_input_owner, g_clock, g_player, g_no_avatar, g_host_avatar, g_idle_avatar;
  uint32_t w_entity_map, w_entity_avatar, avatar_steering, input_device;
  Pin pins[16];
  uint32_t consts[7];
};
static const char *const CARRIER_HEX[3] = {"5556574154415541564157488dac24f0fcffff4881ec10040000", "4c8d8540010000f2410f11475433", "4c8d8530010000"};
// native_ads_editor.cpp:73-82, 196-203, 308-313, 384-433, 446-457, 585-601, 613-617, 626-700, 842, 975-977, 1501.
static const Layout RETAIL = {
    0x6af322c1, 0x3382000, 0x6af2d5e6, 0x29fe000,
    {0x1f25e0, 0x1f2740, 0x1f2660, 0x1f2310},
    {"48895c24084889742410574883ec20498bf08bfa8bd9e8d5e0eaff4c8b5018b801000000f0410fc182380017008bc04c",
     "48895c24084889742410574883ec20498bf08bfa8bd9e875dfeaff4c8b5018b801000000f0410fc182380017008bc04c",
     "48895c24084889742410574881eca0000000440f29b42490000000498bd84863fae84ae0eaff488bc8488bf04c8b0841",
     "40534883ec204863dae8b2e3eaff488bc84c8b0041ff90e8000000488bcb48c1e1064883c124488b40204803c14883c4205bc3"},
    0x28d3c0,
    0x111a809, 0x111a829, 0x111a460, 0x111b11b, 0x1119a81,
    0x2e287d0, 0x2ce1ee0, 0x2e381a8, 0x2ce14c8, 0x1ab76f8, 0x2ce1ea0, 0x2ce41b8, 0x2ce1508, 0xac488,
    0x53e888, 0x53e9c0, 0x1240,
    0xf146a0, 746, 0x2EA0, 0x4D8,
    {0x5120b2, 0x511b3f, 0x511b57, 0x511bb1, 0x11193f3, 0x1119419, 0x1119ad1, 0x1119b83, 0x1119c44},
    {"4869c0d8040000490383b00000004883c428c3", "4c8b90a046f10048b80fdf6f49a121b3af48f7e148c1ea09", "69c2ea020000442bc0", "4869c1d80400004805a02e00004903c2c3",
     "f30f1044245cf30f10159f241301", "f30f5c4424600f54c2f30f5ec1", "f3450f58fb", "f30f59faf3410f59cff3", "f3410f11774cf3450f114750"},
    {0x1118375, 0x111a7cb, 0x111a814},
    0x1f30e0, "40534883ec204863dae8e2d5eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3",
    0x111863e, 0x1f8ff70, 0x2cb2140, 0x2e38250, 0x2ce14f8, 0x2ce1618, 0x2e3fcb8, 0x2e3fa9c, 0x2e3f0e4,
    0xf24990, 0xf349e8, 0x53e9ac, 0x324,
    {{0x1118370, 9688, false, true}, {0x111a948, 24, false, true}, {0x1114950, 1724, false, true}, {0x1f8ff70, 1013, false, true},
     {0x1109110, 1328, false, true}, {0x1109640, 0x1a0, false, true}, {0x110b830, 0x5b0, false, true}, {0x111f860, 0x124, false, true},
     {0xe69ca0, 0xdc, false, true}, {0x110fcf0, 0xbb0, false, true}, {0x11108a0, 0x1dd0, false, true}, {0x1f3050, 51, true, true},
     {0x1f3090, 76, true, true}, {0x1f30e0, 47, true, true}, {0x1f2310, 51, true, true}, {0x28d3c0, 5, true, true}},
    {0x22465c4, 0x2247894, 0x2246fb0, 0x22478c8, 0x2246774, 0x2247ab0, 0x2246900}};
// docs/translation/POSE-HELPER-TRANSLATION.md section 2, column "here"; the open names from diff_pose_helper_open.h.
static const Layout HERE = {
    0x6ab3b43f, 0x4744000, 0x6ab382e4, 0x39e8000,
    {0x1fc720, 0x1fc880, 0x1fc7a0, 0x1fc450},
    {"48895c24084889742410574883ec20498bf08bfa8bd9e88511eaff4c8b5018b801000000f0410fc182380017008bc04c",
     "48895c24084889742410574883ec20498bf08bfa8bd9e82510eaff4c8b5018b801000000f0410fc182380017008bc04c",
     "48895c24084889742410574881eca0000000440f29b42490000000498bd84863fae8fa10eaff488bc8488bf04c8b0841",
     "40534883ec204863dae86214eaff488bc84c8b0041ff90e8000000488bcb48c1e1064883c124488b40204803c14883c4205bc3"},
    0x2bd870,
    0x12887c9, 0x12887e9, 0x1288420, 0x12890db, 0x1287a41,
    0x346d560, 0x3326d20, 0x347ce70, 0x3326308, 0x1a100f0, 0x3326ce0, 0x346bf98, 0x3326340, 0xac408,
    0x53e880, 0x53e9b8, 0x1238,
    0xf12bd8, 730, 0x2da0, 0x4d0,
    {0x509ae2, 0x50957f, 0x5095a3, 0x5095f1, 0x12873b3, 0x12873d9, 0x1287a91, 0x1287b43, 0x1287c04},
    {"4869c0d0040000490383b00000004883c428c3", "4c8b90d82bf10048b8379f711660f3196748f7e1488bc1482bc248d1e84803c248c1e809", "69c0da020000442bc0",
     "4869c1d00400004805a02d00004903c2c3", "f30f1044245cf30f1015df4b1401", "f30f5c4424600f54c2f30f5ec1", "f3450f58fb", "f30f59faf3410f59cff3",
     "f3410f11774cf3450f114750"},
    {0x1286335, 0x128878b, 0x12887d4},
    0x1fd220, "40534883ec204863dae89206eaff488bc84c8b0041ff90e8000000488bcb48c1e10648034828488bc14883c4205bc3",
    0x12865fe, 0x21087d0, 0x32f9990, 0x347cf18, 0x3326348, 0x3326468, B_GAME_NO_AVATAR, B_GAME_HOST_AVATAR, B_GAME_IDLE_AVATAR,
    0xf22ec8, 0xf32f20, 0x53e9a4, B_INPUT_DEVICE,
    {{0x1286330, 9688, false, true}, {B_PIN_2, 24, false, false}, {B_PIN_3, 1724, false, false}, {0x21087d0, 1013, false, false},
     {0x1277250, 1328, false, true}, {B_PIN_6, 0x1a0, false, false}, {B_PIN_7, 0x5b0, false, false}, {0x128d820, 0x124, false, true},
     {B_PIN_9, 0xdc, false, false}, {0x127de20, 0xbb0, false, true}, {0x127e9d0, 0x1dd0, false, false}, {B_PIN_12, 51, true, false},
     {B_PIN_13, 76, true, false}, {0x1fd220, 47, true, true}, {0x1fc450, 51, true, true}, {0x2bd870, 5, true, true}},
    {B_CONST_ONE, B_CONST_MINUS_ONE, B_CONST_RAD_TO_DEG, B_CONST_MINUS_HALF_PI, B_CONST_HALF_PI, B_CONST_MINUS_75, B_CONST_DOUBLE_ONE}};

// The logical identities of the scene.
enum : uint32_t { AVATAR = 0x333, WEAPON = 0x444, BODY = 0x222, VIEWMODEL = 0x400003, WEAPON_UNIT = 0x400004, OTHER_RIG = 0x400005, OTHER_WEAPON = 0x400006 };

// Every allocation, and the build-specific places inside them, by name. An address in one world is compared
// with an address in the other as (name, bytes after it).
enum A {
  A_GAME, A_EXE, A_CONTEXT, A_CAMERA, A_OWNER, A_CONTROLLER, A_DESCRIPTOR, A_WDESC, A_REGISTRY, A_UNITS, A_GENERATIONS,
  A_RIG, A_RIG_VT, A_RIG_LINKS, A_RIG_HASHES, A_RIG_LOCALS, A_RIG2, A_ALT, A_WOBJ, A_WOBJ2, A_API_ROOT, A_API, A_STACK, A_ARG,
  A_POOL, A_PRIMARY, A_OVERRIDES, A_DESCRIPTORS, A_PARAMS, A_PACKED, A_DATA, A_WORLD, A_TABLE, A_SETTINGS,
  A_INPUT_OWNER, A_TIME, A_PLAYER, A_AVMAP, A_ENTMAP, A_IO,
  A_REGIONS,
  A_G_CONTEXT = A_REGIONS, A_G_OWNER, A_G_CONTROLLER, A_G_API_ROOT, A_G_POOL, A_G_WORLD, A_G_SETTINGS,
  A_G_CARRIER_CALL, A_G_CARRIER_PAIR, A_G_RIG_CALL, A_G_ROTATION_CALL, A_G_OBSERVE_CALL,
  A_G_DEPTH1, A_G_DEPTH2, A_G_DEPTH3, A_G_DEPTH4, A_G_DEPTH5, A_G_DEPTH6, A_G_DEPTH7, A_G_DEPTH8, A_G_DEPTH9,
  A_G_CARRIER1, A_G_CARRIER2, A_G_CARRIER3,
  A_G_INPUT_RETURN, A_G_PRESETS, A_G_INPUT_OWNER, A_G_CLOCK, A_G_PLAYER, A_G_NO_AVATAR, A_G_HOST_AVATAR, A_G_IDLE_AVATAR,
  A_G_PIN0, A_G_PIN1, A_G_PIN2, A_G_PIN3, A_G_PIN4, A_G_PIN5, A_G_PIN6, A_G_PIN7, A_G_PIN8, A_G_PIN9, A_G_PIN10,
  A_G_CONST0, A_G_CONST1, A_G_CONST2, A_G_CONST3, A_G_CONST4, A_G_CONST5, A_G_CONST6,
  A_E_WRAPPER0, A_E_WRAPPER1, A_E_WRAPPER2, A_E_WRAPPER3, A_E_GRAPH_GETTER, A_E_REGISTRY, A_E_WORLD_MATRIX, A_E_PIN11, A_E_PIN12,
  A_O_FLAGS0, A_O_RIG0, A_O_STEER0, A_O_FLAGS1, A_O_RIG1, A_O_STEER1,
  A_S_DEGREES, A_W_RESOURCES, A_W_ENTITY_MAP, A_W_ENTITY_AVATAR, A_T_RECORDS, A_I_DEVICE,
  A_COUNT,
  // Named by what they are, not by offset: a weapon definition record (by its index) and the resource rows counted
  // from the weapon's own bucket (the two builds have 746 and 730 buckets and records of 0x4D8 and 0x4D0 bytes).
  A_DEFINITION = A_COUNT, A_T_ROW, A_NAMES_COUNT
};
static const char *const A_NAMES[A_NAMES_COUNT] = {
    "game", "exe", "context", "camera", "avatar_owner", "controller", "descriptor", "weapon_descriptor", "registry", "units", "generations",
    "rig", "rig_vtable", "rig_links", "rig_hashes", "rig_locals", "other_rig", "alternate", "weapon_object", "other_weapon_object", "api_root", "api",
    "stack", "argument", "weapon_pool", "primary_buckets", "override_buckets", "descriptor_table", "parameters", "packed", "override_data",
    "world", "resource_table", "settings", "input_owner", "clock", "player", "avatar_map_buckets", "entity_map_buckets", "io",
    "game:context", "game:avatar_owner", "game:controller", "game:api_root", "game:weapon_pool", "game:world", "game:settings",
    "game:carrier_call", "game:carrier_pair", "game:rig_call", "game:rotation_call", "game:observe_call",
    "game:depth_pin1", "game:depth_pin2", "game:depth_pin3", "game:depth_pin4", "game:depth_pin5", "game:depth_pin6", "game:depth_pin7", "game:depth_pin8", "game:depth_pin9",
    "game:carrier_pin1", "game:carrier_pin2", "game:carrier_pin3",
    "game:input_return", "game:presets", "game:input_owner", "game:clock", "game:player", "game:no_avatar", "game:host_avatar", "game:idle_avatar",
    "game:pin_evaluator", "game:pin2", "game:pin3", "game:exp", "game:pin_solver", "game:pin6", "game:pin7", "game:pin_steering", "game:pin9", "game:pin_enter", "game:pin_exit",
    "game:const_one", "game:const_minus_one", "game:const_rad_to_deg", "game:const_minus_half_pi", "game:const_half_pi", "game:const_minus_75", "game:const_double_one",
    "exe:position", "exe:rotation", "exe:branch_rotation", "exe:local_position", "exe:graph_getter", "exe:registry", "exe:world_matrix", "exe:pin12", "exe:pin13",
    "owner:flags[0]", "owner:rig[0]", "owner:steering[0]", "owner:flags[1]", "owner:rig[1]", "owner:steering[1]",
    "settings:degrees", "world:resources", "world:entity_map", "world:entity_avatar", "resource_table:records", "input_owner:device",
    "definition", "resource_row"};
struct Sym { int id; uint64_t offset; };
static const int PIN_NAMES[16] = {A_G_PIN0, A_G_PIN1, A_G_PIN2, A_G_PIN3, A_G_PIN4, A_G_PIN5, A_G_PIN6, A_G_PIN7, A_G_PIN8, A_G_PIN9, A_G_PIN10,
                                  A_E_PIN11, A_E_PIN12, A_E_WORLD_MATRIX, A_E_WRAPPER3, A_E_GRAPH_GETTER};
// Places in the io page.
enum : uint64_t { IO_CONFIG = 0, IO_GENERATION = 0x100, IO_STATS = 0x200, IO_DEPTH = 0x280, IO_INPUT = 0x300, IO_ENTRY = 0x380, IO_TRACE = 0x400, IO_SCRIPT = 0x1000 };
// Places in the stack page: two evaluator frames (an outer and a nested one).
enum : uint64_t { STACK_S = 0x100, STACK_INNER = 0x800 };
// Places in the argument page.
enum : uint64_t { ARG_ROTATION = 0, ARG_POSITION = 0x40, ARG_GETTER_RESULT = 0x100, ARG_OTHER = 0x200 };

enum { RIG_NODES = 63 };
struct GraphRow { uint32_t node, hash, link; };
static const GraphRow GRAPH_ROWS[15] = { // native_ads_editor.cpp:242-248
    {0, 0x4A182741, 0}, {1, 0x7F30E61C, 1}, {2, 0x32623EB2, 0x10001}, {3, 0x975CEBBD, 0x20001}, {4, 0xBCCF91E5, 0x30001}, {5, 0x9B115563, 0x40001},
    {7, 0x6F3BD021, 0x50001}, {8, 0x4FDD3337, 0x70001}, {10, 0x03752C98, 0x70001}, {12, 0x1CA2CD66, 0xA0001}, {13, 0x5C0C68D3, 0xC0001},
    {30, 0x1620B2CE, 0x70001}, {32, 0x31E34DAC, 0x1E0001}, {34, 0x248004F5, 0x200001}, {50, 0x60ED8C39, 0x40001}};

struct Scene {
  // the camera request ring
  uint32_t ring[2] = {0, 1};
  int row = 1;                                   // the owned request row
  uint8_t request[0xE8] = {};                    // its bytes
  bool write_preset = true;                      // one-shot: also write the 4 bytes at +0x80 (the helper swaps them)
  uint32_t under[4] = {4, AVATAR, 0, 0};         // the header of the row before it
  uint32_t idle[4] = {4, AVATAR, 0, 0};
  uint8_t controller[6] = {};
  uint8_t flags[24] = {};
  uint32_t rig_reference = VIEWMODEL, avatar_count = 2;
  uint32_t descriptor[5] = {0, 0, AVATAR, BODY, 0};
  uint64_t resource = 0xFFFFFFFFFFFFFFFEull;     // first 8 bytes of the weapon descriptor
  uint32_t wdesc[3] = {WEAPON, WEAPON_UNIT, 0};  // +8, +12, +16
  uint32_t registry_capacity = 16; uint8_t generations[16] = {0, 0, 0, 1, 1, 1, 1, 0};
  int rig_object = 0, weapon_object = 0;         // 1: the unit table names the other object
  // the rig's node graph
  uint32_t graph_count = RIG_NODES; uint32_t hashes[RIG_NODES] = {}, links[RIG_NODES] = {};
  int links_at = 0, hashes_at = 0, vtable_at = 0, locals_at = 0; // 1: the pointer names the alternate page
  uint64_t getter_delta = 0;
  float root_local[3] = {}; bool write_root_local = true; // one-shot: node 0's local position (the game's setter writes it)
  // the evaluator's stack frame (S)
  uint8_t frame[0x20] = {}; uint64_t stack_index = 0; bool stack_context = true; float dt_eval = 1.0f / 60;
  float root_source[8] = {0, 0, 0, 1, 0, 0, 0, 1}; // S+0x230: the final quaternion, then the root position at S+0x240
  // weapon definitions (zoom depth)
  struct Depth {
    bool in_primary = true, in_overrides = false, in_resources = true, descriptor_ok = true;
    uint32_t wi = 2, di = 3, capacity = 8, multiplier = 1, empty = 0xffffffffu, shift = 0, override_shift = 0, resource_shift = 0;
    int resource_di = 0;                         // -1: the last valid row index of the build; -2: one past it
    uint32_t kinds[4] = {1, 0, 0, 0}, packed = 64;
    float zoom[3] = {1, 2.5f, 3.5f}, coefficient[3] = {.6f, .6f, .6f}, near_term[2] = {0, 0}, degrees = 90;
    uint8_t filler = 0;                          // the other bytes of the 0xdc definition block
  } depth;
  // look input
  float raw[3] = {.2f, -.1f, .3f}, look[2] = {0, 0}, dt_global = 1.0f / 60, steering[2] = {0, 0};
  uint8_t inhibit = 0, input_flags[0x5c] = {}; uint32_t device = 0;
  uint32_t player_count = 0, entity = 0, no_avatar = 0xffffffffu, host_avatar = AVATAR, idle_avatar = AVATAR, entity_row = 5, entity_avatar = AVATAR;
  uint32_t avatar_map_index = 0; bool avatar_in_map = true, entity_in_map = true;
  uint8_t preset[144] = {}; uint32_t preset_row = 3;
  float rotation[4] = {0, 0, 0, 1};
  float position[4] = {1, 2, 3, 0};
};

struct World {
  const Layout *L = nullptr;
  uint64_t a[A_NAMES_COUNT] = {};
  uint64_t size[A_REGIONS] = {};
  struct Region { uint64_t base, size; int id; };
  std::vector<Region> regions;
  std::vector<int> inside[A_REGIONS];
  uint64_t native[5] = {}, hooks[6] = {};         // what each dispatch slot holds untouched; the six helper values
  uint64_t hole = 0;                              // free address space right after the game image
  uint32_t row_bucket = 0;

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
  void region(int id, uint64_t bytes, uint64_t after = 0) {
    bytes = (bytes + 4095) & ~4095ull;
    // An unreadable page on each side: a read that runs off a region fails the same way in both worlds.
    // `after`: that much address space behind the far guard page is then given back, so it is free.
    void *p = mmap(nullptr, bytes + 8192 + after + 0x20000, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) { std::perror("mmap"); std::_Exit(3); }
    p = static_cast<char *>(p) + 4096;
    if (mprotect(p, bytes, PROT_READ | PROT_WRITE)) { std::perror("mprotect"); std::_Exit(3); }
    a[id] = reinterpret_cast<uint64_t>(p); size[id] = bytes;
    regions.push_back({a[id], bytes, id});
    if (after) {
      hole = (a[id] + bytes + 4096 + 0xffff) & ~0xffffull;
      munmap(reinterpret_cast<void *>(hole), after);
    }
  }
  void inner(int id, int parent, uint64_t offset) { a[id] = a[parent] + offset; inside[parent].push_back(id); }
  uint64_t at(Sym s) const { return a[s.id] + s.offset; }
  uint64_t slot(int which) const { static const uint64_t offsets[5] = {0x38, 0x48, 0x40, 0, 0x90}; return a[A_API] + offsets[which]; }
  uint64_t stack(bool inner_frame = false) const { return a[A_STACK] + (inner_frame ? STACK_INNER : STACK_S); }
  uint64_t request(int row) const { return a[A_CONTEXT] + 0x200 + static_cast<uint64_t>(row) * 0xE8; }
  // (name, offset) of an address; name 0xffff = in none of this world's regions.
  Sym name(uint64_t address) const {
    auto next = std::upper_bound(regions.begin(), regions.end(), address, [](uint64_t v, const Region &r) { return v < r.base; });
    if (next == regions.begin()) return {0xffff, 0};
    const Region &r = *(next - 1);
    if (address - r.base >= r.size + 4096) return {0xffff, 0}; // the unreadable page after a region still names it
    if (r.id == A_DATA) return {A_DEFINITION, 1ull << 48 | ((address - r.base) / L->definition) << 16 | (address - r.base) % L->definition};
    if (r.id == A_TABLE && address >= a[A_T_RECORDS]) {
      uint64_t index = (address - a[A_T_RECORDS]) / L->definition;
      if (index + 1 == L->buckets) index = 0xfffe; else if (index == L->buckets) index = 0xffff; // "the last row" and "one past it" of either build
      return {A_DEFINITION, index << 16 | (address - a[A_T_RECORDS]) % L->definition};
    }
    if (r.id == A_TABLE) {
      int64_t row = static_cast<int64_t>((address - r.base) / 16) - static_cast<int64_t>(row_bucket);
      const int64_t buckets = L->buckets;
      if (row < -buckets / 2) row += buckets; else if (row > buckets / 2) row -= buckets;
      return {A_T_ROW, static_cast<uint64_t>(row * 16 + static_cast<int64_t>((address - r.base) % 16))};
    }
    // Inside one of the sixteen pinned code ranges: that pin names the address (the ranges lie differently next to
    // each other in the two builds). Otherwise the nearest build-specific place within 16 KiB does (offsets may be
    // negative), or else the region.
    for (int i = 0; i < 16; ++i) {
      const Pin &pin = L->pins[i]; const uint64_t base = a[pin.exe ? A_EXE : A_GAME] + pin.offset;
      if (address >= base && address - base < pin.length) return {PIN_NAMES[i], address - base};
    }
    if (r.id == A_WORLD && address >= a[A_W_ENTITY_AVATAR]) return {A_W_ENTITY_AVATAR, address - a[A_W_ENTITY_AVATAR]}; // the entity rows, the last table of the world
    if (r.id == A_GAME && address >= a[A_G_PRESETS] && address - a[A_G_PRESETS] < 256 * 144) return {A_G_PRESETS, address - a[A_G_PRESETS]};
    int best = r.id; uint64_t distance = 0x4000;
    for (int id : inside[r.id]) {
      const uint64_t d = a[id] <= address ? address - a[id] : a[id] - address;
      if (d < distance) { best = id; distance = d; }
    }
    return {best, address - a[best]};
  }

  void build(const Layout &layout) {
    L = &layout;
    region(A_GAME, L->game_size, 0x400000); region(A_EXE, L->exe_size); region(A_CONTEXT, 0x3000); region(A_CAMERA, 0x1000);
    region(A_OWNER, 0x548000); region(A_CONTROLLER, 0x1000); region(A_DESCRIPTOR, 0x1000); region(A_WDESC, 0x1000);
    region(A_REGISTRY, 0x1000); region(A_UNITS, 0x1000); region(A_GENERATIONS, 0x1000);
    region(A_RIG, 0x1000); region(A_RIG_VT, 0x1000); region(A_RIG_LINKS, 0x1000); region(A_RIG_HASHES, 0x1000); region(A_RIG_LOCALS, 0x1000);
    region(A_RIG2, 0x1000); region(A_ALT, 0x4000); region(A_WOBJ, 0x1000); region(A_WOBJ2, 0x1000);
    region(A_API_ROOT, 0x1000); region(A_API, 0x1000); region(A_STACK, 0x1000); region(A_ARG, 0x1000);
    region(A_POOL, 0x1000); region(A_PRIMARY, 0x1000); region(A_OVERRIDES, 0x1000); region(A_DESCRIPTORS, 0x8000); region(A_PARAMS, 0x400000);
    region(A_PACKED, 0x10000); region(A_DATA, 0x500000); region(A_WORLD, 0x2740000); region(A_TABLE, 0x100000); region(A_SETTINGS, 0xAD000);
    region(A_INPUT_OWNER, 0xA8000); region(A_TIME, 0x1000); region(A_PLAYER, 0x1000); region(A_AVMAP, 0x1000); region(A_ENTMAP, 0x1000);
    region(A_IO, 0x2000);
    std::sort(regions.begin(), regions.end(), [](const Region &x, const Region &y) { return x.base < y.base; });
    inner(A_G_CONTEXT, A_GAME, L->g_context); inner(A_G_OWNER, A_GAME, L->g_owner); inner(A_G_CONTROLLER, A_GAME, L->g_controller);
    inner(A_G_API_ROOT, A_GAME, L->g_api_root); inner(A_G_POOL, A_GAME, L->g_pool); inner(A_G_WORLD, A_GAME, L->g_world); inner(A_G_SETTINGS, A_GAME, L->g_settings);
    inner(A_G_CARRIER_CALL, A_GAME, L->carrier_call); inner(A_G_CARRIER_PAIR, A_GAME, L->carrier_pair); inner(A_G_RIG_CALL, A_GAME, L->rig_call);
    inner(A_G_ROTATION_CALL, A_GAME, L->rotation_call); inner(A_G_OBSERVE_CALL, A_GAME, L->observe_call);
    for (int i = 0; i < 9; ++i) inner(A_G_DEPTH1 + i, A_GAME, L->depth_pin[i]);
    for (int i = 0; i < 3; ++i) inner(A_G_CARRIER1 + i, A_GAME, L->carrier_pin[i]);
    inner(A_G_INPUT_RETURN, A_GAME, L->input_return); inner(A_G_PRESETS, A_GAME, L->presets); inner(A_G_INPUT_OWNER, A_GAME, L->g_input_owner);
    inner(A_G_CLOCK, A_GAME, L->g_clock); inner(A_G_PLAYER, A_GAME, L->g_player); inner(A_G_NO_AVATAR, A_GAME, L->g_no_avatar);
    inner(A_G_HOST_AVATAR, A_GAME, L->g_host_avatar); inner(A_G_IDLE_AVATAR, A_GAME, L->g_idle_avatar);
    for (int i = 0; i < 11; ++i) inner(A_G_PIN0 + i, A_GAME, L->pins[i].offset);
    for (int i = 0; i < 7; ++i) inner(A_G_CONST0 + i, A_GAME, L->consts[i]);
    for (int i = 0; i < 4; ++i) inner(A_E_WRAPPER0 + i, A_EXE, L->wrappers[i]);
    inner(A_E_GRAPH_GETTER, A_EXE, L->graph_getter); inner(A_E_REGISTRY, A_EXE, L->e_registry); inner(A_E_WORLD_MATRIX, A_EXE, L->world_matrix);
    inner(A_E_PIN11, A_EXE, L->pins[11].offset); inner(A_E_PIN12, A_EXE, L->pins[12].offset);
    for (int i = 0; i < 2; ++i) {
      inner(A_O_FLAGS0 + i * 3, A_OWNER, L->avatar_flags + i * L->avatar_stride); inner(A_O_RIG0 + i * 3, A_OWNER, L->avatar_rig + i * L->avatar_stride);
      inner(A_O_STEER0 + i * 3, A_OWNER, L->avatar_steering + i * L->avatar_stride);
    }
    inner(A_S_DEGREES, A_SETTINGS, L->s_degrees); inner(A_W_RESOURCES, A_WORLD, L->w_resources); inner(A_W_ENTITY_MAP, A_WORLD, L->w_entity_map);
    inner(A_W_ENTITY_AVATAR, A_WORLD, L->w_entity_avatar); inner(A_T_RECORDS, A_TABLE, L->records); inner(A_I_DEVICE, A_INPUT_OWNER, L->input_device);

    const uint64_t game = a[A_GAME], exe = a[A_EXE];
    pe_header(game, L->game_stamp, L->game_size); pe_header(exe, L->exe_stamp, L->exe_size);
    for (int i = 0; i < 4; ++i) literal(exe + L->wrappers[i], L->hex_wrappers[i]);
    literal(exe + L->graph_getter, "488d4160c3"); literal(exe + L->world_matrix, L->hex_world_matrix);
    literal(game + L->carrier_call, "41ff5138"); literal(game + L->carrier_pair, "41ff5148"); literal(game + L->rig_call, "41ff5138");
    literal(game + L->rotation_call, "41ff5140"); literal(game + L->observe_call, "41ffd0");
    for (int i = 0; i < 9; ++i) literal(game + L->depth_pin[i], L->depth_hex[i]);
    for (int i = 0; i < 3; ++i) literal(game + L->carrier_pin[i], CARRIER_HEX[i]);
    put<uint64_t>(game + L->g_context, a[A_CONTEXT]); put<uint64_t>(game + L->g_owner, a[A_OWNER]); put<uint64_t>(game + L->g_controller, a[A_CONTROLLER]);
    put<uint64_t>(game + L->g_api_root, a[A_API_ROOT]); put<uint64_t>(exe + L->e_registry, a[A_REGISTRY]);
    put<uint64_t>(game + L->g_pool, a[A_POOL]); put<uint64_t>(game + L->g_world, a[A_WORLD]); put<uint64_t>(game + L->g_settings, a[A_SETTINGS]);
    put<uint64_t>(game + L->g_input_owner, a[A_INPUT_OWNER]); put<uint64_t>(game + L->g_clock, a[A_TIME]); put<uint64_t>(game + L->g_player, a[A_PLAYER]);
    static const uint32_t constants[6] = {0x3f800000, 0xbf800000, 0x42652ee0, 0xbfc90fdb, 0x3fc90fdb, 0xc2960000};
    for (int i = 0; i < 6; ++i) put<uint32_t>(game + L->consts[i], constants[i]);
    put<uint64_t>(game + L->consts[6], 0x3ff0000000000000ull);
    put<uint64_t>(a[A_CONTEXT], a[A_CAMERA]);
    put<uint64_t>(a[A_OWNER] + 0x110, a[A_DESCRIPTOR]); put<uint64_t>(a[A_OWNER] + 0x118, a[A_DESCRIPTOR] + 0x100);
    put<uint64_t>(a[A_REGISTRY] + 0x88, a[A_UNITS]); put<uint64_t>(a[A_REGISTRY] + 0xA0, a[A_GENERATIONS]);
    put<uint64_t>(a[A_RIG_VT] + 0xE8, exe + L->graph_getter); put<uint64_t>(a[A_ALT] + 0xE8, exe + L->graph_getter);
    put<uint64_t>(a[A_API_ROOT] + 0x18, a[A_API]);
    // the weapon pool header (0x88 bytes from pool+0x30): two maps and four row tables
    const uint64_t pool = a[A_POOL] + 0x30;
    put<uint64_t>(pool, a[A_PRIMARY]); put<uint64_t>(pool + 0x18, a[A_DESCRIPTORS]); put<uint64_t>(pool + 0x28, a[A_PARAMS]); put<uint64_t>(pool + 0x30, a[A_PACKED]);
    put<uint64_t>(pool + 0x40, a[A_OVERRIDES]); put<uint64_t>(pool + 0x80, a[A_DATA]);
    put<uint64_t>(a[A_WORLD] + L->w_resources, a[A_TABLE]);
    put<uint64_t>(a[A_OWNER] + 0xf8, a[A_AVMAP]); put<uint32_t>(a[A_OWNER] + 0x100, 8); put<uint32_t>(a[A_OWNER] + 0x104, 0xffffffffu); put<uint32_t>(a[A_OWNER] + 0x108, 1);
    put<uint64_t>(a[A_WORLD] + L->w_entity_map, a[A_ENTMAP]); put<uint32_t>(a[A_WORLD] + L->w_entity_map + 8, 8);
    put<uint32_t>(a[A_WORLD] + L->w_entity_map + 12, 0xffffffffu); put<uint32_t>(a[A_WORLD] + L->w_entity_map + 16, 1);
    const float getter_result[3] = {.125f, -.25f, .5f}; put(a[A_ARG] + ARG_GETTER_RESULT, getter_result);
  }
  // The pinned code ranges of the look-gain normalisation: retail's bytes (from a capture of that build), and for
  // this build its own bytes where a pin is established, retail's bytes at the test's place where it is open.
  bool load_pins(const uint8_t *game_image, size_t game_length, const uint8_t *exe_image, size_t exe_length,
                 const uint8_t *retail_game, size_t retail_game_length, const uint8_t *retail_exe, size_t retail_exe_length) {
    for (int i = 0; i < 16; ++i) {
      const Pin &pin = L->pins[i], &source = pin.real ? pin : RETAIL.pins[i];
      const uint8_t *image = pin.real ? (pin.exe ? exe_image : game_image) : (pin.exe ? retail_exe : retail_game);
      const size_t length = pin.real ? (pin.exe ? exe_length : game_length) : (pin.exe ? retail_exe_length : retail_game_length);
      if (!image || source.offset + static_cast<size_t>(pin.length) > length) return false;
      std::memcpy(reinterpret_cast<void *>(a[pin.exe ? A_EXE : A_GAME] + pin.offset), image + source.offset, pin.length);
    }
    return true;
  }

  // The scene, as the game would have it in memory before it calls into the Unit API.
  void apply(const Scene &s, uint32_t avatar_index) {
    const uint64_t context = a[A_CONTEXT];
    put(context + 0x1F8, s.ring);
    const uint64_t row = request(s.row);
    std::memcpy(reinterpret_cast<void *>(row), s.request, 0x80);
    if (s.write_preset) std::memcpy(reinterpret_cast<void *>(row + 0x80), s.request + 0x80, 4);
    std::memcpy(reinterpret_cast<void *>(row + 0x84), s.request + 0x84, 0xE8 - 0x84);
    put(request((s.row + 31) % 32), s.under);
    put(context + 0x1F00, s.idle);
    put(context + 0x15c, s.look); put<uint8_t>(context + 0x1a9, s.inhibit);
    std::memcpy(reinterpret_cast<void *>(a[A_CONTROLLER]), s.controller, 6);
    const uint64_t stride = static_cast<uint64_t>(avatar_index) * L->avatar_stride;
    std::memcpy(reinterpret_cast<void *>(a[A_OWNER] + L->avatar_flags + stride), s.flags, 24);
    put<uint32_t>(a[A_OWNER] + L->avatar_rig + stride, s.rig_reference);
    put(a[A_OWNER] + L->avatar_steering + stride, s.steering);
    put<uint32_t>(a[A_OWNER] + 0x6C, s.avatar_count);
    put(a[A_DESCRIPTOR] + (avatar_index ? 0x100 : 0), s.descriptor);
    put<uint64_t>(a[A_WDESC], s.resource); put(a[A_WDESC] + 8, s.wdesc);
    put<uint32_t>(a[A_REGISTRY] + 0x98, s.registry_capacity);
    std::memcpy(reinterpret_cast<void *>(a[A_GENERATIONS]), s.generations, 16);
    put<uint64_t>(a[A_UNITS] + 3 * 8, s.rig_object ? a[A_RIG2] : a[A_RIG]); put<uint64_t>(a[A_UNITS] + 4 * 8, s.weapon_object ? a[A_WOBJ2] : a[A_WOBJ]);
    put<uint64_t>(a[A_UNITS] + 5 * 8, a[A_RIG2]); put<uint64_t>(a[A_UNITS] + 6 * 8, a[A_WOBJ2]);
    for (int id : {A_RIG, A_RIG2}) {
      put<uint64_t>(a[id], s.vtable_at ? a[A_ALT] : a[A_RIG_VT]); put<uint32_t>(a[id] + 0x70, s.graph_count);
      put<uint64_t>(a[id] + 0x80, s.locals_at ? a[A_ALT] + 0x3000 : a[A_RIG_LOCALS]);
      put<uint64_t>(a[id] + 0x98, s.links_at ? a[A_ALT] + 0x1000 : a[A_RIG_LINKS]); put<uint64_t>(a[id] + 0xA0, s.hashes_at ? a[A_ALT] + 0x2000 : a[A_RIG_HASHES]);
    }
    put<uint64_t>(a[A_RIG_VT] + 0xE8, a[A_EXE] + L->graph_getter + s.getter_delta);
    put(a[A_RIG_HASHES], s.hashes); put(a[A_RIG_LINKS], s.links); put(a[A_ALT] + 0x2000, s.hashes); put(a[A_ALT] + 0x1000, s.links);
    if (s.write_root_local) { put(a[A_RIG_LOCALS] + 0x24, s.root_local); put(a[A_ALT] + 0x3000 + 0x24, s.root_local); }
    for (uint64_t frame : {stack(false), stack(true)}) {
      std::memcpy(reinterpret_cast<void *>(frame + 0x44), s.frame, 0x20);
      put<uint64_t>(frame + 0x48, s.stack_index); put<uint64_t>(frame + 0x80, s.stack_context ? context : context + 0x10); put(frame + 0xa0, s.dt_eval);
      put(frame + 0x230, s.root_source);
    }
    put(a[A_ARG] + ARG_ROTATION, s.rotation); put(a[A_ARG] + ARG_POSITION, s.position);
    // weapon definitions
    const Scene::Depth &d = s.depth;
    const uint64_t pool = a[A_POOL] + 0x30;
    put<uint32_t>(pool + 8, d.capacity); put<uint32_t>(pool + 12, d.empty); put<uint32_t>(pool + 16, d.multiplier);
    put<uint32_t>(pool + 0x48, d.capacity); put<uint32_t>(pool + 0x4C, d.empty); put<uint32_t>(pool + 0x50, d.multiplier);
    const uint32_t weapon = s.wdesc[0], mask = d.capacity && d.capacity <= 256 ? d.capacity - 1 : 7, start = (weapon * d.multiplier) & mask;
    for (uint32_t i = 0; i <= mask; ++i) { put<uint64_t>(a[A_PRIMARY] + i * 8, d.empty); put<uint64_t>(a[A_OVERRIDES] + i * 8, d.empty); }
    for (uint32_t i = 0; i < d.shift; ++i) put<uint64_t>(a[A_PRIMARY] + ((start + i) & mask) * 8, 0x70000000ull + i);
    for (uint32_t i = 0; i < d.override_shift; ++i) put<uint64_t>(a[A_OVERRIDES] + ((start + i) & mask) * 8, 0x70000000ull + i);
    if (d.in_primary) put<uint64_t>(a[A_PRIMARY] + ((start + d.shift) & mask) * 8, weapon | static_cast<uint64_t>(d.wi) << 32);
    if (d.in_overrides) put<uint64_t>(a[A_OVERRIDES] + ((start + d.override_shift) & mask) * 8, weapon | static_cast<uint64_t>(d.di) << 32);
    if (d.wi < 4096) {
      put<uint64_t>(a[A_DESCRIPTORS] + static_cast<uint64_t>(d.wi) * 8, d.descriptor_ok ? a[A_WDESC] : a[A_WDESC] + 0x40);
      put(a[A_PARAMS] + static_cast<uint64_t>(d.wi) * 0x3F0 + 0x350, d.kinds); put<uint32_t>(a[A_PACKED] + static_cast<uint64_t>(d.wi) * 12 + 4, d.packed);
    }
    put(a[A_SETTINGS] + L->s_degrees, d.degrees);
    uint64_t definition = 0;
    if (d.in_overrides) definition = d.di < 4096 ? a[A_DATA] + static_cast<uint64_t>(d.di) * L->definition : 0;
    else {
      row_bucket = static_cast<uint32_t>(s.resource % L->buckets);
      const uint32_t index = d.resource_di == -1 ? L->buckets - 1 : d.resource_di == -2 ? L->buckets : static_cast<uint32_t>(d.resource_di);
      for (uint32_t i = 0; i < 40; ++i) { put<uint64_t>(a[A_TABLE] + ((row_bucket + i) % L->buckets) * 16, i < d.resource_shift ? 0x5000 + i : 0); put<uint32_t>(a[A_TABLE] + ((row_bucket + i) % L->buckets) * 16 + 8, 0); }
      if (d.in_resources) {
        const uint64_t at = a[A_TABLE] + ((row_bucket + d.resource_shift) % L->buckets) * 16;
        put<uint64_t>(at, s.resource); put<uint32_t>(at + 8, index);
      }
      definition = index < L->buckets ? a[A_TABLE] + L->records + static_cast<uint64_t>(index) * L->definition : 0;
    }
    if (definition) {
      uint8_t block[0xdc]; std::memset(block, d.filler, sizeof block);
      std::memcpy(block, d.zoom, 12); std::memcpy(block + 0xC8, d.coefficient, 12); std::memcpy(block + 0xD4, d.near_term, 8);
      std::memcpy(reinterpret_cast<void *>(definition + 0x74), block, sizeof block);
    }
    // look input
    put(a[A_OWNER] + 0xa7c24 + static_cast<uint64_t>(s.stack_index < 8 ? s.stack_index : 0) * 0xa7aec, s.raw);
    put<uint32_t>(a[A_INPUT_OWNER] + L->input_device, s.device);
    std::memcpy(reinterpret_cast<void *>(a[A_INPUT_OWNER] + 0xa7af8), s.input_flags, sizeof s.input_flags);
    put(a[A_TIME], s.dt_global);
    put<uint32_t>(a[A_PLAYER] + 0x88, s.player_count); put<uint32_t>(a[A_PLAYER] + 0x3a8, s.entity);
    put<uint32_t>(a[A_GAME] + L->g_no_avatar, s.no_avatar); put<uint32_t>(a[A_GAME] + L->g_host_avatar, s.host_avatar); put<uint32_t>(a[A_GAME] + L->g_idle_avatar, s.idle_avatar);
    for (uint32_t i = 0; i < 8; ++i) { put<uint64_t>(a[A_AVMAP] + i * 8, 0xffffffffull); put<uint64_t>(a[A_ENTMAP] + i * 8, 0xffffffffull); }
    if (s.avatar_in_map) put<uint64_t>(a[A_AVMAP] + (s.descriptor[2] & 7) * 8, s.descriptor[2] | static_cast<uint64_t>(s.avatar_map_index) << 32);
    if (s.entity_in_map) put<uint64_t>(a[A_ENTMAP] + (s.entity & 7) * 8, s.entity | static_cast<uint64_t>(s.entity_row) << 32);
    if (s.entity_row <= 0x1000) put<uint32_t>(a[A_WORLD] + L->w_entity_avatar + static_cast<uint64_t>(s.entity_row) * 24, s.entity_avatar);
    std::memcpy(reinterpret_cast<void *>(a[A_GAME] + L->presets + static_cast<uint64_t>(s.preset_row) * 144), s.preset, 144);
  }
};

} // namespace dp
