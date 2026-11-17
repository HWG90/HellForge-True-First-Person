/* Native focused locomotion: translation of retail src/first_person/first_person_movement.lua at
 * 0.5.41-smooth.66 (commit 1d0d453). While the first-person mode runs and the game's own S key is
 * held (manual FPV), or while a natural Fird camera owns the focus (update_fird), the "focused" byte
 * of the local avatar's locomotion row is leased from 0 to 1 and written back on stop. No input,
 * aim or bone writes. Every text is retail's.
 * tests/vectors/first_person_movement_vectors.h holds the retail file's own answers. */
#ifndef TFP_FIRST_PERSON_MOVEMENT_H
#define TFP_FIRST_PERSON_MOVEMENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_MOVEMENT_TEXT 256
/* The chunk name LuaJIT prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_MOVEMENT_LUA_CHUNK "src/first_person/first_person_movement.lua"

/* The build data of this file: the six code fingerprints (retail `anchors`, lines 8-15) and the
 * global that holds the locomotion pool (line 45), all relative to the game module. The row layout
 * (pool +0xC, +0x20, +0x38, +0x50; row 0xA0 bytes, the focused byte at +0x94) is the same in both
 * builds: it is what anchors 1 to 3 spell out. */
typedef struct TfpMovementBuild {
  struct { uint32_t at, size; uint8_t bytes[52]; } anchors[6];
  uint32_t pool;
} TfpMovementBuild;
/* Retail 26568081: the numbers of the retail file; the vectors are replayed with these. */
extern const TfpMovementBuild tfp_movement_build_retail;
/* HellForge's build 25480438: tools/verify_effects_constants.py shows where each number comes from. */
extern const TfpMovementBuild tfp_movement_build_25480438;

/* What this module reads of a backend player snapshot. `handle` is the host's own: the module
 * hands the snapshot back (controller_focus_owned, deps_fallback_ready, backend_action_state,
 * backend_on_foot) and those read fields this module does not know. Only the snapshot taken last
 * is ever handed back. */
typedef struct TfpMovementSnapshot {
  void *handle;
  uint64_t base, world, avatar_owner;
  uint32_t avatar_index, avatar, body, camera_kind;
  uint8_t avatar_descriptor[20];
  bool safe, seated, controller_active;
} TfpMovementSnapshot;
/* A snapshot call: the table; `nil, why` (*why NULL: no text); or a raised error (*why its text). */
typedef enum TfpMovementCall { TFP_MOVEMENT_DONE, TFP_MOVEMENT_REFUSED, TFP_MOVEMENT_RAISED } TfpMovementCall;
/* backend.foreign_camera(): retail requires a Boolean. */
typedef enum TfpMovementForeign { TFP_MOVEMENT_FOREIGN_FALSE, TFP_MOVEMENT_FOREIGN_TRUE, TFP_MOVEMENT_FOREIGN_OTHER } TfpMovementForeign;
/* A Lua number or nil. */
typedef struct TfpMovementNumber { bool has; double value; } TfpMovementNumber;
/* MEMORY_BASIC_INFORMATION (retail's NativeADS_Region); 48 bytes. */
typedef struct TfpMovementRegion {
  uint64_t base, allocation; uint32_t allocation_protect;
  uint16_t partition, reserved; uint64_t size;
  uint32_t state, protect, kind, padding;
} TfpMovementRegion;

/* Everything outside this file. A function that returns bool with `error` answers false when its
 * Lua counterpart raises; *error is the raised text. A NULL pointer: retail's field is not a function. */
