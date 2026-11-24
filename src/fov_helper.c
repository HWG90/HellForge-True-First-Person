/* native/first_person/native_ads_fov.cpp (0.5.41-smooth.66, 748 lines) in C11,
 * for build 25480438. Hip/shoulder and FirdPerson projection policies share
 * one existing API slot. Neither policy changes camera/weapon transforms or
 * saved game settings.
 *
 * A translation, not a rewrite: every check, its order, every reset, counter
 * and expression is retail's. "cpp:a-b" above a function is its line range in
 * the retail file. What differs is in docs/translation/FOV-HELPER-TRANSLATION.md. */
#include "fov_helper.h"
#include "fov_lock.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>

/* ======================================================================
 * Build constants, build 25480438. Each line: retail 26568081's value, and
 * how this build's value was established.
 *   build_map  = tools/build_map.py --to-here (the kit's migration records)
 *   image      = bytes read from game-pre1020.dll / r6_reload_exe_20261017.bin
 *                (file offset = RVA) on 2026-11-16
 *   layout     = structure layout, same value in retail; the named evidence
 *                shows the same field on this build
 * ====================================================================== */
/* -- image identity -- */
#define B_GAME_STAMP        UINT32_C(0x6ab3b43f) /* retail 0x6af322c1; build_map; profile.h FP_GAME_STAMP; image PE header */
#define B_GAME_SIZE         UINT32_C(0x4744000)  /* retail 0x3382000;  build_map; profile.h FP_GAME_SIZE; image PE header */
#define B_EXE_STAMP         UINT32_C(0x6ab382e4) /* retail 0x6af2d5e6; build_map; profile.h FP_EXE_STAMP; image PE header */
#define B_EXE_SIZE          UINT32_C(0x39e8000)  /* retail 0x29fe000;  build_map; profile.h FP_EXE_SIZE; image PE header */
/* -- code addresses -- */
#define B_EXE_SETTER        UINT32_C(0x30d990)   /* retail SETTER 0x2dd080; build_map; native_contract.h camera_fov */
#define B_EXE_GETTER        UINT32_C(0x30d980)   /* retail GETTER 0x2dd070; build_map; native_contract.h camera_fov_getter */
#define B_GAME_CALL         UINT32_C(0x127a11a)  /* retail CALL 0x110bfda; build_map; native_contract.h camera_fov_call */
/* -- code bytes -- */
#define B_HEX_CALL          "ff9280000000"       /* retail the same; image at game+0x127a11a (call [rdx+0x80]) */
#define B_HEX_SETTER        "f30f114934c7415000000000e90f211c00"
                                                 /* 17 bytes as retail; retail jmp displacement 2f6c1f00, here 0f211c00; image at exe+0x30d990 */
#define B_HEX_GETTER        "f30f104134c3"       /* retail the same; image at exe+0x30d980 */
/* -- globals -- */
#define B_GAME_CONTEXT      UINT32_C(0x346d560)  /* retail 0x2e287d0; build_map; profile.h FP_CAMERA */
#define B_GAME_AVATAR_OWNER UINT32_C(0x3326d20)  /* retail 0x2ce1ee0; build_map; profile.h FP_AVATAR */
#define B_GAME_CONTROLLER   UINT32_C(0x347ce70)  /* retail 0x2e381a8; build_map; profile.h FP_CONTROLLER */
#define B_EXE_REGISTRY      UINT32_C(0x1a100f0)  /* retail 0x1ab76f8; build_map; profile.h FP_REGISTRY */
#define B_GAME_STATE        UINT32_C(0x3326340)  /* retail 0x2ce1508; build_map; profile.h FP_STATE; target of the rip load in B_HEX_FIRD_SCALE */
#define B_STATE_FOV         UINT32_C(0xac408)    /* retail 0xac488 (third-person degrees); build_map; image: "f30f5e8008c40a00" divss xmm0,[rax+0xac408] in B_HEX_FIRD_SCALE */
#define B_GAME_API_ROOT     UINT32_C(0x3326308)  /* retail 0x2ce14c8; build_map; profile.h FP_API_ROOT; image: "488b05e1c10a02" right after the call resolves to it */
/* -- avatar owner rows -- */
#define B_AVATAR_FLAGS      UINT32_C(0x53e880)   /* retail 0x53e888; build_map; profile.h FP_AVATAR_FLAGS */
#define B_AVATAR_STRIDE     UINT32_C(0x1238)     /* retail 0x1240;   build_map; profile.h FP_AVATAR_STRIDE */
/* -- FirdPerson input contract (cpp:476-481). The migration records do not
 *    have these six (added on 26568081); each was found in this build's game
 *    image as the only match of retail's bytes, displacements left open, and
 *    the distances between them and to B_GAME_CALL are retail's. -- */
#define B_GAME_FIRD_DISPATCH UINT32_C(0x127961b) /* retail 0x110b4db; image: the call 0xaff bytes before B_GAME_CALL (as retail), to the function at 0x1289180 that holds the next four at retail's distances (+0x89e, +0x8b0, +0x8d1, +0x8e2) */
#define B_HEX_FIRD_DISPATCH  "e860fb0000"        /* retail e8e0fc0000; image at game+0x127961b */
#define B_GAME_FIRD_PUBLISH  UINT32_C(0x128c38e) /* retail 0x111e3ce; image: only match */
#define B_HEX_FIRD_PUBLISH   "f3450f119c24c4000000f3450f115c243c" /* retail the same; image at game+0x128c38e */
#define B_GAME_FIRD_INPUT    UINT32_C(0x1289a1e) /* retail 0x111ba5e; image: 0x12 before B_GAME_FIRD_SELECT as retail */
#define B_HEX_FIRD_INPUT     "f3410f1044243c"    /* retail the same; image at game+0x1289a1e */
#define B_GAME_FIRD_SELECT   UINT32_C(0x1289a30) /* retail 0x111ba70; image: only match */
#define B_HEX_FIRD_SELECT    "448bc14969c0e8000000664283bc300002000004740bffc183e11f3bca75e1eb11" /* retail the same; image at game+0x1289a30 */
#define B_GAME_FIRD_ROW      UINT32_C(0x1289a51) /* retail 0x111ba91; image: only match */
#define B_HEX_FIRD_ROW       "4969c0e8000000f3420f1084303c020000" /* retail the same; image at game+0x1289a51 */
#define B_GAME_FIRD_SCALE    UINT32_C(0x1289a62) /* retail 0x111baa2; image: only match with the three displacements open */
#define B_HEX_FIRD_SCALE     "f30f5905c6dc1301488d55a0488b05cbc80902f30f5e8008c40a00f30f59d8f30f59d0f30f59c8"
                                                 /* 39 bytes as retail; retail displacements 06b51201 / 535abc01 / 88c40a00, here c6dc1301 (-> the float 57.29578) / cbc80902 (-> game+0x3326340) / 08c40a00; image at game+0x1289a62 */
/* -- structure layout shared by both builds -- */
#define L_CONTEXT_CAMERA    0      /* layout; backend.c (camera pointer at context+0) */
#define L_CONTEXT_HEAD      0x1F8  /* layout; snapshot.c, backend.c */
#define L_CONTEXT_TAIL      0x1FC  /* layout; snapshot.c, backend.c */
#define L_CONTEXT_RECORDS   0x200  /* layout; image: "664283bc3000020000 04" cmp word [rax+r14+0x200],4 in B_HEX_FIRD_SELECT */
#define L_CONTEXT_RECORD    0xE8   /* layout; image: "4969c0e8000000" imul r8,r8,0xe8 in B_HEX_FIRD_SELECT */
#define L_CONTEXT_IDLE      0x1F00 /* layout; snapshot.c, backend.c */
#define L_REQUEST_LENS      0x3C   /* layout; image: "f3450f115c243c" movss [r12+0x3c] in B_HEX_FIRD_PUBLISH; "...84303c020000" [rax+r14+0x23c] in B_HEX_FIRD_ROW */
#define L_AVATAR_COUNT      0x6C   /* layout; snapshot.c (avatar_owner+0x6c) */
#define L_AVATAR_ROWS       0x110  /* layout; snapshot.c (avatar_owner+0x110+index*8); descriptor: avatar +8, body +12 */
#define L_DESCRIPTOR_AVATAR 8      /* layout; snapshot.c */
#define L_DESCRIPTOR_BODY   12     /* layout; snapshot.c */
#define L_FLAG_SHOULDER_A   10     /* layout; snapshot.c flags[10]&4 */
#define L_FLAG_SHOULDER_B   15     /* layout; snapshot.c flags[15]&32 */
#define L_FLAG_SEATED       14     /* layout; snapshot.c flags[14]&4 */
#define L_CONTROL_AIM       4      /* layout; snapshot.c controller byte 4 (bytes 0-3 are the mask) */
#define L_CONTROL_GUARD     5      /* layout; snapshot.c controller byte 5 */
#define L_REGISTRY_UNITS    0x88   /* layout; backend.c (units +0x88, capacity +0x98, generations +0xa0) */
#define L_REGISTRY_CAPACITY 0x98   /* layout; backend.c */
#define L_REGISTRY_GENERATIONS 0xA0 /* layout; backend.c */
#define L_API_CAMERA        0x20   /* CameraAPI table at root+0x20: image, "488b5020" mov rdx,[rax+0x20] before the call */
#define L_API_FOV_GETTER    0x78   /* getter slot at api+0x78, the slot before the setter: image, exe+0x30ecf8 stores exe+0x30d980 at exe+0x27c9898 and exe+0x30d990 at exe+0x27c98a0 (also exe+0x1cd0d2) */
#define L_API_FOV_SETTER    0x80   /* setter slot at api+0x80: image, "ff9280000000" call [rdx+0x80] */
/* ====================================================================== */

