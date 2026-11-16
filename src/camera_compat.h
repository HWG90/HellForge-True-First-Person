/* Camera sharing with Photo Mode and ActionCam: translation of retail src/first_person/camera_compat.lua
 * at 0.5.41-smooth.66 (commit 1d0d453). The module looks at what other mods publish: Photo's
 * camera-ownership table (only observed) or, with older Photo releases, the ActionCam bridge, into whose
 * set_input_blocked it chains a reader of its own (the shim) that blocks ActionCam input while the mode
 * is on. Every text is retail's. tests/vectors/camera_compat_vectors.h holds the retail file's own
 * answers.
 *
 * The other mods are Lua values. Here each is a TfpCameraCompatValue the host hands out, and every
 * look retail takes at one (a global, a table field, an upvalue, a call) is one host call, made in
 * retail's order. */
#ifndef TFP_CAMERA_COMPAT_H
#define TFP_CAMERA_COMPAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_CAMERA_COMPAT_TEXT 256
/* The chunk name LuaJIT prints before an assertion's message ("<chunk>:<line>: "). */
#define TFP_CAMERA_COMPAT_LUA_CHUNK "src/first_person/camera_compat.lua"
/* debug.getupvalue is asked for at most this many upvalues of a setter (retail line 21). */
#define TFP_CAMERA_COMPAT_UPVALUES 32

typedef enum TfpCameraCompatKind {
  TFP_CAMERA_COMPAT_NIL, TFP_CAMERA_COMPAT_FALSE, TFP_CAMERA_COMPAT_TRUE, TFP_CAMERA_COMPAT_NUMBER, TFP_CAMERA_COMPAT_STRING,
  TFP_CAMERA_COMPAT_TABLE, TFP_CAMERA_COMPAT_FUNCTION, TFP_CAMERA_COMPAT_OTHER
} TfpCameraCompatKind;
/* A Lua value of another mod. `id` tells tables and functions apart: two values are the same object
 * exactly when their ids are equal (and the host keeps an object alive for as long as this module may
 * hold its id). `shim` is not 0 when the function is one of this module's own shims: the serial
 * tfp_camera_compat_shim() is called with; such a value needs no id. */
typedef struct TfpCameraCompatValue { TfpCameraCompatKind kind; uint64_t id; uint32_t shim; } TfpCameraCompatValue;
typedef enum TfpCameraCompatField {
  TFP_CAMERA_COMPAT_HELD, TFP_CAMERA_COMPAT_AVAILABLE, TFP_CAMERA_COMPAT_DISABLED, TFP_CAMERA_COMPAT_SET_INPUT_BLOCKED
} TfpCameraCompatField;
/* debug.getupvalue(function,index) under pcall: it raised; it returned no name; another name;
 * 'input_reader' (and *value). */
typedef enum TfpCameraCompatUpvalue {
  TFP_CAMERA_COMPAT_UPVALUE_RAISED, TFP_CAMERA_COMPAT_UPVALUE_END, TFP_CAMERA_COMPAT_UPVALUE_OTHER, TFP_CAMERA_COMPAT_UPVALUE_INPUT_READER
} TfpCameraCompatUpvalue;
typedef enum TfpCameraCompatCall { TFP_CAMERA_COMPAT_CALLED, TFP_CAMERA_COMPAT_RAISED } TfpCameraCompatCall;
typedef enum TfpCameraCompatRead { TFP_CAMERA_COMPAT_READ_RAISED, TFP_CAMERA_COMPAT_READ_FALSE, TFP_CAMERA_COMPAT_READ_NOT_FALSE } TfpCameraCompatRead;
typedef enum TfpCameraCompatEnabled {
  TFP_CAMERA_COMPAT_ENABLED_FALSE, TFP_CAMERA_COMPAT_ENABLED_TRUE, TFP_CAMERA_COMPAT_ENABLED_NOT_BOOLEAN, TFP_CAMERA_COMPAT_ENABLED_RAISED
} TfpCameraCompatEnabled;

/* Everything outside this file. Only the calls with a RAISED answer stand for something retail
 * protects with pcall; the others are plain reads. */
