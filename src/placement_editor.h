/* The placement editor: retail src/first_person/placement_editor.lua (0.5.41-smooth.66, 1d0d453) in C.
 *
 * A draft placement per weapon and per action, previewed live through the controller, saved for this
 * weapon or all weapons, reset to the default, cancelled when the weapon changes or anything else takes
 * the cursor. Everything the Lua file reaches goes through TfpEdHost: one `call` for every function the
 * Lua calls (in retail's order; tests/vectors/placement_editor_vectors.h holds retail's own answers and
 * call traces) and one `look` for the plain table fields it reads without calling anything.
 *
 * Kept from retail as it is: the panel's geometry and its buttons (the editor hit-tests its own
 * rectangles), the mouse drags, the keys, every text. The panel is handed to the host as a list of
 * rectangles and texts with a colour role each; the host draws it (docs/guides/POSER-AND-MENU.md).
 *
 * Scripted domain (what the vectors cover; outside it the C is not compared with retail):
 *   - controller, store.get, store.get_action, store.save and the provider's methods exist when called;
 *   - a placement's fields are numbers or missing; a weapon key / owner is nil, a boolean, a number or text;
 *   - a "why" is nil, a boolean, a number or text. */
#ifndef TFP_PLACEMENT_EDITOR_H
#define TFP_PLACEMENT_EDITOR_H
#include <stdbool.h>
#include <stddef.h>

typedef enum TfpEdKind { TFP_ED_NIL, TFP_ED_FALSE, TFP_ED_TRUE, TFP_ED_NUMBER, TFP_ED_TEXT, TFP_ED_TABLE } TfpEdKind;
typedef struct TfpEdValue { TfpEdKind kind; double n; char s[200]; } TfpEdValue;
/* x, y, z (metres), yaw, pitch, roll (degrees). has[i] false: the field is nil. */
typedef struct TfpEdPlace { bool has[6]; double v[6]; } TfpEdPlace;

typedef enum TfpEdCall {
  TFP_ED_CLOCK,                 /* monotonic(): a = the value */
  TFP_ED_C_IS_CLEAN, TFP_ED_C_IS_ENABLED,   /* a */
  TFP_ED_C_RESULT,              /* a TABLE with a.s = result.phase ("" when it is not text), or another value */
  TFP_ED_C_SET_PLACEMENT,       /* args.place; a, b = accepted, why */
  TFP_ED_C_GET_PLACEMENT,       /* a TABLE + place, or a, b = value, why */
  TFP_ED_C_START,               /* controller:start(0, 'pose_offset'); a, b */
  TFP_ED_S_SELECT_MODE,         /* args.text = kind; a, b */
  TFP_ED_S_CAN_START,           /* a, b */
  TFP_ED_S_TARGET,              /* a, b = key, owner */
  TFP_ED_S_GET,                 /* as C_GET_PLACEMENT */
  TFP_ED_S_GET_ACTION,          /* args.key, args.text = action; a TABLE + place, or a */
  TFP_ED_S_SAVE,                /* args.place, key, owner, flag = all weapons, text = action; a, b */
  TFP_ED_S_RESET_ALL,           /* a, b */
  TFP_ED_S_BASELINE,            /* args.key, args.text = action; a TABLE + place, or a */
  TFP_ED_S_ACTION_SUPPORTED,    /* args.text = action; a */
  TFP_ED_LUT_STATUS,            /* epic.capture.status(): a TABLE + status, or a */
  TFP_ED_W_MOUSE_FOCUS, TFP_ED_W_SHOW_CURSOR, /* stingray.Window: a */
  TFP_ED_A_CLOSE,               /* provider.close(): a, b */
  TFP_ED_A_IS_OPEN,             /* a */
  TFP_ED_A_INPUT_STATUS,        /* a TABLE + status, or a */
  TFP_ED_A_RESOLUTION,          /* provider.photo_resolution(): a, b = width, height */
  TFP_ED_A_CREATE_VIEW,         /* a TABLE + view, or a; b = why */
  TFP_ED_I_ACQUIRE, TFP_ED_I_RELEASE, /* a, b */
  TFP_ED_I_SET_FOCUS,           /* camera_input.set_focus(true): a, b */
  TFP_ED_I_SAMPLE,              /* camera_input.menu_sample(): a TABLE + sample, or a; b = why */
  TFP_ED_I_DOWN,                /* args.number = virtual key; a */
  TFP_ED_I_FOCUSED,             /* a */
  TFP_ED_V_DRAW,                /* args.commands, args.ncommands; a, b */
  TFP_ED_V_RELEASE,             /* a, b */
  TFP_ED_V_REFRESH_FONT,
  TFP_ED_CALLS
} TfpEdCall;

