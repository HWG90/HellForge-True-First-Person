/* Aiming markers: translation of retail src/first_person/aim_markers.lua at 0.5.41-smooth.66
 * (commit 1d0d453). While the player aims in an owned first-person view, the shown bit of two HUD
 * groups (the fixed crosshair and the moving aiming circle) is cleared through the game's own
 * show/hide function, and set again afterwards. It is a narrow lease on those two bits, not a
 * render hook. Three layers, as in the Lua:
 *   the adapter  (retail's Adapter)   finds and checks the HUD, reads and sets a group's shown bit;
 *   the source   (retail's M.source)  says which weapon the view owns and whether it is aimed;
 *   the lease    (retail's M.new)     decides when to hide, remembers what it hid, gives it back.
 * Every text is retail's. tests/vectors/aim_markers_vectors.h holds the retail file's own answers. */
#ifndef TFP_AIM_MARKERS_H
#define TFP_AIM_MARKERS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_AIM_MARKERS_TEXT 256              /* every error / log text is cut to this (Lua has no cap) */
#define TFP_AIM_MARKERS_IDENTITY 512          /* longest weapon / player identity the host may hand in */
#define TFP_AIM_MARKERS_KEY 17                /* a weapon key: 16 lowercase hex digits and the NUL */
#define TFP_AIM_MARKERS_SAVED 64              /* most per-weapon entries the saved table may hold */
#define TFP_AIM_MARKERS_SAVED_KEY 24          /* a saved entry's key text with its NUL (longer ones: cut, then invalid) */
/* The chunk name LuaJIT prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_AIM_MARKERS_LUA_CHUNK "src/first_person/aim_markers.lua"
/* The build data of this file, relative to the game module: the game's own "set widget shown"
 * function, the four code fingerprints (retail's `checks`; the first is the function itself), the
 * globals that hold the game state and the HUD, the scene field of the state and the crosshair's
 * place in the mission HUD. The other offsets (HUD +0x24e334 built, +0x24e340 mission HUD, +0x820
 * parent; widget +0xf0 parent, +0xf8 context, groups +0x110 and +0x220) are the same in both builds. */
typedef struct TfpAimMarkersBuild {
  uint32_t set_shown;
  struct { uint32_t at, size; uint8_t bytes[32]; } checks[4];
  uint32_t state, scene, root, crosshair;
} TfpAimMarkersBuild;
/* Retail 26568081: the numbers of the retail file; the vectors are replayed with these. */
extern const TfpAimMarkersBuild tfp_aim_markers_build_retail;
/* HellForge's build 25480438: tools/verify_effects_constants.py shows where each number comes from. */
extern const TfpAimMarkersBuild tfp_aim_markers_build_25480438;

/* The HUD the adapter found (retail's identity table). All fields are addresses. */
typedef struct TfpAimMarkersHud { uint64_t game, root, state, parent, context, world, widgets[2]; } TfpAimMarkersHud;

/* MEMORY_BASIC_INFORMATION as backend.lua declares it (NativeADS_Region), 48 bytes. */
typedef struct TfpAimMarkersRegion {
  uint64_t base, allocation; uint32_t allocation_protect;
  uint16_t partition, reserved; uint64_t size;
  uint32_t state, protect, kind, padding;
} TfpAimMarkersRegion;

/* What this module reads of a backend snapshot. Texts are bytes with a length; a `has_` flag that
 * is false stands for a Lua value that is not a text. */
typedef struct TfpAimMarkersSnapshot {
  const void *source;                         /* the host's own; handed back to backend_pose_context */
  bool safe;                                  /* s.safe is neither nil nor false */
  int camera_kind;                            /* s.camera_kind; any other number when it is not one */
  bool has_identity; char identity[TFP_AIM_MARKERS_IDENTITY]; size_t identity_len;
  bool has_player_identity; char player_identity[TFP_AIM_MARKERS_IDENTITY]; size_t player_identity_len;
  bool shoulder_aim, controller_active, aiming;        /* each: the field is exactly true */
  bool foreign_camera;                        /* only of pin_player_snapshot: neither nil nor false */
} TfpAimMarkersSnapshot;

typedef enum TfpAimMarkersTri { TFP_AIM_MARKERS_UNSET, TFP_AIM_MARKERS_SHOW, TFP_AIM_MARKERS_HIDE } TfpAimMarkersTri;
typedef enum TfpAimMarkersSave { TFP_AIM_MARKERS_SAVE_DONE, TFP_AIM_MARKERS_SAVE_REFUSED, TFP_AIM_MARKERS_SAVE_RAISED } TfpAimMarkersSave;
/* The saved per-weapon table: the keys whose value is true (retail tests nothing else of a value). */
typedef struct TfpAimMarkersSaved { size_t count; char keys[TFP_AIM_MARKERS_SAVED][TFP_AIM_MARKERS_SAVED_KEY]; } TfpAimMarkersSaved;

