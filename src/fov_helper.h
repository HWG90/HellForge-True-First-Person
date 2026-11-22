/* The retail field-of-view helper (native/first_person/native_ads_fov.cpp with
 * native_ads_fov.h and native_memory.h at 0.5.41-smooth.66) in C11 for build
 * 25480438. Behaviour is retail's; what differs is listed in
 * docs/translation/FOV-HELPER-TRANSLATION.md. No Windows calls: everything the retail
 * file took from Windows or from its NADSF_FIXTURE seams comes through
 * TfpFovHost. */
#ifndef TFP_FOV_HELPER_H
#define TFP_FOV_HELPER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- native_ads_fov.h: the structures, same fields, order and sizes ---- */
typedef struct TfpFovConfig {
  uint32_t size, abi;
  uint64_t game, exe, context, camera, avatar_owner, request;
  uint32_t avatar_index, avatar, body, weapon;
  uint64_t reserved; /* 0: native ADS passthrough; 80..500: owned ADS handoff milliseconds. */
} TfpFovConfig;
typedef struct TfpFovStats {
  uint32_t size, abi, installed, armed, error, reserved;
  uint64_t seen, forwarded, replaced, immediate_applies, binds, ads_forwarded;
  float last_native, last_applied, source_degrees, padding;
} TfpFovStats;
_Static_assert(sizeof(TfpFovConfig) == 80, "NADSF_Config ABI");
_Static_assert(sizeof(TfpFovStats) == 88, "NADSF_Stats ABI");
_Static_assert(offsetof(TfpFovConfig, request) == 48 && offsetof(TfpFovConfig, avatar_index) == 56 &&
               offsetof(TfpFovConfig, reserved) == 72, "NADSF_Config layout");
_Static_assert(offsetof(TfpFovStats, seen) == 24 && offsetof(TfpFovStats, last_native) == 72, "NADSF_Stats layout");

/* Error codes, retail's numbers (cpp:23-26). 15 and 16 are not used by retail either. */
enum {
  TFP_FOV_OK = 0, TFP_FOV_INVALID_CONFIG = 1, TFP_FOV_BUSY = 2, TFP_FOV_READ_FAILED = 3,
  TFP_FOV_IMAGE_MISMATCH = 4, TFP_FOV_CODE_MISMATCH = 5, TFP_FOV_API_MISMATCH = 6,
  TFP_FOV_OWNER_MISMATCH = 7, TFP_FOV_UNIT_MISMATCH = 8, TFP_FOV_PIN_FAILED = 9,
  TFP_FOV_SLOT_NOT_WRITABLE = 10, TFP_FOV_CAS_MISMATCH = 11, TFP_FOV_CAS_FAULT = 12,
  TFP_FOV_CLEANUP_PENDING = 13, TFP_FOV_SOURCE_INVALID = 14, TFP_FOV_TOKEN_MISMATCH = 17,
  TFP_FOV_REQUEST_SOURCE_MISMATCH = 18, TFP_FOV_REQUEST_FIELD_INVALID = 19
};

