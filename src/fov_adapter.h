/* Adapter and lease of the field-of-view helper: translation of retail
 * src/first_person/fov.lua at 0.5.41-smooth.66 (commit 1d0d453). The adapter
 * (retail's Adapter table) owns the helper generation and its statistics; the
 * lease (retail's Fov.new object) decides per update when to begin, bind,
 * publish the presentation phase, read health and end. Every text is retail's.
 * tests/vectors/fov_adapter_vectors.h holds the retail file's own answers;
 * docs/translation/LENS-ADAPTERS-TRANSLATION.md lists what was dropped and why. */
#ifndef TFP_FOV_ADAPTER_H
#define TFP_FOV_ADAPTER_H
#include "fov_helper.h"

#define TFP_FOV_TEXT 256
/* The chunk name Lua prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_FOV_LUA_CHUNK "src/first_person/fov.lua"

/* The helper, reached only through this table (retail: the loaded DLL). In the
 * mod: tfp_fov_validate, _begin, _bind, _set_transition, _end, _read_stats, _error. */
typedef struct TfpFovHelperApi {
  uint32_t (*validate)(const TfpFovConfig *config);
  uint32_t (*begin)(const TfpFovConfig *config, uint64_t *generation);
  uint32_t (*bind)(uint64_t generation, uint64_t request, uint32_t weapon);
  uint32_t (*set_transition)(uint64_t generation, uint64_t epoch, float progress, uint32_t ads);
  uint32_t (*end)(uint64_t generation);
  uint32_t (*read_stats)(uint64_t generation, TfpFovStats *stats);
  uint32_t (*error)(void);
} TfpFovHelperApi;

/* profile.lua `fov`: abi=1, ads_blend_ms=180 (goes into NADSF_Config.reserved). */
typedef struct TfpFovProfile { uint32_t abi, ads_blend_ms; } TfpFovProfile;
#define TFP_FOV_PROFILE_SHIPPED ((TfpFovProfile){1, 180})

/* What backend:fov_config returns. A set bit in `missing` is a field that is
 * nil in retail's table, in retail's order: game, exe, context, camera,
 * avatar_owner, request, avatar_index, avatar, body, weapon (bit 0 = game). */
typedef struct TfpFovCamera {
  uint32_t missing;
  uint64_t game, exe, context, camera, avatar_owner, request;
  uint32_t avatar_index, avatar, body, weapon;
} TfpFovCamera;
enum { TFP_FOV_MISSING_REQUEST = 1u << 5, TFP_FOV_MISSING_WEAPON = 1u << 9 };

/* The controller's lease as far as this module reads it. `identity` stands for
 * the Lua table's identity (never 0). player_identity 0: nil. */
typedef struct TfpFovPoseLease {
  uint64_t identity;
  bool has_request; uint64_t request;                 /* lease.request */
  bool has_weapon; uint64_t weapon;                   /* lease.weapon */
  bool has_origin;                                    /* lease.origin */
  uint64_t origin_player_identity;                    /* lease.origin.player_identity */
  bool origin_has_weapon; uint64_t origin_weapon;     /* lease.origin.weapon */
} TfpFovPoseLease;
/* controller:result(): mode=='pose_offset' and phase=='running'. */
typedef struct TfpFovControllerState { bool pose_offset, running; } TfpFovControllerState;
/* backend.presentation_transition(): nil, a table, or anything else. */
enum { TFP_FOV_PACKET_NONE, TFP_FOV_PACKET_TABLE, TFP_FOV_PACKET_OTHER };
enum { TFP_FOV_ADS_FALSE, TFP_FOV_ADS_TRUE, TFP_FOV_ADS_NOT_BOOLEAN };
typedef struct TfpFovPacket {
  int kind;
  bool epoch_is_number; double epoch;
  bool progress_is_number; double progress;
  int ads;
} TfpFovPacket;

typedef struct TfpFovIo {
  void *user;
  /* backend:fov_config(lease); lease NULL: the preflight form. False: refused
   * (or raised), *why its text or NULL when retail returns none. */
  bool (*fov_config)(void *user, const TfpFovPoseLease *lease, TfpFovCamera *out, const char **why);
  /* backend.presentation_transition; the pointer may be NULL (retail: not a
   * function). A raised error is TFP_FOV_PACKET_NONE. *why: the second result
   * (only read for TFP_FOV_PACKET_OTHER), or NULL. */
  void (*presentation_transition)(void *user, TfpFovPacket *out, const char **why);
  void (*controller_result)(void *user, TfpFovControllerState *out);
  bool (*controller_enabled)(void *user);
  const TfpFovPoseLease *(*controller_lease)(void *user);   /* NULL: no lease */
  /* The clock. May be NULL. False: it raised or gave no number. */
  bool (*clock)(void *user, double *seconds);
  void (*log)(void *user, const char *text);                /* may be NULL */
} TfpFovIo;

/* Statistics as retail's Lua table: has=false is nil (or the empty table of a
 * lease that never read any). Counters are numbers, as tonumber() gives them. */
