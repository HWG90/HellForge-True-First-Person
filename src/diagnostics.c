#include "diagnostics.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[TFP_DIAGNOSTICS_FIELDS] = {
#define X(name) #name,
  TFP_DIAGNOSTICS_FIELD_LIST(X)
#undef X
};
const char *tfp_diagnostics_field_name(TfpDiagnosticsField field) { return names[field]; }

/* The fields `previous` is read for, retail lines 48-56. */
static const TfpDiagnosticsField kept[TFP_DIAGNOSTICS_KEPT] = {
  TFP_DIAGNOSTICS_player, TFP_DIAGNOSTICS_weapon, TFP_DIAGNOSTICS_request, TFP_DIAGNOSTICS_camera_kind,
  TFP_DIAGNOSTICS_transition_epoch, TFP_DIAGNOSTICS_transition_progress, TFP_DIAGNOSTICS_look_sequence, TFP_DIAGNOSTICS_look_samples};
enum { K_PLAYER, K_WEAPON, K_REQUEST, K_CAMERA_KIND, K_TRANSITION_EPOCH, K_TRANSITION_PROGRESS, K_LOOK_SEQUENCE, K_LOOK_SAMPLES };

static bool raise(TfpDiagnostics *d, const char **error, int line, const char *text) {
  snprintf(d->error, sizeof d->error, "%s:%d: %s", TFP_DIAGNOSTICS_LUA_CHUNK, line, text);
  if (error) *error = d->error;
  return false;
}
/* frame%1~=0 or frame<1, as Lua computes the remainder (an infinity gives NaN, which is not 0). */
static bool whole_from_one(TfpDiagnosticsValue v) {
  double rest;
  if (v.kind != TFP_DIAGNOSTICS_NUMBER) return false;
  rest = v.number - floor(v.number);
  return !(rest != 0 || v.number < 1);
}

bool tfp_diagnostics_init(TfpDiagnostics *d, bool has_options, TfpDiagnosticsValue capacity, const char **error) {
  double cap = TFP_DIAGNOSTICS_DEFAULT_CAPACITY;
  memset(d, 0, sizeof *d);
  if (has_options && !(capacity.kind == TFP_DIAGNOSTICS_NIL || (capacity.kind == TFP_DIAGNOSTICS_BOOLEAN && !capacity.boolean))) {
    if (capacity.kind != TFP_DIAGNOSTICS_NUMBER) return raise(d, error, 24, "assertion failed!");
    cap = capacity.number;
  }
  if (!(cap - floor(cap) == 0 && cap >= 16 && cap <= 4096)) return raise(d, error, 24, "assertion failed!");
  d->ring = calloc((size_t)cap, sizeof *d->ring);
  if (!d->ring) {
    snprintf(d->error, sizeof d->error, "not enough memory");
    if (error) *error = d->error;
    return false;
  }
  d->capacity = (uint32_t)cap;
  return true;
}
void tfp_diagnostics_free(TfpDiagnostics *d) {
  if (d->ring) for (uint32_t i = 0; i < d->capacity; i++) free(d->ring[i].text);
  free(d->ring);
  free(d->stage.text);
  memset(d, 0, sizeof *d);
}

static TfpDiagnosticsValue view(const TfpDiagnosticsCell *cell, const char *text) {
  TfpDiagnosticsValue v = {(TfpDiagnosticsKind)cell->kind, cell->boolean, cell->number, NULL, 0};
  if (cell->kind == TFP_DIAGNOSTICS_STRING) { v.text = text + cell->offset; v.length = cell->length; }
  return v;
}
const TfpDiagnosticsRow *tfp_diagnostics_row(const TfpDiagnostics *d, uint32_t index) {
  return index < d->count ? &d->ring[(d->head + index) % d->capacity] : NULL;
}
TfpDiagnosticsValue tfp_diagnostics_row_value(const TfpDiagnosticsRow *row, TfpDiagnosticsField field) {
  return view(&row->field[field], row->text);
}

