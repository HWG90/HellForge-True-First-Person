/* native/first_person/native_ads_blur.cpp (retail kit 0.5.41-smooth.66, build
 * 26568081) translated to C11 for build 25480438. "cpp:N" names a line of the
 * retail file. Owned native-weapon DoF maxima only; no global effect, lens or
 * transform writes. See docs/BLUR-HELPER-TRANSLATION.md. */
#include "blur_helper.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

/* ==== Build constants ====================================================
 * B_: differs per build. Retail value in the comment, then the evidence:
 *   map   = tools/build_map.py --retail <kit> --to-here <retail value>
 *   image = bytes read on 2026-11-16 from game-pre1020.dll (game) or
 *           r6_reload_exe_20261017.bin (exe, a live capture: its pointers are
 *           relocated, image base 0x7ff672110000)
 *   a file name = that file of this repository holds the same value for this
 *           build. */
#define B_GAME_STAMP        UINT32_C(0x6ab3b43f) /* 0x6af322c1; map; profile.h; image PE header */
#define B_GAME_SIZE         UINT32_C(0x4744000)  /* 0x3382000;  map; profile.h; image PE header */
#define B_EXE_STAMP         UINT32_C(0x6ab382e4) /* 0x6af2d5e6; map; profile.h; image PE header */
#define B_EXE_SIZE          UINT32_C(0x39e8000)  /* 0x29fe000;  map; profile.h; image PE header */
#define B_EXE_SETTER        UINT32_C(0x310050)   /* 0x2df740;  map; feature_contract.h:29; image: [exe+0x27cc638] = base+0x310050 */
#define B_GAME_CALL         UINT32_C(0x100a80b)  /* 0xe9ac4b;  map; feature_contract.h:28; image: "mov rcx,r10; call [r11]" there */
#define B_EXE_PACK          UINT32_C(0x30ffc0)   /* 0x2df6b0;  map; feature_contract.h:30; image: [exe+0x27cc5e0+0x28] = base+0x30ffc0 */
#define B_EXE_LOOKUP        UINT32_C(0x62adb0)   /* 0x66a170;  map; feature_contract.h:31; image: [exe+0x1677dc8+0x38] = base+0x62adb0 */
#define B_EXE_HASH          UINT32_C(0x310140)   /* 0x2df830;  map; feature_contract.h:32; image: a jmp to exe+0x62a0c0 */
#define B_EXE_HASH_BODY     UINT32_C(0x62a0c0)   /* 0x669480;  map; feature_contract.h:33; image: the jmp target above */
#define B_EXE_VTABLE        UINT32_C(0x1677dc8)  /* 0x1715c40; map; blur_policy.c:37; image: its +0x28 and +0x38 hold the two functions below/above */
#define B_EXE_APPLY         UINT32_C(0x2dd5e0)   /* 0x2ad330;  map; blur_policy.c:39; image: [exe+0x1677dc8+0x28] = base+0x2dd5e0 */
#define B_EXE_QUERY         UINT32_C(0x27cc5e0)  /* 0x2873ac0; map; blur_policy.c:41; image: its +0x28 holds base+0x30ffc0 */
#define B_EXE_TABLE         UINT32_C(0x27cc638)  /* 0x2873b18; map; blur_policy.c:43; image: holds base+0x310050, then base+0x3100c0 */
#define B_EXE_GETTER        UINT32_C(0x3100c0)   /* 0x2df7b0;  map; blur_policy.c:44; image: [exe+0x27cc638+8] = base+0x3100c0 */
#define B_EXE_REGISTRY      UINT32_C(0x1a100f0)  /* 0x1ab76f8; map; profile.h FP_REGISTRY */
#define B_GAME_CONTEXT      UINT32_C(0x346d560)  /* 0x2e287d0; map; profile.h FP_CAMERA */
#define B_GAME_AVATAR_OWNER UINT32_C(0x3326d20)  /* 0x2ce1ee0; map; profile.h FP_AVATAR */
#define B_GAME_CONTROLLER   UINT32_C(0x347ce70)  /* 0x2e381a8; map; profile.h FP_CONTROLLER */
#define B_GAME_PARAM_POOL   UINT32_C(0x3326a38)  /* 0x2ce1bf8; map; blur_policy.c:61; BUILD-DATA-25480438.md */
#define B_GAME_ROOT         UINT32_C(0x3326468)  /* 0x2ce1618; map; profile.h FP_PLAYER; BUILD-DATA-25480438.md */
#define B_GAME_API_ROOT     UINT32_C(0x3326308)  /* 0x2ce14c8; map; profile.h FP_API_ROOT */
#define B_AVATAR_FLAGS      UINT32_C(0x53e880)   /* 0x53e888;  map; profile.h FP_AVATAR_FLAGS */
#define B_AVATAR_STRIDE     UINT32_C(0x1238)     /* 0x1240;    map; profile.h FP_AVATAR_STRIDE */
/* Code bytes, each read from this build's image at the address above. */
#define B_HEX_CALL      "41ff13" /* the same in retail */
#define B_HEX_SETTER    "48895c24084889742410574883ec404c8b11498bd94c8bca418bf8448bc249c1e920488d542460488bf141ff5238" /* the same (46 bytes) */
#define B_HEX_PACK      "418bc048c1e0208bca480bc1c3" /* the same */
#define B_HEX_LOOKUP    "405383b9a8000000004c8bda48897c2418488bd9c744241000000000" /* the same */
#define B_HEX_HASH      "e97b9f3100" /* retail e94b9c3800: the jmp displacement; here it lands on exe+0x62a0c0 = B_EXE_HASH_BODY */
#define B_HEX_HASH_BODY "48895c2408574883ec200fb60133ff488bd984c0743e3c2e752f8bd34533c02bd1e85ab7faff48c1e82069c895e9d15b" /* retail ...e89abcfaff...: one call displacement */

