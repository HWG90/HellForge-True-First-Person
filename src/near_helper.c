/* native/near_clip/native_ads_near.cpp (0.5.41-smooth.66, 345 lines) in C11,
 * for build 25480438. First-person near clip: one generation-owned CameraAPI
 * slot; no camera transforms, settings, executable writes or protection
 * changes.
 *
 * The game sets the gameplay camera's near distance to its .rdata 0.05
 * through CameraAPI+0x68 from several call sites. Only a game.dll caller
 * setting exactly that value (or a sight's own distance in (0.05, 1.0]) on
 * the owned camera, with the first-person request (kind 6) current and full
 * ADS released, receives the configured distance instead. Every distinct
 * caller is counted for diagnosis. The distance handed to the game eases
 * toward its target (hip, normal aim, or the game's own value in full ADS)
 * so clipped geometry does not pop when the view changes; it lands exactly,
 * and never goes below the game's value.
 *
 * A translation, not a rewrite: every check, its order, every reset, counter
 * and expression is retail's. "cpp:a-b" above a function is its line range in
 * the retail file. What differs is in docs/translation/NEAR-HELPER-TRANSLATION.md. */
#include "near_helper.h"
#include "ads_seat.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

/* ======================================================================
 * Build constants, build 25480438. Each line: retail 26568081's value, and
 * how this build's value was established.
 *   build_map  = tools/build_map.py --to-here (the kit's migration records)
 *   image      = bytes read from game-pre1020.dll / r6_reload_exe_20261017.bin
 *                (file offset = RVA) on 2026-11-16
 *   layout     = structure layout, same value in retail; the evidence named
 * ====================================================================== */
/* -- image identity (cpp:168) -- */
#define B_GAME_STAMP        UINT32_C(0x6ab3b43f) /* retail 0x6af322c1; build_map; profile.h; image PE header */
#define B_GAME_SIZE         UINT32_C(0x4744000)  /* retail 0x3382000;  build_map; profile.h; image PE header */
#define B_EXE_STAMP         UINT32_C(0x6ab382e4) /* retail 0x6af2d5e6; build_map; profile.h; image PE header */
#define B_EXE_SIZE          UINT32_C(0x39e8000)  /* retail 0x29fe000;  build_map; profile.h; image PE header */
/* -- code and data places (cpp:31) -- */
#define B_EXE_SETTER        UINT32_C(0x30d960)   /* retail 0x2dd050; native_contract.h camera_near_setter; image: the exe stores it in its API table at +0x68 (exe+0x1cd0b6) */
#define B_EXE_GETTER        UINT32_C(0x30d940)   /* retail 0x2dd030; native_contract.h camera_near_getter; image: stored in the same table at +0x58 (exe+0x1cd09a) */
#define B_GAME_SITE         UINT32_C(0x127bf2c)  /* retail 0x110ddec; native_contract.h camera_near_setup; image */
#define B_GAME_NATIVE_NEAR  UINT32_C(0x23c66d4)  /* retail 0x2245efc; image: the movss at site+7 resolves to it (0x127bf3b + 0x0114a799) */
#define B_GAME_TEXT         UINT32_C(0x1000)     /* retail 0x1000;    image section table: first section starts here */
#define B_GAME_TEXT_END     UINT32_C(0x2111000)  /* retail 0x1f99000 (= start of retail's second section; first is 0x1000 + 0x1f98000);
                                                    image section table: first section 0x1000 + 0x210fa93, second starts at 0x2111000 */
/* -- code bytes (cpp:170-172), all read from this build's images -- */
/* mov rax,[api root]; movss xmm1,[0.05]; mov rcx,[r15]; movaps; mov rdx,[rax+20]; call [rdx+68] */
#define B_HEX_SITE          "488b05d5a30a02f30f100d99a71401498b0f440f29bc2490010000488b5020ff5268"
                                                 /* retail 488b05d536bd01f30f100d01811301...: only the two rip displacements differ; the first
                                                    resolves to game+0x3326308 = B_GAME_API_ROOT, the second to B_GAME_NATIVE_NEAR */
