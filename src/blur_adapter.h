/* Adapter and lease of the scoped-blur helper: translation of retail
 * src/first_person/blur.lua at 0.5.41-smooth.66 (commit 1d0d453). The adapter
 * (retail's Adapter table) owns the helper generation and its statistics; the
 * lease (retail's Blur.new object) decides per update when to begin, read
 * health, retire, recheck a weapon without a blur source, and end. Every text
 * is retail's. tests/vectors/blur_adapter_vectors.h holds the retail file's own
 * answers; docs/translation/LENS-ADAPTERS-TRANSLATION.md lists what was dropped and why. */
#ifndef TFP_BLUR_ADAPTER_H
#define TFP_BLUR_ADAPTER_H
#include "blur_helper.h"

#define TFP_BLUR_TEXT 256
/* The chunk name Lua prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_BLUR_LUA_CHUNK "src/first_person/blur.lua"
#define TFP_BLUR_NO_SOURCE 18u                 /* blur.lua:7 */
#define TFP_BLUR_SOURCE_RECHECK_SECONDS .5     /* blur.lua:9 */

/* The helper, reached only through this table (retail: the loaded DLL). In the
 * mod: tfp_blur_validate, _begin, _end, _read_stats, _error. */
typedef struct TfpBlurHelperApi {
  uint32_t (*validate)(const TfpBlurConfig *config);
  uint32_t (*begin)(const TfpBlurConfig *config, uint64_t *generation);
  uint32_t (*end)(uint64_t generation);
  uint32_t (*read_stats)(uint64_t generation, TfpBlurStats *stats);
  uint32_t (*error)(void);
} TfpBlurHelperApi;

/* profile.lua `blur`: abi=1. */
typedef struct TfpBlurProfile { uint32_t abi; } TfpBlurProfile;
#define TFP_BLUR_PROFILE_SHIPPED ((TfpBlurProfile){1})

/* What backend:blur_config returns. A set bit in `missing` is a field that is
 * nil in retail's table, in retail's order: game, exe, context, camera,
 * avatar_owner, request, avatar_index, avatar, body, weapon (bit 0 = game). */
typedef struct TfpBlurCamera {
  uint32_t missing;
  uint64_t game, exe, context, camera, avatar_owner, request;
  uint32_t avatar_index, avatar, body, weapon;
} TfpBlurCamera;
/* The controller's lease as far as this module reads it. `identity` stands for
 * the Lua table's identity (never 0). player_identity 0: nil. */
typedef struct TfpBlurPoseLease {
  uint64_t identity;
  bool has_request; uint64_t request;                 /* lease.request */
  bool has_weapon; uint64_t weapon;                   /* lease.weapon */
  bool has_origin;                                    /* lease.origin */
  uint64_t origin_player_identity;                    /* lease.origin.player_identity */
  bool origin_has_weapon; uint64_t origin_weapon;     /* lease.origin.weapon */
} TfpBlurPoseLease;
typedef struct TfpBlurControllerState { bool pose_offset, running; } TfpBlurControllerState;

typedef struct TfpBlurIo {
  void *user;
  /* backend:blur_config(lease); lease NULL: the preflight form. False: refused
   * (or raised), *why its text or NULL when retail returns none. */
  bool (*blur_config)(void *user, const TfpBlurPoseLease *lease, TfpBlurCamera *out, const char **why);
  void (*controller_result)(void *user, TfpBlurControllerState *out);
  bool (*controller_enabled)(void *user);
  const TfpBlurPoseLease *(*controller_lease)(void *user);   /* NULL: no lease */
  /* The monotonic clock. False: it raised or gave no number. */
  bool (*clock)(void *user, double *seconds);
  void (*log)(void *user, const char *text);                 /* may be NULL */
} TfpBlurIo;

typedef struct TfpBlurStatsView {
  bool has;
  double seen, forwarded, replaced, immediate_applies, last_hash, last_instance, last_native, last_applied;
  bool installed, armed;
  uint32_t error;
} TfpBlurStatsView;
/* An optional native error number (retail: a number or nil; 0 is a number). */
typedef struct TfpBlurCode { bool has; uint32_t value; } TfpBlurCode;

struct TfpBlurAdapter;
typedef struct TfpBlurHandle {
  const struct TfpBlurAdapter *owner;
  bool disarmed;
  uint64_t generation;
  TfpBlurStatsView stats;      /* final statistics once disarmed */
} TfpBlurHandle;
typedef struct TfpBlurAdapter {
  TfpBlurProfile profile;
  const TfpBlurHelperApi *api;
  const TfpBlurIo *io;         /* the clock only */
  TfpBlurHandle *owned;        /* NULL: retail's false */
  TfpBlurCode failure_native_error;
  TfpBlurStats buffer;         /* reused between reads, as retail's stats_buffer */
  bool has_cache; TfpBlurStatsView cache; const TfpBlurHandle *stats_handle;
  bool has_at; double stats_at, stats_due;
  char text[TFP_BLUR_TEXT];
} TfpBlurAdapter;