/* L_: structure layout, the same number as retail. Evidence: blur_policy.c
 * (this repository's 0.5.34 port, written on this build) reads the same
 * offsets; "image" where this build's code shows them too. */
#define L_CONTEXT_RING      0x1F8  /* head, then tail at +4; snapshot.c */
#define L_CONTEXT_IDLE      0x1F00
#define L_CONTEXT_RECORDS   0x200
#define L_CONTEXT_RECORD    0xE8
#define L_AVATAR_COUNT      0x6C   /* snapshot.c */
#define L_AVATAR_ROWS       0x110
#define L_FLAG_SEATED       14     /* bit 4; snapshot.c */
#define L_CONTROL_GUARD     5
#define L_REGISTRY_BLOCK    0x88   /* units */
#define L_REGISTRY_CAPACITY 0x98
#define L_REGISTRY_GENERATIONS 0xA0
#define L_ROOT_SYSTEM       0x18   /* blur_policy.c:35 */
#define L_SYSTEM_PROVIDER   0xDC8  /* image: mov r10,[rcx+0xdc8] at game+0x100a769 */
#define L_SYSTEM_QUERY      0xDD8  /* blur_policy.c:40 */
#define L_SYSTEM_TABLE      0xDE8  /* image: mov r11,[rcx+0xde8] at game+0x100a779 */
#define L_SYSTEM_ENTITIES   0x348  /* blur_policy.c:209 */
#define L_SYSTEM_ENTITY     0x30
#define L_VT_LOOKUP         0x38
#define L_VT_APPLY          0x28
#define L_QUERY_PACK        0x28
#define L_TABLE_GETTER      8
#define L_API_HASH          0xC0   /* blur_policy.c:46 */
#define L_HASH_API_HASH     0x28
#define L_POOL_BUCKETS      0x278  /* blur_policy.c:62 */
#define L_POOL_CAPACITY     0x280
#define L_POOL_EMPTY        0x284
#define L_POOL_MULTIPLIER   0x288
#define L_POOL_DESCRIPTORS  0x290
#define L_POOL_ROWS         0x298
#define L_PROVIDER_COUNT    0x90   /* blur_policy.c:94-100 */
#define L_PROVIDER_CAPACITY 0x94
#define L_PROVIDER_BUCKETS  0x98   /* image: mov rdi,[rcx+0x98] in exe+0x62adb0 */
#define L_PROVIDER_ACTIVE   0xA8   /* image: cmp dword [rcx+0xa8],0 */
#define L_PROVIDER_DIVISOR  0xAC   /* image: div dword [rcx+0xac] */
#define L_PROVIDER_ROWS     0xB8
#define L_PROVIDER_ALLOCATED 0xBC
#define L_PROVIDER_IDS      0xC8   /* blur_policy.c:180-183 */
#define L_PROVIDER_CLASSES  0xD0
#define L_PROVIDER_CHAINS   0xD8
#define L_PROVIDER_BLOBS    0xE8
#define L_BLOB_COUNT        4
#define L_BLOB_KEYS         0x10
/* Name hashes: not build data (blur_policy.c:22-23 has the same four). */
#define DOF_CLASS UINT32_C(0xF7A3101F)
static const uint32_t HASHES[3] = {UINT32_C(0xA7990477), UINT32_C(0xF07FC12E), UINT32_C(0x209F72CF)};
#define HIGH UINT64_C(0x800000000000)
/* ==== end of build constants ============================================ */

/* cpp:21-24 */
enum { OK = TFP_BLUR_OK, INVALID_CONFIG = TFP_BLUR_INVALID_CONFIG, BUSY = TFP_BLUR_BUSY, READ_FAILED = TFP_BLUR_READ_FAILED,
       IMAGE_MISMATCH = TFP_BLUR_IMAGE_MISMATCH, CODE_MISMATCH = TFP_BLUR_CODE_MISMATCH, API_MISMATCH = TFP_BLUR_API_MISMATCH,
       OWNER_MISMATCH = TFP_BLUR_OWNER_MISMATCH, UNIT_MISMATCH = TFP_BLUR_UNIT_MISMATCH, PIN_FAILED = TFP_BLUR_PIN_FAILED,
       SLOT_NOT_WRITABLE = TFP_BLUR_SLOT_NOT_WRITABLE, CAS_MISMATCH = TFP_BLUR_CAS_MISMATCH, CAS_FAULT = TFP_BLUR_CAS_FAULT,
       CLEANUP_PENDING = TFP_BLUR_CLEANUP_PENDING, SOURCE_INVALID = TFP_BLUR_SOURCE_INVALID,
       TOKEN_MISMATCH = TFP_BLUR_TOKEN_MISMATCH, NO_SOURCE = TFP_BLUR_NO_SOURCE };

static TfpBlurHost host;
void tfp_blur_set_host(const TfpBlurHost *h) { host = *h; }

/* cpp:13-19 */
static void control_acquire(void) { host.lock_exclusive(host.user, TFP_BLUR_LOCK_CONTROL); }
static void control_release(void) { host.unlock_exclusive(host.user, TFP_BLUR_LOCK_CONTROL); }
static void policy_acquire(void) { host.lock_exclusive(host.user, TFP_BLUR_LOCK_POLICY); }
static void policy_release(void) { host.unlock_exclusive(host.user, TFP_BLUR_LOCK_POLICY); }
static _Atomic uintptr_t original;
static _Atomic uint32_t installed, armed, reserved, last_error;
static _Atomic uint64_t seen, forwarded, replaced, immediate_applies;
static uint64_t slot = 0, slot_image = 0, generation_counter = 0, owned_generation = 0, retired_generation = 0;
static _Atomic uint64_t return_address;
static bool possible = false;