#define B_HEX_NATIVE_NEAR   "cdcc4c3d"           /* retail the same; image at game+0x23c66d4 */
#define B_HEX_SETTER        "f30f114928e9a6201c00" /* retail f30f114928e9c66b1f00: only the jmp displacement differs; image at exe+0x30d960 */
#define B_HEX_GETTER        "f30f104128c3"       /* retail the same; image at exe+0x30d940 */
#define B_NATIVE_BITS       UINT32_C(0x3d4ccccd) /* retail the same (cpp:32): the float the four bytes above are */
/* -- globals in game.dll -- */
#define B_GAME_CONTEXT      UINT32_C(0x346d560)  /* retail 0x2e287d0; build_map; profile.h FP_CAMERA */
#define B_GAME_AVATAR_OWNER UINT32_C(0x3326d20)  /* retail 0x2ce1ee0; build_map; profile.h FP_AVATAR */
#define B_GAME_CONTROLLER   UINT32_C(0x347ce70)  /* retail 0x2e381a8; build_map; profile.h FP_CONTROLLER */
#define B_GAME_API_ROOT     UINT32_C(0x3326308)  /* retail 0x2ce14c8; build_map; profile.h FP_API_ROOT; image (site bytes) */
/* -- the avatar rows inside the avatar owner -- */
#define B_AVATAR_FLAGS      UINT32_C(0x53e880)   /* retail 0x53e888; build_map; profile.h FP_AVATAR_FLAGS */
#define B_AVATAR_STRIDE     UINT32_C(0x1238)     /* retail 0x1240;   build_map; profile.h FP_AVATAR_STRIDE */
/* -- structure layout: the same numbers as retail -- */
#define L_API_CAMERA        0x20   /* image: mov rdx,[rax+0x20] in the site bytes */
#define L_API_NEAR_SETTER   0x68   /* image: call [rdx+0x68] in the site bytes; the exe's table store */
#define L_API_NEAR_GETTER   0x58   /* image: the exe stores the getter 0x10 below the setter (exe+0x1cd0a1 / exe+0x1cd0bd) */
#define L_CAMERA_NEAR       0x28   /* image: setter = movss [rcx+0x28],xmm1; getter = movss xmm0,[rcx+0x28] */
#define L_CONTEXT_CAMERA    0x00   /* snapshot.c, camera_helper.c */
#define L_CONTEXT_HEAD      0x1F8  /* snapshot.c, camera_helper.c (L_CONTEXT_RING) */
#define L_CONTEXT_TAIL      0x1FC
#define L_CONTEXT_RECORDS   0x200
#define L_CONTEXT_RECORD    0xE8
#define L_CONTEXT_IDLE      0x1F00
#define L_AVATAR_COUNT      0x6C   /* snapshot.c */
#define L_CONTROL_AIM       4      /* snapshot.c */
#define L_FLAG_SHOULDER_A   10     /* snapshot.c; bit 4 */
#define L_FLAG_SHOULDER_B   15     /* snapshot.c; bit 32 */
/* ====================================================================== */

#define HIGH UINT64_C(0x800000000000)

/* cpp:22-30 */
static TfpNearHost host;
static _Atomic uintptr_t original;
static _Atomic uint32_t installed, armed, reserved, last_error;
static _Atomic uint64_t seen, forwarded, replaced, ads_forwarded, aim_applied, other_kind, text_low, text_high;
/* std::atomic<float>: the float's bit pattern in a lock-free 32-bit atomic
 * (C has no always-lock-free guarantee to assert for _Atomic float); the
 * float read back is bit for bit the float stored. */