typedef enum TfpEdColor { TFP_ED_COLOR_PANEL, TFP_ED_COLOR_FIELD, TFP_ED_COLOR_TEXT, TFP_ED_COLOR_ACCENT, TFP_ED_COLOR_TEXT_DIM } TfpEdColor;
/* One drawn item, in retail's units: pixels, origin at the bottom left, y up. A text's x, y is its
 * lower left corner and `size` its height. panel_alpha: retail's opacity.panel instead of solid. */
typedef struct TfpEdCommand {
  bool is_text, panel_alpha;
  double x, y, w, h, size;
  TfpEdColor color;
  int layer;
  char text[400];
} TfpEdCommand;
enum { TFP_ED_COMMANDS = 32, TFP_ED_ACTIONS = 24 };

typedef struct TfpEdArgs {
  TfpEdPlace place;
  TfpEdValue key, owner;
  const char *text;
  bool flag;
  double number;
  const TfpEdCommand *commands;
  int ncommands;
} TfpEdArgs;
typedef struct TfpEdReply {
  TfpEdValue a, b;
  TfpEdPlace place;                                             /* with a TABLE answer of a placement call */
  struct { TfpEdValue owner; bool active, pending_restore; } status; /* input_status / capture.status: truthiness, and owner's text */
  struct { bool has_draw, has_release, has_refresh_font; } view;
  /* menu_sample: click == true; x, y, wheel, dt are NAN when the field is not a finite number. */
  struct { bool click; double x, y, wheel, dt; } sample;
} TfpEdReply;

/* The plain fields the Lua reads without a call. Asked again at every place retail reads one. */
typedef struct TfpEdLook {
  /* store */
  bool has_select_mode, has_can_start, has_target, has_action_supported, reset_all, single_save, rotation_false;
  int baseline;                                                 /* 0 nil (or not a placement), 1 a table (baseline_place), 2 a function */
  TfpEdPlace baseline_place;
  const char *title, *save_label, *all_label, *footer;          /* NULL: nil */
  int nactions;                                                 /* -1: store.actions is nil */
  const char *action_id[TFP_ED_ACTIONS], *action_label[TFP_ED_ACTIONS];
  /* package.loaded['dbf.epic_lut.frontend.v1'] */
  bool lut, lut_closed, lut_menu_visible, lut_was_visible, lut_capture, lut_capture_active, lut_capture_status;
  /* stingray.Window */
  bool window, window_mouse_focus, window_show_cursor;
  /* Integration.editor_api(): 0 nil, else its identity; whether it has the shape supported() asks for;
   * the identity of its camera_input (0: none). */
  unsigned provider, provider_input;
  bool provider_supported;
} TfpEdLook;

typedef struct TfpEdHost {
  void *user;
  /* false: the function raised, reply->a holds the error text. */
  bool (*call)(void *user, TfpEdCall which, const TfpEdArgs *args, TfpEdReply *reply);
  void (*look)(void *user, TfpEdLook *out);
  void (*log)(void *user, const char *text);                    /* NULL: retail's `log or function() end` */
} TfpEdHost;

typedef struct TfpEditor TfpEditor;
/* What a public function answered: retail's first two results. raised: the Lua raised (r1 = its text). */
typedef struct TfpEdResult { bool raised; TfpEdValue r1, r2; } TfpEdResult;

TfpEditor *tfp_editor_new(const TfpEdHost *host);               /* M.new(controller, store, log, monotonic) */
void tfp_editor_free(TfpEditor *e);
bool tfp_editor_is_active(const TfpEditor *e);
bool tfp_editor_is_clean(const TfpEditor *e);
const char *tfp_editor_status(const TfpEditor *e);
const char *tfp_editor_phase(const TfpEditor *e);               /* self.phase */
const char *tfp_editor_kind(const TfpEditor *e);                /* self.kind; NULL: nil */
const char *tfp_editor_preview_action(const TfpEditor *e);
bool tfp_editor_preview_rotation(const TfpEditor *e, double out[3]); /* false: nil */
TfpEdResult tfp_editor_request_open(TfpEditor *e, const char *kind); /* kind NULL: nil */
TfpEdResult tfp_editor_cancel(TfpEditor *e, const char *reason);
TfpEdResult tfp_editor_finish(TfpEditor *e, bool all_weapons);
TfpEdResult tfp_editor_shutdown(TfpEditor *e);
TfpEdResult tfp_editor_update(TfpEditor *e);
/* Not in retail: whether a draft differs from what the editor opened with (the live-replace check). */
bool tfp_editor_holds_draft(const TfpEditor *e);
#endif