/* cpp:26-38 */
typedef struct Source {
  uint32_t post_index, entity, row, count; uint64_t blob;
  uint32_t hashes[512]; uint64_t fields[3];
} Source;
typedef struct Policy {
  TfpBlurConfig c; uint64_t controller, descriptor, registry, units, generations, body_object;
  uint8_t descriptor_bytes[20]; bool safe;
  uint64_t system, provider, setter_table, param_pool, param_descriptor, param_row;
  uint8_t param_bytes[24]; uint32_t post_indices[2], source_count; Source sources[2];
  uint64_t ids, classes, chains, blobs;
  uint32_t last_hash, last_instance; float last_native, last_applied;
} Policy;
static Policy policy;

/* cpp:49-60. Fault evidence belongs to one explicit owner-data guard.
 * The constructor/destructor pair is written out in owner_status. */
typedef struct OwnerReadProbe { struct OwnerReadProbe *previous; bool transport, unsafe; } OwnerReadProbe;
static _Thread_local OwnerReadProbe *owner_read_probe = NULL;
/* cpp:61-78 */
static bool readable_span(uint64_t at, size_t length) { return host.readable_span(host.user, at, length); }
/* cpp:79-104 */
static bool read(uint64_t at, void *out, size_t length) {
  if (at < 65536 || at >= HIGH || length == 0 || length > 4096 || at + length >= HIGH) {
    if (owner_read_probe) owner_read_probe->unsafe = true;
    return false;
  }
  const bool complete = host.read(host.user, at, out, length);
  if (complete) return true;
  if (owner_read_probe) {
    if (readable_span(at, length)) owner_read_probe->transport = true;
    else owner_read_probe->unsafe = true;
  }
  return false;
}
/* cpp:105 */
#define value(at, out) read((at), &(out), sizeof(out))
/* cpp:106-110 */
static bool pointer(uint64_t at, uint64_t *out) {
  if (!value(at, *out)) return false;
  if (*out >= 65536 && *out < HIGH) return true;
  if (owner_read_probe) owner_read_probe->unsafe = true;
  return false;
}
/* cpp:111 */
static void error(uint32_t code) { atomic_store_explicit(&last_error, code, memory_order_release); }
/* cpp:112 */
static uint32_t reject(uint32_t code) { error(code); return 0; }
/* cpp:113-119. IMAGE_DOS_HEADER is 64 bytes (e_magic +0, e_lfanew +0x3c);
 * IMAGE_NT_HEADERS64 is 264 (Signature +0, Machine +4, TimeDateStamp +8,
 * OptionalHeader.Magic +24, SizeOfImage +80). Same two reads, same sizes. */
static bool image(uint64_t base, uint32_t stamp, uint32_t length) {
  uint8_t dos[64] = {0}, nt[264] = {0};
  uint16_t e_magic = 0, machine = 0, magic = 0; int32_t e_lfanew = 0; uint32_t signature = 0, time_date_stamp = 0, size_of_image = 0;
  if (!read(base, dos, sizeof dos)) return false;
  memcpy(&e_magic, dos, 2); memcpy(&e_lfanew, dos + 0x3c, 4);
  if (!(e_magic == 0x5A4D && e_lfanew >= 64 && e_lfanew <= 4096)) return false;
  if (!read(base + (uint64_t)(int64_t)e_lfanew, nt, sizeof nt)) return false;
  memcpy(&signature, nt, 4); memcpy(&machine, nt + 4, 2); memcpy(&time_date_stamp, nt + 8, 4);
  memcpy(&magic, nt + 24, 2); memcpy(&size_of_image, nt + 80, 4);
  return signature == 0x00004550 && machine == 0x8664 && magic == 0x20b
      && time_date_stamp == stamp && size_of_image == length;
}
/* cpp:120-127 */
static uint8_t hex_digit(char c) { return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10); }
static bool bytes(uint64_t at, const char *hex) {
  uint8_t actual[64] = {0}; const size_t count = strlen(hex) / 2;
  if (!count || count > sizeof actual || !read(at, actual, count)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (actual[i] != (uint8_t)((hex_digit(hex[i*2]) << 4) | hex_digit(hex[i*2+1]))) return false;
  }
  return true;
}
/* cpp:128-132 */
static bool executable(uint64_t at) { return host.executable(host.user, at); }
/* cpp:133-159 */
static bool owner(Policy *p, bool capture) {
  const TfpBlurConfig *c = &p->c; uint64_t ctx = 0, camera = 0, avatar_owner = 0, controller = 0, descriptor = 0, registry = 0, units = 0, generations = 0, object = 0;
  uint32_t count = 0, capacity = 0; uint8_t d[20] = {0}, flags[24] = {0}, controls[6] = {0}, generation = 0, confirmed = 0; uint64_t confirmed_object = 0;
  const uint32_t index = c->body & 0x3fffff;
  if (!pointer(c->game + B_GAME_CONTEXT, &ctx) || ctx != c->context || !pointer(ctx, &camera) || camera != c->camera
      || !pointer(c->game + B_GAME_AVATAR_OWNER, &avatar_owner) || avatar_owner != c->avatar_owner
      || !pointer(c->game + B_GAME_CONTROLLER, &controller) || !read(controller, controls, 6)
      || !value(avatar_owner + L_AVATAR_COUNT, count) || count == 0 || count > 8 || c->avatar_index >= count
      || !pointer(avatar_owner + L_AVATAR_ROWS + (uint64_t)c->avatar_index*8, &descriptor) || !read(descriptor, d, 20)
      || !read(avatar_owner + B_AVATAR_FLAGS + (uint64_t)c->avatar_index*B_AVATAR_STRIDE, flags, 24)
      || !pointer(c->exe + B_EXE_REGISTRY, &registry) || !value(registry + L_REGISTRY_CAPACITY, capacity) || capacity == 0 || capacity > 0x400000 || index >= capacity
      || !pointer(registry + L_REGISTRY_GENERATIONS, &generations) || !value(generations + index, generation) || generation != (uint8_t)(c->body >> 22)
      || !pointer(registry + L_REGISTRY_BLOCK, &units) || !pointer(units + (uint64_t)index*8, &object)
      || !value(generations + index, confirmed) || confirmed != generation
      || !pointer(units + (uint64_t)index*8, &confirmed_object) || confirmed_object != object) return false;
  uint32_t avatar = 0, body = 0, mask = 0; memcpy(&avatar, d + 8, 4); memcpy(&body, d + 12, 4); memcpy(&mask, controls, 4);
  if (avatar != c->avatar || body != c->body) return false;
  if (capture) {
    p->controller = controller; p->descriptor = descriptor; p->registry = registry; p->units = units; p->generations = generations; p->body_object = object;
    memcpy(p->descriptor_bytes, d, 20);
  } else if (controller != p->controller || descriptor != p->descriptor || registry != p->registry || units != p->units
      || generations != p->generations || object != p->body_object || memcmp(d, p->descriptor_bytes, 20) != 0) return false;
  /* The per-weapon blur filter serves both hip and ADS. Inhibited actions,
   * seated state and the unknown controller guard retain native effects. */
  p->safe = (mask == 0 || mask == 2) && controls[L_CONTROL_GUARD] == 0 && (flags[L_FLAG_SEATED] & 4) == 0;
  return true;
}
/* cpp:160-166. Only an invocation-local owner-data failure may produce
 * retryable code 3. */