/* Everything outside this file. A function that returns bool with `error` answers false when its
 * Lua counterpart raises; *error is the raised text (NULL: the error value nil). Functions without
 * `error` stand for calls that cannot raise on the shipped path (see the translation notes). */
typedef struct TfpAimMarkersHost {
  void *user;
  /* -- the adapter -- */
  /* backend:_module('identity'): the validated game module base. */
  bool (*backend_module_identity)(void *user, uint64_t *game, const char **error);
  /* backend:_read(address,size) (size <= 32 here), :_ptr, :_u32, :_u8. */
  bool (*backend_read)(void *user, uint64_t address, uint32_t size, uint8_t *out, const char **error);
  bool (*backend_ptr)(void *user, uint64_t address, uint64_t *out, const char **error);
  bool (*backend_u32)(void *user, uint64_t address, uint32_t *out, const char **error);
  bool (*backend_u8)(void *user, uint64_t address, uint8_t *out, const char **error);
  /* backend:_windows(): raises when the process is not Windows x64. *wine: its field `wine`. */
  bool (*backend_windows)(void *user, bool *wine, const char **error);
  /* kernel32 VirtualQuery(address, region, size): the number of bytes it filled. */
  size_t (*windows_virtual_query)(void *user, uint64_t address, TfpAimMarkersRegion *region, size_t size);
  /* The call of the game's function at `function` (= game + the build's set_shown) with
   * (widget, shown 0|1). Retail calls it through FFI, where only a fault can stop it; the error
   * channel is for a host that guards the call. It may have written before it fails. */
  bool (*game_set_widget_shown)(void *user, uint64_t function, uint64_t widget, uint8_t shown, const char **error);
  /* Adapter.new's second argument (retail's tests): replaces the three calls above. NULL on the shipped path. */
  bool (*setter)(void *user, uint64_t widget, bool shown, const char **error);
  /* -- the source -- */
  bool (*foreign)(void *user);                         /* options.foreign(): neither nil nor false */
  bool (*backend_snapshot)(void *user, TfpAimMarkersSnapshot *out);            /* false: nil */
  bool backend_pin_first_person;                       /* backend.pin_first_person==true */
  bool (*backend_pin_player_snapshot)(void *user, TfpAimMarkersSnapshot *out); /* may be NULL (not a function) */
  bool (*pinned_ready)(void *user, const TfpAimMarkersSnapshot *s);            /* options.pinned_ready(s)==true; may be NULL */
  bool (*fallback_ready)(void *user);                  /* options.fallback_ready(): neither nil nor false */
  bool (*controller_is_enabled)(void *user);
  bool (*controller_pose_offset)(void *user);          /* controller:result().mode=='pose_offset' */
  /* backend:_pose_context(s).weapon_object. */
  bool (*backend_pose_context)(void *user, const TfpAimMarkersSnapshot *s, uint64_t *weapon_object, const char **error);
  /* -- the lease -- */
  /* options.settings(): false when it is not a table. */
  bool (*settings)(void *user, TfpAimMarkersSaved *out);
  /* options.save(values): the whole new table. False: not saved, *why its second result (NULL: none). */
  bool (*save)(void *user, const char (*keys)[TFP_AIM_MARKERS_KEY], size_t count, const char **why);
  /* options.global_setting(): UNSET for anything that is not a Boolean. NULL: not a function. */
  TfpAimMarkersTri (*global_setting)(void *user);
  /* options.save_global(value). REFUSED: its first result was not true, *why its second (NULL: none).
   * RAISED: it raised *why. NULL: not a function. */
  TfpAimMarkersSave (*save_global)(void *user, bool value, const char **why);
  void (*log)(void *user, const char *text);           /* may be NULL */
  /* The build whose addresses and fingerprints are used. NULL: retail's. */
  const TfpAimMarkersBuild *build;
} TfpAimMarkersHost;

/* ---- the adapter ---- */
typedef enum TfpAimMarkersProbe { TFP_AIM_MARKERS_PROBE_HUD, TFP_AIM_MARKERS_PROBE_NONE, TFP_AIM_MARKERS_PROBE_RAISED } TfpAimMarkersProbe;
/* A RAISED / false answer is a raised Lua error: *error (when not NULL) receives its text, valid
 * until the next adapter or source call of this module, or NULL for the error value nil. */