typedef struct TfpMovementHost {
  void *user;
  /* backend:player_snapshot(), :pin_player_snapshot() (chosen when backend_pin_first_person and the pointer is set). */
  TfpMovementCall (*backend_player_snapshot)(void *user, TfpMovementSnapshot *out, const char **why);
  TfpMovementCall (*backend_pin_player_snapshot)(void *user, TfpMovementSnapshot *out, const char **why);
  bool backend_pin_first_person;                    /* backend.pin_first_person==true */
  /* backend:_read(address,size), :_ptr, :_u32. */
  bool (*backend_read)(void *user, uint64_t address, uint32_t size, uint8_t *out, const char **error);
  bool (*backend_ptr)(void *user, uint64_t address, uint64_t *out, const char **error);
  bool (*backend_u32)(void *user, uint64_t address, uint32_t *out, const char **error);
  /* backend:action_state(s) (the result is the host's, only handed on) and backend:on_foot(s,action):
   * *on_foot true only when retail's result is true. */
  bool (*backend_action_state)(void *user, const TfpMovementSnapshot *s, const void **action, const char **error);
  bool (*backend_on_foot)(void *user, const TfpMovementSnapshot *s, const void *action, bool *on_foot, const char **error);
  /* backend.foreign_camera(). NULL: not set. */
  bool (*backend_foreign_camera)(void *user, TfpMovementForeign *out, const char **error);
  /* The page query and the write retail reaches through backend:_windows(): VirtualQuery(address, region,
   * size) with its byte count as the result, and WriteProcessMemory(current process, address, bytes, size,
   * written) with its BOOL. */
  size_t (*windows_virtual_query)(void *user, uint64_t address, TfpMovementRegion *region, size_t size);
  int (*windows_write_process_memory)(void *user, uint64_t address, const uint8_t *bytes, size_t size, size_t *written);
  /* stingray.Keyboard. engine_keyboard: 0 when it is not a table, else a number that changes when the
   * engine's table is replaced (the S binding is looked up again). keyboard_button_index is preferred
   * to keyboard_button_id; both take 's'. A result without `has` is nil. */
  uint64_t engine_keyboard;
  bool (*keyboard_button_index)(void *user, const char *name, TfpMovementNumber *key, const char **error);
  bool (*keyboard_button_id)(void *user, const char *name, TfpMovementNumber *key, const char **error);
  bool (*keyboard_button)(void *user, double key, TfpMovementNumber *value, const char **error);
  /* stingray.Window.mouse_focus(): *focus true only when retail's result is true. show_cursor():
   * *shown false only when retail's result is false. */
  bool (*window_mouse_focus)(void *user, bool *focus, const char **error);
  bool (*window_show_cursor)(void *user, bool *shown, const char **error);
  /* The controller argument of update: controller_is_enabled NULL makes it "unavailable".
   * is_enabled(): true unless false/nil. result(): mode=='pose_offset'. focus_owned(s): *owned true
   * only when retail's result is true (update_fird only). */
  bool (*controller_is_enabled)(void *user);
  bool (*controller_result_pose_offset)(void *user);
  bool (*controller_focus_owned)(void *user, const TfpMovementSnapshot *s, bool *owned, const char **error);
  /* deps.fallback_ready(s): true only when retail's result is true. */
  bool (*deps_fallback_ready)(void *user, const TfpMovementSnapshot *s);
  /* deps.writable(address) and deps.write(address, byte): retail's test hooks, not set on the shipped
   * path (NULL: the windows_* pair is used). *answer true only when retail's result is true. */
  bool (*deps_writable)(void *user, uint64_t address, bool *answer, const char **error);
  bool (*deps_write)(void *user, uint64_t address, uint8_t value, bool *answer, const char **error);
  /* log(text). NULL: none. */
  void (*log)(void *user, const char *text);
  /* The build whose anchors and pool global are used. NULL: retail's. */
  const TfpMovementBuild *build;
} TfpMovementHost;

/* The leased byte and who it belongs to (retail's `location` table plus `before`). */
typedef struct TfpMovementRecord {
  uint64_t base, world, avatar_owner, descriptor, pool, descriptors, rows, address;
  uint32_t avatar_index, avatar, body, index;
  uint8_t descriptor_bytes[20];
  uint8_t before;
} TfpMovementRecord;

typedef enum TfpMovementPhase {
  TFP_MOVEMENT_IDLE, TFP_MOVEMENT_ACTIVE, TFP_MOVEMENT_NATIVE, TFP_MOVEMENT_FOREIGN, TFP_MOVEMENT_UNAVAILABLE, TFP_MOVEMENT_CLEANUP_PENDING
} TfpMovementPhase;
typedef enum TfpMovementScope { TFP_MOVEMENT_SCOPE_NONE, TFP_MOVEMENT_SCOPE_MANUAL, TFP_MOVEMENT_SCOPE_FIRD } TfpMovementScope;
typedef struct TfpMovement {
  const TfpMovementHost *host;
  bool has_owned; TfpMovementRecord owned;
  bool has_verified; uint64_t verified;            /* the module base whose fingerprints were checked */
  TfpMovementNumber key;                           /* the S binding */
  uint64_t keyboard;                               /* the engine_keyboard it was looked up on; 0: none */
  bool closing, draining;
  TfpMovementPhase phase;
  uint32_t writes, restores, foreign_changes;
  bool has_reason; char reason[TFP_MOVEMENT_TEXT];
  TfpMovementScope scope;
} TfpMovement;
typedef struct TfpMovementResult {
  TfpMovementPhase phase;
  uint32_t writes, restores, foreign_changes;
  const char *reason;                              /* NULL: nil */
  TfpMovementScope scope;
  bool owned, cleanup_pending, closing;
} TfpMovementResult;

/* *why (may be passed NULL) receives retail's second result: a text valid until the next call, or NULL. */
void tfp_movement_init(TfpMovement *m, const TfpMovementHost *host);
bool tfp_movement_update(TfpMovement *m, bool skip, const char **why);
bool tfp_movement_update_fird(TfpMovement *m, bool skip, const char **why);
bool tfp_movement_stop(TfpMovement *m, const char **why);
bool tfp_movement_shutdown(TfpMovement *m, const char **why);
static inline bool tfp_movement_is_clean(const TfpMovement *m) { return !m->has_owned; }
void tfp_movement_result(const TfpMovement *m, TfpMovementResult *out);
/* "<phase>; focused writes=<n>; restores=<n>" */
void tfp_movement_status(const TfpMovement *m, char *out, size_t size);
const char *tfp_movement_phase_name(TfpMovementPhase phase);
const char *tfp_movement_scope_name(TfpMovementScope scope);     /* NULL: nil */
#endif
