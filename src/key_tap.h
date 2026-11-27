/* Round 50: a key tapped between two game updates is still a press.
 *
 * The mod's keys (the toggle key, F10, the F11 notice, the placement editor's key) are level-polled once per game update
 * (GetAsyncKeyState's high bit) and acted on at the press edge. At 10 to 30 updates a second (a loading hitch, a heavy
 * mission) a quick tap begins and ends between two polls and no level ever shows it: in three harness sessions
 * (~/.cache/tfp-nest1, tfp-nest2, tfp-nest2b) about half the 80 ms taps of U, F10 and F11 were lost that way, while a
 * key the Goose Overlay Host took from the window's own key message never was.
 *
 * So each polled key also has a "pressed since the last read" answer, from two sources (src/wire_win.c):
 *   - GetAsyncKeyState's low bit. Windows documents it as unreliable because any other caller of GetAsyncKeyState for
 *     the same key (another thread, another process) takes it first; then a tap is missed as before, never invented.
 *     Upstream Wine keeps the bit in its server. Proton does not: GE-Proton11-12's win32u (NtUserGetAsyncKeyState,
 *     files/lib/wine/x86_64-unix/win32u.so+0xc3eb0, and Valve's wine dlls/win32u/input.c) answers 1 only when the key is
 *     down now and was not at this process's previous call, so under Proton the bit never shows a tap that is over.
 *   - the game window's own key messages: a fresh WM_KEYDOWN (not a repeat) seen by a keyboard hook on the window's
 *     thread sets a latch the next read takes. This is how the host's panel hotkeys arrive (overlay-host src/host.cpp
 *     HostProc), and the only source that works under Proton. The host itself has no shortcut registration to use
 *     (goose_overlay.h, API 8: register_panel's hotkey toggles a window; key_down / key_pressed are for draw callbacks).
 *
 * "Since the last read" alone is not a press now. It is acted on only when all of this holds:
 *   - the key is up now (a key that is down is the level's business, and its edge is the consumers' as before)
 *   - the key was read at the previous update and was up then: a key that was held is never "released into" a press
 *     (its auto-repeat also sets the bit), and a key not watched for a while has an old answer waiting
 *   - the game window is in front now and was at the previous update, and that update is at most TFP_KEY_TAP_GAP_MS ago
 *   - the key was not down, and gave no tap, in the last TFP_KEY_TAP_QUIET_MS: the key message of a press the level
 *     already showed can arrive an update late (the window's thread behind the keyboard), and with both sources one tap
 *     could be answered twice
 * so a tap gives one press, a held key gives one edge, and what is missed is missed as it was before this round.
 *
 * A consumer turns a tap into its press with its own edge keeper: down then up within one update (see
 * tfp_key_guard_toggle, tfp_f11_hint_tick, tfp_wire_panel_tick), so "just bound, not yet seen up" still refuses it.
 *
 * Header only, no Windows: src/wire_win.c gives the raw read, tests/key_tap_test.c a scripted keyboard. */
#ifndef TFP_KEY_TAP_H
#define TFP_KEY_TAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TFP_KEY_TAP_QUIET_MS 400u
#define TFP_KEY_TAP_GAP_MS 2000u

enum { TFP_KEY_RAW_DOWN = 1,    /* down now (GetAsyncKeyState's high bit) */
       TFP_KEY_RAW_SINCE = 2 }; /* pressed since the last raw read of this key (the low bit, or a key message) */
enum { TFP_KEY_DOWN = 1,        /* down now */
       TFP_KEY_TAPPED = 2 };    /* up now, pressed and let go since the previous update: one press, answered once */
/* One raw read of a key: TFP_KEY_RAW_* bits. Reading takes the "since" answer. */
typedef unsigned (*TfpKeyRawFn)(void *user, int vk);

typedef struct TfpKeyTaps {
  uint32_t tick;            /* updates begun; 0 before the first */
  uint64_t now_ms;
  bool focused, was_focused;
  uint32_t read_tick[256];  /* the update of the key's last read (0: never) */
  uint8_t state[256];       /* TFP_KEY_DOWN, TFP_KEY_TAPPED as its last read answered; 4: busy_ms is set */
  uint64_t busy_ms[256];    /* when the key was last down, or last gave a tap */
  unsigned taps;            /* taps answered, in all */
  int last_tap;             /* the key of the last one */
} TfpKeyTaps;

/* Once per game update, before any read. now_ms: a millisecond clock that does not go back. */
static inline void tfp_key_taps_begin(TfpKeyTaps *t, uint64_t now_ms, bool focused) {
  const bool recent = t->tick && now_ms - t->now_ms <= TFP_KEY_TAP_GAP_MS;
  t->was_focused = t->focused && recent;
  t->focused = focused, t->now_ms = now_ms;
  if (!++t->tick) t->tick = 2; /* wrapped: never 0 (never read) and 1 has no previous update */
}
/* The key at this update: TFP_KEY_DOWN, TFP_KEY_TAPPED or 0. One raw read per key and update; asked again in the same
 * update, the same answer. The level is the keyboard's whether or not the game is in front (the caller's to refuse). */
static inline unsigned tfp_key_taps_read(TfpKeyTaps *t, int vk, TfpKeyRawFn raw, void *user) {
  if (vk <= 0 || vk > 255 || !t->tick) return 0;
  if (t->read_tick[vk] == t->tick) return t->state[vk] & (TFP_KEY_DOWN | TFP_KEY_TAPPED);
  const unsigned r = raw(user, vk);
  const bool down = (r & TFP_KEY_RAW_DOWN) != 0, since = (r & TFP_KEY_RAW_SINCE) != 0;
  const bool was_read = t->tick > 1 && t->read_tick[vk] == t->tick - 1, was_down = (t->state[vk] & TFP_KEY_DOWN) != 0;
  const bool busy = (t->state[vk] & 4) && t->now_ms - t->busy_ms[vk] <= TFP_KEY_TAP_QUIET_MS;
  const bool tapped = !down && since && was_read && !was_down && t->focused && t->was_focused && !busy;
  uint8_t state = (uint8_t)(t->state[vk] & 4);
  if (down || tapped) t->busy_ms[vk] = t->now_ms, state |= 4;
  if (down) state |= TFP_KEY_DOWN;
  if (tapped) state |= TFP_KEY_TAPPED, t->taps++, t->last_tap = vk;
  t->state[vk] = state, t->read_tick[vk] = t->tick;
  return state & (TFP_KEY_DOWN | TFP_KEY_TAPPED);
}
#endif