static uint32_t owner_status(Policy *p, bool capture) {
  OwnerReadProbe probe; probe.previous = owner_read_probe; probe.transport = false; probe.unsafe = false;
  owner_read_probe = &probe;
  uint32_t result;
  if (owner(p, capture)) result = OK;
  else result = probe.transport && !probe.unsafe ? READ_FAILED : OWNER_MISMATCH;
  owner_read_probe = probe.previous;
  return result;
}
/* cpp:167-169 */
static bool checked_owner(Policy *p, bool capture, uint32_t *failure) {
  *failure = owner_status(p, capture); return *failure == OK;
}
/* cpp:170-177 (allow_normal defaults to false in retail) */
static bool request_matches(const Policy *p, bool allow_normal) {
  uint32_t head = 0, tail = 0, r[4] = {0};
  if (!value(p->c.context + L_CONTEXT_RING, head) || !value(p->c.context + L_CONTEXT_RING + 4, tail) || head >= 32 || tail >= 32) return false;
  const uint64_t at = head == tail ? p->c.context + L_CONTEXT_IDLE : p->c.context + L_CONTEXT_RECORDS + ((tail + 31) % 32) * L_CONTEXT_RECORD;
  if (at != p->c.request || !read(at, r, 16) || r[1] != p->c.avatar) return false;
  if (allow_normal && (r[0] & 0xffff) == 4) return true;
  return head != tail && (r[0] & 0xffff) == 6 && r[2] == p->c.weapon && r[3] == p->c.body;
}
static bool provider(const TfpBlurConfig *c, uint64_t *system, uint64_t *object, uint64_t *table);
/* cpp:179-202. The weapon parameter map uses the same bounded open-addressed
 * lookup as the verified native ADS constructor. */
static int weapon_parameters(Policy *p, bool capture, uint32_t *indices) {
  uint64_t pool = 0, buckets = 0, descriptors = 0, rows = 0, d = 0; uint32_t cap = 0, empty = 0, mult = 0, index = 0;
  if (!pointer(p->c.game + B_GAME_PARAM_POOL, &pool) || !pointer(pool + L_POOL_BUCKETS, &buckets)
      || !value(pool + L_POOL_CAPACITY, cap) || !value(pool + L_POOL_EMPTY, empty) || !value(pool + L_POOL_MULTIPLIER, mult)
      || !cap || cap > 1048576 || (cap & (cap - 1)) || empty == p->c.weapon
      || !pointer(pool + L_POOL_DESCRIPTORS, &descriptors) || !pointer(pool + L_POOL_ROWS, &rows)) return -1;
  bool found = false; const uint32_t first = p->c.weapon * mult;
  for (uint32_t n = 0; n < cap && n < 4096; ++n) {
    uint32_t r[2] = {0}; if (!read(buckets + (uint64_t)((first + n) & (cap - 1))*8, r, 8)) return -1;
    if (r[0] == empty) return 0;
    if (r[0] == p->c.weapon) { index = r[1]; found = true; break; }
  }
  if (!found || index >= 65536) return -1;
  uint8_t bytes24[24] = {0}; uint32_t entity = 0;
  if (!pointer(descriptors + (uint64_t)index*8, &d) || !read(d, bytes24, 24)) return -1;
  memcpy(&entity, bytes24 + 8, 4); if (entity != p->c.weapon) return -1;
  if ((bytes24[20] & 1) == 0) return 0;
  const uint64_t row = rows + (uint64_t)index*8;
  if (!read(row, indices, 8)) return -1;
  if (capture) { p->param_pool = pool; p->param_descriptor = d; p->param_row = row; memcpy(p->param_bytes, bytes24, 24); }
  else if (pool != p->param_pool || d != p->param_descriptor || row != p->param_row || memcmp(bytes24, p->param_bytes, 24) != 0) return -1;
  return 1;
}
/* cpp:203-228. Exact EXE62ADB0 semantics: +AC is the bucket modulus, while
 * +94 is storage capacity (overflow next indices may legitimately exceed +AC). */