static _Atomic uint32_t hip_meters, aim_meters, last_native, last_applied;
static _Atomic uint32_t ads_sight; /* not in retail (round 20): src/ads_seat.h */
static _Atomic(TfpNearAdsSource) ads_source; /* not in retail (round 20): the pose helper's closer, read in each call */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && sizeof(float) == 4, "Near clip requires lock-free floats");
static uint32_t float_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bits_float(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint64_t slot = 0, slot_image = 0, generation_counter = 0, owned_generation = 0, retired_generation = 0;
static bool possible = false;
/* cpp:33-34 */
static const float MAX_METERS = 1.0f;
static const float GAME_METERS = .05f;
/* cpp:35-37 */
enum { OK = 0, INVALID_CONFIG = 1, BUSY = 2, READ_FAILED = 3, IMAGE_MISMATCH = 4,
       CODE_MISMATCH = 5, API_MISMATCH = 6, OWNER_MISMATCH = 7, PIN_FAILED = 9,
       SLOT_NOT_WRITABLE = 10, CAS_MISMATCH = 11, CAS_FAULT = 12, CLEANUP_PENDING = 13, TOKEN_MISMATCH = 17 };
/* cpp:38-39 */
typedef struct Policy { uint64_t game, exe, context, camera, avatar_owner; uint32_t avatar_index; } Policy;
static Policy policy;
/* cpp:40-42 */
typedef struct SiteRow { _Atomic uint64_t address, calls, owned; _Atomic uint32_t value; } SiteRow;
static SiteRow site_rows[16];
static _Atomic uint32_t site_overflow;

static void control_acquire(void) { host.lock_exclusive(host.user, TFP_NEAR_LOCK_CONTROL); }
static void control_release(void) { host.unlock_exclusive(host.user, TFP_NEAR_LOCK_CONTROL); }
static void policy_acquire(void) { host.lock_exclusive(host.user, TFP_NEAR_LOCK_POLICY); }
static void policy_release(void) { host.unlock_exclusive(host.user, TFP_NEAR_LOCK_POLICY); }
static void policy_acquire_shared(void) { host.lock_shared(host.user, TFP_NEAR_LOCK_POLICY); }
static void policy_release_shared(void) { host.unlock_shared(host.user, TFP_NEAR_LOCK_POLICY); }

/* cpp:43-56 */
static void note_site(uint64_t ret, bool owned, float value) {
  for (unsigned i = 0; i < 16; ++i) {
    SiteRow *row = &site_rows[i];
    uint64_t current = atomic_load_explicit(&row->address, memory_order_acquire);
    if (current == 0) {
      uint64_t expected = 0;
      if (atomic_compare_exchange_strong_explicit(&row->address, &expected, ret, memory_order_acq_rel, memory_order_acquire)) current = ret;
      else current = expected;
    }
    if (current == ret) {
      atomic_fetch_add_explicit(&row->calls, 1, memory_order_relaxed);
      if (owned) atomic_fetch_add_explicit(&row->owned, 1, memory_order_relaxed);
      atomic_store_explicit(&row->value, float_bits(value), memory_order_relaxed);
      return;
    }
  }
  atomic_fetch_add_explicit(&site_overflow, 1, memory_order_relaxed);
}
/* cpp:64-67 */
static bool read(uint64_t at, void *out, size_t length) {
  if (at < 65536 || at >= HIGH || length > 1024 || at + length >= HIGH) return false;
  return host.read(host.user, at, out, length);
}
/* cpp:68 */
#define value(at, out) read((at), &(out), sizeof(out))
/* cpp:69 */
static bool pointer(uint64_t at, uint64_t *out) { return value(at, *out) && *out >= 65536 && *out < HIGH; }
/* cpp:70 */
static void error(uint32_t code) { atomic_store_explicit(&last_error, code, memory_order_release); }
/* cpp:71 */
static uint32_t reject(uint32_t code) { error(code); return 0; }
/* cpp:72 */
static bool meters(float v) { return isfinite(v) && v >= 0 && v <= MAX_METERS; }
/* cpp:73-79. IMAGE_DOS_HEADER is 64 bytes (e_magic +0, e_lfanew +0x3c);
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
/* cpp:80-87 */
static uint8_t hex_digit(char c) { return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10); }
static bool bytes(uint64_t at, const char *hex) {
  uint8_t actual[64] = {0}; const size_t count = strlen(hex) / 2;
  if (!count || count > sizeof actual || !read(at, actual, count)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (actual[i] != (uint8_t)((hex_digit(hex[i*2]) << 4) | hex_digit(hex[i*2+1]))) return false;
  }
  return true;
}
/* cpp:88-92 */
static bool executable(uint64_t at) { return host.executable(host.user, at); }
/* cpp:93-99. Ease state: the last distance handed to the game on the owned
 * camera and when. A gap means the first-person view was not being driven,
 * so the next distance applies at once instead of resuming a stale glide. */
static _Atomic uint32_t shown_meters; /* a float's bits */
static _Atomic uint64_t shown_at;
#define EASE_GAP_US UINT64_C(250000)
#define EASE_STEP_US UINT64_C(50000)
static const float EASE_TAU_US = 45000.0f, EASE_SNAP_METERS = .002f;
/* cpp:100-110 */
static uint64_t now_us(void) { return host.now_us(host.user); }
/* cpp:111 */
static _Atomic uint32_t ads_enter_ms = 180, ads_was; /* round 42: "Aim-in time" (tfp_near_set_ads_enter_ms); the last owned call was full aim */
static void ease_reset(void) {
  atomic_store_explicit(&ads_was, 0, memory_order_relaxed);
  atomic_store_explicit(&shown_at, 0, memory_order_relaxed);
  atomic_store_explicit(&shown_meters, float_bits(0), memory_order_relaxed);
}
/* cpp:112-123 */
static float ease(float target, float tau) {
  const uint64_t now = now_us(), before = atomic_load_explicit(&shown_at, memory_order_relaxed);
  const float from = bits_float(atomic_load_explicit(&shown_meters, memory_order_relaxed));
  float out = target;
  if (now && before && now >= before && now - before <= EASE_GAP_US && isfinite(from) && from > 0) {
    const uint64_t elapsed = now - before < EASE_STEP_US ? now - before : EASE_STEP_US;
    out = from + (target - from) * (1.0f - expf(-(float)elapsed / tau));
    if (!isfinite(out) || fabsf(out - target) < EASE_SNAP_METERS) out = target;
  }
  atomic_store_explicit(&shown_meters, float_bits(out), memory_order_relaxed);
  atomic_store_explicit(&shown_at, now, memory_order_relaxed);
  return out;
}
/* cpp:124-141.
 * LOST: the owned camera, context or avatar row is no longer current.
 * OTHER: not the first-person request (kind 6) or its state is unreadable. */