/* Appends a text of the row being built, cut to `limit` bytes (retail's v:sub(1,limit)). */
static void put_text(TfpDiagnosticsRow *row, TfpDiagnosticsCell *cell, TfpDiagnosticsValue v, size_t limit) {
  size_t length = v.length < limit ? v.length : limit;
  cell->kind = TFP_DIAGNOSTICS_STRING;
  cell->offset = (uint32_t)row->text_used;
  cell->length = (uint32_t)length;
  if (length) memcpy(row->text + row->text_used, v.text, length);
  row->text_used += length;
}
/* retail row_of(frame,sample) with scalar(), lines 15-21 and 35-39. False: no memory. */
static bool row_of(TfpDiagnostics *d, double frame, const TfpDiagnosticsSample *sample) {
  TfpDiagnosticsRow *row = &d->stage;
  size_t need = TFP_DIAGNOSTICS_EVENT_TEXT;
  if (sample)
    for (int i = 0; i < TFP_DIAGNOSTICS_FIELDS; i++)
      if (sample->field[i].kind == TFP_DIAGNOSTICS_STRING)
        need += sample->field[i].length < TFP_DIAGNOSTICS_TEXT ? sample->field[i].length : TFP_DIAGNOSTICS_TEXT;
  if (need > row->text_size) {
    char *grown = realloc(row->text, need);
    if (!grown) return false;
    row->text = grown; row->text_size = need;
  }
  row->frame = frame; row->text_used = 0;
  for (int i = 0; i < TFP_DIAGNOSTICS_FIELDS; i++) {
    TfpDiagnosticsCell *cell = &row->field[i];
    memset(cell, 0, sizeof *cell);
    if (!sample) continue;
    switch (sample->field[i].kind) {
      case TFP_DIAGNOSTICS_BOOLEAN: cell->kind = TFP_DIAGNOSTICS_BOOLEAN; cell->boolean = sample->field[i].boolean; break;
      case TFP_DIAGNOSTICS_STRING: put_text(row, cell, sample->field[i], TFP_DIAGNOSTICS_TEXT); break;
      case TFP_DIAGNOSTICS_NUMBER:
        if (sample->field[i].number == sample->field[i].number && fabs(sample->field[i].number) < HUGE_VAL) {
          cell->kind = TFP_DIAGNOSTICS_NUMBER; cell->number = sample->field[i].number;
        }
        break;
      default: break;
    }
  }
  return true;
}
/* retail insert(row), lines 29-34: the staged row changes place with the slot it takes. */
static void insert(TfpDiagnostics *d) {
  uint32_t index = (d->head + d->count) % d->capacity;
  TfpDiagnosticsRow old;
  if (d->count == d->capacity) { index = d->head; d->head = (d->head + 1) % d->capacity; d->totals.overwritten++; }
  else d->count++;
  old = d->ring[index]; d->ring[index] = d->stage; d->stage = old;
}

static bool truthy(TfpDiagnosticsValue v) { return v.kind != TFP_DIAGNOSTICS_NIL && !(v.kind == TFP_DIAGNOSTICS_BOOLEAN && !v.boolean); }
static bool is_true(TfpDiagnosticsValue v) { return v.kind == TFP_DIAGNOSTICS_BOOLEAN && v.boolean; }
static bool is_false(TfpDiagnosticsValue v) { return v.kind == TFP_DIAGNOSTICS_BOOLEAN && !v.boolean; }
static bool equal(TfpDiagnosticsValue a, TfpDiagnosticsValue b) {
  if (a.kind != b.kind) return false;
  switch (a.kind) {
    case TFP_DIAGNOSTICS_BOOLEAN: return a.boolean == b.boolean;
    case TFP_DIAGNOSTICS_NUMBER: return a.number == b.number;
    case TFP_DIAGNOSTICS_STRING: return a.length == b.length && memcmp(a.text, b.text, a.length) == 0;
    default: return true;
  }
}
/* Lua's a<b. 1 / 0, or -1 when Lua raises (two kinds, or two Booleans); the text is LuaJIT's. */
static int less(TfpDiagnostics *d, const char **error, int line, TfpDiagnosticsValue a, TfpDiagnosticsValue b) {
  static const char *const kinds[] = {"nil", "boolean", "number", "string", "table"};
  char text[64];
  if (a.kind == TFP_DIAGNOSTICS_NUMBER && b.kind == TFP_DIAGNOSTICS_NUMBER) return a.number < b.number;
  if (a.kind == TFP_DIAGNOSTICS_STRING && b.kind == TFP_DIAGNOSTICS_STRING) {
    size_t shared = a.length < b.length ? a.length : b.length;
    int order = memcmp(a.text, b.text, shared);
    return order ? order < 0 : a.length < b.length;
  }
  if (a.kind == b.kind) snprintf(text, sizeof text, "attempt to compare two %s values", kinds[a.kind]);
  else snprintf(text, sizeof text, "attempt to compare %s with %s", kinds[a.kind], kinds[b.kind]);
  raise(d, error, line, text);
  return -1;
}

