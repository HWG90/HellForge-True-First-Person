/* Where the level of the aim button comes from.
 *
 * Retail asks the engine: stingray.Mouse.button(Mouse.button_index('right')) (runtime.lua:395-421), so
 * it sees a release in the same update as the game does, and the aim-view keeper (backend.lua:1340)
 * clears the controller's active byte before the game acts on it a frame later. Until 0.6.0-feel.6
 * this build asked Windows instead (GetAsyncKeyState), which is sampled at another moment: in the
 * session of 2026-11-18 (log b4a88652) 8 of 18 aim releases were seen only after the game had
 * already dropped the native view, each a cut of 0.24 to 0.94 m.
 *
 * The engine is used once its two functions are proven to be this build's own (the addresses of the
 * live census, docs/history/research-0.5.34/live-sdk-census-g1-9472b065.tsv) and the button's index
 * is a number; until then, and whenever a call fails, the answer is Windows' and the source says so.
 *
 * 0.6.0-feel.8 ran that way (session 76bf7718): all 6 aim releases were seen after the game had dropped the
 * view, and 5 presses were taken for releases. The engine's level, read in HellForge's tick before the game's
 * update, is a frame behind the game's own input on build 25480438. windows_first puts Windows back in front.
 * Platform-free: the engine and Windows come in as function pointers. */
#ifndef TFP_WIRE_MOUSE_H
#define TFP_WIRE_MOUSE_H
#include <stdbool.h>
#include <stdint.h>

#define TFP_MOUSE_BUTTON_INDEX_RVA 0x4830c0u /* exe: Mouse.button_index and Mouse.button_id (shared with Keyboard's) */
#define TFP_MOUSE_BUTTON_RVA       0x482480u /* exe: Mouse.button */

typedef struct TfpWireMouseHost TfpWireMouseHost;
struct TfpWireMouseHost {
  void *user;
  bool (*engine_ready)(void *user);
  /* The C function registered as stingray.<path>, 0: none. */
  uint64_t (*engine_function)(void *user, const char *path);
  uint64_t (*exe_base)(void *user);
  /* Mouse.button_index / button_id (name): false: the call failed; *has false: not a number. */
  bool (*engine_index)(void *user, const char *path, const char *name, bool *has, double *index);
  /* Mouse.button(index): false: the call failed; *has false: not a number. */
  bool (*engine_button)(void *user, double index, bool *has, double *level);
  uint64_t (*engine_generation)(void *user); /* changes when the engine's Lua state is replaced */
  int (*windows_button)(void *user, int button); /* 1 down, 0 up, -1 no answer */
};

typedef enum TfpWireMouseSource { TFP_MOUSE_UNKNOWN, TFP_MOUSE_ENGINE, TFP_MOUSE_WINDOWS } TfpWireMouseSource;
typedef struct TfpWireMouse {
  TfpWireMouseHost host;
  /* Set after init by a build where the engine's level arrives too late for the aim-view keeper (25480438,
   * src/wire_action.h): Windows answers first, the engine only when Windows gives no answer. */
  bool windows_first;
  bool proven, refused;       /* refused: the engine's functions are not this build's; Windows for the session */
  uint64_t generation;
  bool has_index[3], tried[3]; double index[3];   /* by button: 1 right, 2 middle */
  const char *index_path;     /* Mouse.button_index, or Mouse.button_id where the first is missing */
  TfpWireMouseSource source;  /* of the last answer */
  unsigned engine_failures;
  char note[160];             /* why the engine is not used; empty when it is */
  bool note_fresh;            /* set when note changed; the caller logs it once and clears it */
} TfpWireMouse;

void tfp_wire_mouse_init(TfpWireMouse *m, const TfpWireMouseHost *host);
/* The level of button 1 (right) or 2 (middle): 1 down, 0 up, -1 no answer. */
int tfp_wire_mouse_level(TfpWireMouse *m, int button);
const char *tfp_wire_mouse_source_name(const TfpWireMouse *m);
#endif