typedef enum View { VIEW_LOST, VIEW_OTHER, VIEW_ADS, VIEW_SHOULDER, VIEW_HIP } View;
static View observe(const Policy *p) {
  uint64_t ctx = 0, camera = 0, owner = 0, controller = 0; uint32_t count = 0, head = 0, tail = 0, kind = 0;
  uint8_t controls[6] = {0}, flags[24] = {0};
  if (!pointer(p->game + B_GAME_CONTEXT, &ctx) || ctx != p->context || !pointer(ctx + L_CONTEXT_CAMERA, &camera) || camera != p->camera
      || !pointer(p->game + B_GAME_AVATAR_OWNER, &owner) || owner != p->avatar_owner
      || !value(owner + L_AVATAR_COUNT, count) || count == 0 || count > 8 || p->avatar_index >= count) return VIEW_LOST;
  if (!value(ctx + L_CONTEXT_HEAD, head) || !value(ctx + L_CONTEXT_TAIL, tail) || head >= 32 || tail >= 32) return VIEW_OTHER;
  const uint64_t at = head == tail ? ctx + L_CONTEXT_IDLE : ctx + L_CONTEXT_RECORDS + ((tail + 31) % 32) * L_CONTEXT_RECORD;
  if (!value(at, kind) || (kind & 0xffff) != 6) return VIEW_OTHER;
  if (!pointer(p->game + B_GAME_CONTROLLER, &controller) || !read(controller, controls, 6)) return VIEW_OTHER;
  if (controls[L_CONTROL_AIM] != 0) return VIEW_ADS;
  if (!read(owner + B_AVATAR_FLAGS + (uint64_t)p->avatar_index * B_AVATAR_STRIDE, flags, 24)) return VIEW_OTHER;
  /* Current normal aim is the agreeing 82/125 pair, as in the FOV policy. */
  return (flags[L_FLAG_SHOULDER_A] & 4) != 0 && (flags[L_FLAG_SHOULDER_B] & 32) != 0 ? VIEW_SHOULDER : VIEW_HIP;
}
/* cpp:142-156, the game path (not the fixture's): the 8-byte alignment test
 * stays here, the VirtualQuery part is the host's. */
static bool writable_slot(uint64_t at, uint64_t exe) {
  if ((at & 7) != 0) return false;
  return host.writable_slot(host.user, at, exe);
}
/* cpp:157-166 */
static bool cas(uint64_t expected, uint64_t desired, uint64_t *observed) {
  return host.cas(host.user, slot, expected, desired, observed);
}
/* cpp:167-174 */
static bool binary_contract(const TfpNearConfig *c) {
  if (!image(c->game, B_GAME_STAMP, B_GAME_SIZE) || !image(c->exe, B_EXE_STAMP, B_EXE_SIZE)) { reject(IMAGE_MISMATCH); return false; }
  if (!bytes(c->game + B_GAME_SITE, B_HEX_SITE)
      || !bytes(c->game + B_GAME_NATIVE_NEAR, B_HEX_NATIVE_NEAR) || !bytes(c->exe + B_EXE_SETTER, B_HEX_SETTER)
      || !bytes(c->exe + B_EXE_GETTER, B_HEX_GETTER)) { reject(CODE_MISMATCH); return false; }
  return true;
}
/* cpp:175-189, the game path: the slot must hold exe+setter, and the getter
 * must be executable too (cpp:185). */
