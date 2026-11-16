/* Adapter and lease of the near-clip helper: translation of retail
 * src/first_person/near_clip.lua at 0.5.41-smooth.66 (commit 1d0d453). The
 * adapter (retail's Adapter table) owns the helper generation; the lease
 * (retail's Near.new object) decides per update when to begin, send the
 * distances, poll health and end. Every text is retail's.
 * tests/vectors/near_adapter_vectors.h holds the retail file's own answers;
 * docs/LENS-ADAPTERS-TRANSLATION.md lists what was dropped and why. */
#ifndef TFP_NEAR_ADAPTER_H
#define TFP_NEAR_ADAPTER_H
#include "near_helper.h"

#define TFP_NEAR_TEXT 256
/* The chunk name Lua prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_NEAR_LUA_CHUNK "src/first_person/near_clip.lua"

/* The helper, reached only through this table (retail: the loaded DLL). In the
 * mod: tfp_near_begin, _set, _end, _read_stats, _error. */
typedef struct TfpNearHelperApi {
  uint32_t (*begin)(const TfpNearConfig *config, uint64_t *generation);
  uint32_t (*set)(uint64_t generation, float hip, float aim);
  uint32_t (*end)(uint64_t generation);
  uint32_t (*read_stats)(uint64_t generation, TfpNearStats *stats);
  uint32_t (*error)(void);
} TfpNearHelperApi;

/* profile.lua `near`: abi=1, pull_sight_distance=true. */
typedef struct TfpNearProfile { uint32_t abi; bool pull_sight_distance; } TfpNearProfile;
#define TFP_NEAR_PROFILE_SHIPPED ((TfpNearProfile){1, true})

/* What this module reads of backend:fov_config(lease). A set bit in `missing`
 * is a field that is nil in retail's table: game, exe, context, camera,
 * avatar_owner, avatar_index (bit 0 = game). */
typedef struct TfpNearCamera {
  uint32_t missing;
  uint64_t game, exe, context, camera, avatar_owner;
  uint32_t avatar_index;
} TfpNearCamera;
/* The controller's lease as far as this module reads it. */
typedef struct TfpNearPoseLease {
  bool has_origin;                     /* lease.origin */
  uint64_t origin_player_identity;     /* lease.origin.player_identity; 0: nil */
} TfpNearPoseLease;
typedef struct TfpNearControllerState { bool pose_offset, running; } TfpNearControllerState;
/* options(): the menu's distances in metres. is_table false: not a table.
 * A field that is absent or not a number has its *_is_number false. */
typedef struct TfpNearOptions { bool is_table, hip_is_number; double hip; bool aim_is_number; double aim; } TfpNearOptions;

typedef struct TfpNearIo {
  void *user;
  /* backend:fov_config(lease). False: refused (or raised), *why its text or
   * NULL when retail returns none. */
  bool (*fov_config)(void *user, const TfpNearPoseLease *lease, TfpNearCamera *out, const char **why);
  void (*controller_result)(void *user, TfpNearControllerState *out);
  bool (*controller_enabled)(void *user);
  const TfpNearPoseLease *(*controller_lease)(void *user);   /* NULL: no lease */
  void (*options)(void *user, TfpNearOptions *out);
  /* The clock. False: it raised; *error is the message. Retail calls it
   * unprotected, so the update ends there (TFP_NEAR_RAISED). */
  bool (*clock)(void *user, double *seconds, const char **error);
  void (*log)(void *user, const char *text);                 /* may be NULL */
} TfpNearIo;

typedef struct TfpNearStatsView {
  bool has;
  double seen, forwarded, replaced, ads_forwarded, aim_applied, other_kind, last_native, last_applied, hip, aim;
  bool installed, armed;
  uint32_t error;
} TfpNearStatsView;

struct TfpNearAdapter;
typedef struct TfpNearHandle {
  const struct TfpNearAdapter *owner;
  bool disarmed;
  uint64_t generation;
  TfpNearStatsView stats;      /* final statistics once disarmed */
} TfpNearHandle;
typedef struct TfpNearAdapter {
  TfpNearProfile profile;
  const TfpNearHelperApi *api;
  TfpNearHandle *owned;        /* NULL: retail's false */
  TfpNearStats buffer;         /* reused between reads, as retail's stats_buffer */
  char text[TFP_NEAR_TEXT];
} TfpNearAdapter;

/* The adapter. A false result carries its text in *why (retail: a returned
 * `false,text` or a raised error, which the lease treats alike). */
void tfp_near_adapter_init(TfpNearAdapter *a, const TfpNearHelperApi *api, TfpNearProfile profile);
bool tfp_near_adapter_stats(TfpNearAdapter *a, TfpNearHandle *handle, TfpNearStatsView *out, const char **why);
/* `handle` is storage for the new handle; it stays referenced while owned. */
bool tfp_near_adapter_begin(TfpNearAdapter *a, const TfpNearCamera *config, double hip, double aim, TfpNearHandle *handle,
                            const char **why);
bool tfp_near_adapter_set(TfpNearAdapter *a, TfpNearHandle *handle, double hip, double aim, const char **why);
bool tfp_near_adapter_finish(TfpNearAdapter *a, TfpNearHandle *handle, TfpNearStatsView *stats, const char **why);

typedef enum TfpNearPhase { TFP_NEAR_LEASE_IDLE, TFP_NEAR_LEASE_ACTIVE, TFP_NEAR_LEASE_CLEANUP_PENDING } TfpNearPhase;
typedef enum TfpNearAnswer { TFP_NEAR_FALSE, TFP_NEAR_TRUE, TFP_NEAR_RAISED } TfpNearAnswer;
typedef struct TfpNearLease {
  TfpNearAdapter helper;
  const TfpNearIo *io;
  TfpNearHandle *handle, storage;
  bool closing;
  bool has_fault; char fault[TFP_NEAR_TEXT];
  TfpNearStatsView last;
  TfpNearPhase phase;
  bool has_hip; double hip; bool has_aim; double aim;
  bool has_retry_at; double retry_at;
  bool has_next_collect; double next_collect, last_collect;
  uint64_t bound_player;                              /* 0: nil */
  char reply[TFP_NEAR_TEXT];
  const char *why;
} TfpNearLease;
typedef struct TfpNearResult {
  TfpNearStatsView stats;
  TfpNearPhase phase;
  bool cleanup_pending;
  const char *error_message;                          /* NULL: nil */
} TfpNearResult;

/* The lease. *why (may be passed NULL) receives retail's second result, or
 * the message of a raised error: a text valid until the next call, or NULL. */
void tfp_near_lease_init(TfpNearLease *l, const TfpNearHelperApi *api, const TfpNearIo *io, TfpNearProfile profile);
TfpNearAnswer tfp_near_lease_update(TfpNearLease *l, const char **why);
bool tfp_near_lease_stop(TfpNearLease *l, const char **why);
bool tfp_near_lease_shutdown(TfpNearLease *l, const char **why);
static inline bool tfp_near_lease_is_clean(const TfpNearLease *l) { return l->handle == NULL; }
void tfp_near_lease_result(const TfpNearLease *l, TfpNearResult *out);
/* The status line; returns the length snprintf reports. */
int tfp_near_lease_status(const TfpNearLease *l, char *out, size_t size);
#endif
