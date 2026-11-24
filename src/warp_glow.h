/* The Warp Pack's glow in first person (round 22; docs/design/specs/2026-11-24-warp-pack-glow.md).
 *
 * The glow is the pack's effect events, played by the game's displacement updater (game+0x88c130). The game stops them
 * itself only in full aim-down-sights (avatar flag bit 48), which TFP's view never has. Every tick on the game thread,
 * this module reads the pack's displacement row and mode table and which of its events play (by memory, the way
 * game+0x8abf40 answers), and says so in a "Warp:" line. With the test switch on (warp_hide.txt) and TFP owning the
 * view, it stops the playing events of the three stop lists the game's own full-ADS stop walks, with the game's stop
 * function and the game's arguments (no network message). On leaving the view, a dropped or changed pack, or the switch
 * going, it writes 0xffffffff to the pack's cached mode word, and the game's next update plays the current mode again.
 * It never writes bit 48 or the ADS cache byte, and does nothing while bit 48 is set (the game does it then).
 *
 * Memory and the one native call go through TfpOpsHost (guarded reads, HellForge's write, the SEH-guarded call), so the
 * tests run it on mocked memory. */
#ifndef TFP_WARP_GLOW_H
#define TFP_WARP_GLOW_H
#include "backend_ops.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_WARP_TYPE UINT64_C(0xB49756795D9B0E68) /* the Warp Pack's entity type (descriptor +0) */

/* game.dll RVAs and layouts, build 25480438 (the spec, sections 2 and 3). */
#define TFP_WARP_G_MANAGER   UINT32_C(0x3326ac8)  /* M: the displacement component manager */
#define TFP_WARP_G_EFFECTS   UINT32_C(0x3326570)  /* E: the entity effect manager */
#define TFP_WARP_G_WORLD     UINT32_C(0x346bf98)  /* the world (FP_WORLD) */
#define TFP_WARP_F_STOP      UINT32_C(0x8abb20)   /* void (E, entity, event, filter 0, replicate 0, flag 0) */
#define TFP_WARP_AVATAR_BIT48 UINT32_C(0x53e886)  /* avatar owner + this + index * 0x1238, bit 0 */
#define TFP_WARP_AVATAR_STRIDE UINT32_C(0x1238)

typedef struct TfpWarpIn {
  bool owns;               /* TFP owns the first-person view this tick (switched on, head visibility active) */
  bool armed;              /* the test switch (warp_hide.txt) */
  bool player;             /* a player snapshot was had this tick */
  uint64_t avatar_owner;   /* the snapshot's */
  uint32_t avatar_index;
  int backpack;            /* tfp_ops_backpack: 1 found, 0 none, -1 refused or not asked */
  uint32_t pack;           /* the backpack's entity (1) */
  uint8_t pack_type[8];    /* its descriptor bytes +0..+7 (1) */
  double now;              /* seconds */
} TfpWarpIn;

typedef struct TfpWarpModeRow { uint32_t tag, play[5], stop[5], extra[2]; } TfpWarpModeRow; /* settings + 0x1c0 + m*0x34 */

typedef enum TfpWarpDid {
  TFP_WARP_NOTHING,        /* no Warp Pack, or nothing to do */
  TFP_WARP_WATCHING,       /* the pack is read; the switch is off */
  TFP_WARP_NOT_OWNED,      /* the switch is on; the view is not TFP's */
  TFP_WARP_GAME_ADS,       /* bit 48 is set: the game's own full-ADS stop is in charge */
  TFP_WARP_QUIET,          /* owned and armed: nothing of the stop lists plays */
  TFP_WARP_STOPPED,        /* stops sent this tick */
  TFP_WARP_RESTORED,       /* the cached mode word was set to 0xffffffff this tick */
  TFP_WARP_REFUSED         /* a gate failed (why) */
} TfpWarpDid;

typedef struct TfpWarpRead {   /* what one tick read of the pack */
  bool ok;                     /* all of it was read */
  uint64_t manager, descriptor, settings, effects;
  uint32_t index, count;
  bool override_settings;      /* the settings came from M+0xa0 (else the type table) */
  uint8_t bit48, ads_cache;
  uint32_t mode_word;
  float charge;
  TfpWarpModeRow modes[3];
  bool in_effects;             /* E holds the pack */
  uint32_t ids[30]; unsigned id_count;     /* the table's distinct nonzero ids: stop lists first, then play lists */
  uint32_t playing[30]; unsigned playing_count;
} TfpWarpRead;

typedef struct TfpWarp {
  uint64_t pinned_base;        /* the game base the fingerprints were checked for (0: not yet) */
  bool pins_ok;
  char pin_failed[96];
  /* the restore owed: the pack stops were sent for */
  bool owed;
  uint32_t owed_pack, owed_index;
  uint64_t owed_manager, owed_descriptor;
  /* the log */
  uint64_t last_key, settings_key;
  double last_line;
  bool said_none;
  uint64_t stops, restores, stop_ticks;
  TfpWarpRead read;            /* the last tick's */
  TfpWarpDid did;
  unsigned did_stops;          /* stops this tick */
  uint32_t stopped[15]; unsigned stopped_count;
  char why[160];
} TfpWarp;

void tfp_warp_init(TfpWarp *w);
/* One tick, on the game thread (HellForge's tick, before the game's update). Writes at most two lines (a settings line
 * when the pack's table is first read or changes, then the "Warp:" line), each ended by '\n', into `text`; true when
 * there is any. */
bool tfp_warp_tick(TfpWarp *w, const TfpOpsHost *host, const TfpWarpIn *in, char *text, size_t size);
/* The mod stops (or the view is switched off for good): the restore, if one is owed. true: it was written. */
bool tfp_warp_release(TfpWarp *w, const TfpOpsHost *host);

/* The fingerprints (the spec, section 4), for the tests. */
typedef struct TfpWarpPin { const char *name; uint32_t rva; const char *hex; } TfpWarpPin;
extern const TfpWarpPin tfp_warp_pins[];
extern const size_t tfp_warp_pin_count;
#endif