typedef struct TfpFovStatsView {
  bool has;
  double seen, forwarded, replaced, immediate_applies, binds, ads_forwarded, last_native, last_applied, source_degrees;
  bool installed, armed;
  uint32_t error;
} TfpFovStatsView;

struct TfpFovAdapter;
typedef struct TfpFovHandle {
  const struct TfpFovAdapter *owner;
  bool disarmed;
  uint64_t generation;
  TfpFovStatsView stats;      /* final statistics once disarmed */
} TfpFovHandle;
typedef struct TfpFovAdapter {
  TfpFovProfile profile;
  const TfpFovHelperApi *api;
  const TfpFovIo *io;         /* the clock only */
  TfpFovHandle *owned;        /* NULL: retail's false */
  bool has_last_failure_code; uint32_t last_failure_code;
  TfpFovStats buffer;         /* reused between reads, as retail's stats_buffer */
  bool has_cache; TfpFovStatsView cache; const TfpFovHandle *stats_handle;
  bool has_at; double stats_at, stats_due;
  char text[TFP_FOV_TEXT];
} TfpFovAdapter;

/* The adapter. A false result carries its text in *why (retail: a returned
 * `false,text` or a raised error, which the lease treats alike). */
void tfp_fov_adapter_init(TfpFovAdapter *a, const TfpFovHelperApi *api, const TfpFovIo *io, TfpFovProfile profile);
bool tfp_fov_adapter_stats(TfpFovAdapter *a, TfpFovHandle *handle, TfpFovStatsView *out, const char **why);
bool tfp_fov_adapter_validate(TfpFovAdapter *a, const TfpFovCamera *config, const char **why);
/* `handle` is storage for the new handle; it stays referenced while owned. */
bool tfp_fov_adapter_begin(TfpFovAdapter *a, const TfpFovCamera *config, TfpFovHandle *handle, const char **why);
bool tfp_fov_adapter_bind(TfpFovAdapter *a, TfpFovHandle *handle, const TfpFovCamera *config, const char **why);
bool tfp_fov_adapter_transition(TfpFovAdapter *a, TfpFovHandle *handle, const TfpFovPacket *token, const char **why);
bool tfp_fov_adapter_finish(TfpFovAdapter *a, TfpFovHandle *handle, TfpFovStatsView *stats, const char **why);

typedef enum TfpFovPhase { TFP_FOV_LEASE_IDLE, TFP_FOV_LEASE_ACTIVE, TFP_FOV_LEASE_CLEANUP_PENDING } TfpFovPhase;
typedef enum TfpFovFailureKind { TFP_FOV_FAILURE_NONE, TFP_FOV_FAILURE_TEMPORARY_READ, TFP_FOV_FAILURE_UNSAFE } TfpFovFailureKind;
typedef struct TfpFovLease {
  TfpFovAdapter helper;
  const TfpFovIo *io;
  TfpFovHandle *handle, storage;
  uint64_t bound;                                     /* identity of the bound pose lease; 0: nil */
  bool has_bound_request; uint64_t bound_request;
  bool has_bound_weapon; uint64_t bound_weapon;
  uint64_t bound_player;                              /* 0: nil */
  bool closing;
  bool has_fault; char fault[TFP_FOV_TEXT];
  bool has_failure_native_error; uint32_t failure_native_error;
  TfpFovFailureKind failure_kind;
  bool has_presentation; double presentation_epoch, presentation_progress;
  TfpFovStatsView last;
  TfpFovPhase phase;
  char reply[TFP_FOV_TEXT];
  const char *why;
} TfpFovLease;
typedef struct TfpFovResult {
  TfpFovStatsView stats;
  TfpFovPhase phase;
  bool cleanup_pending;
  const char *error_message;                          /* NULL: nil */
  bool has_failure_native_error; uint32_t failure_native_error;
  TfpFovFailureKind failure_kind;
  bool has_presentation; double presentation_epoch, presentation_progress, transition_epoch, transition_progress;
} TfpFovResult;

/* The lease. update/stop/shutdown/preflight return retail's first result;
 * *why (may be passed NULL) receives the second: a text valid until the next
 * call, or NULL. */
void tfp_fov_lease_init(TfpFovLease *l, const TfpFovHelperApi *api, const TfpFovIo *io, TfpFovProfile profile);
bool tfp_fov_lease_update(TfpFovLease *l, const char **why);
bool tfp_fov_lease_stop(TfpFovLease *l, const char **why);
bool tfp_fov_lease_shutdown(TfpFovLease *l, const char **why);
bool tfp_fov_lease_preflight(TfpFovLease *l, const char **why);
static inline bool tfp_fov_lease_is_clean(const TfpFovLease *l) { return l->handle == NULL; }
void tfp_fov_lease_result(const TfpFovLease *l, TfpFovResult *out);
/* The status line; returns the length snprintf reports. */
int tfp_fov_lease_status(const TfpFovLease *l, char *out, size_t size);
#endif
