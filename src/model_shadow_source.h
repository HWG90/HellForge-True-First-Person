/* Read-only identity of the native first-person models: the rig, the arm
 * pieces hung on it and the weapon unit tree. Translation of retail
 * src/first_person/model_shadow_source.lua at 0.5.41-smooth.66 (commit
 * 1d0d453), whole file. It makes no engine call and writes nothing; head
 * visibility (head_visibility.c) turns its rows into shadow-pass writes.
 * tests/vectors/model_shadow_source_vectors.h holds the retail file's own
 * answers. What differs from the Lua and why: docs/translation/VISIBILITY-TRANSLATION.md.
 *
 * Addresses, unit references and counts are `double`, as in Lua: every sum
 * and comparison is then the one the Lua makes, whatever the bytes read. */
#ifndef TFP_MODEL_SHADOW_SOURCE_H
#define TFP_MODEL_SHADOW_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A Lua value that is a text or nil; the text of a raised error. */
#define TFP_VIS_TEXT 512
typedef struct TfpVisText { bool present; char text[TFP_VIS_TEXT]; } TfpVisText;
/* player_identity / identity of a snapshot: bytes with a length (they carry raw descriptor bytes). */
#define TFP_VIS_IDENTITY 256
typedef struct TfpVisIdentity { bool present; uint16_t len; uint8_t bytes[TFP_VIS_IDENTITY]; } TfpVisIdentity;

/* What backend:snapshot(), player_snapshot() and pin_player_snapshot() return, reduced to the
 * fields the two visibility modules read. `has_*` false: the field is nil (a player snapshot
 * carries no weapon fields). */
typedef struct TfpVisSnapshot {
  double base, record, head, tail, camera_kind, request_weapon, world, avatar_owner, avatar_index, body;
  bool has_viewmodel, has_weapon, has_weapon_unit, has_weapon_component_descriptor;
  double viewmodel, weapon, weapon_unit, weapon_component_descriptor;
  bool safe, seated, weapon_transition, head_camera_transition, controller_active, aiming;
  bool has_controller_bytes, has_flags, has_weapon_descriptor;
  uint8_t controller_len, flags_len, weapon_descriptor_len;
  uint8_t controller_bytes[8], flags[32], weapon_descriptor[32];
  TfpVisIdentity player_identity, identity;
} TfpVisSnapshot;

/* The table backend:_alignment_context returns, reduced to what is read here. */
typedef struct TfpModelContext { double unit, index, generation, object, units, generations; } TfpModelContext;

/* The backend. Every function returns false when the Lua call raised (or, for snapshot,
 * returned nil): `why` then holds the error's text; `why->present` false with snapshot is a
 * plain nil. */
typedef struct TfpModelShadowIo {
  void *user;
  bool (*snapshot)(void *user, TfpVisSnapshot *out, TfpVisText *why);                        /* backend:snapshot() */
  bool (*alignment_context)(void *user, double viewmodel, double avatar_owner, double avatar_index,
                            TfpModelContext *out, TfpVisText *why);                          /* backend:_alignment_context{...} */
  bool (*read)(void *user, double at, size_t size, uint8_t *out, TfpVisText *why);           /* backend:_read(at,size) */
  bool (*ptr)(void *user, double at, double *out, TfpVisText *why);                          /* backend:_ptr(at) */
  bool (*u32)(void *user, double at, double *out, TfpVisText *why);                          /* backend:_u32(at) */
  bool (*u8)(void *user, double at, double *out, TfpVisText *why);                           /* backend:_u8(at) */
  /* May be NULL (backend._descriptor is not a function): no observation is remembered. 24 bytes. */
  bool (*descriptor)(void *user, double world, double id, uint8_t out[24], TfpVisText *why); /* backend:_descriptor(world,id) */
  /* May be NULL. *value: 0 for false, anything else for any other answer. */
  bool (*foreign_camera)(void *user, int *value, TfpVisText *why);                           /* backend.foreign_camera() */
} TfpModelShadowIo;

