/* The retail camera helper (native/first_person/native_ads_camera.cpp with
 * native_ads_camera.h at 0.5.41-smooth.66) in C11 for build 25480438.
 * Behaviour is retail's; what differs is listed in
 * docs/translation/CAMERA-HELPER-TRANSLATION.md. No Windows calls: everything the retail
 * file took from Windows or from its NADSC_FIXTURE seams comes through
 * TfpCameraHost. */
#ifndef TFP_CAMERA_HELPER_H
#define TFP_CAMERA_HELPER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- native_ads_camera.h: the structures, same fields, order and sizes ---- */
#define TFP_CAMERA_ENTRY_TRACE_ROWS 4
typedef struct TfpCameraEntryTraceRow {
  uint32_t frame, linked, rig_known, padding;
  float argument[3], camera_world[3], camera_local[3], rig_world[3], head_world[3], padding2;
} TfpCameraEntryTraceRow;
typedef struct TfpCameraEntryTraceData {
  uint32_t size, count, exit_holds, hold_exit;
  TfpCameraEntryTraceRow rows[TFP_CAMERA_ENTRY_TRACE_ROWS];
} TfpCameraEntryTraceData;
typedef struct TfpCameraConfig {
  uint32_t size, abi;
  uint64_t game, exe, context, camera, avatar_owner;
  uint32_t avatar_index, avatar, body, camera_unit;
  /* Horizontal view clearance/world-up metres; head damping is root-local. */
  float forward, up, smoothing_ms, blend_ms;
  uint32_t max_gap_ms, reserved;
} TfpCameraConfig;
typedef struct TfpCameraStats {
  uint32_t size, abi, installed, armed, error, reserved;
  uint64_t seen, forwarded, replaced, native6_forwarded, resets;
  float blend, head_distance, last_dt, padding;
} TfpCameraStats;
typedef struct TfpCameraHandoffData {
  uint32_t size, rotation_proven;
  float entry_ms, rotation_ms;
  uint64_t entry_started, entry_frames, entry_rejected, rotation_started, rotation_frames, rotation_rejected, proof_mismatches;
  uint64_t native_mismatches;
  uint32_t native_proven, entry_last_delay, entry_max_delay, padding;
  uint32_t sample_reasons[8];
  uint32_t pending_paths[4];
  float rotation_start_gap, padding2;
} TfpCameraHandoffData;
typedef struct TfpCameraAnchorData {
  uint32_t size, enabled, known, frames;
  float current[3], padding;
} TfpCameraAnchorData;
_Static_assert(sizeof(TfpCameraEntryTraceRow) == 80 &&
               sizeof(TfpCameraEntryTraceData) == 16 + 80 * TFP_CAMERA_ENTRY_TRACE_ROWS, "entry trace ABI");
_Static_assert(sizeof(TfpCameraConfig) == 88, "config ABI");
_Static_assert(sizeof(TfpCameraStats) == 80, "stats ABI");
_Static_assert(sizeof(TfpCameraHandoffData) == 152, "handoff data ABI");
_Static_assert(sizeof(TfpCameraAnchorData) == 32, "anchor data layout");

/* Error codes, retail's numbers (cpp:24-27). */
enum {
  TFP_CAMERA_OK = 0, TFP_CAMERA_INVALID_CONFIG = 1, TFP_CAMERA_BUSY = 2, TFP_CAMERA_READ_FAILED = 3,
  TFP_CAMERA_IMAGE_MISMATCH = 4, TFP_CAMERA_CODE_MISMATCH = 5, TFP_CAMERA_API_MISMATCH = 6,
  TFP_CAMERA_OWNER_MISMATCH = 7, TFP_CAMERA_UNIT_MISMATCH = 8, TFP_CAMERA_PIN_FAILED = 9,
  TFP_CAMERA_SLOT_NOT_WRITABLE = 10, TFP_CAMERA_CAS_MISMATCH = 11, TFP_CAMERA_CAS_FAULT = 12,
  TFP_CAMERA_CLEANUP_PENDING = 13, TFP_CAMERA_POSE_INVALID = 14, TFP_CAMERA_SPACE_INVALID = 15,
  TFP_CAMERA_GRAPH_MISMATCH = 16, TFP_CAMERA_TOKEN_MISMATCH = 17, TFP_CAMERA_ON_FOOT_REQUIRED = 18
};

/* ---- the host: what retail took from Windows ---- */
typedef void (*TfpCameraSetter)(void *camera, uint32_t unit, const float *native);
enum { TFP_CAMERA_LOCK_CONTROL = 0, TFP_CAMERA_LOCK_POLICY = 1 };
typedef struct TfpCameraHost {
  void *user;
  /* The value the helper puts in the dispatch slot and recognises there as its
   * own (retail: the address of setter_hook). In the game: the wrapper that
   * calls tfp_camera_setter_from with its own return address. */
  uint64_t hook;
  /* ReadProcessMemory on this process: all `length` bytes copied, or false.
   * The helper has already bounded `at` and `length`. */
  bool (*read)(void *user, uint64_t at, void *out, size_t length);
  /* Asked only after a failed read inside an owner proof: is the whole span
   * committed and readable now (retail readable_span, VirtualQuery over at
   * most four regions, none past 0x800000000000). */
  bool (*readable_span)(void *user, uint64_t at, size_t length);
  /* Milliseconds as a double (retail now(): QueryPerformanceCounter). */
  double (*now)(void *user);
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
  /* The two SRW locks, by TFP_CAMERA_LOCK_*. */
  void (*lock_exclusive)(void *user, int lock);
  void (*unlock_exclusive)(void *user, int lock);
  void (*lock_shared)(void *user, int lock);
  void (*unlock_shared)(void *user, int lock);
  /* Optional. NULL: the original setter is called directly, as retail does.
   * Set: the helper hands the call over (a test whose "original" is not real
   * code at exe+setter). Must call `original`'s behaviour exactly once. */
  void (*forward)(void *user, uint64_t original, void *camera, uint32_t unit, const float *native);
} TfpCameraHost;

/* Supply the host once, before any other call. The struct is copied. */
void tfp_camera_set_host(const TfpCameraHost *host);
/* All static state back to its start-up values (the host is kept). */
void tfp_camera_reset_for_tests(void);

/* The hook. `return_address` is the caller's return address (retail:
 * _ReturnAddress()); the game build's wrapper supplies it. */
void tfp_camera_setter_from(uint64_t return_address, void *camera, uint32_t unit, const float *native);

/* ---- exports: retail's NADSC_* with the same signatures and results ---- */
uint32_t tfp_camera_validate(const TfpCameraConfig *config);
/* 0: refused, generation 0; 1: armed; 2: the slot could not be restored. */
uint32_t tfp_camera_begin(const TfpCameraConfig *config, uint64_t *generation);
uint32_t tfp_camera_end(uint64_t generation);
uint32_t tfp_camera_read_stats(uint64_t generation, TfpCameraStats *stats);
uint32_t tfp_camera_error(void);
uint32_t tfp_camera_set_presentation(uint64_t generation);
uint32_t tfp_camera_set_handoff(uint64_t generation, float entry_ms, float rotation_ms);
uint32_t tfp_camera_handoff_stats(uint64_t generation, TfpCameraHandoffData *out);
uint32_t tfp_camera_set_cut_hold(uint64_t generation, uint32_t mode);
uint32_t tfp_camera_entry_trace(TfpCameraEntryTraceData *out);
uint32_t tfp_camera_set_weapon_key(uint64_t generation, uint32_t key);
uint32_t tfp_camera_anchor_stats(TfpCameraAnchorData *out);
#endif
