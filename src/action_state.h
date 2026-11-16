/* The body's animation state-machine layers, read from copied game memory.
 * Translation of retail src/first_person/action_state.lua at 0.5.41-smooth.66
 * (commit 1d0d453), on game build 25480438;
 * tests/vectors/action_state_vectors.h holds the retail file's own answers to
 * the same scenes. Line numbers in action_state.c are the Lua file's.
 *
 * Reads only. What the Lua module takes from its backend comes through one
 * struct: the memory reader (backend:_read, _ptr, _u32) and two methods
 * (backend:player_snapshot, backend:_alignment_context). This file also
 * carries the translation of those two backend methods on this build
 * (backend.lua:330-334 through src/snapshot.c, and backend.lua:688-725 and
 * 783-803), because locomotion needs them too and no other file has them yet. */
#ifndef TFP_ACTION_STATE_H
#define TFP_ACTION_STATE_H
#include "snapshot.h"

enum { TFP_ACTION_STATE_LAYERS = 32, TFP_ACTION_STATE_STATES = 2048, TFP_ACTION_STATE_ERROR = 200 };

/* backend:_alignment_context(s), backend.lua:783-803: a Unit reference resolved
 * through the executable's unit registry. */
typedef struct TfpAlignmentContext {
  uintptr_t exe, object, registry, units, generations, owner;
  uint32_t unit, index, generation, avatar_index;
} TfpAlignmentContext;
/* backend:_alignment_executable(profile), backend.lua:688-725: memory->exe
 * checked as this build's helldivers2.exe. false with retail's text. */
bool tfp_alignment_executable(const FpMemory *memory, uintptr_t *exe, char *error, size_t error_size);
bool tfp_alignment_context(const FpMemory *memory, uint32_t viewmodel, uintptr_t avatar_owner, uint32_t avatar_index,
                           TfpAlignmentContext *out, char *error, size_t error_size);

typedef struct TfpActionStateBackend {
  const FpMemory *memory;
  void *user;
  /* backend:player_snapshot(): true and *out, or false and retail's text. */
  bool (*player_snapshot)(void *user, TfpSnapshot *out, char *error, size_t error_size);
  /* backend:_alignment_context({viewmodel=..., avatar_owner=..., avatar_index=...}) */
  bool (*alignment_context)(void *user, uint32_t viewmodel, uintptr_t avatar_owner, uint32_t avatar_index,
                            TfpAlignmentContext *out, char *error, size_t error_size);
} TfpActionStateBackend;
/* The backend of this repository: tfp_snapshot(TFP_SNAPSHOT_PLAYER) and
 * tfp_alignment_context over `memory`, which must outlive the backend. */
void tfp_action_state_backend(TfpActionStateBackend *backend, const FpMemory *memory);

/* The sampler (`self` and its `cache` upvalue). Retail keeps, per layer, the
 * map from state address to state index while the layer's definition address
 * and state count are unchanged; a changed entry behind an unchanged
 * definition is NOT noticed, and that is kept. About 260 KB: do not put it on
 * a small stack. */
typedef struct TfpActionStateReader {
  bool cached;
  uintptr_t body, instance, graph;         /* cache.body (the object), cache.instance, cache.graph */
  uint32_t ref, generation;                /* cache.ref (the Unit reference), cache.generation */
  uint8_t resource[8], header[12], definitions[TFP_ACTION_STATE_LAYERS * 4];
  uint32_t definition_count;               /* how many definition bytes / 4 the cache holds */
  struct { bool present; uintptr_t def; uint32_t count; } map[TFP_ACTION_STATE_LAYERS + 1];
  uint32_t offsets[TFP_ACTION_STATE_LAYERS + 1][TFP_ACTION_STATE_STATES]; /* state address = def + offset */
} TfpActionStateReader;
void tfp_action_state_init(TfpActionStateReader *reader);

typedef struct TfpActionState {
  TfpSnapshot player;                      /* owner: the snapshot whose player_identity retail returns */
  uint32_t body;
  uintptr_t body_object, graph, instance;
  uint32_t layer_count;
  char resource[17];                       /* the eight resource bytes, last first, upper-case hex */
  bool resource_match;
  bool has[TFP_ACTION_STATE_LAYERS + 1];   /* states[layer] is nil in retail unless the layer was asked for */
  int states[TFP_ACTION_STATE_LAYERS + 1]; /* 1-based: l1 = states[1], l3, l10, l11, l14 likewise */
  int unknown_layers[TFP_ACTION_STATE_LAYERS];
  unsigned unknown_count;
  bool sprint_hypothesis;
} TfpActionState;
extern const char tfp_action_state_sprint_evidence[];

/* self:sample(expected, layers). expected NULL: nil (the backend's player
 * snapshot is taken). layers NULL: nil, the default {1, 3, 14}. true and *out,
 * or false with retail's text in error (at least TFP_ACTION_STATE_ERROR
 * bytes; the Lua runtime's "file:line: " prefix is not part of it) and *out
 * untouched. */
bool tfp_action_state_sample(TfpActionStateReader *reader, const TfpActionStateBackend *backend, const TfpSnapshot *expected,
                             const int *layers, size_t layer_count, TfpActionState *out, char *error, size_t error_size);
#endif
