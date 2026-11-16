/* The "native ADS" option: translation of retail src/first_person/native_ads_option.lua at
 * 0.5.41-smooth.66 (commit 1d0d453). While the mode is on and the menu switch is set, the game's own
 * aim-mode preference byte (per weapon or per input device, by the game's aim-memory setting) is
 * leased from 0 to 1 and written back on stop. The source (retail's M.source) finds, reads and
 * writes the byte; the lease (retail's M.new) decides when. Every text is retail's.
 * tests/vectors/native_ads_option_vectors.h holds the retail file's own answers;
 * docs/EFFECTS-MODULES-TRANSLATION.md lists what was dropped and why. */
#ifndef TFP_NATIVE_ADS_OPTION_H
#define TFP_NATIVE_ADS_OPTION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_NATIVE_AIM_TEXT 256
#define TFP_NATIVE_AIM_IDENTITY 512
/* The chunk name LuaJIT prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_NATIVE_AIM_LUA_CHUNK "src/first_person/native_ads_option.lua"

/* What this module reads of a backend snapshot. The host fills the texts (bytes, not
 * NUL-terminated) and refuses a snapshot whose identity does not fit. */
typedef struct TfpNativeAimSnapshot {
  uint64_t base, avatar_owner;
  char player_identity[TFP_NATIVE_AIM_IDENTITY]; size_t player_identity_len;
  /* Only of backend:snapshot() (the full, firearm snapshot): */
  char identity[TFP_NATIVE_AIM_IDENTITY]; size_t identity_len;
  uint8_t weapon_descriptor[20];
  uint32_t weapon;
  bool safe;
  int camera_kind;
} TfpNativeAimSnapshot;
typedef enum TfpNativeAimCopy { TFP_NATIVE_AIM_COPY_DONE, TFP_NATIVE_AIM_COPY_REFUSED, TFP_NATIVE_AIM_COPY_RAISED } TfpNativeAimCopy;
typedef struct TfpNativeAimControllerState { bool pose_offset; } TfpNativeAimControllerState;

/* Everything outside this file. A function that returns bool with `error` answers false when its
 * Lua counterpart raises; *error is the raised text. */
typedef struct TfpNativeAimHost {
  void *user;
  /* backend:snapshot(), :player_snapshot(), :pin_player_snapshot(). False: nil, *why its second
   * result (NULL: none). pin_player_snapshot may be NULL (retail: not a function). */
  bool (*backend_snapshot)(void *user, TfpNativeAimSnapshot *out, const char **why);
  bool (*backend_player_snapshot)(void *user, TfpNativeAimSnapshot *out, const char **why);
  bool (*backend_pin_player_snapshot)(void *user, TfpNativeAimSnapshot *out, const char **why);
  bool backend_pin_first_person;                    /* backend.pin_first_person==true */
  /* backend:_read(address,size), :_ptr, :_u32, :_u8. */
  bool (*backend_read)(void *user, uint64_t address, uint32_t size, uint8_t *out, const char **error);
  bool (*backend_ptr)(void *user, uint64_t address, uint64_t *out, const char **error);
  bool (*backend_u32)(void *user, uint64_t address, uint32_t *out, const char **error);
  bool (*backend_u8)(void *user, uint64_t address, uint8_t *out, const char **error);
  /* backend:_map(address,id[,limit]): the index of `id` in the hash map at `address`. */
  bool (*backend_map)(void *user, uint64_t address, uint32_t id, bool has_limit, uint32_t limit, uint32_t *index, const char **error);
  /* backend.foreign_camera(): true unless retail's returns false. NULL: not set. */
  bool (*backend_foreign_camera)(void *user);
  /* src/memory.lua writer:copy(address, one byte). REFUSED: it returned a false first result, *why its
   * second (NULL: none). RAISED: it raised *why. */
  TfpNativeAimCopy (*memory_copy)(void *user, uint64_t address, uint8_t value, const char **why);
  /* The lease's arguments: the menu switch (true: retail's value is not false/nil), the camera-ownership
   * callback (true unless retail's returns false; NULL: none) and the controller. */
  bool (*enabled)(void *user);
  bool (*foreign)(void *user);
  bool (*controller_is_enabled)(void *user);
  void (*controller_result)(void *user, TfpNativeAimControllerState *out);
} TfpNativeAimHost;

/* Where the preference byte of one owner lives (retail's record `r`). */
typedef struct TfpNativeAimRecord {
  uint64_t base, player, settings, at;
  uint32_t mode, weapon;
  char owner[TFP_NATIVE_AIM_IDENTITY]; size_t owner_len;
  char key[TFP_NATIVE_AIM_IDENTITY + 32]; size_t key_len;
  uint8_t descriptor[20];
  uint8_t value;
  bool has_before; uint8_t before;
} TfpNativeAimRecord;

typedef enum TfpNativeAimRead { TFP_NATIVE_AIM_READ_VALUE, TFP_NATIVE_AIM_READ_RETIRED, TFP_NATIVE_AIM_READ_RAISED } TfpNativeAimRead;
/* The source. A false / RAISED answer is a raised Lua error: *error (when not NULL) receives its
 * text, valid until the next call on the same host from this module, or NULL for an error value of nil. */
bool tfp_native_aim_source_probe(const TfpNativeAimHost *host, TfpNativeAimRecord *out, const char **error);
TfpNativeAimRead tfp_native_aim_source_read(const TfpNativeAimHost *host, const TfpNativeAimRecord *r, uint8_t *value, const char **error);
bool tfp_native_aim_source_write(const TfpNativeAimHost *host, const TfpNativeAimRecord *r, uint8_t expected, uint8_t value, const char **error);

typedef enum TfpNativeAimPhase {
  TFP_NATIVE_AIM_IDLE, TFP_NATIVE_AIM_ACTIVE, TFP_NATIVE_AIM_SUSPENDED, TFP_NATIVE_AIM_WAITING, TFP_NATIVE_AIM_CLEANUP_PENDING
} TfpNativeAimPhase;
typedef struct TfpNativeAim {
  const TfpNativeAimHost *host;
  TfpNativeAimPhase phase;
  uint32_t writes, restores;
  bool has_lease; TfpNativeAimRecord lease;
  bool closing;
  bool has_error; char error_message[TFP_NATIVE_AIM_TEXT];
} TfpNativeAim;
typedef struct TfpNativeAimResult {
  TfpNativeAimPhase phase;
  bool owned;
  uint32_t writes, restores;
  const char *error_message;                     /* NULL: nil */
} TfpNativeAimResult;

/* The lease. *why (may be passed NULL) receives retail's second result: a text valid until the
 * next call, or NULL. */
void tfp_native_aim_init(TfpNativeAim *a, const TfpNativeAimHost *host);
bool tfp_native_aim_update(TfpNativeAim *a, const char **why);
bool tfp_native_aim_stop(TfpNativeAim *a, const char **why);
bool tfp_native_aim_shutdown(TfpNativeAim *a, const char **why);
static inline bool tfp_native_aim_is_clean(const TfpNativeAim *a) { return !a->has_lease; }
void tfp_native_aim_result(const TfpNativeAim *a, TfpNativeAimResult *out);
const char *tfp_native_aim_phase_name(TfpNativeAimPhase phase);
#endif