TfpAimMarkersProbe tfp_aim_markers_probe(const TfpAimMarkersHost *host, bool mission_only, TfpAimMarkersHud *out, const char **error);
/* False when either is NULL (retail answers nil there). */
bool tfp_aim_markers_same(const TfpAimMarkersHud *a, const TfpAimMarkersHud *b);
bool tfp_aim_markers_visible(const TfpAimMarkersHost *host, uint64_t widget, bool *visible, const char **error);
bool tfp_aim_markers_show(const TfpAimMarkersHost *host, const TfpAimMarkersHud *hud, uint64_t widget, bool shown, const char **error);

/* ---- the source ---- */
typedef struct TfpAimMarkersTarget {
  bool view_only;
  bool has_key; char key[TFP_AIM_MARKERS_KEY];
  bool has_identity; char identity[TFP_AIM_MARKERS_IDENTITY + 16]; size_t identity_len;
  bool has_player_identity; char player_identity[TFP_AIM_MARKERS_IDENTITY]; size_t player_identity_len;
  bool aiming;
} TfpAimMarkersTarget;
typedef struct TfpAimMarkersSource {
  const TfpAimMarkersHost *host;
  bool has_cached_identity; char cached_identity[TFP_AIM_MARKERS_IDENTITY]; size_t cached_identity_len;
  bool has_cached_key; char cached_key[TFP_AIM_MARKERS_KEY];
} TfpAimMarkersSource;
typedef enum TfpAimMarkersRead { TFP_AIM_MARKERS_READ_TARGET, TFP_AIM_MARKERS_READ_NONE, TFP_AIM_MARKERS_READ_RAISED } TfpAimMarkersRead;
void tfp_aim_markers_source_init(TfpAimMarkersSource *s, const TfpAimMarkersHost *host);
/* retail's current(fresh). */
TfpAimMarkersRead tfp_aim_markers_source_current(TfpAimMarkersSource *s, bool fresh, TfpAimMarkersTarget *out, const char **error);

/* ---- the lease ---- */
typedef struct TfpAimMarkersRow { uint64_t widget; bool before, done; } TfpAimMarkersRow;
typedef struct TfpAimMarkersLease {
  TfpAimMarkersHud hud;
  char weapon[TFP_AIM_MARKERS_IDENTITY + 16]; size_t weapon_len;
  char player[TFP_AIM_MARKERS_IDENTITY]; size_t player_len;
  bool releasing;
  size_t count; TfpAimMarkersRow rows[2];              /* retail's rows / by_widget: one per group */
} TfpAimMarkersLease;
typedef struct TfpAimMarkers {
  const TfpAimMarkersHost *host;
  TfpAimMarkersSource source;
  bool has_lease; TfpAimMarkersLease lease;
  bool has_fault; char fault[TFP_AIM_MARKERS_TEXT];
  bool closing;
  char why[TFP_AIM_MARKERS_TEXT];                      /* the text the last call handed out */
} TfpAimMarkers;
typedef struct TfpAimMarkersResult { bool hidden; const char *error; bool cleanup_pending; } TfpAimMarkersResult;
typedef struct TfpAimMarkersMenuTarget {
  char key[TFP_AIM_MARKERS_KEY];
  char identity[TFP_AIM_MARKERS_IDENTITY + 16]; size_t identity_len;
  bool allowed;
} TfpAimMarkersMenuTarget;

/* *why (may be passed NULL) receives retail's second result: a text valid until the next call on
 * the same lease, or NULL. */
void tfp_aim_markers_init(TfpAimMarkers *m, const TfpAimMarkersHost *host);
bool tfp_aim_markers_setting(TfpAimMarkers *m);
bool tfp_aim_markers_set_setting(TfpAimMarkers *m, bool value, const char **why);
/* RAISED: retail's target() raised *error (the source could not read the weapon). */
TfpAimMarkersRead tfp_aim_markers_target(TfpAimMarkers *m, TfpAimMarkersMenuTarget *out, const char **error);
static inline bool tfp_aim_markers_is_clean(const TfpAimMarkers *m) { return !m->has_lease; }
bool tfp_aim_markers_stop(TfpAimMarkers *m, const char **why);
bool tfp_aim_markers_update(TfpAimMarkers *m, const char **why);
bool tfp_aim_markers_toggle(TfpAimMarkers *m, const char *key, const char *identity, size_t identity_len, const char **why);
bool tfp_aim_markers_shutdown(TfpAimMarkers *m, const char **why);
void tfp_aim_markers_result(const TfpAimMarkers *m, TfpAimMarkersResult *out);
#endif
