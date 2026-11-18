/* The presentation history: translation of retail src/first_person/diagnostics.lua at
 * 0.5.41-smooth.66 (commit 1d0d453). A ring of copied per-frame samples and event rows with
 * running counters; read only through the debug interface. It calls nothing outside itself.
 * tests/vectors/diagnostics_vectors.h holds the retail file's own answers;
 * docs/translation/EFFECTS-MODULES-TRANSLATION.md lists what was dropped and why. */
#ifndef TFP_DIAGNOSTICS_H
#define TFP_DIAGNOSTICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_DIAGNOSTICS_LUA_CHUNK "src/first_person/diagnostics.lua"
#define TFP_DIAGNOSTICS_TEXT 200          /* a copied text is cut to this many bytes */
#define TFP_DIAGNOSTICS_EVENT_TEXT 64     /* an event name is cut to this many bytes */
#define TFP_DIAGNOSTICS_DEFAULT_CAPACITY 360

/* Retail's `fields`, in the order of the source text. */
#define TFP_DIAGNOSTICS_FIELD_LIST(X) \
  X(time) X(enabled) X(player) X(weapon) X(request) X(camera_kind) X(ads) X(foreign) X(editor) X(phase) X(transition_epoch) \
  X(transition_progress) X(pinned) X(pin_expected) X(camera_retired) X(view_expected) X(view_visible) X(recovering) X(fov_native) \
  X(fov_applied) X(camera_blend) X(pose_blend) X(camera_epoch) X(fov_epoch) X(pose_epoch) X(look_sequence) X(look_samples) \
  X(look_skipped) X(cleanup_pending) X(reason) X(policy) X(event) X(placement_x) X(placement_y) X(placement_z) X(placement_yaw) \
  X(placement_pitch) X(placement_roll) X(motion_x) X(motion_y) X(motion_z) X(look_yaw) X(look_pitch) X(ads_rebases) X(control_mask) \
  X(control_guard) X(safe) X(aiming) X(snapshot_reason) X(weapon_ready) X(entry_block) X(action) X(softened) X(head_calls) \
  X(camera_gate) X(head_phase) X(cam_x) X(cam_y) X(cam_z) X(cam_fx) X(cam_fy) X(cam_fz) X(vm_lx) X(vm_ly) X(vm_lz) X(vm_cx) X(vm_cy) \
  X(vm_cz) X(gun_cx) X(gun_cy) X(gun_cz) X(gun_turn) X(head_distance)
typedef enum TfpDiagnosticsField {
#define X(name) TFP_DIAGNOSTICS_##name,
  TFP_DIAGNOSTICS_FIELD_LIST(X)
#undef X
  TFP_DIAGNOSTICS_FIELDS
} TfpDiagnosticsField;

/* A Lua value as far as this module tells values apart. OTHER: any other type (a table, a
 * function, userdata); it is never copied. A text is bytes with a length (it may hold NUL). */
typedef enum TfpDiagnosticsKind {
  TFP_DIAGNOSTICS_NIL, TFP_DIAGNOSTICS_BOOLEAN, TFP_DIAGNOSTICS_NUMBER, TFP_DIAGNOSTICS_STRING, TFP_DIAGNOSTICS_OTHER
} TfpDiagnosticsKind;
typedef struct TfpDiagnosticsValue {
  TfpDiagnosticsKind kind;
  bool boolean;
  double number;
  const char *text;
  size_t length;
} TfpDiagnosticsValue;
typedef struct TfpDiagnosticsSample { TfpDiagnosticsValue field[TFP_DIAGNOSTICS_FIELDS]; } TfpDiagnosticsSample;

typedef struct TfpDiagnosticsCounters {
  uint64_t frames, duplicate_frames, out_of_order_frames, events, overwritten, player_changes, weapon_changes, request_changes,
      camera_changes, transition_restarts, mixed_publication_frames, unexpected_view_frames, look_duplicates, cleanup_frames;
} TfpDiagnosticsCounters;

typedef struct TfpDiagnosticsCell { uint8_t kind; bool boolean; double number; uint32_t offset, length; } TfpDiagnosticsCell;
typedef struct TfpDiagnosticsRow {
  double frame;
  TfpDiagnosticsCell field[TFP_DIAGNOSTICS_FIELDS];
  char *text;                      /* the row's texts, one after the other */
  size_t text_used, text_size;
} TfpDiagnosticsRow;
/* The eight fields of the previous recorded row that the counters read. */
enum { TFP_DIAGNOSTICS_KEPT = 8 };
typedef struct TfpDiagnosticsKept { TfpDiagnosticsCell cell; char text[TFP_DIAGNOSTICS_TEXT]; } TfpDiagnosticsKept;

typedef struct TfpDiagnostics {
  uint32_t capacity, head, count;  /* head: index of the oldest row, from 0 */
  TfpDiagnosticsRow *ring, stage;
  bool has_previous; TfpDiagnosticsKept previous[TFP_DIAGNOSTICS_KEPT];
  bool has_last_frame; double last_frame;
  TfpDiagnosticsCounters totals;
  char error[96];
} TfpDiagnostics;

typedef enum TfpDiagnosticsAnswer { TFP_DIAGNOSTICS_FALSE, TFP_DIAGNOSTICS_TRUE, TFP_DIAGNOSTICS_RAISED } TfpDiagnosticsAnswer;

/* M.new(options). has_options false: no options table. `capacity` is options.capacity (nil or
 * false: 360). False: retail's assertion raised (*error, when not NULL, receives the text, valid
 * until the next call) or the ring could not be allocated ("not enough memory"). */
bool tfp_diagnostics_init(TfpDiagnostics *d, bool has_options, TfpDiagnosticsValue capacity, const char **error);
void tfp_diagnostics_free(TfpDiagnostics *d);
/* self:record(frame,sample). sample NULL: not a table. RAISED: retail raises while counting (a
 * comparison of two kinds of value, see the note); the frame is counted, the row is not kept. */
TfpDiagnosticsAnswer tfp_diagnostics_record(TfpDiagnostics *d, TfpDiagnosticsValue frame, const TfpDiagnosticsSample *sample,
                                            const char **error);
/* self:event(frame,name,sample). sample NULL: not a table (the row then has only the name). */
TfpDiagnosticsAnswer tfp_diagnostics_event(TfpDiagnostics *d, TfpDiagnosticsValue frame, TfpDiagnosticsValue name,
                                           const TfpDiagnosticsSample *sample, const char **error);
void tfp_diagnostics_clear(TfpDiagnostics *d);
/* self:snapshot(), read in place: capacity, count, counters and the rows from the oldest (0). */
static inline uint32_t tfp_diagnostics_capacity(const TfpDiagnostics *d) { return d->capacity; }
static inline uint32_t tfp_diagnostics_count(const TfpDiagnostics *d) { return d->count; }
static inline const TfpDiagnosticsCounters *tfp_diagnostics_counters(const TfpDiagnostics *d) { return &d->totals; }
const TfpDiagnosticsRow *tfp_diagnostics_row(const TfpDiagnostics *d, uint32_t index);
TfpDiagnosticsValue tfp_diagnostics_row_value(const TfpDiagnosticsRow *row, TfpDiagnosticsField field);
const char *tfp_diagnostics_field_name(TfpDiagnosticsField field);
#endif
