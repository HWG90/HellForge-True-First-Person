/* Adapter of the editor/pose helper: translation of retail
 * src/first_person/pose.lua at 0.5.41-smooth.66 (commit 1d0d453). The adapter
 * (retail's Pose object) owns the helper generation: read-only preparation,
 * Begin, the placement / motion / zoom / transition setters, statistics and
 * End. Every text is retail's. pose.lua writes no log line.
 * tests/vectors/pose_adapter_vectors.h holds the retail file's own answers;
 * docs/translation/POSE-ADAPTER-TRANSLATION.md lists what was dropped and why. */
#ifndef TFP_POSE_ADAPTER_H
#define TFP_POSE_ADAPTER_H
#include "pose_helper.h"

#define TFP_POSE_TEXT 256
/* The chunk name Lua prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_POSE_LUA_CHUNK "src/first_person/pose.lua"

/* The helper, reached only through this table (retail: the loaded DLL). In the
 * mod: the tfp_pose_* exports of pose_helper.h with the same names. */
typedef struct TfpPoseHelperApi {
  uint32_t (*validate)(const TfpPoseConfig *config);
  uint32_t (*validate_input)(const TfpPoseConfig *config);
  uint32_t (*begin)(const TfpPoseConfig *config, uint64_t *generation);
  uint32_t (*begin_steady)(const TfpPoseConfig *config, uint64_t *generation);
  uint32_t (*begin_input)(const TfpPoseConfig *config, uint64_t *generation);
  uint32_t (*begin_steady_input)(const TfpPoseConfig *config, uint64_t *generation);
  uint32_t (*set_placement)(uint64_t generation, float x, float y, float z);
  uint32_t (*set_ads_placement)(uint64_t generation, float x, float y, float z);
  uint32_t (*set_transform)(uint64_t generation, float dx, float dy, float dz, float qx, float qy, float qz, float qw);
  uint32_t (*set_authored_transform)(uint64_t generation, float dx, float dy, float dz, float qx, float qy, float qz, float qw);
  uint32_t (*set_zoom_policy)(uint64_t generation, uint32_t active);
  uint32_t (*set_transition)(uint64_t generation, uint64_t epoch, float progress, uint32_t ads);
  uint32_t (*end)(uint64_t generation);
  uint32_t (*read_stats)(uint64_t generation, TfpPoseStats *stats);
  uint32_t (*read_depth_stats)(uint64_t generation, TfpPoseDepthStats *stats);
  uint32_t (*read_input_stats)(uint64_t generation, TfpPoseInputStats *stats);
  uint32_t (*error)(void);
} TfpPoseHelperApi;

/* profile.lua `pose` as far as pose.lua reads it: abi=2, normalize_zoom_input=true. */
typedef struct TfpPoseAdapterProfile { uint32_t abi; bool normalize_zoom_input; } TfpPoseAdapterProfile;
#define TFP_POSE_ADAPTER_PROFILE_SHIPPED ((TfpPoseAdapterProfile){2, true})

/* The time of the statistics display tick (pose.lua:29-36). `clock` is the
 * function handed to Pose.new: false when it raised or did not return a
 * number. When `clock` is NULL (the shipped path: backend.lua:807 passes
 * none) `tick` is GetTickCount64 in milliseconds; both NULL: no time (retail:
 * kernel32 cannot be loaded). The struct itself must be given. */
typedef struct TfpPoseAdapterIo {
  void *user;
  bool (*clock)(void *user, double *seconds);
  uint64_t (*tick)(void *user);
} TfpPoseAdapterIo;

/* What pose.lua reads of the backend's configuration table. A set bit in
 * `missing` is a field that is nil (or false) in retail's table, in the order
 * of TFP_POSE_ADAPTER_CONFIG_FIELDS (bit 0 = game). */
#define TFP_POSE_ADAPTER_CONFIG_FIELDS 17
typedef struct TfpPoseAdapterConfig {
  uint32_t missing;
  uint64_t game, exe, context, request, avatar_owner;
  uint32_t avatar_index, avatar, weapon, body, viewmodel, duration_ms;
  uint64_t weapon_descriptor;
  uint32_t weapon_unit, blend_ms;
  double offset_x, offset_y, offset_z;
  bool steady_entry;                   /* config.steady_entry==true */
} TfpPoseAdapterConfig;

typedef struct TfpPoseStatsView {
  bool has;
  double seen_position, seen_rotation, forwarded_position, forwarded_rotation, weapon_updates, zero_restores;
  bool installed, armed, expired;
  uint32_t error;
  double blend, target_weight;
} TfpPoseStatsView;
typedef struct TfpPoseDepthView {
  bool installed, armed, supported;
  double reason, zoom_index, applied, unsupported, cache_refreshes, cache_checks, ads_bypassed;
  double native_distance, reference_distance, correction, zoom, target_fov, current_fov, gain;
} TfpPoseDepthView;
typedef struct TfpPoseInputView {
  bool installed, active;
  double reason, pending, cache_entries, observed, normalized, unsupported, restored, foreign_wins, quarantined;
  double ratio, yaw_gain, pitch_gain;
} TfpPoseInputView;
/* The shared presentation phase handed to transition(). A has_* false is a
 * field that is absent or of another type. */