static int component(const Policy *p, uint32_t entity, uint32_t *row) {
  uint64_t buckets = 0; uint32_t count = 0, capacity = 0, active = 0, divisor = 0, rows = 0, allocated = 0;
  if (!pointer(p->provider + L_PROVIDER_BUCKETS, &buckets) || !value(p->provider + L_PROVIDER_COUNT, count) || !value(p->provider + L_PROVIDER_CAPACITY, capacity)
      || !value(p->provider + L_PROVIDER_ACTIVE, active) || !value(p->provider + L_PROVIDER_DIVISOR, divisor)
      || !value(p->provider + L_PROVIDER_ROWS, rows) || !value(p->provider + L_PROVIDER_ALLOCATED, allocated)
      || !capacity || capacity > 4096 || count > capacity || active > count || !divisor || divisor > capacity
      || !rows || rows > allocated || allocated > 4096) return -1;
  uint32_t at = entity % divisor, index = 0; bool found = false;
  for (uint32_t n = 0; n < capacity; ++n) {
    uint32_t b[3] = {0}; if (at >= count || !read(buckets + (uint64_t)at*12, b, 12)) return -1;
    if (b[2] == 0xFFFFFFFE) return 0;
    if (b[0] == entity) { index = b[1]; found = true; break; }
    if (b[2] == 0x7FFFFFFF) return 0;
    at = b[2];
  }
  if (!found) return -1;
  for (uint32_t n = 0; n < rows; ++n) {
    if (index == 0) return 0;
    if (index >= rows) return -1;
    uint32_t id = 0, kind = 0, next = 0;
    if (!value(p->ids + (uint64_t)index*4, id) || id != entity
        || !value(p->classes + (uint64_t)index*4, kind)) return -1;
    if (kind == DOF_CLASS) { *row = index; return 1; }
    if (!value(p->chains + (uint64_t)index*4, next)) return -1;
    index = next;
  }
  return -1;
}
/* cpp:229-242 */
static bool fields(Source *s, bool capture) {
  uint32_t count = 0, keys[512] = {0};
  if (!value(s->blob + L_BLOB_COUNT, count) || !count || count > 512 || !read(s->blob + L_BLOB_KEYS, keys, count*4)) return false;
  if (!capture && (count != s->count || memcmp(keys, s->hashes, count*4) != 0)) return false;
  uint64_t offsets[3] = {0};
  for (uint32_t i = 0; i < count; ++i) for (unsigned k = 0; k < 3; ++k) if (keys[i] == HASHES[k]) {
    uint32_t type = 0; if (offsets[k]) return false;
    offsets[k] = s->blob + L_BLOB_KEYS + (uint64_t)count*4 + (uint64_t)i*16;
    if (!value(offsets[k], type) || type != 2) return false;
  }
  for (unsigned k = 0; k < 3; ++k) if (!offsets[k] || (!capture && offsets[k] != s->fields[k])) return false;
  if (capture) { s->count = count; memcpy(s->hashes, keys, count*4); memcpy(s->fields, offsets, sizeof offsets); }
  return true;
}
/* cpp:243-272. Every refusal is written as retail has it, including the
 * `capture ? ... : ...` in branches where capture is already known. */
static bool source(Policy *p, bool capture) {
  uint64_t system = 0, object = 0, table = 0, ids = 0, classes = 0, chains = 0, blobs = 0;
  if (!provider(&p->c, &system, &object, &table) || !pointer(object + L_PROVIDER_IDS, &ids) || !pointer(object + L_PROVIDER_CLASSES, &classes)
      || !pointer(object + L_PROVIDER_CHAINS, &chains) || !pointer(object + L_PROVIDER_BLOBS, &blobs)) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
  if (capture) { p->system = system; p->provider = object; p->setter_table = table; p->ids = ids; p->classes = classes; p->chains = chains; p->blobs = blobs; }
  else if (system != p->system || object != p->provider || table != p->setter_table || ids != p->ids
      || classes != p->classes || chains != p->chains || blobs != p->blobs) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
  uint32_t indices[2] = {0}; const int params = weapon_parameters(p, capture, indices);
  if (params <= 0) return reject(capture ? (params == 0 ? NO_SOURCE : SOURCE_INVALID) : UNIT_MISMATCH) != 0;
  if (!capture && memcmp(indices, p->post_indices, 8) != 0) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
  Source current[2]; memset(current, 0, sizeof current); uint32_t found = 0;
  for (unsigned i = 0; i < 2; ++i) {
    if (indices[i] >= 4096) continue;
    uint32_t entity = 0, row = 0;
    if (!value(system + L_SYSTEM_ENTITIES + (uint64_t)indices[i]*L_SYSTEM_ENTITY, entity) || !entity || entity == UINT32_MAX) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
    const int member = component(p, entity, &row);
    if (member < 0) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
    if (member == 0) continue;
    Source s; memset(&s, 0, sizeof s); s.post_index = indices[i]; s.entity = entity; s.row = row;
    if (!pointer(blobs + (uint64_t)row*8, &s.blob)) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
    if (capture) { if (!fields(&s, true)) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0; }
    else {
      if (found >= p->source_count || s.post_index != p->sources[found].post_index || s.entity != p->sources[found].entity
          || s.row != p->sources[found].row || s.blob != p->sources[found].blob) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
      s = p->sources[found]; if (!fields(&s, false)) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
    }
    if (found && s.entity == current[0].entity) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
    current[found++] = s;
  }
  if (!found) return reject(capture ? NO_SOURCE : UNIT_MISMATCH) != 0;
  if (capture) { p->source_count = found; memcpy(p->post_indices, indices, 8); memcpy(p->sources, current, sizeof current); }
  else if (found != p->source_count) return reject(capture ? SOURCE_INVALID : UNIT_MISMATCH) != 0;
  return true;
}
/* cpp:273-287. The 8-byte alignment test stays here; the page questions
 * (retail's non-fixture path: a writable section of the image at `exe`) are
 * the host's. */