static bool contract(const TfpNearConfig *c, uintptr_t *setter, uint64_t *address) {
  if (!binary_contract(c)) return false;
  uint64_t root = 0, api = 0, target = 0, getter = 0;
  if (!pointer(c->game + B_GAME_API_ROOT, &root) || !pointer(root + L_API_CAMERA, &api) || !value(api + L_API_NEAR_SETTER, target)
      || !pointer(api + L_API_NEAR_GETTER, &getter) || getter != c->exe + B_EXE_GETTER) { reject(API_MISMATCH); return false; }
  *address = api + L_API_NEAR_SETTER;
  if (!writable_slot(*address, c->exe)) { reject(SLOT_NOT_WRITABLE); return false; }
  if (target != c->exe + B_EXE_SETTER || !executable(getter)) { reject(API_MISMATCH); return false; }
  if (!executable(target)) { reject(API_MISMATCH); return false; }
  *setter = (uintptr_t)target; return true;
}
/* cpp:190, 235: the value that sits in the slot while the helper owns it. */
static uint64_t callback(void) { return host.hook; }
/* cpp:191-195 */
static bool dispatch_owned(const Policy *p) {
  uint64_t root = 0, api = 0, target = 0, getter = 0;
  return pointer(p->game + B_GAME_API_ROOT, &root) && pointer(root + L_API_CAMERA, &api) && api + L_API_NEAR_SETTER == slot
      && pointer(api + L_API_NEAR_GETTER, &getter) && getter == p->exe + B_EXE_GETTER && value(slot, target) && target == callback();
}
/* Retail calls the recorded original directly (cpp:233, 313). */
static void call_original(uintptr_t target, void *camera, float meters_value) {
  if (host.forward) host.forward(host.user, (uint64_t)target, camera, meters_value);
  else ((TfpNearSetter)target)(camera, meters_value);
}
/* cpp:196-234 */
void tfp_near_setter_from(uint64_t ret, void *camera, float native) {
  atomic_fetch_add_explicit(&seen, 1, memory_order_relaxed);
  float applied = native; bool owned = false;
  if (atomic_load_explicit(&armed, memory_order_acquire)) {
    policy_acquire_shared();
    uint32_t bits = 0; memcpy(&bits, &native, 4);
    owned = atomic_load_explicit(&armed, memory_order_acquire) && (uint64_t)(uintptr_t)camera == policy.camera;
    /* A sight's own distance: the first-person request is the weapon's aim camera, so outside full ADS
     * the game still pushes the near distance out to the optic's (live: 0.22 m and 0.35 m for the two
     * zoom levels of one scope, 0.115 m for another weapon) and the viewmodel is cut away at the hip. */
    const bool game_value = bits == B_NATIVE_BITS;
    const bool sight_value = !game_value && isfinite(native) && native > GAME_METERS && native <= MAX_METERS;
    if (owned && (game_value || sight_value) && ret >= atomic_load_explicit(&text_low, memory_order_relaxed)
        && ret < atomic_load_explicit(&text_high, memory_order_relaxed)) {
      const View view = observe(&policy);
      if (view == VIEW_LOST) { atomic_store_explicit(&armed, 0, memory_order_release); error(OWNER_MISMATCH); ease_reset(); }
      else if (view == VIEW_OTHER) { atomic_fetch_add_explicit(&other_kind, 1, memory_order_relaxed); ease_reset(); }
      else {
        /* Full ADS targets the game's own distance, whatever the sight asks for; hip and normal
         * aim target theirs when it is further than the game's 0.05, and 0.05 otherwise. */
        const bool aim = view == VIEW_SHOULDER; float target = native;
        /* Not in retail (round 20): the game's sight, for the pose helper's pull; and in full aim the near clip moved
         * in with the gun (src/ads_seat.h), never under the game's own 0.05. Nothing closer: retail's value. */
        atomic_store_explicit(&ads_sight, sight_value ? float_bits(native) : 0, memory_order_relaxed);
        /* The pose helper's closer is read here, in this call, not handed over by the tick: the near clip has to be in
         * with the gun in the frame the gun comes in (the review's B1). */
        float closer = 0;
        const TfpNearAdsSource source = view == VIEW_ADS ? atomic_load_explicit(&ads_source, memory_order_acquire) : NULL;
        if (source && (!source(&closer) || !meters(closer) || closer > MAX_METERS)) closer = 0;
        /* Round 20c (review 28, B1): the game's own value less the gun's move, no more: the same parts are cut as the
         * game cuts them, however the game's seat moves. */
        if (closer > 0) target = tfp_ads_near(native, closer);
        if (view == VIEW_ADS) atomic_fetch_add_explicit(&ads_forwarded, 1, memory_order_relaxed);
        else {
          const float wanted = bits_float(atomic_load_explicit(aim ? &aim_meters : &hip_meters, memory_order_relaxed));
          target = GAME_METERS;
          if (meters(wanted) && wanted > GAME_METERS) {
            target = wanted; if (aim) atomic_fetch_add_explicit(&aim_applied, 1, memory_order_relaxed);
          }
        }
        /* Round 42 (not in retail): in full aim, an aim-in time below 180 scales the ease (0 is the snap below). */
        const uint32_t enter = atomic_load_explicit(&ads_enter_ms, memory_order_relaxed);
        float eased = ease(target, view == VIEW_ADS && enter > 0 && enter < 180 ? EASE_TAU_US*((float)enter/180.0f) : EASE_TAU_US);
        /* Not in retail (round 42): "Aim-in time" 0, the first call of full aim: full aim's value at once. */
        const bool entered = view == VIEW_ADS && atomic_load_explicit(&ads_was, memory_order_relaxed) == 0;
        atomic_store_explicit(&ads_was, view == VIEW_ADS ? 1u : 0u, memory_order_relaxed);
        if (entered && enter == 0 && eased != target) {
          eased = target; atomic_store_explicit(&shown_meters, float_bits(eased), memory_order_relaxed);
        }
        /* Not in retail (round 20): with the gun drawn closer in full aim, a near clip coming in is taken at once; the
         * ease would leave it behind the sight's rear while the gun comes in. Going out it eases, as retail's. */
        if (closer > 0 && eased > target) { eased = target; atomic_store_explicit(&shown_meters, float_bits(eased), memory_order_relaxed); }
        if (meters(eased) && eased >= GAME_METERS && eased != native) { applied = eased; atomic_fetch_add_explicit(&replaced, 1, memory_order_relaxed); }
      }
      atomic_store_explicit(&last_native, float_bits(native), memory_order_relaxed);
      atomic_store_explicit(&last_applied, float_bits(applied), memory_order_relaxed);
    }
    policy_release_shared();
  }
  note_site(ret, owned, native);
  atomic_fetch_add_explicit(&forwarded, 1, memory_order_relaxed);
  call_original(atomic_load_explicit(&original, memory_order_acquire), camera, applied);
}
/* cpp:236-239 */
static bool pin(uint64_t address) { return host.pin(host.user, address); }
/* cpp:240-251 */
static bool restore_slot(void) {
  if (possible) {
    uint64_t current = 0; if (!value(slot, current)) return false;
    if (current == callback()) {
      if (!writable_slot(slot, slot_image)) return false;
      uint64_t observed = 0; if (!cas(callback(), (uint64_t)atomic_load(&original), &observed)) return false;
    }
    possible = false;
  }
  atomic_store_explicit(&installed, 0, memory_order_release); retired_generation = owned_generation; owned_generation = 0;
  atomic_store_explicit(&reserved, 0, memory_order_release); return true;
}
/* cpp:252-254 */
static bool output_generation(uint64_t *output, uint64_t generation) {
  return host.write(host.user, output, &generation, 8);
}
/* cpp:255-260. Disarm, drain in-flight callbacks, then put back only our own pointer. */
static bool drain_and_restore(void) {
  atomic_store_explicit(&armed, 0, memory_order_release);
  policy_acquire(); policy_release();
  return restore_slot();
}
/* cpp:261-264 */
static uint32_t failed_begin(uint32_t cause, uint64_t *output) {
  if (!drain_and_restore()) { error(CLEANUP_PENDING); return 2; }
  output_generation(output, 0); return reject(cause);
}
/* cpp:265-270 */
static bool config(const TfpNearConfig *input, TfpNearConfig *c) {
  if (!read((uint64_t)(uintptr_t)input, c, sizeof *c) || c->size != sizeof *c || c->abi != 1 || c->reserved != 0 || c->reserved2 != 0
      || c->avatar_index >= 8 || c->context < 65536 || c->camera < 65536 || c->avatar_owner < 65536
      || !meters(c->hip) || !meters(c->aim)) { reject(INVALID_CONFIG); return false; }
  return true;
}
/* cpp:271-300. The control lock is retail's `struct Unlock` guard: released
 * on every return path, by the export below. */