/* true / false / nil, where retail hands all three on (preflight). */
typedef enum TfpBlurAnswer { TFP_BLUR_FALSE, TFP_BLUR_TRUE, TFP_BLUR_NIL } TfpBlurAnswer;

/* The adapter. A refusal carries its text in *why and, where retail returns
 * the helper's error number as a third result, that number in *code. */
void tfp_blur_adapter_init(TfpBlurAdapter *a, const TfpBlurHelperApi *api, const TfpBlurIo *io, TfpBlurProfile profile);
/* TFP_BLUR_NIL: retail raised (a missing field); the lease's invoke() turns that into nil. */
TfpBlurAnswer tfp_blur_adapter_validate(TfpBlurAdapter *a, const TfpBlurCamera *config, const char **why, TfpBlurCode *code);
bool tfp_blur_adapter_stats(TfpBlurAdapter *a, TfpBlurHandle *handle, TfpBlurStatsView *out, const char **why);
/* `handle` is storage for the new handle; it stays referenced while owned. */
bool tfp_blur_adapter_begin(TfpBlurAdapter *a, const TfpBlurCamera *config, TfpBlurHandle *handle, const char **why, TfpBlurCode *code);
bool tfp_blur_adapter_finish(TfpBlurAdapter *a, TfpBlurHandle *handle, TfpBlurStatsView *stats, const char **why);

typedef enum TfpBlurPhase { TFP_BLUR_LEASE_IDLE, TFP_BLUR_LEASE_WAITING, TFP_BLUR_LEASE_UNAVAILABLE, TFP_BLUR_LEASE_ACTIVE,
                            TFP_BLUR_LEASE_CLEANUP_PENDING } TfpBlurPhase;
typedef enum TfpBlurFailureKind { TFP_BLUR_FAILURE_NONE, TFP_BLUR_FAILURE_TEMPORARY_READ, TFP_BLUR_FAILURE_UNSAFE } TfpBlurFailureKind;
typedef struct TfpBlurLease {
  TfpBlurAdapter helper;
  const TfpBlurIo *io;
  TfpBlurHandle *handle, storage;
  TfpBlurPhase phase;
  TfpBlurStatsView last;
  bool closing, retiring;
  bool has_fault; char fault[TFP_BLUR_TEXT];
  TfpBlurCode failure_native_error;
  TfpBlurFailureKind failure_kind;
  uint64_t bound;                                     /* identity of the bound pose lease; 0: nil */
  bool has_bound_request; uint64_t bound_request;
  bool has_bound_weapon; uint64_t bound_weapon;
  uint64_t bound_player;                              /* 0: nil */
  bool has_source_times; double source_checked_at, source_retry_at;
  char reply[TFP_BLUR_TEXT];
  const char *why;
} TfpBlurLease;
typedef struct TfpBlurResult {
  TfpBlurStatsView stats;
  TfpBlurPhase phase;
  bool cleanup_pending;
  const char *error_message;                          /* NULL: nil */
  TfpBlurCode failure_native_error;
  TfpBlurFailureKind failure_kind;
  bool retiring, retirement_pending;
} TfpBlurResult;

/* The lease. update/stop/shutdown return retail's first result, preflight all
 * three of its possible ones; *why (may be passed NULL) receives the second
 * result: a text valid until the next call, or NULL. update can return true
 * with a text (a retirement whose End is still owed). */
void tfp_blur_lease_init(TfpBlurLease *l, const TfpBlurHelperApi *api, const TfpBlurIo *io, TfpBlurProfile profile);
bool tfp_blur_lease_update(TfpBlurLease *l, const char **why);
bool tfp_blur_lease_stop(TfpBlurLease *l, const char **why);
bool tfp_blur_lease_shutdown(TfpBlurLease *l, const char **why);
TfpBlurAnswer tfp_blur_lease_preflight(TfpBlurLease *l, const char **why);
static inline bool tfp_blur_lease_is_clean(const TfpBlurLease *l) { return l->handle == NULL; }
void tfp_blur_lease_result(const TfpBlurLease *l, TfpBlurResult *out);
/* The status line; returns the length snprintf reports. */
int tfp_blur_lease_status(const TfpBlurLease *l, char *out, size_t size);
#endif