static bool writable_slot(uint64_t at, uint64_t exe) {
  if ((at & 7) != 0) return false;
  return host.writable_slot(host.user, at, exe);
}
/* cpp:288-293 */
static bool slot_read(uint64_t at, uint64_t *out) { return value(at, *out); }
/* cpp:294-303 */
static bool cas(uint64_t expected, uint64_t desired, uint64_t *observed) {
  return host.cas(host.user, slot, expected, desired, observed);
}
/* cpp:304-313 */
static bool binary_contract(const TfpBlurConfig *c) {
  if (!image(c->game, B_GAME_STAMP, B_GAME_SIZE) || !image(c->exe, B_EXE_STAMP, B_EXE_SIZE)) return reject(IMAGE_MISMATCH) != 0;
  if (!bytes(c->game + B_GAME_CALL, B_HEX_CALL)
      || !bytes(c->exe + B_EXE_SETTER, B_HEX_SETTER)
      || !bytes(c->exe + B_EXE_PACK, B_HEX_PACK)
      || !bytes(c->exe + B_EXE_LOOKUP, B_HEX_LOOKUP)
      || !bytes(c->exe + B_EXE_HASH, B_HEX_HASH)
      || !bytes(c->exe + B_EXE_HASH_BODY, B_HEX_HASH_BODY)) return reject(CODE_MISMATCH) != 0;
  return true;
}
/* cpp:314-326 */
static bool provider(const TfpBlurConfig *c, uint64_t *system, uint64_t *object, uint64_t *table) {
  uint64_t root = 0, vt = 0, lookup = 0, apply = 0, query = 0, pack = 0, getter = 0, apis = 0, hash_api = 0, hash = 0;
  return pointer(c->game + B_GAME_ROOT, &root) && pointer(root + L_ROOT_SYSTEM, system)
      && pointer(*system + L_SYSTEM_PROVIDER, object) && pointer(*object, &vt) && vt == c->exe + B_EXE_VTABLE
      && pointer(vt + L_VT_LOOKUP, &lookup) && lookup == c->exe + B_EXE_LOOKUP
      && pointer(vt + L_VT_APPLY, &apply) && apply == c->exe + B_EXE_APPLY
      && pointer(*system + L_SYSTEM_QUERY, &query) && query == c->exe + B_EXE_QUERY
      && pointer(query + L_QUERY_PACK, &pack) && pack == c->exe + B_EXE_PACK
      && pointer(*system + L_SYSTEM_TABLE, table) && *table == c->exe + B_EXE_TABLE
      && pointer(*table + L_TABLE_GETTER, &getter) && getter == c->exe + B_EXE_GETTER
      && pointer(c->game + B_GAME_API_ROOT, &apis) && pointer(apis + L_API_HASH, &hash_api)
      && pointer(hash_api + L_HASH_API_HASH, &hash) && hash == c->exe + B_EXE_HASH;
}
/* cpp:327-340. The slot must hold exe+setter and the three engine functions
 * must be executable (retail's non-fixture path, cpp:335-336). */
static bool contract(const TfpBlurConfig *c, uint64_t *setter, uint64_t *address) {
  if (!binary_contract(c)) return false;
  uint64_t system = 0, object = 0, table = 0, target = 0;
  if (!provider(c, &system, &object, &table) || !slot_read(table, &target)) return reject(API_MISMATCH) != 0;
  *address = table; if (!writable_slot(*address, c->exe)) return reject(SLOT_NOT_WRITABLE) != 0;
  if (target != c->exe + B_EXE_SETTER || !executable(c->exe + B_EXE_PACK) || !executable(c->exe + B_EXE_LOOKUP)
      || !executable(c->exe + B_EXE_APPLY)) return reject(API_MISMATCH) != 0;
  if (!executable(target)) return reject(API_MISMATCH) != 0;
  *setter = target; return true;
}
/* cpp:341, 395 */
static uint64_t callback(void) { return host.hook; }
/* cpp:342-346 */
static bool dispatch_owned(const Policy *p) {
  uint64_t system = 0, object = 0, table = 0, target = 0;
  return provider(&p->c, &system, &object, &table) && system == p->system && object == p->provider && table == p->setter_table
      && table == slot && slot_read(slot, &target) && target == callback();
}
/* cpp:347 */
static bool selected_hash(uint32_t hash) { return hash == HASHES[0] || hash == HASHES[1] || hash == HASHES[2]; }
/* cpp:348-352 */
static bool selected_source(const Policy *p, uint64_t packed) {
  if ((uint32_t)(packed >> 32) != DOF_CLASS) return false;
  for (unsigned i = 0; i < p->source_count; ++i) if ((uint32_t)packed == p->sources[i].entity) return true;
  return false;
}
/* The pinned original (cpp:360, 368, 393). Retail calls it directly; so does
 * this when host.forward is NULL. */
static void call_original(void *object, uint64_t instance, uint32_t hash, const void *variant) {
  const uintptr_t target = atomic_load_explicit(&original, memory_order_acquire);
  if (host.forward) host.forward(host.user, (uint64_t)target, object, instance, hash, variant);
  else ((TfpBlurSetter)target)(object, instance, hash, variant);
}
/* cpp:353-364. Under policy_lock; the pinned original is the only mutating
 * engine call. */