typedef struct TfpPosePhasePacket {
  bool has_epoch; double epoch;
  bool has_progress; double progress;
  bool has_ads, ads;
} TfpPosePhasePacket;

struct TfpPoseAdapter;
/* prepare()'s token: retail's {owner=self}. */
typedef struct TfpPosePrepared { const struct TfpPoseAdapter *owner; } TfpPosePrepared;
typedef struct TfpPoseHandle {
  const struct TfpPoseAdapter *owner;
  bool disarmed;
  uint64_t generation;
  TfpPoseStatsView stats;              /* final statistics once disarmed */
} TfpPoseHandle;
typedef struct TfpPoseAdapter {
  TfpPoseAdapterProfile profile;
  const TfpPoseHelperApi *api;
  const TfpPoseAdapterIo *io;
  TfpPoseHandle *owned;                /* NULL: retail's false */
  /* The display counters shared between the staggered 10 Hz ticks. */
  bool has_cache; TfpPoseStatsView cache;
  const TfpPoseHandle *stats_handle;
  bool has_at; double stats_at, stats_due;
  /* Reused between reads, as retail's stats_buffer / depth_buffer / input_buffer. */
  TfpPoseStats stats_buffer;
  TfpPoseDepthStats depth_buffer;
  TfpPoseInputStats input_buffer;
  char text[TFP_POSE_TEXT];
} TfpPoseAdapter;

/* prepare and begin can end in a raised Lua error (a failed assertion); the
 * backend words its own refusal differently for the two (backend.lua:604-625). */
typedef enum TfpPoseAnswer { TFP_POSE_FALSE, TFP_POSE_TRUE, TFP_POSE_RAISED } TfpPoseAnswer;

/* A false / raised result carries retail's text in *why (valid until the next
 * call on the adapter), or NULL where retail returns none. A handle or number
 * argument passed as NULL is retail's nil. */
void tfp_pose_adapter_init(TfpPoseAdapter *a, const TfpPoseHelperApi *api, const TfpPoseAdapterIo *io, TfpPoseAdapterProfile profile);
bool tfp_pose_adapter_stats(TfpPoseAdapter *a, TfpPoseHandle *handle, TfpPoseStatsView *out, const char **why);
bool tfp_pose_adapter_depth_stats(TfpPoseAdapter *a, TfpPoseHandle *handle, TfpPoseDepthView *out, const char **why);
bool tfp_pose_adapter_input_stats(TfpPoseAdapter *a, TfpPoseHandle *handle, TfpPoseInputView *out, const char **why);
TfpPoseAnswer tfp_pose_adapter_prepare(TfpPoseAdapter *a, const TfpPoseAdapterConfig *config, TfpPosePrepared *prepared, const char **why);
/* `handle` is storage for the new handle; it stays referenced while owned.
 * After anything but TFP_POSE_TRUE ask tfp_pose_adapter_owned_handle(): a
 * generation that came back with a refusal is owned and must be finished. */
TfpPoseAnswer tfp_pose_adapter_begin(TfpPoseAdapter *a, const TfpPoseAdapterConfig *config, const TfpPosePrepared *prepared,
                                     TfpPoseHandle *handle, const char **why);
bool tfp_pose_adapter_set_placement(TfpPoseAdapter *a, TfpPoseHandle *handle, const double *x, const double *y, const double *z,
                                    const char **why);
bool tfp_pose_adapter_set_ads_placement(TfpPoseAdapter *a, TfpPoseHandle *handle, const double *x, const double *y, const double *z,
                                        const char **why);
bool tfp_pose_adapter_set_motion(TfpPoseAdapter *a, TfpPoseHandle *handle, const double *dx, const double *dy, const double *dz,
                                 const double *qx, const double *qy, const double *qz, const double *qw, bool authored_ads,
                                 const char **why);
bool tfp_pose_adapter_set_zoom_policy(TfpPoseAdapter *a, TfpPoseHandle *handle, const bool *active, const char **why);
bool tfp_pose_adapter_transition(TfpPoseAdapter *a, TfpPoseHandle *handle, const TfpPosePhasePacket *packet, const char **why);
static inline TfpPoseHandle *tfp_pose_adapter_owned_handle(const TfpPoseAdapter *a) { return a->owned; }
/* True with stats->has false: retail's `true,nil` (no statistics could be read). */
bool tfp_pose_adapter_finish(TfpPoseAdapter *a, TfpPoseHandle *handle, TfpPoseStatsView *stats, const char **why);
#endif
