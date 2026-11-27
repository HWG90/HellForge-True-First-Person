/* The retail near-clip helper (native/near_clip/native_ads_near.cpp with
 * native_ads_near.h at 0.5.41-smooth.66) in C11 for build 25480438.
 * Behaviour is retail's; what differs is listed in
 * docs/translation/NEAR-HELPER-TRANSLATION.md. No Windows calls: everything the retail
 * file took from Windows or from its NADSN_FIXTURE seams comes through
 * TfpNearHost. */
#ifndef TFP_NEAR_HELPER_H
#define TFP_NEAR_HELPER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- native_ads_near.h: the structures, same fields, order and sizes ---- */
/* Distances are metres. A value at or below the game's own near distance
 * forwards the game's value unchanged; full native ADS always forwards it. */
typedef struct TfpNearConfig {
  uint32_t size, abi;
  uint64_t game, exe, context, camera, avatar_owner;
  uint32_t avatar_index;
  float hip, aim;
  uint32_t reserved;
  uint64_t reserved2;
} TfpNearConfig;
typedef struct TfpNearStats {
  uint32_t size, abi, installed, armed, error, reserved;
  uint64_t seen, forwarded, replaced, ads_forwarded, aim_applied, other_kind;
  float last_native, last_applied, hip, aim;
} TfpNearStats;
/* Diagnostics: every distinct caller of the slot (return address), any camera. */
typedef struct TfpNearSite {
  uint64_t return_address, calls, owned_calls;
  float last_value;
  uint32_t reserved;
} TfpNearSite;
_Static_assert(sizeof(TfpNearConfig) == 72, "TfpNearConfig ABI");
_Static_assert(sizeof(TfpNearSite) == 32, "TfpNearSite ABI");
_Static_assert(sizeof(TfpNearStats) == 88, "TfpNearStats ABI");

/* Error numbers: retail's (native_ads_near.cpp:35-37), unchanged. */
enum {
  TFP_NEAR_OK = 0, TFP_NEAR_INVALID_CONFIG = 1, TFP_NEAR_BUSY = 2, TFP_NEAR_READ_FAILED = 3,
  TFP_NEAR_IMAGE_MISMATCH = 4, TFP_NEAR_CODE_MISMATCH = 5, TFP_NEAR_API_MISMATCH = 6,
  TFP_NEAR_OWNER_MISMATCH = 7, TFP_NEAR_PIN_FAILED = 9, TFP_NEAR_SLOT_NOT_WRITABLE = 10,
  TFP_NEAR_CAS_MISMATCH = 11, TFP_NEAR_CAS_FAULT = 12, TFP_NEAR_CLEANUP_PENDING = 13,
  TFP_NEAR_TOKEN_MISMATCH = 17
};

/* ---- the host: what retail took from Windows ---- */
typedef void (*TfpNearSetter)(void *camera, float meters);
enum { TFP_NEAR_LOCK_CONTROL = 0, TFP_NEAR_LOCK_POLICY = 1 };
typedef struct TfpNearHost {
  void *user;
  /* The value the helper puts in the dispatch slot and recognises there as its
   * own (retail: the address of setter_hook). In the game: the wrapper that
   * calls tfp_near_setter_from with its own return address. */
  uint64_t hook;
  /* ReadProcessMemory on this process: all `length` bytes copied, or false.
   * The helper has already bounded `at` and `length`. */
  bool (*read)(void *user, uint64_t at, void *out, size_t length);
  /* Microseconds (retail now_us(): QueryPerformanceCounter scaled as
   * (t/f)*1000000 + (t%f)*1000000/f; 0 when the counter cannot be read). */
  uint64_t (*now_us)(void *user);
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
  /* The two SRW locks, by TFP_NEAR_LOCK_*. */
  void (*lock_exclusive)(void *user, int lock);
  void (*unlock_exclusive)(void *user, int lock);
  void (*lock_shared)(void *user, int lock);
  void (*unlock_shared)(void *user, int lock);
  /* Optional. NULL: the original setter is called directly, as retail does.
   * Set: the helper hands the call over (a test whose "original" is not real
   * code at exe+setter). Must do what `original` does exactly once. */
  void (*forward)(void *user, uint64_t original, void *camera, float meters);
} TfpNearHost;

/* Supply the host once, before any other call. The struct is copied. */
void tfp_near_set_host(const TfpNearHost *host);
/* All static state back to its start-up values (the host is kept). */
void tfp_near_reset_for_tests(void);

/* The hook. `return_address` is the caller's return address (retail:
 * _ReturnAddress()); the game build's wrapper supplies it. */
void tfp_near_setter_from(uint64_t return_address, void *camera, float native);

/* ---- exports: retail's NADSN_* with the same signatures and results ---- */
/* 0: output generation 0 / no owned lease; 1: armed; 2: owned cleanup required. */
uint32_t tfp_near_begin(const TfpNearConfig *config, uint64_t *generation);
uint32_t tfp_near_set(uint64_t generation, float hip, float aim);
uint32_t tfp_near_end(uint64_t generation);
/* Generation 0 is valid only while no lease is reserved. */
uint32_t tfp_near_read_stats(uint64_t generation, TfpNearStats *stats);
uint32_t tfp_near_error(void);
/* Copies up to capacity rows; returns the number of distinct callers seen. */
uint32_t tfp_near_read_sites(TfpNearSite *output, uint32_t capacity);
/* Not in retail (round 20, src/ads_seat.h): in full aim the near clip moves in with the gun. `source` is asked in each
 * call of the game's near setter in full aim (the pose helper's tfp_pose_ads_near: `*closer` m the gun is drawn closer
 * than the game seats it, 0 unless the player asks for it); the near clip goes to the game's value less `*closer`
 * (round 20c, review 28 B1: not the sight's least distance), never under the game's own 0.05, and a change toward the
 * eye is taken at once. NULL (the start-up value), false or nothing closer is retail's: full aim forwards the game's own
 * value. Set it before arming; it must not block. tfp_near_ads_probe: the game's sight (its near clip of a sight in the
 * first-person request, m; 0 when none is seen or no lease is armed), the game's last value and what was handed on.
 * false: no lease. */
typedef bool (*TfpNearAdsSource)(float *closer);
void tfp_near_set_ads_source(TfpNearAdsSource source);
TfpNearAdsSource tfp_near_ads_source(void); /* what was set (NULL: none). Reads only. */
bool tfp_near_ads_probe(float *sight, float *native, float *applied);
/* Round 42, "Aim-in time" (locked_view.json ads_transition_ms; not in retail): the shared clock's ADS_ENTER time in ms.
 * 0: the first call of full aim hands the game full aim's value at once, without the 45 ms ease from the hip's. Any
 * other value: retail's ease (180, the start-up value). Any thread; read in each call. */
void tfp_near_set_ads_enter_ms(uint32_t ms);
uint32_t tfp_near_ads_enter_ms(void); /* what was set (180 at start-up). Reads only. */
#endif