#define HIGH UINT64_C(0x800000000000)

/* cpp:14-26 */
static TfpFovHost host;
static _Atomic uintptr_t original;
static _Atomic uint32_t installed, armed, reserved, last_error;
static _Atomic uint64_t seen, forwarded, replaced, immediate_applies, binds, ads_forwarded, return_address;
static uint64_t slot = 0, slot_image = 0, generation_counter = 0, owned_generation = 0, retired_generation = 0;
static bool possible = false;
static const float RAD_PER_DEG = 0.01745329251994329577f;
enum { OK = 0, INVALID_CONFIG = 1, BUSY = 2, READ_FAILED = 3, IMAGE_MISMATCH = 4,
       CODE_MISMATCH = 5, API_MISMATCH = 6, OWNER_MISMATCH = 7, UNIT_MISMATCH = 8, PIN_FAILED = 9,
       SLOT_NOT_WRITABLE = 10, CAS_MISMATCH = 11, CAS_FAULT = 12, CLEANUP_PENDING = 13,
       SOURCE_INVALID = 14, TOKEN_MISMATCH = 17, REQUEST_SOURCE_MISMATCH = 18, REQUEST_FIELD_INVALID = 19 };

static void control_acquire(void) { host.lock_exclusive(host.user, TFP_FOV_LOCK_CONTROL); }
static void control_release(void) { host.unlock_exclusive(host.user, TFP_FOV_LOCK_CONTROL); }
static void policy_acquire(void) { host.lock_exclusive(host.user, TFP_FOV_LOCK_POLICY); }
static void policy_release(void) { host.unlock_exclusive(host.user, TFP_FOV_LOCK_POLICY); }

/* cpp:27-43 */
typedef struct RequestField {
  uint64_t request, tag, tail; uint32_t before, last; bool active;
} RequestField;
typedef struct Policy {
  TfpFovConfig c; uint64_t controller, descriptor, registry, units, generations, body_object;
  uint8_t descriptor_bytes[20]; bool shoulder_aim, full_ads, safe;
  bool shoulder_target, shoulder_clock; double shoulder_previous;
  float shoulder_weight;
  float last_native, last_applied, source_degrees;
  bool fird; float captured_radians;
  RequestField input_lens;
  bool lens_ready, lens_ads, lens_blending;
  double lens_start; float lens_from, lens_last;
  bool shared_phase, shared_ads, shared_started, shared_ads_transition;
  uint64_t shared_epoch; float shared_progress, shared_used; double shared_at;
} Policy;
/* Policy{}: the default member initialisers. Everything 0/false except
 * shared_progress = 1. */
static void policy_init(Policy *p) { memset(p, 0, sizeof *p); p->shared_progress = 1; }
static Policy policy = {.shared_progress = 1};
/* RequestField{} */
static void request_field_clear(RequestField *l) { memset(l, 0, sizeof *l); }

/* cpp:44-68. Diagnostics must never make the update thread wait for an
 * in-flight camera setter. Writers are serialized by policy_lock (or a
 * disarmed new Begin). The three display floats are kept as their bit patterns
 * in lock-free 32-bit atomics (C has no always-lock-free guarantee for
 * _Atomic float); the values read back are the values stored. */