static uint32_t begin_locked(const TfpNearConfig *input, uint64_t *output) {
  if (!output_generation(output, 0)) return reject(INVALID_CONFIG);
  TfpNearConfig c; memset(&c, 0, sizeof c); if (!config(input, &c)) return 0; if (atomic_load(&reserved)) return reject(BUSY);
  Policy next; memset(&next, 0, sizeof next); next.game = c.game; next.exe = c.exe; next.context = c.context; next.camera = c.camera;
  next.avatar_owner = c.avatar_owner; next.avatar_index = c.avatar_index;
  if (observe(&next) == VIEW_LOST) return reject(OWNER_MISMATCH);
  uintptr_t target = 0; uint64_t address = 0; if (!contract(&c, &target, &address)) return 0;
  if (atomic_load(&original) && atomic_load(&original) != target) return reject(API_MISMATCH);
  if (!pin((uint64_t)(uintptr_t)&tfp_near_begin)) return reject(PIN_FAILED);
  if (!pin(c.exe)) return reject(PIN_FAILED);
  atomic_store_explicit(&original, target, memory_order_release); slot = address; slot_image = c.exe; possible = false;
  if (++generation_counter == 0) ++generation_counter;
  if (!output_generation(output, generation_counter)) return reject(INVALID_CONFIG);
  owned_generation = generation_counter; atomic_store_explicit(&reserved, 1, memory_order_release); possible = true;
  policy_acquire();
  policy = next; atomic_store(&hip_meters, float_bits(c.hip)); atomic_store(&aim_meters, float_bits(c.aim));
  atomic_store(&last_native, float_bits(0)); atomic_store(&last_applied, float_bits(0)); ease_reset();
  atomic_store(&seen, 0); atomic_store(&forwarded, 0); atomic_store(&replaced, 0); atomic_store(&ads_forwarded, 0);
  atomic_store(&aim_applied, 0); atomic_store(&other_kind, 0);
  atomic_store_explicit(&text_low, c.game + B_GAME_TEXT, memory_order_relaxed);
  atomic_store_explicit(&text_high, c.game + B_GAME_TEXT_END, memory_order_relaxed);
  policy_release();
  if (!writable_slot(slot, c.exe)) return failed_begin(SLOT_NOT_WRITABLE, output);
  uint64_t observed = 0; if (!cas((uint64_t)target, callback(), &observed)) return failed_begin(CAS_FAULT, output);
  if (observed != (uint64_t)target) { possible = false; return failed_begin(CAS_MISMATCH, output); }
  atomic_store_explicit(&installed, 1, memory_order_release);
  if (!dispatch_owned(&next)) return failed_begin(CAS_MISMATCH, output);
  if (observe(&next) == VIEW_LOST) return failed_begin(OWNER_MISMATCH, output);
  atomic_store_explicit(&armed, 1, memory_order_release); error(OK); return 1;
}
uint32_t tfp_near_begin(const TfpNearConfig *input, uint64_t *output) {
  control_acquire(); const uint32_t result = begin_locked(input, output); control_release(); return result;
}
/* cpp:301-306 */
static uint32_t set_locked(uint64_t generation, float hip, float aim) {
  if (!generation || !atomic_load(&reserved) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  if (!meters(hip) || !meters(aim)) return reject(INVALID_CONFIG);
  atomic_store_explicit(&hip_meters, float_bits(hip), memory_order_relaxed);
  atomic_store_explicit(&aim_meters, float_bits(aim), memory_order_relaxed); error(OK); return 1;
}
uint32_t tfp_near_set(uint64_t generation, float hip, float aim) {
  control_acquire(); const uint32_t result = set_locked(generation, hip, aim); control_release(); return result;
}
/* cpp:307-314. Some callers run only on entry/exit, so the game may not write
 * its own distance again soon: put 0.05 back if the owned camera still holds ours. */
static void restore_distance(void) {
  const float applied = bits_float(atomic_load(&last_applied)), native = bits_float(atomic_load(&last_native));
  float current = 0; uint64_t ctx = 0, camera = 0;
  if (applied <= native || !pointer(policy.game + B_GAME_CONTEXT, &ctx) || ctx != policy.context || !pointer(ctx + L_CONTEXT_CAMERA, &camera)
      || camera != policy.camera || !value(camera + L_CAMERA_NEAR, current) || current != applied) return;
  call_original(atomic_load_explicit(&original, memory_order_acquire), (void *)(uintptr_t)camera, native);
}
/* cpp:315-322 */
static uint32_t end_locked(uint64_t generation) {
  if (!generation) return reject(TOKEN_MISMATCH);
  if (!atomic_load(&reserved)) { if (generation != retired_generation) return reject(TOKEN_MISMATCH); error(OK); return 1; }
  if (generation != owned_generation) return reject(TOKEN_MISMATCH);
  if (!drain_and_restore()) return reject(CLEANUP_PENDING);
  restore_distance(); error(OK); return 1;
}
uint32_t tfp_near_end(uint64_t generation) {
  control_acquire(); const uint32_t result = end_locked(generation); control_release(); return result;
}
/* cpp:323-334. No lock, as retail. */
uint32_t tfp_near_read_sites(TfpNearSite *output, uint32_t capacity) {
  uint32_t count = 0;
  for (unsigned i = 0; i < 16; ++i) {
    SiteRow *row = &site_rows[i];
    const uint64_t address = atomic_load_explicit(&row->address, memory_order_acquire); if (!address) break;
    if (count < capacity) {
      TfpNearSite r; memset(&r, 0, sizeof r); r.return_address = address; r.calls = atomic_load(&row->calls); r.owned_calls = atomic_load(&row->owned);
      r.last_value = bits_float(atomic_load(&row->value));
      if (!host.write(host.user, output + count, &r, sizeof r)) return reject(INVALID_CONFIG);
    }
    ++count;
  }
  return count;
}
/* cpp:335-344 */
static uint32_t read_stats_locked(uint64_t generation, TfpNearStats *output) {
  if (atomic_load(&reserved) ? (!generation || generation != owned_generation) : generation != 0) return reject(TOKEN_MISMATCH);
  TfpNearStats s; memset(&s, 0, sizeof s);
  if (!read((uint64_t)(uintptr_t)output, &s, sizeof s) || s.size != sizeof s || s.abi != 1) return reject(INVALID_CONFIG);
  s.reserved = 0; s.installed = atomic_load(&installed); s.armed = atomic_load(&armed); s.error = atomic_load(&last_error);
  s.seen = atomic_load(&seen); s.forwarded = atomic_load(&forwarded); s.replaced = atomic_load(&replaced); s.ads_forwarded = atomic_load(&ads_forwarded);
  s.aim_applied = atomic_load(&aim_applied); s.other_kind = atomic_load(&other_kind);
  s.last_native = bits_float(atomic_load(&last_native)); s.last_applied = bits_float(atomic_load(&last_applied));
  s.hip = bits_float(atomic_load(&hip_meters)); s.aim = bits_float(atomic_load(&aim_meters));
  return host.write(host.user, output, &s, sizeof s);
}
uint32_t tfp_near_read_stats(uint64_t generation, TfpNearStats *output) {
  control_acquire(); const uint32_t result = read_stats_locked(generation, output); control_release(); return result;
}
/* cpp:345 */
uint32_t tfp_near_error(void) { return atomic_load_explicit(&last_error, memory_order_acquire); }

/* ---- not in retail ---- */
void tfp_near_set_ads_enter_ms(uint32_t ms) { atomic_store_explicit(&ads_enter_ms, ms, memory_order_relaxed); } /* round 42 */
uint32_t tfp_near_ads_enter_ms(void) { return atomic_load_explicit(&ads_enter_ms, memory_order_relaxed); }
void tfp_near_set_ads_source(TfpNearAdsSource source) { atomic_store_explicit(&ads_source, source, memory_order_release); }
TfpNearAdsSource tfp_near_ads_source(void) { return atomic_load_explicit(&ads_source, memory_order_acquire); }
bool tfp_near_ads_probe(float *sight, float *native, float *applied) {
  const bool on = atomic_load_explicit(&armed, memory_order_acquire) != 0;
  if (sight) *sight = on ? bits_float(atomic_load_explicit(&ads_sight, memory_order_relaxed)) : 0;
  if (native) *native = bits_float(atomic_load_explicit(&last_native, memory_order_relaxed));
  if (applied) *applied = bits_float(atomic_load_explicit(&last_applied, memory_order_relaxed));
  return on;
}
void tfp_near_set_host(const TfpNearHost *h) { host = *h; }
void tfp_near_reset_for_tests(void) {
  atomic_store(&original, 0);
  atomic_store(&installed, 0); atomic_store(&armed, 0); atomic_store(&reserved, 0); atomic_store(&last_error, 0);
  atomic_store(&seen, 0); atomic_store(&forwarded, 0); atomic_store(&replaced, 0); atomic_store(&ads_forwarded, 0);
  atomic_store(&aim_applied, 0); atomic_store(&other_kind, 0); atomic_store(&text_low, 0); atomic_store(&text_high, 0);
  atomic_store(&hip_meters, 0); atomic_store(&aim_meters, 0); atomic_store(&last_native, 0); atomic_store(&last_applied, 0);
  atomic_store(&ads_sight, 0);
  slot = 0; slot_image = 0; generation_counter = 0; owned_generation = 0; retired_generation = 0; possible = false;
  memset(&policy, 0, sizeof policy);
  for (unsigned i = 0; i < 16; ++i) {
    atomic_store(&site_rows[i].address, 0); atomic_store(&site_rows[i].calls, 0); atomic_store(&site_rows[i].owned, 0); atomic_store(&site_rows[i].value, 0);
  }
  atomic_store(&site_overflow, 0);
  atomic_store(&shown_meters, 0); atomic_store(&shown_at, 0);
}