TfpDiagnosticsAnswer tfp_diagnostics_record(TfpDiagnostics *d, TfpDiagnosticsValue frame, const TfpDiagnosticsSample *sample,
                                            const char **error) {
  const TfpDiagnosticsRow *row = &d->stage;
  TfpDiagnosticsValue now[TFP_DIAGNOSTICS_KEPT], was[TFP_DIAGNOSTICS_KEPT];
  uint64_t *const changes[4] = {&d->totals.player_changes, &d->totals.weapon_changes, &d->totals.request_changes, &d->totals.camera_changes};
#define FIELD(name) tfp_diagnostics_row_value(row, TFP_DIAGNOSTICS_##name)
  if (!whole_from_one(frame) || !sample) return TFP_DIAGNOSTICS_FALSE;
  if (d->has_last_frame && frame.number <= d->last_frame) {
    if (frame.number == d->last_frame) d->totals.duplicate_frames++; else d->totals.out_of_order_frames++;
    return TFP_DIAGNOSTICS_FALSE;
  }
  if (!row_of(d, frame.number, sample)) {
    snprintf(d->error, sizeof d->error, "not enough memory");
    if (error) *error = d->error;
    return TFP_DIAGNOSTICS_RAISED;
  }
  d->last_frame = frame.number; d->has_last_frame = true;
  d->totals.frames++;
  for (int i = 0; i < TFP_DIAGNOSTICS_KEPT; i++) now[i] = tfp_diagnostics_row_value(row, kept[i]);
  if (d->has_previous) {
    for (int i = 0; i < TFP_DIAGNOSTICS_KEPT; i++) was[i] = view(&d->previous[i].cell, d->previous[i].text);
    for (int i = 0; i < 4; i++)
      if (now[i].kind != TFP_DIAGNOSTICS_NIL && was[i].kind != TFP_DIAGNOSTICS_NIL && !equal(now[i], was[i])) (*changes[i])++;
    if (truthy(now[K_TRANSITION_EPOCH]) && truthy(was[K_TRANSITION_EPOCH]) && !equal(now[K_TRANSITION_EPOCH], was[K_TRANSITION_EPOCH])) {
      const TfpDiagnosticsValue one = {TFP_DIAGNOSTICS_NUMBER, false, 1, NULL, 0};
      int unfinished = less(d, error, 52, truthy(was[K_TRANSITION_PROGRESS]) ? was[K_TRANSITION_PROGRESS] : one, one);
      if (unfinished < 0) return TFP_DIAGNOSTICS_RAISED;
      if (unfinished) d->totals.transition_restarts++;
    }
    if (truthy(now[K_LOOK_SEQUENCE]) && equal(was[K_LOOK_SEQUENCE], now[K_LOOK_SEQUENCE]) && truthy(FIELD(enabled)) && !truthy(FIELD(foreign)) &&
        !truthy(FIELD(editor)) && truthy(now[K_LOOK_SAMPLES]) && truthy(was[K_LOOK_SAMPLES])) {
      int more = less(d, error, 54, was[K_LOOK_SAMPLES], now[K_LOOK_SAMPLES]);
      if (more < 0) return TFP_DIAGNOSTICS_RAISED;
      if (more) d->totals.look_duplicates++;
    }
  }
  if (((is_true(FIELD(pin_expected)) && is_false(FIELD(pinned))) || (is_true(FIELD(view_expected)) && is_false(FIELD(view_visible)))) &&
      !is_true(FIELD(foreign)) && !is_true(FIELD(editor)) && !is_true(FIELD(recovering)))
    d->totals.unexpected_view_frames++;
  if (truthy(FIELD(fov_epoch)) && truthy(FIELD(pose_epoch)) && !is_true(FIELD(recovering)) &&
      (!equal(FIELD(fov_epoch), FIELD(pose_epoch)) || (truthy(FIELD(camera_epoch)) && !equal(FIELD(camera_epoch), FIELD(pose_epoch)))))
    d->totals.mixed_publication_frames++;
  if (truthy(FIELD(cleanup_pending))) d->totals.cleanup_frames++;
#undef FIELD
  for (int i = 0; i < TFP_DIAGNOSTICS_KEPT; i++) {          /* previous=copy(row) */
    d->previous[i].cell = row->field[kept[i]];
    d->previous[i].cell.offset = 0;
    if (now[i].kind == TFP_DIAGNOSTICS_STRING && now[i].length) memcpy(d->previous[i].text, now[i].text, now[i].length);
  }
  d->has_previous = true;
  insert(d);
  return TFP_DIAGNOSTICS_TRUE;
}

TfpDiagnosticsAnswer tfp_diagnostics_event(TfpDiagnostics *d, TfpDiagnosticsValue frame, TfpDiagnosticsValue name,
                                           const TfpDiagnosticsSample *sample, const char **error) {
  if (!whole_from_one(frame) || name.kind != TFP_DIAGNOSTICS_STRING) return TFP_DIAGNOSTICS_FALSE;
  if (!row_of(d, frame.number, sample)) {
    snprintf(d->error, sizeof d->error, "not enough memory");
    if (error) *error = d->error;
    return TFP_DIAGNOSTICS_RAISED;
  }
  put_text(&d->stage, &d->stage.field[TFP_DIAGNOSTICS_event], name, TFP_DIAGNOSTICS_EVENT_TEXT);
  d->totals.events++;
  insert(d);
  return TFP_DIAGNOSTICS_TRUE;
}

void tfp_diagnostics_clear(TfpDiagnostics *d) {
  d->head = d->count = 0;
  d->has_previous = d->has_last_frame = false;
  memset(&d->totals, 0, sizeof d->totals);
}