static bool apply_immediately(Policy *p, uint32_t *failure) {
  if (!checked_owner(p, false, failure) || !request_matches(p, false) || !p->safe || !source(p, false) || !dispatch_owned(p)) return false;
  for (unsigned i = 0; i < p->source_count; ++i) for (unsigned k = 0; k < 3; ++k) {
    if (!checked_owner(p, false, failure) || !request_matches(p, false) || !p->safe || !source(p, false) || !dispatch_owned(p)) return false;
    _Alignas(16) uint8_t variant[24] = {0}; const uint32_t type = 2; memcpy(variant, &type, 4);
    const uint64_t packed = (uint64_t)DOF_CLASS << 32 | p->sources[i].entity;
    call_original((void *)(uintptr_t)p->provider, packed, HASHES[k], variant);
    p->last_hash = HASHES[k]; p->last_instance = DOF_CLASS; p->last_applied = 0; atomic_fetch_add_explicit(&immediate_applies, 1, memory_order_relaxed);
  }
  return true;
}
/* cpp:365-394 setter_hook. `caller` replaces _ReturnAddress(). The policy
 * lock is released at the single exit below (retail's Unlock guard). */
void tfp_blur_setter_from(uint64_t caller, void *object, uint64_t instance, uint32_t hash, const void *native) {
  atomic_fetch_add_explicit(&seen, 1, memory_order_relaxed);
  if (!atomic_load_explicit(&armed, memory_order_acquire) || caller != atomic_load_explicit(&return_address, memory_order_relaxed)) {
    atomic_fetch_add_explicit(&forwarded, 1, memory_order_relaxed); call_original(object, instance, hash, native); return;
  }
  policy_acquire();
  const void *output = native; _Alignas(16) uint8_t adjusted[24] = {0};
  if (atomic_load_explicit(&armed, memory_order_acquire) && (uint64_t)(uintptr_t)object == policy.provider && selected_hash(hash)
      && selected_source(&policy, instance)) {
    const uint32_t failure = owner_status(&policy, false);
    if (failure != OK) { atomic_store_explicit(&armed, 0, memory_order_release); error(failure); }
    else if (!dispatch_owned(&policy)) { atomic_store_explicit(&armed, 0, memory_order_release); error(API_MISMATCH); }
    else if (!request_matches(&policy, false)) { atomic_store_explicit(&armed, 0, memory_order_release); error(OWNER_MISMATCH); }
    else if (policy.safe) {
      if (!source(&policy, false)) { atomic_store_explicit(&armed, 0, memory_order_release); }
      else {
        uint32_t tag = 0; float scalar = 0;
        if (read((uint64_t)(uintptr_t)native, adjusted, 24)) {
          memcpy(&tag, adjusted, 4); memcpy(&scalar, adjusted + 8, 4);
          if (tag == 2 && isfinite(scalar)) {
            const float zero = 0; memcpy(adjusted + 8, &zero, 4); output = adjusted;
            policy.last_hash = hash; policy.last_instance = (uint32_t)(instance >> 32);
            policy.last_native = scalar; policy.last_applied = 0; atomic_fetch_add_explicit(&replaced, 1, memory_order_relaxed);
          }
        }
      }
    }
  }
  atomic_fetch_add_explicit(&forwarded, 1, memory_order_relaxed); call_original(object, instance, hash, output);
  policy_release();
}
/* cpp:396-399 */
static bool pin(uint64_t address) { return host.pin(host.user, address); }
/* cpp:400-411 */
static bool restore_slot(void) {
  if (possible) {
    uint64_t current = 0; if (!slot_read(slot, &current)) return false;
    if (current == callback()) {
      if (!writable_slot(slot, slot_image)) return false;
      uint64_t observed = 0; if (!cas(callback(), (uint64_t)atomic_load(&original), &observed)) return false;
    }
    possible = false;
  }
  atomic_store_explicit(&installed, 0, memory_order_release); retired_generation = owned_generation; owned_generation = 0;
  atomic_store_explicit(&reserved, 0, memory_order_release); return true;
}
/* cpp:412-414 */
static bool output_generation(uint64_t *output, uint64_t generation) {
  return host.write(host.user, output, &generation, 8);
}
/* cpp:415-420 */
static uint32_t failed_begin(uint32_t cause, uint64_t *output) {
  atomic_store_explicit(&armed, 0, memory_order_release);
  policy_acquire(); policy_release();
  if (!restore_slot()) { error(CLEANUP_PENDING); return 2; }
  output_generation(output, 0); return reject(cause);
}
/* cpp:421-426 */
static bool config(const TfpBlurConfig *input, TfpBlurConfig *c) {
  if (!read((uint64_t)(uintptr_t)input, c, sizeof *c) || c->size != sizeof *c || c->abi != 1 || c->reserved != 0
      || c->avatar_index >= 8 || !c->avatar || c->avatar == 0xffffffff || !c->body || c->body == 0xffffffff
      || !c->weapon || c->weapon == 0xffffffff || c->context < 65536 || c->camera < 65536 || c->request < 65536) return reject(INVALID_CONFIG) != 0;
  return true;
}
/* cpp:427-437. Retail's Unlock guard for control_lock: the body is a
 * *_locked function; the export locks, calls it, unlocks. */