/* ---- the host: what retail took from Windows ---- */
typedef void (*TfpFovSetter)(void *camera, float radians);
enum { TFP_FOV_LOCK_CONTROL = 0, TFP_FOV_LOCK_POLICY = 1 };
typedef struct TfpFovHost {
  void *user;
  /* The value the helper puts in the dispatch slot and recognises there as its
   * own (retail: the address of setter_hook). In the game: the wrapper that
   * calls tfp_fov_setter_from with its own return address. */
  uint64_t hook;
  /* ReadProcessMemory on this process: all `length` bytes copied, or false.
   * The helper has already bounded `at` and `length`. */
  bool (*read)(void *user, uint64_t at, void *out, size_t length);
  /* Asked only after a failed read inside an owner proof: is the whole span
   * committed and readable now (retail readable_span: VirtualQuery over at
   * most four regions, none past 0x800000000000). */
  bool (*readable_span)(void *user, uint64_t at, size_t length);
  /* The performance counter in seconds (retail seconds(): ticks / frequency as
   * doubles). False when the frequency is not positive or the counter cannot
   * be read; *out is then left alone. The helper tests the value for finite. */
  bool (*seconds)(void *user, double *out);
  /* `at` (already known 8-aligned) is 8 committed, writable bytes of the image
   * loaded at `exe` (retail writable_slot after its alignment test). */
  bool (*writable_slot)(void *user, uint64_t at, uint64_t exe);
  /* `at` (already known 4-aligned) is 4 committed bytes of one region whose
   * protection is exactly PAGE_READWRITE or PAGE_WRITECOPY (retail
   * writable_request_field after its alignment test). */
  bool (*writable_request_field)(void *user, uint64_t at);
  /* `at` is committed, executable code in a mapped image (retail executable). */
  bool (*executable)(void *user, uint64_t at);
  /* InterlockedCompareExchangePointer on the 8-byte slot at `slot`: store
   * `desired` when it holds `expected`; *observed = the value found. False
   * only when the operation itself faulted (retail's __except). */
  bool (*cas)(void *user, uint64_t slot, uint64_t expected, uint64_t desired, uint64_t *observed);
  /* InterlockedCompareExchange on the 4 bytes at `at`, same convention
   * (retail request_cas). */
  bool (*cas32)(void *user, uint64_t at, uint32_t expected, uint32_t desired, uint32_t *observed);
  /* WriteProcessMemory on this process: all `length` bytes written, or false. */
  bool (*write)(void *user, void *out, const void *data, size_t length);
  /* GetModuleHandleExW(FROM_ADDRESS|PIN) for the module holding `address`. */
  bool (*pin)(void *user, uint64_t address);
  /* The two SRW locks, by TFP_FOV_LOCK_*; exclusive only, as retail. */
  void (*lock_exclusive)(void *user, int lock);
  void (*unlock_exclusive)(void *user, int lock);
  /* Optional. NULL: the original setter is called directly, as retail does.
   * Set: the helper hands the call over (a test whose "original" is not real
   * code at exe+setter). Must do what `original` does exactly once. */
  void (*forward)(void *user, uint64_t original, void *camera, float radians);
} TfpFovHost;

/* Supply the host once, before any other call. The struct is copied. */
void tfp_fov_set_host(const TfpFovHost *host);
/* All static state back to its start-up values (the host is kept). */
void tfp_fov_reset_for_tests(void);

/* The hook. `return_address` is the caller's return address (retail:
 * _ReturnAddress()); the game build's wrapper supplies it. */
void tfp_fov_setter_from(uint64_t return_address, void *camera, float native);

/* ---- exports: retail's NADSF_* with the same signatures and results ---- */
/* Validate permits current normal4 for read-only preflight; Begin requires6. */
uint32_t tfp_fov_validate(const TfpFovConfig *config);
/* 0: output generation0/no owned lease; 1: armed; 2: owned cleanup required. */
uint32_t tfp_fov_begin(const TfpFovConfig *config, uint64_t *generation);
uint32_t tfp_fov_bind(uint64_t generation, uint64_t request, uint32_t weapon);
/* FirdPerson leases the captured display projection on owned normal camera4.
 * It shares the existing setter dispatch slot; native camera6 is forwarded. */
uint32_t tfp_fov_validate_fird(const TfpFovConfig *config);
uint32_t tfp_fov_begin_fird(const TfpFovConfig *config, float captured_radians, uint64_t *generation);
uint32_t tfp_fov_bind_fird(uint64_t generation, uint64_t request);
/* Optional presentation phase. Epochs increase; progress is raw normalized
 * 0..1 and never regresses within an epoch. */
uint32_t tfp_fov_set_transition(uint64_t generation, uint64_t epoch, float progress, uint32_t ads);
uint32_t tfp_fov_end(uint64_t generation);
/* Generation0 is valid only while no lease is reserved. */
uint32_t tfp_fov_read_stats(uint64_t generation, TfpFovStats *stats);
uint32_t tfp_fov_error(void);
/* Beyond retail: the field-of-view lock (docs/design/specs/2026-11-22-locked-view-design.md part 3). `radians` is
 * what the game's own setter takes; 0 switches the lock off. With `zoom_when_aiming` full aim keeps its own lens. */
/* Called every tick. The glide runs on the host's `seconds` clock, read in every rendered frame; `now_ms` (any
 * millisecond clock that keeps going) is only what it falls back on while that clock cannot be read. */
void tfp_fov_set_lock(float radians, bool zoom_when_aiming, double now_ms);
typedef struct TfpFovLockStats { uint64_t applied; float game_radians, shown_radians; } TfpFovLockStats;
void tfp_fov_lock_stats(TfpFovLockStats *out);
#endif