typedef struct TfpCameraCompatHost {
  void *user;
  /* rawget(G,'DBF_PHOTO_CAMERA_OWNERSHIP') and rawget(G,'DBF_PHOTO_ACTIONCAM_COMPAT'). */
  TfpCameraCompatValue (*photo_camera_ownership)(void *user);
  TfpCameraCompatValue (*photo_actioncam_compat)(void *user);
  /* rawget(G,'package') is a table, its .loaded is a table and loaded['mods/actioncam/actioncam'] is
   * neither nil nor false. */
  bool (*actioncam_loaded)(void *user);
  /* table.held / .available / .disabled / .set_input_blocked of a value of kind TABLE. */
  TfpCameraCompatValue (*table_field)(void *user, TfpCameraCompatValue table, TfpCameraCompatField field);
  /* pcall(debug.getupvalue, function, index), index from 1. `function` is whatever the provider's
   * set_input_blocked was when it was read (it can be no function: the real one raises then).
   * NULL: there is no debug table or its getupvalue is no function. */
  TfpCameraCompatUpvalue (*debug_getupvalue)(void *user, TfpCameraCompatValue function, int index, TfpCameraCompatValue *value);
  /* pcall(setter, reader): reader is nil, a function read from the setter earlier, or this module's
   * shim (reader.shim not 0: the host hands the provider the function that calls
   * tfp_camera_compat_shim with that serial). CALLED: *accepted_is_false when its first result is
   * false, *text its second result (NULL: nil or false). RAISED: *text = tostring(the error). */
  TfpCameraCompatCall (*set_input_blocked)(void *user, TfpCameraCompatValue setter, TfpCameraCompatValue reader, bool *accepted_is_false,
                                           const char **text);
  /* pcall(previous): the older reader of the chain, called without arguments. */
  TfpCameraCompatRead (*previous_reader_call)(void *user, TfpCameraCompatValue reader);
  /* pcall(controller.is_enabled, controller). NULL after construction counts as RAISED. */
  TfpCameraCompatEnabled (*controller_is_enabled)(void *user);
  /* log(text). NULL: none. */
  void (*log)(void *user, const char *text);
} TfpCameraCompatHost;

typedef enum TfpCameraCompatPhase {
  TFP_CAMERA_COMPAT_IDLE, TFP_CAMERA_COMPAT_ABSENT, TFP_CAMERA_COMPAT_OBSERVING, TFP_CAMERA_COMPAT_ACTIVE, TFP_CAMERA_COMPAT_FOREIGN,
  TFP_CAMERA_COMPAT_CONFLICT, TFP_CAMERA_COMPAT_CLEANUP_PENDING
} TfpCameraCompatPhase;
/* Retail's record `r`: one installation into a provider's reader chain, and the state of its shim.
 * A record lives as long as its shim can still be called, which is for ever: another mod may keep
 * the function and put it back. */
typedef struct TfpCameraCompatRecord {
  TfpCameraCompatValue provider, setter, previous;
  bool blocking, reading;
} TfpCameraCompatRecord;
typedef struct TfpCameraCompat {
  const TfpCameraCompatHost *host;
  TfpCameraCompatRecord *records;                /* records[serial-1] */
  uint32_t count, capacity;
  uint32_t record;                               /* the serial of retail's `record`; 0: nil */
  bool closing, conflict;
  TfpCameraCompatPhase phase;
  bool has_message; char message[TFP_CAMERA_COMPAT_TEXT];
  char status[96];
} TfpCameraCompat;
typedef struct TfpCameraCompatResult {
  TfpCameraCompatPhase phase;
  bool installed, blocking, foreign, cleanup_pending, closing;
  const char *foreign_reason, *error_message;    /* NULL: nil */
} TfpCameraCompatResult;

/* M.new(controller,log,G). False: retail raises *error (no controller_is_enabled). */
bool tfp_camera_compat_init(TfpCameraCompat *c, const TfpCameraCompatHost *host, const char **error);
void tfp_camera_compat_free(TfpCameraCompat *c);
/* *why (may be passed NULL) receives retail's second result: a text valid until the next call, or NULL. */
bool tfp_camera_compat_foreign(TfpCameraCompat *c, const char **why);
bool tfp_camera_compat_prepare(TfpCameraCompat *c, const char **why);
bool tfp_camera_compat_stop(TfpCameraCompat *c, const char **why);
bool tfp_camera_compat_shutdown(TfpCameraCompat *c, const char **why);
bool tfp_camera_compat_update(TfpCameraCompat *c, const char **why);
static inline bool tfp_camera_compat_is_clean(const TfpCameraCompat *c) { return c->record == 0; }
void tfp_camera_compat_result(TfpCameraCompat *c, TfpCameraCompatResult *out);
/* Valid until the next status call. */
const char *tfp_camera_compat_status(TfpCameraCompat *c);
/* The function retail hands to the provider (r.shim), for the record with that serial: true = ActionCam
 * input is blocked. The other mod calls it at any time, also from inside set_input_blocked and from
 * inside the older reader this very function calls. */
bool tfp_camera_compat_shim(TfpCameraCompat *c, uint32_t serial);
const char *tfp_camera_compat_phase_name(TfpCameraCompatPhase phase);
#endif
