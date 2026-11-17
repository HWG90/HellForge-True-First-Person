#ifndef TFP_RUNTIME_ORDER_H
#define TFP_RUNTIME_ORDER_H
/* Retail src/first_person/runtime.lua (True First Person 0.5.41-smooth.66, kit commit 1d0d453) in C:
 * the per-update order that drives every other module of the mode, with the state runtime.lua owns
 * and nothing else. Every module it calls is reached through TfpRtHost (tfp_runtime_host.h), one
 * function pointer per collaborator method, so this file includes no other module's header.
 * docs/RUNTIME-TRANSLATION.md has the binding table. (src/runtime.h is the older 0.5.34 plumbing
 * and is unrelated.)
 *
 * runtime.lua is dynamically typed and passes many values on without looking at them, so what
 * crosses the host boundary is a small tagged value. A text or a ref handed to the runtime must
 * stay readable until the outermost tfp_rt_call that received it returns; what the runtime keeps
 * longer it copies. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum TfpRtKind { TFP_RT_NIL, TFP_RT_FALSE, TFP_RT_TRUE, TFP_RT_NUMBER, TFP_RT_TEXT, TFP_RT_REF } TfpRtKind;
typedef struct TfpRtValue {
  TfpRtKind kind;
  double number;    /* NUMBER */
  const char *text; /* TEXT: length bytes (may hold zero bytes; need not end in one) */
  size_t length;
  const void *ref;  /* REF: a Lua table (or other object) known only by its identity */
} TfpRtValue;

/* A Lua table whose fields runtime.lua reads: the fields of one of the kinds of tfp_runtime_host.h
 * (TFP_RT_<KIND>_<FIELD>), and its identity. present = false is nil. */
enum { TFP_RT_FIELDS = 20 };
typedef struct TfpRtTable {
  bool present;
  const void *ref;
  TfpRtValue f[TFP_RT_FIELDS];
} TfpRtTable;

/* The Update scope ({phase='qualified_update', tick=n} in Lua, compared by identity). */
typedef struct TfpRtContext {
  uint64_t serial;
  double tick;
} TfpRtContext;

#include "tfp_runtime_host.h"

/* Tables runtime.lua builds itself and hands on as a value: the REF of such a value points here. */
extern const double tfp_rt_camera_baseline[3];   /* {x=0,y=0,z=.04} */
extern const double tfp_rt_default_placement[3]; /* {x=.15,y=0,z=-.12} */
extern const char tfp_rt_empty_table;            /* {} */

typedef enum TfpRtEntry {
#define TFP_RT_ENTRY_ID(name, stage, nret) TFP_RT_E_##name,
  TFP_RT_ENTRY_LIST(TFP_RT_ENTRY_ID)
#undef TFP_RT_ENTRY_ID
  TFP_RT_E_COUNT
} TfpRtEntry;

typedef struct TfpRtArgs {
  TfpRtValue v[5];
  const TfpRtTable *table;     /* NULL: nil */
  const TfpRtContext *context; /* NULL: nil */
} TfpRtArgs;

typedef enum TfpRtStatus {
  TFP_RT_OK,     /* returned; the answers are in ret */
  TFP_RT_RAISED, /* raised; the value is rt->error */
  TFP_RT_ABSENT  /* runtime.lua has not created this function (yet, or any more) */
} TfpRtStatus;

/* A table the runtime keeps across calls, with its texts copied. */
enum { TFP_RT_KEPT_TEXT = 64 };
typedef struct TfpRtKept {
  TfpRtTable table;
  char text[TFP_RT_FIELDS][TFP_RT_KEPT_TEXT];
} TfpRtKept;

typedef struct TfpRtOwner {
  bool present;
  TfpRtValue identity, body, world, frame, cleaned_at;
  char text[3][TFP_RT_KEPT_TEXT];
} TfpRtOwner;

enum { TFP_RT_ARENA = 32768, TFP_RT_TEXT_MAX = 512 };
typedef struct TfpRt {
  TfpRtHost *host;
  /* runtime.lua's module-level variables, under their names. */
  bool has[TFP_RT_O_N]; /* controller, menu, ... : not nil */
  bool stopping;
  TfpRtValue idle_clean, handoff_ready, recovery_enabled;
  char recovery_enabled_text[TFP_RT_KEPT_TEXT];
  TfpRtValue fp_dof_player;
  char fp_dof_text[TFP_RT_KEPT_TEXT];
  double fird_tick, ready_tick;
  TfpRtValue fird_capture_phase;
  bool has_context; /* ready_context */
  TfpRtContext context;
  uint64_t context_serial;
  TfpRtValue diagnostics_fault;
  char fault_text[TFP_RT_TEXT_MAX];
  TfpRtValue diagnostics_look_at;
  TfpRtValue diagnostics_look; /* NIL, FALSE (the read raised) or REF with look */
  TfpRtKept look;
  TfpRtOwner recovery_owner, owner_draining;
  TfpRtValue aim_button, aim_mode_button; /* NIL not looked up yet, FALSE none, NUMBER */
  bool aim_button_held, aim_mode_held, seated_view_held;
  /* Locals of on_enable that its functions share. */
  bool made[TFP_RT_STAGE_FIRDENTRY + 1]; /* which of the handed-out functions exist */
  TfpRtValue clock_previous;
  int edit_mode;  /* 0 'weapon', 1 'camera' */
  int store_mode; /* store.select_mode: 0 never called, 1 'weapon', 2 'camera' */
  TfpRtValue probe_deadline;
  bool entry_initialize; /* fird_entry was given an initialize function */
  bool has_candidate, has_hip_capture;
  TfpRtKept candidate, hip_capture;
  TfpRtValue candidate_time;
  /* Raising and texts. */
  TfpRtValue error;
  void *frame;
  int depth;
  size_t arena_used;
  char arena[TFP_RT_ARENA];
} TfpRt;

void tfp_rt_init(TfpRt *rt, TfpRtHost *host);
/* Calls one of the functions runtime.lua hands out (TFP_RT_ENTRY_LIST): the four of the module
 * table (on_enable, on_update, on_disable, on_cleanup_poll) and every closure it gives to a
 * collaborator, the menu or the debug api. ret takes that entry's number of values. May be called
 * from inside a host call (the collaborators call back), as in Lua. */
TfpRtStatus tfp_rt_call(TfpRt *rt, TfpRtEntry entry, const TfpRtArgs *args, TfpRtValue *ret);
const char *tfp_rt_entry_name(TfpRtEntry entry);
int tfp_rt_entry_values(TfpRtEntry entry);

#endif