static uint32_t validate_locked(const TfpBlurConfig *input) {
  TfpBlurConfig c; memset(&c, 0, sizeof c); if (!config(input, &c)) return 0;
  if (atomic_load(&reserved)) return reject(BUSY);
  Policy next; memset(&next, 0, sizeof next); next.c = c; uint64_t target = 0; uint64_t address = 0;
  uint32_t failure = OK;
  if (!checked_owner(&next, true, &failure) || !next.safe || !request_matches(&next, true)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  if (!contract(&c, &target, &address)) return 0;
  if (!source(&next, true)) return 0;
  if (!checked_owner(&next, false, &failure) || !next.safe || !request_matches(&next, true)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  if (!source(&next, false)) return 0;
  error(OK); return 1;
}
uint32_t tfp_blur_validate(const TfpBlurConfig *input) {
  control_acquire(); const uint32_t result = validate_locked(input); control_release(); return result;
}
/* cpp:438-472 */
static uint32_t begin_locked(const TfpBlurConfig *input, uint64_t *output) {
  if (!output_generation(output, 0)) return reject(INVALID_CONFIG);
  TfpBlurConfig c; memset(&c, 0, sizeof c); if (!config(input, &c)) return 0;
  if (atomic_load(&reserved)) return reject(BUSY);
  Policy next; memset(&next, 0, sizeof next); next.c = c; uint64_t target = 0; uint64_t address = 0;
  uint32_t failure = OK;
  if (!checked_owner(&next, true, &failure) || !next.safe || !request_matches(&next, false)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  if (!contract(&c, &target, &address)) return 0;
  if (!source(&next, true)) return 0;
  if (atomic_load(&original) && atomic_load(&original) != (uintptr_t)target) return reject(API_MISMATCH);
  if (!pin((uint64_t)(uintptr_t)&tfp_blur_begin)) return reject(PIN_FAILED);
  if (!pin(c.exe)) return reject(PIN_FAILED); /* retail's non-fixture path, cpp:448-450 */
  atomic_store_explicit(&original, (uintptr_t)target, memory_order_release); slot = address; slot_image = c.exe; possible = false;
  if (++generation_counter == 0) ++generation_counter;
  if (!output_generation(output, generation_counter)) return reject(INVALID_CONFIG);
  owned_generation = generation_counter; atomic_store_explicit(&reserved, 1, memory_order_release); possible = true;
  if (!writable_slot(slot, c.exe)) return failed_begin(SLOT_NOT_WRITABLE, output);
  uint64_t observed = 0; if (!cas(target, callback(), &observed)) return failed_begin(CAS_FAULT, output);
  if (observed != target) { possible = false; return failed_begin(CAS_MISMATCH, output); }
  atomic_store_explicit(&installed, 1, memory_order_release);
  if (!dispatch_owned(&next)) return failed_begin(CAS_MISMATCH, output);
  policy_acquire();
  policy = next; atomic_store(&seen, 0); atomic_store(&forwarded, 0); atomic_store(&replaced, 0); atomic_store(&immediate_applies, 0);
  const bool applied = apply_immediately(&policy, &failure);
  const bool stable = applied && checked_owner(&policy, false, &failure) && request_matches(&policy, false) && policy.safe && source(&policy, false) && dispatch_owned(&policy);
  atomic_store_explicit(&return_address, c.game + B_GAME_CALL + 3, memory_order_relaxed);
  if (stable) atomic_store_explicit(&armed, 1, memory_order_release);
  policy_release();
  if (!stable) return failed_begin(failure == OK ? OWNER_MISMATCH : failure, output);
  error(OK); return 1;
}
uint32_t tfp_blur_begin(const TfpBlurConfig *input, uint64_t *output) {
  control_acquire(); const uint32_t result = begin_locked(input, output); control_release(); return result;
}
/* cpp:473-480 */
static uint32_t end_locked(uint64_t generation) {
  if (!generation) return reject(TOKEN_MISMATCH);
  if (!atomic_load(&reserved)) { if (generation != retired_generation) return reject(TOKEN_MISMATCH); error(OK); return 1; }
  if (generation != owned_generation) return reject(TOKEN_MISMATCH);
  atomic_store_explicit(&armed, 0, memory_order_release); policy_acquire(); policy_release();
  if (!restore_slot()) return reject(CLEANUP_PENDING);
  error(OK); return 1;
}
uint32_t tfp_blur_end(uint64_t generation) {
  control_acquire(); const uint32_t result = end_locked(generation); control_release(); return result;
}
/* cpp:481-490 */
static uint32_t read_stats_locked(uint64_t generation, TfpBlurStats *output) {
  if (atomic_load(&reserved) ? (!generation || generation != owned_generation) : generation != 0) return reject(TOKEN_MISMATCH);
  TfpBlurStats s; memset(&s, 0, sizeof s);
  if (!read((uint64_t)(uintptr_t)output, &s, sizeof s) || s.size != sizeof s || s.abi != 1) return reject(INVALID_CONFIG);
  policy_acquire(); s.last_native = policy.last_native; s.last_applied = policy.last_applied;
  s.last_hash = policy.last_hash; s.last_instance = policy.last_instance; policy_release();
  s.reserved = 0; s.reserved_tail = 0; s.installed = atomic_load(&installed); s.armed = atomic_load(&armed); s.error = atomic_load(&last_error);
  s.seen = atomic_load(&seen); s.forwarded = atomic_load(&forwarded); s.replaced = atomic_load(&replaced); s.immediate_applies = atomic_load(&immediate_applies);
  return host.write(host.user, output, &s, sizeof s);
}
uint32_t tfp_blur_read_stats(uint64_t generation, TfpBlurStats *output) {
  control_acquire(); const uint32_t result = read_stats_locked(generation, output); control_release(); return result;
}
/* cpp:491 */
uint32_t tfp_blur_error(void) { return atomic_load_explicit(&last_error, memory_order_acquire); }

/* Not in retail: every static back to its start-up value; the host is kept. */
void tfp_blur_reset_for_tests(void) {
  atomic_store(&original, 0);
  atomic_store(&installed, 0); atomic_store(&armed, 0); atomic_store(&reserved, 0); atomic_store(&last_error, 0);
  atomic_store(&seen, 0); atomic_store(&forwarded, 0); atomic_store(&replaced, 0); atomic_store(&immediate_applies, 0);
  atomic_store(&return_address, 0);
  slot = 0; slot_image = 0; generation_counter = 0; owned_generation = 0; retired_generation = 0; possible = false;
  memset(&policy, 0, sizeof policy);
  owner_read_probe = NULL;
}