typedef struct Display { float native, applied, degrees; } Display;
static Display display_cache; /* control_lock only; never accessed by a setter. */
static _Atomic uint32_t display_sequence;
static _Atomic uint32_t display_native, display_applied, display_degrees;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && sizeof(float) == 4, "FOV diagnostics require lock-free floats");
static uint32_t float_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bits_float(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
/* cpp:53-59 */
static void publish_diagnostics(const Policy *p) {
  atomic_fetch_add_explicit(&display_sequence, 1, memory_order_seq_cst);
  atomic_store_explicit(&display_native, float_bits(p->last_native), memory_order_seq_cst);
  atomic_store_explicit(&display_applied, float_bits(p->last_applied), memory_order_seq_cst);
  atomic_store_explicit(&display_degrees, float_bits(p->source_degrees), memory_order_seq_cst);
  atomic_fetch_add_explicit(&display_sequence, 1, memory_order_seq_cst);
}
/* cpp:60-68 */
static void read_diagnostics(TfpFovStats *s) {
  const uint32_t before = atomic_load_explicit(&display_sequence, memory_order_seq_cst);
  if ((before & 1) == 0) {
    Display next;
    next.native = bits_float(atomic_load_explicit(&display_native, memory_order_seq_cst));
    next.applied = bits_float(atomic_load_explicit(&display_applied, memory_order_seq_cst));
    next.degrees = bits_float(atomic_load_explicit(&display_degrees, memory_order_seq_cst));
    if (before == atomic_load_explicit(&display_sequence, memory_order_seq_cst)) display_cache = next;
  }
  s->last_native = display_cache.native; s->last_applied = display_cache.applied; s->source_degrees = display_cache.degrees;
}
/* cpp:81-92. The host answers what QueryPerformanceFrequency/Counter did;
 * the finite test is retail's. */
static bool seconds(double *out) {
  if (!host.seconds(host.user, out)) return false;
  return isfinite(*out);
}
/* cpp:93-95 */
static void reset_shoulder(Policy *p) {
  p->shoulder_weight = 0; p->shoulder_target = false; p->shoulder_clock = false;
}
/* cpp:96-124 */
static float shoulder_lens(Policy *p, float hip) {
  const float target = p->shoulder_aim ? 1.0f : 0.0f;
  if (p->shared_phase) {
    /* The shared phase supplies the entire normal-aim lens handoff too;
     * stacking the former local tween would put the lens behind the rig. */
    p->shoulder_weight = target; p->shoulder_target = p->shoulder_aim; p->shoulder_clock = false;
    return target == 0 ? hip : 2*atanf(tanf(hip*.5f)/1.05f);
  }
  /* Settled hip is bit-exact TPV, without clock sampling or trigonometry.
   * A settled shoulder also needs no clock until the aim state changes. */
  if (p->shoulder_weight != target) {
    double now = 0;
    if (!seconds(&now)) { reset_shoulder(p); return hip; }
    if (!p->shoulder_clock || p->shoulder_target != p->shoulder_aim) {
      p->shoulder_target = p->shoulder_aim; p->shoulder_clock = true;
    } else {
      const double elapsed = now-p->shoulder_previous;
      /* Rebase after a pause/rollback; never integrate an unbounded gap. */
      if (elapsed >= 0 && elapsed <= .25) {
        const float step = (float)(fmin(elapsed, .12)/.12);
        p->shoulder_weight = p->shoulder_aim ? fminf(1, p->shoulder_weight+step) : fmaxf(0, p->shoulder_weight-step);
      }
    }
    p->shoulder_previous = now;
  }
  if (p->shoulder_weight == 0) return hip;
  const float w = p->shoulder_weight*p->shoulder_weight*(3-2*p->shoulder_weight);
  return 2*atanf(tanf(hip*.5f)/(1+.05f*w));
}
/* cpp:125-128. Opt-in handoff on this already-owned camera. Blend focal
 * magnification, preserving the fresh native lens and exact settled values,
 * including zoom. */
static void reset_lens(Policy *p) { p->lens_ready = false; p->lens_blending = false; }
static float phase_ease(float x) { return x*x*(3-2*x); }
/* std::min(a, b) and std::max(a, b) on floats, as the library defines them
 * (the second argument only when it compares smaller / larger). */
static float min_f(float a, float b) { return b < a ? b : a; }
static float max_f(float a, float b) { return a < b ? b : a; }
/* cpp:129-157 */
static bool shared_lens(Policy *p, float target, bool ads, float *result) {
  if (!p->shared_phase) return false;
  double now = 0;
  if (!seconds(&now) || now < p->shared_at || now-p->shared_at > .25) {
    /* An interrupted Lua producer must not freeze a native sight halfway. */
    p->shared_phase = false; reset_lens(p); *result = target; return true;
  }
  if (p->shared_ads != ads) {
    /* Native ownership is authoritative while the qualified token catches
     * up to a mode change. Never delay ADS using a token for the old mode. */
    p->shared_started = false; p->lens_ready = true; p->lens_ads = ads;
    p->lens_blending = false; p->lens_last = target; *result = target; return true;
  }
  if (!p->shared_started) { p->shared_started = true; p->shared_used = 0; }
  *result = target;
  if (p->lens_ready && p->shared_progress < 1) {
    if (p->shared_progress == p->shared_used) *result = p->lens_last;
    else {
      const float prior = phase_ease(p->shared_used), current = phase_ease(p->shared_progress);
      const float step = min_f(1.0f, max_f(0.0f, (current-prior)/max_f(.000001f, 1-prior)));
      /* Rebase every solve on the actual prior presentation. A new zoom or
       * late native lens solve cannot jump around the shared handoff curve. */
      const float from = 1/tanf(p->lens_last*.5f), to = 1/tanf(target*.5f);
      *result = 2*atanf(1/(from+(to-from)*step));
    }
  }
  p->shared_used = p->shared_progress; p->lens_ready = true; p->lens_ads = ads;
  p->lens_blending = p->shared_ads_transition && p->shared_progress < 1; p->lens_last = *result; return true;
}
/* cpp:158-180 */
static float handoff_lens(Policy *p, float target, bool ads) {
  if (!p->c.reserved || p->fird) return target;
  if (!isfinite(target) || target <= 0 || target >= 3.14159265f) { reset_lens(p); return target; }
  float shared = target; if (shared_lens(p, target, ads, &shared)) return shared;
  if (!p->lens_ready) { p->lens_ready = true; p->lens_ads = ads; p->lens_last = target; return target; }
  double now = 0;
  if (ads != p->lens_ads) {
    if (!seconds(&now)) { reset_lens(p); return target; }
    p->lens_ads = ads; p->lens_from = p->lens_last; p->lens_start = now; p->lens_blending = true;
  }
  float result = target;
  if (p->lens_blending) {
    if (!now && !seconds(&now)) { reset_lens(p); return target; }
    const double elapsed = now-p->lens_start, duration = (double)p->c.reserved/1000;
    if (elapsed < 0 || elapsed >= duration) { p->lens_blending = false; }
    else {
      const float t = (float)(elapsed/duration), w = t*t*(3-2*t);
      const float from = 1/tanf(p->lens_from*.5f), to = 1/tanf(target*.5f);
      result = 2*atanf(1/(from+(to-from)*w));
    }
  }
  p->lens_last = result; return result;
}
/* cpp:181-192. Fault evidence belongs to one explicit owner-data guard.
 * Successful reads take the original RPM path; only an actual failure queries
 * memory mappings. Optional history/code/diagnostic reads outside this scope
 * cannot taint it. The constructor/destructor pair is written out in
 * owner_status. */
typedef struct OwnerReadProbe { struct OwnerReadProbe *previous; bool transport, unsafe; } OwnerReadProbe;
static _Thread_local OwnerReadProbe *owner_read_probe = NULL;
/* cpp:193-210 */
static bool readable_span(uint64_t at, size_t length) { return host.readable_span(host.user, at, length); }
/* cpp:211-236 */
static bool read(uint64_t at, void *out, size_t length) {
  if (at < 65536 || at >= HIGH || length == 0 || length > 1024 || at+length >= HIGH) {
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
/* cpp:237 */
#define value(at, out) read((at), &(out), sizeof(out))
/* cpp:238-242 */
static bool pointer(uint64_t at, uint64_t *out) {
  if (!value(at, *out)) return false;
  if (*out >= 65536 && *out < HIGH) return true;
  if (owner_read_probe) owner_read_probe->unsafe = true;
  return false;
}
/* cpp:243 */
static void error(uint32_t code) { atomic_store_explicit(&last_error, code, memory_order_release); }
/* cpp:244 */
static uint32_t reject(uint32_t code) { error(code); return 0; }
/* cpp:245-251. IMAGE_DOS_HEADER is 64 bytes (e_magic +0, e_lfanew +0x3c);
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
/* cpp:252-259 */
static uint8_t hex_digit(char c) { return (uint8_t)(c <= '9' ? c - '0' : c - 'a' + 10); }
static bool bytes(uint64_t at, const char *hex) {
  uint8_t actual[64] = {0}; const size_t count = strlen(hex)/2;
  if (!count || count > sizeof actual || !read(at, actual, count)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (actual[i] != (uint8_t)((hex_digit(hex[i*2]) << 4) | hex_digit(hex[i*2+1]))) return false;
  }
  return true;
}
/* cpp:260-264 */
static bool executable(uint64_t at) { return host.executable(host.user, at); }
/* cpp:265-293 */
static bool owner(Policy *p, bool capture) {
  const TfpFovConfig *c = &p->c; uint64_t ctx = 0, camera = 0, avatar_owner = 0, controller = 0, descriptor = 0, registry = 0, units = 0, generations = 0, object = 0;
  uint32_t count = 0, capacity = 0; uint8_t d[20] = {0}, flags[24] = {0}, controls[6] = {0}, generation = 0, confirmed = 0; uint64_t confirmed_object = 0;
  const uint32_t index = c->body & 0x3fffff;
  if (!pointer(c->game + B_GAME_CONTEXT, &ctx) || ctx != c->context || !pointer(ctx + L_CONTEXT_CAMERA, &camera) || camera != c->camera
      || !pointer(c->game + B_GAME_AVATAR_OWNER, &avatar_owner) || avatar_owner != c->avatar_owner
      || !pointer(c->game + B_GAME_CONTROLLER, &controller) || !read(controller, controls, 6)
      || !value(avatar_owner + L_AVATAR_COUNT, count) || count == 0 || count > 8 || c->avatar_index >= count
      || !pointer(avatar_owner + L_AVATAR_ROWS + (uint64_t)c->avatar_index*8, &descriptor) || !read(descriptor, d, 20)
      || !read(avatar_owner + B_AVATAR_FLAGS + (uint64_t)c->avatar_index*B_AVATAR_STRIDE, flags, 24)
      || !pointer(c->exe + B_EXE_REGISTRY, &registry) || !value(registry + L_REGISTRY_CAPACITY, capacity) || capacity == 0 || capacity > 0x400000 || index >= capacity
      || !pointer(registry + L_REGISTRY_GENERATIONS, &generations) || !value(generations + index, generation) || generation != (uint8_t)(c->body >> 22)
      || !pointer(registry + L_REGISTRY_UNITS, &units) || !pointer(units + (uint64_t)index*8, &object)
      || !value(generations + index, confirmed) || confirmed != generation
      || !pointer(units + (uint64_t)index*8, &confirmed_object) || confirmed_object != object) return false;
  uint32_t avatar = 0, body = 0, mask = 0; memcpy(&avatar, d + L_DESCRIPTOR_AVATAR, 4); memcpy(&body, d + L_DESCRIPTOR_BODY, 4); memcpy(&mask, controls, 4);
  if (avatar != c->avatar || body != c->body) return false;
  if (capture) {
    p->controller = controller; p->descriptor = descriptor; p->registry = registry; p->units = units; p->generations = generations; p->body_object = object;
    memcpy(p->descriptor_bytes, d, 20);
  } else if (controller != p->controller || descriptor != p->descriptor || registry != p->registry || units != p->units
      || generations != p->generations || object != p->body_object || memcmp(d, p->descriptor_bytes, 20) != 0) return false;
  /* Current normal aim is the agreeing82/125 pair, independent of legacy48.
   * Full native ADS always wins, even while those shoulder flags linger. */
  p->full_ads = controls[L_CONTROL_AIM] != 0;
  p->shoulder_aim = !p->full_ads && (flags[L_FLAG_SHOULDER_A] & 4) != 0 && (flags[L_FLAG_SHOULDER_B] & 32) != 0;
  p->safe = (mask == 0 || mask == 2) && controls[L_CONTROL_GUARD] == 0 && (flags[L_FLAG_SEATED] & 4) == 0;
  return true;
}
/* cpp:294-300. Only an invocation-local owner-data failure may produce
 * retryable code3. OwnerReadProbe's constructor and destructor written out. */
static uint32_t owner_status(Policy *p, bool capture) {
  OwnerReadProbe probe; probe.previous = owner_read_probe; probe.transport = false; probe.unsafe = false;
  owner_read_probe = &probe;
  uint32_t result;
  if (owner(p, capture)) result = OK;
  else result = probe.transport && !probe.unsafe ? READ_FAILED : OWNER_MISMATCH;
  owner_read_probe = probe.previous;
  return result;
}
/* cpp:301-303 */
static bool checked_owner(Policy *p, bool capture, uint32_t *failure) {
  *failure = owner_status(p, capture); return *failure == OK;
}
/* cpp:304-309 */
static bool current_request(const Policy *p, uint64_t *at, uint32_t *r, bool *queued) {
  uint32_t head = 0, tail = 0;
  if (!value(p->c.context + L_CONTEXT_HEAD, head) || !value(p->c.context + L_CONTEXT_TAIL, tail) || head >= 32 || tail >= 32) return false;
  *queued = head != tail; *at = *queued ? p->c.context + L_CONTEXT_RECORDS + ((tail+31)%32)*L_CONTEXT_RECORD : p->c.context + L_CONTEXT_IDLE;
  return read(*at, r, 16) && r[1] == p->c.avatar;
}
/* cpp:310-316. allow_normal is retail's default argument (false). */
static bool request_matches(const Policy *p, bool allow_normal) {
  uint64_t at = 0; uint32_t r[4] = {0}; bool queued = false;
  if (!current_request(p, &at, r, &queued) || at != p->c.request) return false;
  if (p->fird) return (r[0] & 0xffff) == 4;
  if (allow_normal && (r[0] & 0xffff) == 4) return true;
  return queued && (r[0] & 0xffff) == 6 && r[2] == p->c.weapon && r[3] == p->c.body;
}
/* cpp:317-320 */
static bool native_ads_request(const Policy *p) {
  uint64_t at = 0; uint32_t r[4] = {0}; bool queued = false;
  return current_request(p, &at, r, &queued) && queued && (r[0] & 0xffff) == 6;
}
/* cpp:321-323 */
static bool captured_lens(float radians) {
  return isfinite(radians) && radians >= 10*RAD_PER_DEG && radians <= 170*RAD_PER_DEG;
}
/* cpp:324-345. Normal4 input selects the FIRST normal request from the ring
 * head, whereas presentation binds the top request. They can be different
 * owned normal4 rows during a fade; only the actual input source needs
 * normalization. */
static bool fird_request_field(const Policy *p, uint64_t *at, uint64_t *tag, uint64_t *tail_tag) {
  if (!p->fird || !request_matches(p, false)) return false;
  uint32_t head = 0, tail = 0;
  if (!value(p->c.context + L_CONTEXT_HEAD, head) || !value(p->c.context + L_CONTEXT_TAIL, tail) || head >= 32 || tail >= 32) return false;
  uint64_t request = p->c.request;
  if (head != tail) {
    bool found = false;
    for (uint32_t index = head; index != tail; index = (index+1)%32) {
      const uint64_t row = p->c.context + L_CONTEXT_RECORDS + (uint64_t)index*L_CONTEXT_RECORD; uint16_t kind = 0;
      if (!value(row, kind)) return false;
      if (kind == 4) { request = row; found = true; break; }
    }
    if (!found) return false;
  }
  uint64_t header[2] = {0};
  if (!read(request, header, sizeof header) || (uint16_t)header[0] != 4 || (uint32_t)(header[0] >> 32) != p->c.avatar) return false;
  *tag = header[0]; *tail_tag = header[1];
  *at = request + L_REQUEST_LENS; return true;
}
/* cpp:346-354 */
static bool active_normal_request(const Policy *p, uint64_t request, uint64_t tag, uint64_t tail_tag) {
  uint32_t head = 0, tail = 0; uint64_t actual[2] = {0};
  if (!value(p->c.context + L_CONTEXT_HEAD, head) || !value(p->c.context + L_CONTEXT_TAIL, tail) || head >= 32 || tail >= 32) return false;
  bool active = head == tail && request == p->c.context + L_CONTEXT_IDLE;
  for (uint32_t index = head; !active && index != tail; index = (index+1)%32)
    active = request == p->c.context + L_CONTEXT_RECORDS + (uint64_t)index*L_CONTEXT_RECORD;
  return active && read(request, actual, sizeof actual) && actual[0] == tag && actual[1] == tail_tag
      && (uint16_t)actual[0] == 4 && (uint32_t)(actual[0] >> 32) == p->c.avatar;
}
/* cpp:355-361. The 4-byte alignment test stays here; the region question is
 * the host's. */
static bool writable_request_field(uint64_t at) {
  if ((at & 3) != 0 || !host.writable_request_field(host.user, at)) return false;
  return true;
}
/* cpp:362-365 */
static bool finite_request_lens(uint32_t bits) {
  float radians = 0; memcpy(&radians, &bits, 4);
  return isfinite(radians) && radians > 0 && radians < 3.14159265358979323846f;
}
/* cpp:366-371 */
static bool request_cas(uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed) {
  return host.cas32(host.user, at, expected, desired, observed);
}
/* cpp:372-375 */
static bool validate_fird_request_field(const Policy *p) {
  uint64_t at = 0, tag = 0, tail_tag = 0; uint32_t native = 0;
  return fird_request_field(p, &at, &tag, &tail_tag) && value(at, native) && finite_request_lens(native) && writable_request_field(at);
}
/* cpp:376 */
static bool restore_fird_request(Policy *p);
/* cpp:377-410. policy_lock owns the ledger. A new native evaluation can
 * replace our value; keep its freshest finite value for End, and compare
 * before every data write. post_evaluation and owner_failure are retail's
 * default arguments (false, nullptr). */
static bool normalize_fird_request(Policy *p, bool post_evaluation, uint32_t *owner_failure) {
  uint64_t at = 0, tag = 0, tail_tag = 0; uint32_t native = 0, fixed = 0;
  if (!fird_request_field(p, &at, &tag, &tail_tag)) { error(REQUEST_SOURCE_MISMATCH); return false; }
  if (!value(at, native) || !finite_request_lens(native) || !writable_request_field(at)) { error(REQUEST_FIELD_INVALID); return false; }
  memcpy(&fixed, &p->captured_radians, 4);
  RequestField *l = &p->input_lens;
  if (l->active && (l->request != at-L_REQUEST_LENS || l->tag != tag || l->tail != tail_tag)) {
    /* A setter callback follows native evaluation, but an older queue row
     * can have returned early. Its equal value cannot prove freshness or
     * ownership; retire without replay. Immediate Begin/Bind can compare-
     * restore the still-owned old source because they do not evaluate it. */
    if (post_evaluation) request_field_clear(l);
    else if (!restore_fird_request(p)) { error(CLEANUP_PENDING); return false; }
    if (!value(at, native) || !finite_request_lens(native)) { error(REQUEST_FIELD_INVALID); return false; }
  }
  /* The verified callback follows evaluation, but a normal4 row can return
   * early. Equality with our last value is therefore ambiguous: conservatively
   * retain the observed value and never replay an older native lens. Begin/
   * Bind do not evaluate and can preserve their existing restoration value. */
  const uint32_t before = !post_evaluation && l->active && native == l->last ? l->before : native;
  uint64_t checked_at = 0, checked_tag = 0, checked_tail = 0;
  const uint32_t failure = owner_status(p, false);
  if (owner_failure) *owner_failure = failure;
  if (failure != OK || p->full_ads || !p->safe || !fird_request_field(p, &checked_at, &checked_tag, &checked_tail)
      || checked_at != at || checked_tag != tag || checked_tail != tail_tag) {
    error(failure == OK ? OWNER_MISMATCH : failure); return false;
  }
  uint32_t observed = 0;
  if (!request_cas(at, native, fixed, &observed)) { error(CAS_FAULT); return false; }
  if (observed != native) { request_field_clear(l); error(CAS_MISMATCH); return false; }
  l->request = at-L_REQUEST_LENS; l->tag = tag; l->tail = tail_tag; l->before = before; l->last = fixed; l->active = true; return true;
}
/* cpp:411-428. Lost/replaced requests are never replayed. Retain cleanup
 * ownership only when this same live normal4 field still equals our last
 * authored value and a bounded read/CAS cannot yet complete. */
static bool restore_fird_request(Policy *p) {
  RequestField *l = &p->input_lens; if (!l->active) return true;
  const uint64_t at = l->request + L_REQUEST_LENS;
  if (!owner(p, false) || !active_normal_request(p, l->request, l->tag, l->tail)) {
    request_field_clear(l); return true;
  }
  uint32_t current = 0; if (!value(at, current)) return false;
  if (current != l->last || l->before == l->last) { request_field_clear(l); return true; }
  if (!writable_request_field(at)) return false;
  if (!owner(p, false) || !active_normal_request(p, l->request, l->tag, l->tail)) {
    request_field_clear(l); return true;
  }
  uint32_t observed = 0; if (!request_cas(at, l->last, l->before, &observed)) return false;
  request_field_clear(l); return true;
}
/* cpp:429-434 */
static bool source(Policy *p, float *radians) {
  uint64_t settings = 0; float degrees = 0;
  if (!pointer(p->c.game + B_GAME_STATE, &settings) || !value(settings + B_STATE_FOV, degrees)
      || !isfinite(degrees) || degrees < 1 || degrees > 179) return false;
  *radians = degrees*RAD_PER_DEG; p->source_degrees = degrees; return isfinite(*radians);
}
/* cpp:435-449. The 8-byte alignment test stays here; the image question is
 * the host's (retail's non-fixture path). */
static bool writable_slot(uint64_t at, uint64_t exe) {
  if ((at & 7) != 0) return false;
  return host.writable_slot(host.user, at, exe);
}
/* cpp:450-455 */
static bool slot_read(uint64_t at, uint64_t *out) {
  return value(at, *out);
}
/* cpp:456-465 */
static bool cas(uint64_t expected, uint64_t desired, uint64_t *observed) {
  return host.cas(host.user, slot, expected, desired, observed);
}
/* cpp:466-471 */
static bool binary_contract(const TfpFovConfig *c) {
  if (!image(c->game, B_GAME_STAMP, B_GAME_SIZE) || !image(c->exe, B_EXE_STAMP, B_EXE_SIZE)) return reject(IMAGE_MISMATCH) != 0;
  if (!bytes(c->game + B_GAME_CALL, B_HEX_CALL) || !bytes(c->exe + B_EXE_SETTER, B_HEX_SETTER)
      || !bytes(c->exe + B_EXE_GETTER, B_HEX_GETTER)) return reject(CODE_MISMATCH) != 0;
  return true;
}
/* cpp:472-484. Current-build evaluator dispatch precedes the shared CameraAPI
 * setter. Normal4 publishes radians at3C after its input read; the next input
 * pass selects the first kind4 and scales by radians*RAD2DEG/live TP degrees. */
static bool fird_input_contract(const TfpFovConfig *c) {
  if (!bytes(c->game + B_GAME_FIRD_DISPATCH, B_HEX_FIRD_DISPATCH)
      || !bytes(c->game + B_GAME_FIRD_PUBLISH, B_HEX_FIRD_PUBLISH)
      || !bytes(c->game + B_GAME_FIRD_INPUT, B_HEX_FIRD_INPUT)
      || !bytes(c->game + B_GAME_FIRD_SELECT, B_HEX_FIRD_SELECT)
      || !bytes(c->game + B_GAME_FIRD_ROW, B_HEX_FIRD_ROW)
      || !bytes(c->game + B_GAME_FIRD_SCALE, B_HEX_FIRD_SCALE))
    return reject(CODE_MISMATCH) != 0;
  return true;
}
/* cpp:485-499. The slot must hold exe+setter and the getter must be
 * executable (retail's non-fixture path). */
static bool contract(const TfpFovConfig *c, uint64_t *setter, uint64_t *address) {
  if (!binary_contract(c)) return false;
  uint64_t root = 0, api = 0, target = 0, getter = 0;
  if (!pointer(c->game + B_GAME_API_ROOT, &root) || !pointer(root + L_API_CAMERA, &api) || !slot_read(api + L_API_FOV_SETTER, &target)
      || !pointer(api + L_API_FOV_GETTER, &getter) || getter != c->exe + B_EXE_GETTER) return reject(API_MISMATCH) != 0;
  *address = api + L_API_FOV_SETTER;
  if (!writable_slot(*address, c->exe)) return reject(SLOT_NOT_WRITABLE) != 0;
  if (target != c->exe + B_EXE_SETTER || !executable(getter)) return reject(API_MISMATCH) != 0;
  if (!executable(target)) return reject(API_MISMATCH) != 0;
  *setter = target; return true;
}
/* cpp:500, 579. The address of setter_hook in retail; here the host's value. */
static uint64_t callback(void) { return host.hook; }
/* cpp:501-505 */
static bool dispatch_owned(const Policy *p) {
  uint64_t root = 0, api = 0, target = 0, getter = 0;
  return pointer(p->c.game + B_GAME_API_ROOT, &root) && pointer(root + L_API_CAMERA, &api) && api + L_API_FOV_SETTER == slot
      && pointer(api + L_API_FOV_GETTER, &getter) && getter == p->c.exe + B_EXE_GETTER && slot_read(slot, &target) && target == callback();
}
/* The pinned original, never the API slot. An addition for the tester: with
 * host.forward set the call is handed over (see the notes); NULL = retail. */
static void call_original(void *camera, float radians) {
  const uintptr_t target = atomic_load_explicit(&original, memory_order_acquire);
  if (host.forward) host.forward(host.user, (uint64_t)target, camera, radians);
  else ((TfpFovSetter)target)(camera, radians);
}
/* cpp:506-516. Called under policy_lock. */
static bool apply_immediately(Policy *p, uint32_t *failure) {
  if (!checked_owner(p, false, failure) || !request_matches(p, false) || !p->safe || !dispatch_owned(p)) return false;
  if (p->full_ads) { reset_shoulder(p); return true; }
  float radians = p->captured_radians;
  if (!p->fird) { if (!source(p, &radians)) return false; radians = shoulder_lens(p, radians); }
  else if (!normalize_fird_request(p, false, failure)) return false;
  radians = handoff_lens(p, radians, false);
  call_original((void *)(uintptr_t)p->c.camera, radians);
  p->last_applied = radians; atomic_fetch_add_explicit(&immediate_applies, 1, memory_order_relaxed); return true;
}
/* cpp:536-540: the first lambda of setter_hook. */
static bool hook_ads_request(const Policy *p) {
  uint64_t at = 0; uint32_t r[4] = {0}; bool queued = false;
  return current_request(p, &at, r, &queued) && queued && (r[0] & 0xffff) == 6
      && r[2] == p->c.weapon && r[3] == p->c.body;
}
/* cpp:545-549: the second lambda of setter_hook. */
static bool hook_exit_request(const Policy *p) {
  uint64_t at = 0; uint32_t r[4] = {0}; bool queued = false;
  return current_request(p, &at, r, &queued) && ((r[0] & 0xffff) == 4
      || (queued && (r[0] & 0xffff) == 6 && r[2] == p->c.weapon && r[3] == p->c.body));
}
/* cpp:517-578 setter_hook. `caller` replaces _ReturnAddress(). The policy
 * lock is released at the single exit below (retail's Unlock guard). */
/* Beyond retail (tfp_fov_set_lock): one field of view for every frame of the view, whatever the weapon, action or
 * camera. 0 switches it off, and then nothing here runs. The value glides (80 ms) so switching it never pops. */
static volatile float lock_radians, lock_seen_native, lock_shown, lock_aim = TFP_FOV_AIM_ZOOM; static _Atomic uint64_t lock_camera, lock_applied;
static TfpFovLock lock_state;
static volatile double lock_clock; /* milliseconds, as the binder last gave them: only for a host clock that cannot be read */
void tfp_fov_set_lock_aim(float radians, float aim, double now_ms) {
  lock_clock = now_ms; lock_aim = aim > .05f && aim < 3.0f ? aim : aim == TFP_FOV_AIM_LOCKED ? TFP_FOV_AIM_LOCKED : TFP_FOV_AIM_ZOOM;
  lock_radians = radians > .05f && radians < 3.0f ? radians : 0;
}
void tfp_fov_set_lock(float radians, bool zoom_when_aiming, double now_ms) { tfp_fov_set_lock_aim(radians, zoom_when_aiming ? TFP_FOV_AIM_ZOOM : TFP_FOV_AIM_LOCKED, now_ms); }
void tfp_fov_lock_stats(TfpFovLockStats *out) {
  if (!out) return;
  out->applied = atomic_load(&lock_applied); out->game_radians = lock_seen_native; out->shown_radians = lock_shown;
}
/* The arithmetic is src/fov_lock.h. Switched off, the value glides out to the game's as it glided in (three or four
 * frames more of this), and then nothing here changes anything. Another camera than the armed one is never touched.
 * The glide's time is the host's performance counter, read here in every rendered frame (the clock the camera
 * helper's frames run on): the binder's time only moves once a game tick, in steps of about 16 ms, and a glide on it
 * stood still for a frame or two and then jumped. With the lock off and nothing gliding the clock is not read.
 *
 * One value a frame (feel.50: the view pulsed in ADS). In the first-person view the game sets this camera's field of
 * view twice a frame on one thread: the hooked call (the queue's dispatcher, return game+0x127a120) and then, in the
 * post-camera rig update, game+0x127aa90 (return game+0x127aa92), a forwarded call. Both stepped the one glide, the
 * hooked one toward the aim lens and the forwarded one toward the locked value, so in full aim the shown value sat
 * between the two and moved with how each frame's time split between the calls. Now the hooked call decides the
 * frame (`decides`): it steps the glide and keeps what it chose. The forwarded call that follows hands the setter
 * that same value without stepping. A forwarded call with no hooked call before it in this frame (the hooked call
 * not made, or a third call) steps toward the target the hooked call last chose, kept with its time (stale after
 * TFP_FOV_LOCK_STALE_MS): steps toward one target compose, so the frame's time split cannot move the value. Gliding
 * out with the lock off, the target is the forwarded call's own lens when there is one: it is the later write of
 * the frame, what the screen keeps when the lock lets go. */
static volatile double lock_frame_at, lock_tail_at;      /* host milliseconds of the last hooked / forwarded call */
static volatile float lock_frame_value, lock_frame_applied, lock_tail_native;
static volatile uint32_t lock_frame_open, lock_frame_aim; /* open: the hooked call's value not yet repeated */
static bool lock_recent(double at, double tick) { return at > 0 && tick >= at && tick - at <= TFP_FOV_LOCK_STALE_MS; }
static float lock_field(void *camera, float native, float applied, bool aiming, bool decides) {
  const float wanted = lock_radians;
  if ((uint64_t)(uintptr_t)camera != atomic_load_explicit(&lock_camera, memory_order_relaxed)) {
    tfp_fov_lock_reset(&lock_state); lock_shown = 0; lock_frame_open = 0; lock_frame_at = lock_tail_at = 0; return applied;
  }
  lock_seen_native = native; /* the armed camera's only: the "Aim:" line's game field of view (review N3) */
  const float aim_field = lock_aim;
  /* A chosen field of view in aim holds with the lock off too: this frame's aim, or the hooked call's, decides. */
  const bool chosen_aim = aim_field > 0 && (decides ? aiming : lock_frame_aim != 0);
  if (!(wanted > 0) && !chosen_aim && !tfp_fov_lock_active(&lock_state)) { lock_shown = 0; lock_frame_open = 0; lock_tail_at = 0; return applied; }
  double tick = 0;
  if (host.seconds && seconds(&tick)) tick *= 1000; else tick = lock_clock;
  if (!decides) {
    lock_tail_native = native; lock_tail_at = tick;
    if (lock_frame_open) { /* the frame's second call: the hooked call's value, no second step */
      lock_frame_open = 0;
      if (lock_recent(lock_frame_at, tick) && tfp_fov_lock_active(&lock_state)) return lock_frame_value;
    }
  }
  const bool kept = lock_recent(lock_frame_at, tick);
  float goal = applied; bool aim = aiming;
  if (!decides) { aim = kept && lock_frame_aim; if (kept) goal = lock_frame_applied; }
  if (!(wanted > 0) && lock_recent(lock_tail_at, tick)) goal = lock_tail_native; /* gliding out: to what stays on screen */
  float shown = tfp_fov_lock_step_aim(&lock_state, wanted, aim_field, aim, goal, tick);
  const bool active = tfp_fov_lock_active(&lock_state);
  if (!active) shown = applied; /* let go (or never taken): the caller's own value, untouched */
  lock_shown = active ? shown : 0;
  if ((wanted > 0 || chosen_aim) && active) atomic_fetch_add_explicit(&lock_applied, 1, memory_order_relaxed);
  if (decides) { lock_frame_aim = aiming ? 1u : 0u; lock_frame_applied = applied; lock_frame_at = tick; lock_frame_value = shown; lock_frame_open = active ? 1u : 0u; }
  return shown;
}
void tfp_fov_setter_from(uint64_t caller, void *camera, float native) {
  atomic_fetch_add_explicit(&seen, 1, memory_order_relaxed);
  if (!atomic_load_explicit(&armed, memory_order_acquire) || caller != atomic_load_explicit(&return_address, memory_order_relaxed)) {
    atomic_fetch_add_explicit(&forwarded, 1, memory_order_relaxed); call_original(camera, lock_field(camera, native, native, false, false)); return;
  }
  policy_acquire();
  float applied = native; bool diagnostics_changed = false;
  if (atomic_load_explicit(&armed, memory_order_acquire) && (uint64_t)(uintptr_t)camera == policy.c.camera) {
    atomic_store_explicit(&lock_camera, (uint64_t)(uintptr_t)camera, memory_order_relaxed);
    const uint32_t failure = owner_status(&policy, false);
    if (failure != OK) { atomic_store_explicit(&armed, 0, memory_order_release); error(failure); }
    else if (!dispatch_owned(&policy)) { atomic_store_explicit(&armed, 0, memory_order_release); error(API_MISMATCH); }
    else {
      if (policy.fird && (native_ads_request(&policy) || (policy.full_ads && request_matches(&policy, false)))) {
        /* Camera6 can precede the controller's full-ADS bit. The current
         * request itself takes priority over the retained normal4 lease. */
        policy.last_native = policy.last_applied = native;
        request_field_clear(&policy.input_lens);
        atomic_fetch_add_explicit(&ads_forwarded, 1, memory_order_relaxed); diagnostics_changed = true;
      } else if (!policy.fird && policy.c.reserved && policy.full_ads && policy.safe && hook_ads_request(&policy)) {
        reset_shoulder(&policy); applied = handoff_lens(&policy, native, true);
        policy.last_native = native; policy.last_applied = applied;
        atomic_fetch_add_explicit(&ads_forwarded, 1, memory_order_relaxed); diagnostics_changed = true;
      } else if (!policy.fird && policy.c.reserved && policy.safe && policy.lens_ready
          && (policy.lens_ads || policy.lens_blending) && hook_exit_request(&policy)) {
        reset_shoulder(&policy); float target = native;
        uint64_t at = 0; uint32_t r[4] = {0}; bool queued = false;
        if (current_request(&policy, &at, r, &queued) && (r[0] & 0xffff) == 6) source(&policy, &target);
        applied = handoff_lens(&policy, target, false);
        policy.last_native = native; policy.last_applied = applied; diagnostics_changed = true;
      } else if (request_matches(&policy, false) && policy.safe) {
        policy.last_native = native;
        if (policy.full_ads) { reset_shoulder(&policy); atomic_fetch_add_explicit(&ads_forwarded, 1, memory_order_relaxed); }
        else if (policy.fird) {
          if (normalize_fird_request(&policy, true, NULL)) {
            applied = policy.captured_radians; atomic_fetch_add_explicit(&replaced, 1, memory_order_relaxed);
          } else {
            request_field_clear(&policy.input_lens); atomic_store_explicit(&armed, 0, memory_order_release);
          }
        } else if (source(&policy, &applied)) {
          applied = shoulder_lens(&policy, applied); atomic_fetch_add_explicit(&replaced, 1, memory_order_relaxed);
        } else reset_shoulder(&policy);
        applied = handoff_lens(&policy, applied, policy.full_ads);
        policy.last_applied = applied;
        diagnostics_changed = true;
      } else {
        reset_shoulder(&policy); reset_lens(&policy);
        if (policy.fird) request_field_clear(&policy.input_lens);
      }
    }
  }
  applied = lock_field(camera, native, applied, policy.full_ads, true);
  atomic_fetch_add_explicit(&forwarded, 1, memory_order_relaxed); call_original(camera, applied);
  if (diagnostics_changed) publish_diagnostics(&policy);
  policy_release();
}
/* cpp:580-583 */
static bool pin(uint64_t address) { return host.pin(host.user, address); }
/* cpp:584-595 */
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
/* cpp:596-598 */
static bool output_generation(uint64_t *output, uint64_t generation) {
  return host.write(host.user, output, &generation, 8);
}
/* cpp:599-605 */
static uint32_t failed_begin(uint32_t cause, uint64_t *output) {
  atomic_store_explicit(&armed, 0, memory_order_release);
  policy_acquire(); const bool restored = restore_fird_request(&policy); policy_release();
  if (!restored) { error(CLEANUP_PENDING); return 2; }
  if (!restore_slot()) { error(CLEANUP_PENDING); return 2; }
  output_generation(output, 0); return reject(cause);
}
/* cpp:606-611. fird is retail's default argument (false). */
static bool config(const TfpFovConfig *input, TfpFovConfig *c, bool fird) {
  if (!read((uint64_t)(uintptr_t)input, c, sizeof *c) || c->size != sizeof *c || c->abi != 1 || (c->reserved != 0 && (fird || c->reserved < 80 || c->reserved > 500))
      || c->avatar_index >= 8 || !c->avatar || c->avatar == 0xffffffff || !c->body || c->body == 0xffffffff
      || (!fird && (!c->weapon || c->weapon == 0xffffffff)) || c->context < 65536 || c->camera < 65536 || c->request < 65536) return reject(INVALID_CONFIG) != 0;
  return true;
}
/* cpp:612-621. The Unlock guard: the body runs between acquire and release. */
static uint32_t validate_locked(const TfpFovConfig *input) {
  TfpFovConfig c; memset(&c, 0, sizeof c); if (!config(input, &c, false)) return 0; if (atomic_load(&reserved)) return reject(BUSY);
  Policy next; policy_init(&next); next.c = c; uint64_t target = 0; uint64_t address = 0; float radians = 0;
  uint32_t failure = OK;
  if (!checked_owner(&next, true, &failure) || !next.safe || !request_matches(&next, true)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  if (!contract(&c, &target, &address)) return 0;
  if (!source(&next, &radians)) return reject(SOURCE_INVALID);
  if (!checked_owner(&next, false, &failure) || !next.safe || !request_matches(&next, true)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  error(OK); return 1;
}
uint32_t tfp_fov_validate(const TfpFovConfig *input) {
  control_acquire(); const uint32_t result = validate_locked(input); control_release(); return result;
}
/* cpp:622-632 */
static uint32_t validate_fird_locked(const TfpFovConfig *input) {
  TfpFovConfig c; memset(&c, 0, sizeof c); if (!config(input, &c, true)) return 0; if (atomic_load(&reserved)) return reject(BUSY);
  Policy next; policy_init(&next); next.c = c; next.fird = true; uint64_t target = 0; uint64_t address = 0;
  uint32_t failure = OK;
  if (!checked_owner(&next, true, &failure) || !next.safe || next.full_ads || !request_matches(&next, false)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  if (!contract(&c, &target, &address) || !fird_input_contract(&c)) return 0;
  if (!validate_fird_request_field(&next)) return reject(REQUEST_FIELD_INVALID);
  if (!checked_owner(&next, false, &failure) || !next.safe || next.full_ads || !request_matches(&next, false)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  error(OK); return 1;
}
uint32_t tfp_fov_validate_fird(const TfpFovConfig *input) {
  control_acquire(); const uint32_t result = validate_fird_locked(input); control_release(); return result;
}
/* cpp:633-673. Both modules are pinned (retail's non-fixture path): this one
 * by the address of tfp_fov_begin, then the executable. */
static uint32_t begin_policy_locked(const TfpFovConfig *input, uint64_t *output, bool fird, float captured) {
  if (!output_generation(output, 0)) return reject(INVALID_CONFIG);
  TfpFovConfig c; memset(&c, 0, sizeof c); if (!config(input, &c, fird)) return 0; if (atomic_load(&reserved)) return reject(BUSY);
  Policy next; policy_init(&next); next.c = c; uint64_t target = 0; uint64_t address = 0; float radians = 0;
  next.fird = fird; next.captured_radians = captured;
  if (fird) { if (!captured_lens(captured)) return reject(SOURCE_INVALID); next.source_degrees = captured/RAD_PER_DEG; }
  uint32_t failure = OK;
  if (!checked_owner(&next, true, &failure) || !next.safe || (fird && next.full_ads) || !request_matches(&next, false)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  if (!contract(&c, &target, &address)) return 0;
  if (fird && (!fird_input_contract(&c) || !validate_fird_request_field(&next))) return reject(REQUEST_FIELD_INVALID);
  if (!fird && !source(&next, &radians)) return reject(SOURCE_INVALID);
  if (atomic_load(&original) && atomic_load(&original) != (uintptr_t)target) return reject(API_MISMATCH);
  if (!pin((uint64_t)(uintptr_t)&tfp_fov_begin)) return reject(PIN_FAILED);
  if (!pin(c.exe)) return reject(PIN_FAILED);
  atomic_store_explicit(&original, (uintptr_t)target, memory_order_release); slot = address; slot_image = c.exe; possible = false;
  if (++generation_counter == 0) ++generation_counter;
  if (!output_generation(output, generation_counter)) return reject(INVALID_CONFIG);
  owned_generation = generation_counter; atomic_store_explicit(&reserved, 1, memory_order_release); possible = true;
  display_cache.native = 0; display_cache.applied = 0; display_cache.degrees = next.source_degrees; publish_diagnostics(&next);
  if (!writable_slot(slot, c.exe)) return failed_begin(SLOT_NOT_WRITABLE, output);
  uint64_t observed = 0; if (!cas(target, callback(), &observed)) return failed_begin(CAS_FAULT, output);
  if (observed != target) { possible = false; return failed_begin(CAS_MISMATCH, output); }
  atomic_store_explicit(&installed, 1, memory_order_release);
  if (!dispatch_owned(&next)) return failed_begin(CAS_MISMATCH, output);
  policy_acquire();
  policy = next; atomic_store(&seen, 0); atomic_store(&forwarded, 0); atomic_store(&replaced, 0); atomic_store(&immediate_applies, 0); atomic_store(&binds, 0); atomic_store(&ads_forwarded, 0);
  const bool applied = apply_immediately(&policy, &failure);
  const bool stable = applied && checked_owner(&policy, false, &failure) && request_matches(&policy, false) && policy.safe && (!fird || !policy.full_ads) && dispatch_owned(&policy);
  publish_diagnostics(&policy);
  atomic_store_explicit(&return_address, c.game + B_GAME_CALL + 6, memory_order_relaxed);
  if (stable) atomic_store_explicit(&armed, 1, memory_order_release);
  policy_release();
  if (!stable) return failed_begin(failure == OK ? OWNER_MISMATCH : failure, output);
  error(OK); return 1;
}
static uint32_t begin_policy(const TfpFovConfig *input, uint64_t *output, bool fird, float captured) {
  control_acquire(); const uint32_t result = begin_policy_locked(input, output, fird, captured); control_release(); return result;
}
/* cpp:674-676 */
uint32_t tfp_fov_begin(const TfpFovConfig *input, uint64_t *output) {
  return begin_policy(input, output, false, 0);
}
/* cpp:677-679 */
uint32_t tfp_fov_begin_fird(const TfpFovConfig *input, float captured, uint64_t *output) {
  return begin_policy(input, output, true, captured);
}
/* cpp:680-692. Two guards: control, then policy; released in reverse. */
static uint32_t bind_policy_locked(uint64_t request, uint32_t weapon) {
  if (!atomic_load(&armed) || policy.fird) return reject(OWNER_MISMATCH);
  Policy next = policy; next.c.request = request; next.c.weapon = weapon;
  uint32_t failure = OK;
  if (!checked_owner(&next, false, &failure) || !next.safe || !request_matches(&next, false)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  if (!binary_contract(&next.c) || !dispatch_owned(&next)) return reject(API_MISMATCH);
  if (!apply_immediately(&next, &failure) || !checked_owner(&next, false, &failure) || !request_matches(&next, false) || !next.safe || !dispatch_owned(&next)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  policy = next; publish_diagnostics(&policy); atomic_fetch_add_explicit(&binds, 1, memory_order_relaxed); error(OK); return 1;
}
static uint32_t bind_locked(uint64_t generation, uint64_t request, uint32_t weapon) {
  if (!generation || !atomic_load(&reserved) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  if (request < 65536 || !weapon || weapon == 0xffffffff) return reject(INVALID_CONFIG);
  policy_acquire(); const uint32_t result = bind_policy_locked(request, weapon); policy_release(); return result;
}
uint32_t tfp_fov_bind(uint64_t generation, uint64_t request, uint32_t weapon) {
  control_acquire(); const uint32_t result = bind_locked(generation, request, weapon); control_release(); return result;
}
/* cpp:693-710 */
static uint32_t bind_fird_policy_locked(uint64_t request) {
  if (!atomic_load(&armed) || !policy.fird) return reject(OWNER_MISMATCH);
  Policy next = policy; next.c.request = request;
  uint32_t failure = OK;
  if (!checked_owner(&next, false, &failure) || !next.safe || next.full_ads || !request_matches(&next, false)) return reject(failure == OK ? OWNER_MISMATCH : failure);
  if (!binary_contract(&next.c) || !fird_input_contract(&next.c) || !dispatch_owned(&next)) return reject(API_MISMATCH);
  /* Persist the ledger before the immediate setter: a racing owner change
   * after its data CAS still leaves this generation responsible for cleanup. */
  policy = next;
  if (!apply_immediately(&policy, &failure) || !checked_owner(&policy, false, &failure) || !request_matches(&policy, false) || !policy.safe || policy.full_ads || !dispatch_owned(&policy)) {
    atomic_store_explicit(&armed, 0, memory_order_release); return reject(failure == OK ? OWNER_MISMATCH : failure);
  }
  publish_diagnostics(&policy); atomic_fetch_add_explicit(&binds, 1, memory_order_relaxed); error(OK); return 1;
}
static uint32_t bind_fird_locked(uint64_t generation, uint64_t request) {
  if (!generation || !atomic_load(&reserved) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  if (request < 65536) return reject(INVALID_CONFIG);
  policy_acquire(); const uint32_t result = bind_fird_policy_locked(request); policy_release(); return result;
}
uint32_t tfp_fov_bind_fird(uint64_t generation, uint64_t request) {
  control_acquire(); const uint32_t result = bind_fird_locked(generation, request); control_release(); return result;
}
/* cpp:711-728 */
static uint32_t set_transition_policy_locked(uint64_t epoch, float progress, uint32_t ads) {
  if (!atomic_load(&armed) || policy.fird || !policy.c.reserved) return reject(OWNER_MISMATCH);
  const uint32_t failure = owner_status(&policy, false); if (failure != OK) return reject(failure);
  if (!dispatch_owned(&policy)) return reject(API_MISMATCH);
  if (epoch < policy.shared_epoch || (epoch == policy.shared_epoch && (progress < policy.shared_progress || (ads != 0) != policy.shared_ads)))
    return reject(INVALID_CONFIG);
  double now = 0; if (!seconds(&now)) return reject(SOURCE_INVALID);
  if (epoch != policy.shared_epoch || !policy.shared_phase) {
    policy.shared_started = false; policy.shared_used = 0;
    policy.shared_ads_transition = policy.lens_ready && (policy.lens_ads != (ads != 0) || policy.lens_blending);
  }
  policy.shared_phase = true; policy.shared_epoch = epoch; policy.shared_progress = progress; policy.shared_ads = ads != 0; policy.shared_at = now;
  error(OK); return 1;
}
static uint32_t set_transition_locked(uint64_t generation, uint64_t epoch, float progress, uint32_t ads) {
  if (!generation || !atomic_load(&reserved) || generation != owned_generation) return reject(TOKEN_MISMATCH);
  if (!epoch || !isfinite(progress) || progress < 0 || progress > 1 || ads > 1) return reject(INVALID_CONFIG);
  policy_acquire(); const uint32_t result = set_transition_policy_locked(epoch, progress, ads); policy_release(); return result;
}
uint32_t tfp_fov_set_transition(uint64_t generation, uint64_t epoch, float progress, uint32_t ads) {
  control_acquire(); const uint32_t result = set_transition_locked(generation, epoch, progress, ads); control_release(); return result;
}
/* cpp:729-738 */
static uint32_t end_locked(uint64_t generation) {
  if (!generation) return reject(TOKEN_MISMATCH);
  if (!atomic_load(&reserved)) { if (generation != retired_generation) return reject(TOKEN_MISMATCH); error(OK); return 1; }
  if (generation != owned_generation) return reject(TOKEN_MISMATCH);
  atomic_store_explicit(&armed, 0, memory_order_release); policy_acquire();
  const bool restored = restore_fird_request(&policy); policy_release();
  if (!restored) return reject(CLEANUP_PENDING);
  if (!restore_slot()) return reject(CLEANUP_PENDING);
  error(OK); return 1;
}
uint32_t tfp_fov_end(uint64_t generation) {
  control_acquire(); const uint32_t result = end_locked(generation); control_release(); return result;
}
/* cpp:739-747 */
static uint32_t read_stats_locked(uint64_t generation, TfpFovStats *output) {
  if (atomic_load(&reserved) ? (!generation || generation != owned_generation) : generation != 0) return reject(TOKEN_MISMATCH);
  TfpFovStats s; memset(&s, 0, sizeof s); if (!read((uint64_t)(uintptr_t)output, &s, sizeof s) || s.size != sizeof s || s.abi != 1) return reject(INVALID_CONFIG);
  read_diagnostics(&s);
  s.reserved = 0; s.padding = 0; s.installed = atomic_load(&installed); s.armed = atomic_load(&armed); s.error = atomic_load(&last_error);
  s.seen = atomic_load(&seen); s.forwarded = atomic_load(&forwarded); s.replaced = atomic_load(&replaced); s.immediate_applies = atomic_load(&immediate_applies); s.binds = atomic_load(&binds); s.ads_forwarded = atomic_load(&ads_forwarded);
  return host.write(host.user, output, &s, sizeof s);
}
uint32_t tfp_fov_read_stats(uint64_t generation, TfpFovStats *output) {
  control_acquire(); const uint32_t result = read_stats_locked(generation, output); control_release(); return result;
}
/* cpp:748 */
uint32_t tfp_fov_error(void) { return atomic_load_explicit(&last_error, memory_order_acquire); }

/* Not in retail. */
void tfp_fov_set_host(const TfpFovHost *supplied) {
  if (supplied) host = *supplied; else memset(&host, 0, sizeof host);
}
void tfp_fov_reset_for_tests(void) {
  atomic_store(&original, 0);
  atomic_store(&installed, 0); atomic_store(&armed, 0); atomic_store(&reserved, 0); atomic_store(&last_error, 0);
  atomic_store(&seen, 0); atomic_store(&forwarded, 0); atomic_store(&replaced, 0); atomic_store(&immediate_applies, 0);
  atomic_store(&binds, 0); atomic_store(&ads_forwarded, 0); atomic_store(&return_address, 0);
  slot = 0; slot_image = 0; generation_counter = 0; owned_generation = 0; retired_generation = 0; possible = false;
  policy_init(&policy);
  lock_radians = 0; lock_seen_native = 0; lock_shown = 0; lock_aim = TFP_FOV_AIM_ZOOM; lock_clock = 0; tfp_fov_lock_reset(&lock_state);
  lock_frame_at = 0; lock_tail_at = 0; lock_frame_value = 0; lock_frame_applied = 0; lock_tail_native = 0; lock_frame_open = 0; lock_frame_aim = 0;
  atomic_store(&lock_camera, 0); atomic_store(&lock_applied, 0);
  display_cache.native = 0; display_cache.applied = 0; display_cache.degrees = 0;
  atomic_store(&display_sequence, 0); atomic_store(&display_native, 0); atomic_store(&display_applied, 0); atomic_store(&display_degrees, 0);
  owner_read_probe = NULL;
}
