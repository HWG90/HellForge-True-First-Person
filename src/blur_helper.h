/* The retail scoped-blur helper (native/first_person/native_ads_blur.cpp with
 * native_ads_blur.h at 0.5.41-smooth.66) in C11 for build 25480438.
 * Behaviour is retail's; what differs is listed in
 * docs/BLUR-HELPER-TRANSLATION.md. No Windows calls: everything the retail
 * file took from Windows or from its NADSB_FIXTURE seams comes through
 * TfpBlurHost. */
#ifndef TFP_BLUR_HELPER_H
#define TFP_BLUR_HELPER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- native_ads_blur.h: the structures, same fields, order and sizes ---- */
typedef struct TfpBlurConfig {
  uint32_t size, abi;
  uint64_t game, exe, context, camera, avatar_owner, request;
  uint32_t avatar_index, avatar, body, weapon;
  uint64_t reserved;
} TfpBlurConfig;
typedef struct TfpBlurStats {
  uint32_t size, abi, installed, armed, error, reserved;
  uint64_t seen, forwarded, replaced, immediate_applies;
  uint32_t last_hash, last_instance;
  float last_native, last_applied;
  uint64_t reserved_tail;
} TfpBlurStats;
_Static_assert(sizeof(TfpBlurConfig) == 80, "config ABI");
_Static_assert(sizeof(TfpBlurStats) == 80, "stats ABI");
_Static_assert(offsetof(TfpBlurConfig, game) == 8 && offsetof(TfpBlurConfig, request) == 48 &&
               offsetof(TfpBlurConfig, avatar_index) == 56 && offsetof(TfpBlurConfig, weapon) == 68 &&
               offsetof(TfpBlurConfig, reserved) == 72, "config layout");
_Static_assert(offsetof(TfpBlurStats, seen) == 24 && offsetof(TfpBlurStats, immediate_applies) == 48 &&
               offsetof(TfpBlurStats, last_hash) == 56 && offsetof(TfpBlurStats, last_native) == 64 &&
               offsetof(TfpBlurStats, reserved_tail) == 72, "stats layout");

/* Error codes, retail's numbers (cpp:21-24; 15, 16 and 19 do not exist here). */
enum {
  TFP_BLUR_OK = 0, TFP_BLUR_INVALID_CONFIG = 1, TFP_BLUR_BUSY = 2, TFP_BLUR_READ_FAILED = 3,
  TFP_BLUR_IMAGE_MISMATCH = 4, TFP_BLUR_CODE_MISMATCH = 5, TFP_BLUR_API_MISMATCH = 6,
  TFP_BLUR_OWNER_MISMATCH = 7, TFP_BLUR_UNIT_MISMATCH = 8, TFP_BLUR_PIN_FAILED = 9,
  TFP_BLUR_SLOT_NOT_WRITABLE = 10, TFP_BLUR_CAS_MISMATCH = 11, TFP_BLUR_CAS_FAULT = 12,
  TFP_BLUR_CLEANUP_PENDING = 13, TFP_BLUR_SOURCE_INVALID = 14, TFP_BLUR_TOKEN_MISMATCH = 17,
  TFP_BLUR_NO_SOURCE = 18
};

/* ---- the host: what retail took from Windows ---- */
/* The game's parameter setter: provider object, class hash << 32 | entity,
 * parameter name hash, 24-byte variant (u32 type at +0, 2 = float at +8). */
typedef void (*TfpBlurSetter)(void *provider, uint64_t instance, uint32_t hash, const void *variant);
enum { TFP_BLUR_LOCK_CONTROL = 0, TFP_BLUR_LOCK_POLICY = 1 };
typedef struct TfpBlurHost {
  void *user;
  /* The value the helper puts in the dispatch slot and recognises there as its
   * own (retail: the address of setter_hook). In the game: the wrapper that
   * calls tfp_blur_setter_from with its own return address. */
  uint64_t hook;
  /* ReadProcessMemory on this process: all `length` bytes copied, or false.
   * The helper has already bounded `at` and `length`. */
  bool (*read)(void *user, uint64_t at, void *out, size_t length);
  /* Asked only after a failed read inside an owner proof: is the whole span
   * committed and readable now (retail readable_span, VirtualQuery over at
   * most four regions, none past 0x800000000000). */
  bool (*readable_span)(void *user, uint64_t at, size_t length);
  /* `at` (already known 8-aligned) is 8 committed, writable bytes of the image
   * loaded at `exe` (retail writable_slot after its alignment test). */
  bool (*writable_slot)(void *user, uint64_t at, uint64_t exe);
  /* `at` is committed, executable code in a mapped image (retail executable). */
  bool (*executable)(void *user, uint64_t at);
  /* InterlockedCompareExchangePointer on the 8-byte slot at `slot`: store
   * `desired` when it holds `expected`; *observed = the value found. False
   * only when the operation itself faulted (retail's __except). */
  bool (*cas)(void *user, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed);
  /* WriteProcessMemory on this process: all `length` bytes written, or false. */
  bool (*write)(void *user, void *out, const void *data, size_t length);
  /* GetModuleHandleExW(FROM_ADDRESS|PIN) for the module holding `address`. */
  bool (*pin)(void *user, uint64_t address);
  /* The two SRW locks (exclusive only), by TFP_BLUR_LOCK_*. */
  void (*lock_exclusive)(void *user, int lock);
  void (*unlock_exclusive)(void *user, int lock);
  /* Optional. NULL: the original setter is called directly, as retail does.
   * Set: the helper hands the call over (a test whose "original" is not real
   * code at exe+setter). Must do `original`'s work exactly once. */
  void (*forward)(void *user, uint64_t original, void *provider, uint64_t instance, uint32_t hash, const void *variant);
} TfpBlurHost;

/* Supply the host once, before any other call. The struct is copied. */
void tfp_blur_set_host(const TfpBlurHost *host);
/* All static state back to its start-up values (the host is kept). */
void tfp_blur_reset_for_tests(void);

/* The hook. `return_address` is the caller's return address (retail:
 * _ReturnAddress()); the game build's wrapper supplies it. */
void tfp_blur_setter_from(uint64_t return_address, void *provider, uint64_t instance, uint32_t hash, const void *variant);

/* ---- exports: retail's NADSB_* with the same signatures and results ---- */
/* Read-only preflight; accepts the normal request (kind 4). Error 18: the
 * weapon has no eligible optic source. */
uint32_t tfp_blur_validate(const TfpBlurConfig *config);
/* 0: refused, generation 0; 1: armed; 2: a nonzero generation owns cleanup. */
uint32_t tfp_blur_begin(const TfpBlurConfig *config, uint64_t *generation);
uint32_t tfp_blur_end(uint64_t generation);
/* Generation 0 is valid only while no lease is reserved. */
uint32_t tfp_blur_read_stats(uint64_t generation, TfpBlurStats *stats);
uint32_t tfp_blur_error(void);
#endif
