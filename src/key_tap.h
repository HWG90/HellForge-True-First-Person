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
 * "Since the last read" alone is not a press now. A tap is answered only when all of this holds:
 *   - the key is up now (a key that is down is the level's business, and its edge is the consumers' as before)
 *   - the key was read at the previous update and was up then: a key that was held is never "released into" a press
 *     (its auto-repeat also sets the bit), and a key not watched for a while has an old answer waiting
 *   - the game window is in front now and was at the previous update, and that update is at most TFP_KEY_TAP_GAP_MS ago
 * and one of the two sources says so, each by its own rule (round 50b; they were one "since" bit before, and the
 * independent check found a press answered twice and a key typed into another window answered):
 *   - a key message, when the hook took it more than TFP_KEY_TAP_LATE_MS after this reader last answered the key
 *     without one (the level seen down, or a tap from the low bit). The message of a press arrives when the window's
 *     thread gets to it, which can be after the update that saw the key down, and after the one that saw it up again;
 *     then it is that press's own message and no second press. So the hook stamps each message with the time it took
 *     it, and the message is judged by that time, not by when an update gets to read it: a hitch between the message
 *     and its read changes nothing (it did, while the quiet time ran from the update: one press toggled twice). The
 *     bound is the window's thread, not the game's updates: a message taken later than TFP_KEY_TAP_LATE_MS after its
 *     press was shown is a second press still. What is given up: a tap in that time after a press the level showed.
 *     (Counting messages against presses instead, with no time bound, does not work: the messages of earlier presses
 *     that no update acted on are still to come, and cannot be told from this press's.)
 *   - the low bit, only while the hook has never seen a key message (it was refused, or the window's thread gets
 *     none), only when the previous update is at most TFP_KEY_TAP_LOW_GAP_MS ago, and not within TFP_KEY_TAP_QUIET_MS
 *     of the key being down or of a tap. The bit is every window's: across a longer gap the player can have left,
 *     typed the key elsewhere and come back, with the game in front at both updates. Key messages are the game
 *     window's own, so they keep the long gap.
 * so a tap gives one press, a held key gives one edge, one press never gives two, and what is missed is missed as it
 * was before this round.
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
#define TFP_KEY_TAP_LOW_GAP_MS 250u  /* the low bit: too short to leave the game, type the key and come back */
#define TFP_KEY_TAP_LATE_MS 750u     /* a press's key message is taken at most this long after the press was shown */

enum { TFP_KEY_RAW_DOWN = 1,      /* down now (GetAsyncKeyState's high bit) */
       TFP_KEY_RAW_LOW = 2,       /* GetAsyncKeyState's low bit: pressed since the last raw read of this key, anywhere */
       TFP_KEY_RAW_MESSAGE = 4 }; /* the game window took a fresh key-down message for it since the last raw read */
enum { TFP_KEY_DOWN = 1,        /* down now */
       TFP_KEY_TAPPED = 2 };    /* up now, pressed and let go since the previous update: one press, answered once */
/* One raw read of a key: TFP_KEY_RAW_* bits; with TFP_KEY_RAW_MESSAGE, *message_ms: when the last such message was
 * taken, on tfp_key_taps_begin's clock. Reading takes the low bit and the messages. */
typedef unsigned (*TfpKeyRawFn)(void *user, int vk, uint64_t *message_ms);

typedef struct TfpKeyTaps {
  uint32_t tick;            /* updates begun; 0 before the first */
  uint64_t now_ms;
  bool focused, was_focused;
  bool short_gap;           /* the previous update is at most TFP_KEY_TAP_LOW_GAP_MS ago */
  bool hooked;              /* the hook has seen a key message (any key): the low bit is not asked any more. The caller
                               sets it; a read that is given a message sets it too */
  uint32_t read_tick[256];  /* the update of the key's last read (0: never) */
  uint8_t state[256];       /* TFP_KEY_DOWN, TFP_KEY_TAPPED as its last read answered; 4: busy_ms is set; 8: shown_ms is */
  uint64_t busy_ms[256];    /* when the key was last down, or last gave a tap */
  uint64_t shown_ms[256];   /* when the key was last down, or last gave a tap from the low bit: a press whose own
                               message may still come */
  unsigned taps;            /* taps answered, in all */
  unsigned late;            /* key messages that belonged to a press already answered, in all */
  int last_tap;             /* the key of the last one */
} TfpKeyTaps;

/* Once per game update, before any read. now_ms: a millisecond clock that does not go back. */
static inline void tfp_key_taps_begin(TfpKeyTaps *t, uint64_t now_ms, bool focused) {
  const bool recent = t->tick && now_ms - t->now_ms <= TFP_KEY_TAP_GAP_MS;
  t->short_gap = t->tick && now_ms - t->now_ms <= TFP_KEY_TAP_LOW_GAP_MS;
  t->was_focused = t->focused && recent;
  t->focused = focused, t->now_ms = now_ms;
  if (!++t->tick) t->tick = 2; /* wrapped: never 0 (never read) and 1 has no previous update */
}
/* The key at this update: TFP_KEY_DOWN, TFP_KEY_TAPPED or 0. One raw read per key and update; asked again in the same
 * update, the same answer. The level is the keyboard's whether or not the game is in front (the caller's to refuse). */
static inline unsigned tfp_key_taps_read(TfpKeyTaps *t, int vk, TfpKeyRawFn raw, void *user) {
  if (vk <= 0 || vk > 255 || !t->tick) return 0;
  if (t->read_tick[vk] == t->tick) return t->state[vk] & (TFP_KEY_DOWN | TFP_KEY_TAPPED);
  uint64_t message_ms = 0;
  const unsigned r = raw(user, vk, &message_ms);
  const bool down = (r & TFP_KEY_RAW_DOWN) != 0, low = (r & TFP_KEY_RAW_LOW) != 0, message = (r & TFP_KEY_RAW_MESSAGE) != 0;
  const bool was_read = t->tick > 1 && t->read_tick[vk] == t->tick - 1, was_down = (t->state[vk] & TFP_KEY_DOWN) != 0;
  const bool busy = (t->state[vk] & 4) && t->now_ms - t->busy_ms[vk] <= TFP_KEY_TAP_QUIET_MS;
  uint8_t state = (uint8_t)(t->state[vk] & 12);
  if (down) t->shown_ms[vk] = t->now_ms, state |= 8;   /* before the message is judged: it may be this very press's */
  if (message) t->hooked = true;
  /* a message no answered press is still waiting for: taken later than any of theirs can be */
  const bool fresh = message && !((state & 8) && message_ms <= t->shown_ms[vk] + TFP_KEY_TAP_LATE_MS);
  if (message && !fresh && !down) t->late++;
  const bool may = !down && was_read && !was_down && t->focused && t->was_focused;
  const bool by_low = low && !t->hooked && t->short_gap && !busy;
  const bool tapped = may && (fresh || by_low);
  if (tapped && !fresh) t->shown_ms[vk] = t->now_ms, state |= 8;   /* the low bit's tap: its message may still come */
  if (down || tapped) t->busy_ms[vk] = t->now_ms, state |= 4;
  if (down) state |= TFP_KEY_DOWN;
  if (tapped) state |= TFP_KEY_TAPPED, t->taps++, t->last_tap = vk;
  t->state[vk] = state, t->read_tick[vk] = t->tick;
  return state & (TFP_KEY_DOWN | TFP_KEY_TAPPED);
}
#endif