/* `model_owner` of a row: what valid() proves again before a write. */
typedef struct TfpModelOwner {
  TfpVisIdentity identity;
  double rig, rig_object, weapon, weapon_object, world, parent, unit, object;
} TfpModelOwner;
/* One entry of the table probe() returns, in ascending order of `unit`.
 * rig false: {part='model', offsets={}, model_shadow=true, model_parent, model_owner}.
 * rig true:  {part='shoulder', rig=true, shadow_rig=true, offsets={}} (the other fields are unused). */
typedef struct TfpModelRow { double unit; bool rig; double model_parent; TfpModelOwner owner; } TfpModelRow;

typedef struct TfpModelInspected { double id, object, parent, world; bool has_parent, has_links; uint8_t links[16]; } TfpModelInspected;
typedef struct TfpModelClosing { double id; TfpModelContext context; double parent; bool has_links; uint8_t links[16]; } TfpModelClosing;

typedef struct TfpModelObservation { double object; uint8_t raw[24]; size_t len; } TfpModelObservation;

/* Asked by unchanged() for every unit of the remembered observation: resolve `id`, which must
 * be the object `expected`; give the object and its first 32 bytes. false: it raised (`why`). */
typedef bool (*TfpModelInspect)(void *context, double id, double expected, double *object, uint8_t header[32], TfpVisText *why);

typedef struct TfpModelShadow {
  const TfpModelShadowIo *io;
  double appearance;   /* where the appearance rows start in the avatar owner (:33): build data */
  /* `fingerprint` */
  bool has_fingerprint;
  struct {
    TfpVisIdentity owner;
    double body, rig, weapon, weapon_unit, record, world, component;
    uint8_t descriptor[20];
    uint8_t *appearance; size_t appearance_len;
    TfpModelInspected *inspected; size_t inspected_count;
  } fingerprint;
  /* The last probe's answer and work tables (owned here; valid until the next probe). */
  TfpModelRow *rows; size_t row_count, row_room;
  uint8_t *text; size_t text_len, text_room;
  TfpModelInspected *inspected; size_t inspected_count, inspected_room;
  TfpModelClosing *closing; size_t closing_count, closing_room;
  double visited[40]; size_t visited_count;
  TfpModelObservation *observations; size_t observation_count, observation_room;
  TfpVisText error;
  void *jump;
} TfpModelShadow;

/* The appearance rows in the avatar owner: retail 26568081 and HellForge's build 25480438
 * (docs/build-25480438/BUILD-DATA-25480438.md; the same number as TfpHeadBuild.appearance). */
#define TFP_VIS_APPEARANCE_RETAIL 0x546ab0
#define TFP_VIS_APPEARANCE_25480438 0x546a70
/* M.new(backend). `appearance`: one of the two numbers above. */
void tfp_model_shadow_init(TfpModelShadow *m, const TfpModelShadowIo *io, double appearance);
void tfp_model_shadow_free(TfpModelShadow *m);
/* self:probe(expected, appearance). true: m->rows / m->row_count hold the rows and
 * *text / *text_len the signature. false: the Lua returned nil and *text is tostring(error). */
bool tfp_model_shadow_probe(TfpModelShadow *m, const TfpVisSnapshot *expected, const uint8_t *appearance, size_t appearance_len,
                            const uint8_t **text, size_t *text_len);
bool tfp_model_shadow_has_observation(const TfpModelShadow *m);
/* self:unchanged(s, appearance, inspect). `inspect` NULL: not a function. `why` (may be NULL): the second result. */
bool tfp_model_shadow_unchanged(TfpModelShadow *m, const TfpVisSnapshot *s, const uint8_t *appearance, size_t appearance_len,
                                TfpModelInspect inspect, void *inspect_context, TfpVisText *why);
/* self:valid(expected, owner). */
bool tfp_model_shadow_valid(TfpModelShadow *m, const TfpVisSnapshot *expected, const TfpModelOwner *owner);

/* Shared with head_visibility.c: tostring(number) as the reference prints it ("%.14g"; nan, inf, -inf). */
size_t tfp_vis_number(double value, char *out, size_t size);
bool tfp_vis_identity_equal(const TfpVisIdentity *a, const TfpVisIdentity *b);
#endif
