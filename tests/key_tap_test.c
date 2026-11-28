/* Round 50 (src/key_tap.h): a key tapped between two game updates is one press. A scripted keyboard stands in for
 * Windows: key events at their times, GetAsyncKeyState's two bits as Windows (and upstream Wine) or as Proton answers
 * them, and the game window's key messages with a delay. The consumers are the mod's own: the toggle key through
 * tfp_key_guard_toggle (src/key_guard.h, with the menu's TfpKeyEdge rule) and F11 through tfp_f11_hint_tick. */
#include "key_tap.h"
#include "key_guard.h"
#include "f11_hint.h"
#include <stdlib.h>
static int checks, failed;
#define CHECK(c) do { ++checks; if (!(c)) { ++failed; printf("FAILED line %d: %s\n", __LINE__, #c); } } while (0)

/* ---- the keyboard ---- */
enum { LOW_WINDOWS = 1,   /* the low bit: pressed since the last call, down or not */
       LOW_PROTON = 2,    /* the low bit: down now and not down at this process's last call (GE-Proton11-12) */
       MESSAGES = 4 };    /* a fresh WM_KEYDOWN is counted by the hook, message_delay ms after the press */
typedef struct Event { uint64_t at; int vk; bool down; } Event;
typedef struct Board {
  unsigned mode;
  uint64_t now, message_delay;
  Event events[64];
  int count;
  bool level[256], low[256], last_call_down[256];
  bool latch[256];        /* the hook took a message since the last read */
  uint64_t latch_ms[256]; /* when it took the last of them */
  uint64_t silent_before; /* presses before this time give no message (another window had the keyboard) */
  bool message_done[64];
  bool repeat;            /* a held key keeps setting the low bit (auto-repeat), as Wine's server does */
  bool thief;             /* another caller of GetAsyncKeyState takes the low bit before every read */
  unsigned raw_calls;
} Board;
static void ev(Board *b, uint64_t at, int vk, bool down) { b->events[b->count++] = (Event){at, vk, down}; }
static void tap(Board *b, uint64_t at, int vk, uint64_t ms) { ev(b, at, vk, true); ev(b, at + ms, vk, false); }
static void advance(Board *b, uint64_t now) {
  for (int i = 0; i < b->count; i++) {
    const Event *e = &b->events[i];
    if (e->at > b->now && e->at <= now) {
      if (e->down && !b->level[e->vk]) b->low[e->vk] = true;
      b->level[e->vk] = e->down;
    }
    if (e->down && (b->mode & MESSAGES) && e->at >= b->silent_before && !b->message_done[i] && e->at + b->message_delay <= now)
      b->message_done[i] = true, b->latch[e->vk] = true, b->latch_ms[e->vk] = e->at + b->message_delay;
  }
  b->now = now;
}
static unsigned raw(void *u, int vk, uint64_t *message_ms) {
  Board *b = u;
  b->raw_calls++;
  const bool down = b->level[vk];
  bool since = false;
  if (down && b->repeat) b->low[vk] = true;
  if (b->thief) b->low[vk] = false;
  if (b->mode & LOW_WINDOWS) since = b->low[vk];
  if (b->mode & LOW_PROTON) since = down && !b->last_call_down[vk];
  b->low[vk] = false, b->last_call_down[vk] = down;
  const bool message = b->latch[vk];
  *message_ms = b->latch_ms[vk], b->latch[vk] = false;
  return (down ? TFP_KEY_RAW_DOWN : 0) | (since ? TFP_KEY_RAW_LOW : 0) | (message ? TFP_KEY_RAW_MESSAGE : 0);
}

/* ---- the toggle key as src/mod_view.c reads it ---- */
typedef struct Rig {
  Board board;
  TfpKeyTaps taps;
  TfpKeyGuard guard;
  struct { int key; bool held; } edge;   /* src/wire_menu.c tfp_wire_menu_key_edge */
  int chat;
  bool overlay, panel, no_keyboard;
  /* round 50c: from change_at on (0: never), chat, overlay and panel answer these instead (a chat line sent, a text
   * field or window closed, between two updates) */
  uint64_t change_at;
  int chat_after;
  bool overlay_after, panel_after;
  uint64_t enter_at[4];   /* Enter key-downs the window took, in order */
  int enters;
  int says;
  char said[200];
} Rig;
static bool r_down(void *u, int vk) { Rig *r = u; return (tfp_key_taps_read(&r->taps, vk, raw, &r->board) & TFP_KEY_DOWN) != 0; }
static bool r_tapped(void *u, int vk) { Rig *r = u; return (tfp_key_taps_read(&r->taps, vk, raw, &r->board) & TFP_KEY_TAPPED) != 0; }
static bool r_edge(void *u, int key, bool down) {
  Rig *r = u;
  if (key != r->edge.key) r->edge.key = key, r->edge.held = true;
  const bool pressed = down && !r->edge.held;
  r->edge.held = down;
  return pressed;
}
static bool r_keyboard(void *u, uint8_t keys[256]) {
  Rig *r = u;
  if (r->no_keyboard) return false;
  for (int i = 0; i < 256; i++) keys[i] = r->board.level[i] ? 0x80 : 0;
  return true;
}
static bool r_changed(const Rig *r) { return r->change_at && r->board.now >= r->change_at; }
static int r_chat(void *u) { Rig *r = u; return r_changed(r) ? r->chat_after : r->chat; }
static bool r_overlay(void *u) { Rig *r = u; return r_changed(r) ? r->overlay_after : r->overlay; }
static bool r_panel(void *u) { Rig *r = u; return r_changed(r) ? r->panel_after : r->panel; }
static uint64_t r_enter(void *u) {   /* round 50c: the last Enter the window took by now (the hook's stamp) */
  Rig *r = u;
  uint64_t last = 0;
  for (int i = 0; i < r->enters; i++) if (r->enter_at[i] <= r->board.now) last = r->enter_at[i];
  return last;
}
static void r_say(void *u, const char *line) { Rig *r = u; r->says++; snprintf(r->said, sizeof r->said, "%s", line); }
static const TfpKeyGuardIo guard_io = {NULL, r_down, r_edge, r_keyboard, r_chat, r_overlay, r_panel, r_say, r_tapped, r_enter};
static Rig *rig(unsigned mode) {
  static Rig r;
  memset(&r, 0, sizeof r);
  r.board.mode = mode;
  r.edge.key = 'U';   /* bound long ago */
  return &r;
}
static bool focus_at(const uint64_t *lost, int n, uint64_t now) {
  for (int i = 0; i < n; i++) if (lost[i] == now) return false;
  return true;
}
/* Game updates every `step` ms from `from` to `to`; the presses of U that came out of the guard. */
static int run_lost(Rig *r, uint64_t from, uint64_t to, uint64_t step, const uint64_t *lost, int n) {
  TfpKeyGuardIo io = guard_io;
  io.user = r;
  int presses = 0;
  for (uint64_t now = from; now <= to; now += step) {
    const bool focused = focus_at(lost, n, now);
    advance(&r->board, now);
    tfp_key_taps_begin(&r->taps, now, focused);
    presses += tfp_key_guard_toggle(&r->guard, &io, now, 'U', focused, r->chat < 0);
  }
  return presses;
}
static int run(Rig *r, uint64_t from, uint64_t to, uint64_t step) { return run_lost(r, from, to, step, NULL, 0); }
/* Game updates at the given times, the game in front at each. */
static int run_at(Rig *r, const uint64_t *updates, size_t n) {
  TfpKeyGuardIo io = guard_io;
  io.user = r;
  int presses = 0;
  for (size_t i = 0; i < n; i++) {
    advance(&r->board, updates[i]);
    tfp_key_taps_begin(&r->taps, updates[i], true);
    presses += tfp_key_guard_toggle(&r->guard, &io, updates[i], 'U', true, r->chat < 0);
  }
  return presses;
}
#define RUN_AT(r, ...) run_at(r, (const uint64_t[]){__VA_ARGS__}, sizeof((const uint64_t[]){__VA_ARGS__}) / sizeof(uint64_t))

static void tap_between_two_updates_is_one_press(void) {
  static const unsigned modes[] = {LOW_WINDOWS, MESSAGES, LOW_WINDOWS | MESSAGES, LOW_PROTON | MESSAGES};
  for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
    Rig *r = rig(modes[m]);
    tap(&r->board, 1030, 'U', 20);                   /* down at 1030, up at 1050: between the updates at 1000 and 1100 */
    CHECK(run(r, 0, 3000, 100) == 1);
    CHECK(r->taps.taps == 1 && r->taps.last_tap == 'U' && !r->says);
  }
  /* level only (what the mod did before this round), and Proton's low bit alone: the tap is not seen at all */
  Rig *r = rig(0);
  tap(&r->board, 1030, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 0);
  r = rig(LOW_PROTON);
  tap(&r->board, 1030, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 0 && !r->taps.taps);
  /* two taps a second apart: two presses; three inside one update: one */
  r = rig(MESSAGES);
  tap(&r->board, 1030, 'U', 20); tap(&r->board, 2030, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 2);
  r = rig(LOW_WINDOWS | MESSAGES);
  tap(&r->board, 1010, 'U', 10); tap(&r->board, 1040, 'U', 10); tap(&r->board, 1070, 'U', 10);
  CHECK(run(r, 0, 3000, 100) == 1);
}
static void held_key_is_one_edge(void) {
  static const unsigned modes[] = {LOW_WINDOWS, MESSAGES, LOW_WINDOWS | MESSAGES, LOW_PROTON | MESSAGES, LOW_PROTON, 0};
  for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
    for (int repeat = 0; repeat < 2; repeat++) {
      Rig *r = rig(modes[m]);
      r->board.repeat = repeat;                      /* auto-repeat keeps setting the low bit while the key is down */
      ev(&r->board, 1030, 'U', true); ev(&r->board, 1830, 'U', false);
      CHECK(run(r, 0, 3000, 100) == 1);
      CHECK(!r->taps.taps);                          /* the level's edge; never a tap, and none as the key is let go */
    }
  }
  /* an 80 ms press that one update does see: the level's edge, and the same press's low bit and message add nothing */
  Rig *r = rig(LOW_WINDOWS | MESSAGES);
  tap(&r->board, 1060, 'U', 80);                     /* down across the update at 1100 */
  CHECK(run(r, 0, 3000, 100) == 1 && !r->taps.taps);
}
static void late_key_message_is_not_a_second_press(void) {
  /* the window's thread behind the keyboard: the key message of a press arrives after the level showed it and went */
  for (uint64_t delay = 0; delay <= 600; delay += 50) {
    Rig *r = rig(MESSAGES);
    r->board.message_delay = delay;
    tap(&r->board, 1090, 'U', 30);                   /* down at the update at 1100, up by 1200 */
    CHECK(run(r, 0, 3000, 100) == 1);
    CHECK(!r->taps.taps && r->taps.late == (delay >= 50));
    /* a tap the low bit showed, its message later */
    r = rig(LOW_WINDOWS | MESSAGES);
    r->board.message_delay = delay;
    tap(&r->board, 1030, 'U', 20);
    CHECK(run(r, 0, 3000, 100) == 1);
    /* ... and whatever the update that reads the late message took to come (a hitch of up to two seconds) */
    for (uint64_t hitch = 100; hitch <= 2000; hitch += 100) {
      r = rig(LOW_PROTON | MESSAGES);
      r->board.message_delay = delay;
      ev(&r->board, 1105, 'U', true); ev(&r->board, 1120, 'U', false);
      CHECK(RUN_AT(r, 1000, 1100, 1110, 1130, 1130 + hitch, 1230 + hitch, 1330 + hitch, 1930 + hitch, 2030 + hitch) == 1);
      CHECK(!r->taps.taps);
    }
  }
  /* the messages of taps no update acted on do not stand in for the message of the press that follows them (a count
   * of messages against presses would take them for it, and the press's own, later, for a second press) */
  Rig *r = rig(MESSAGES);
  r->board.message_delay = 400;
  tap(&r->board, 1010, 'U', 20); tap(&r->board, 1050, 'U', 20);    /* two taps and a press inside one hitch */
  ev(&r->board, 1700, 'U', true); ev(&r->board, 1850, 'U', false);
  CHECK(RUN_AT(r, 800, 900, 1000, 1800, 1900, 2000, 2050, 2150, 2250, 2350) == 1 && !r->taps.taps);
  /* past TFP_KEY_TAP_LATE_MS after a press the level showed, a message is a new tap; inside it, the one thing given up */
  r = rig(MESSAGES);
  tap(&r->board, 1090, 'U', 30); tap(&r->board, 1100 + TFP_KEY_TAP_LATE_MS + 30, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 2 && r->taps.taps == 1);
  r = rig(MESSAGES);
  tap(&r->board, 1090, 'U', 30); tap(&r->board, 1100 + TFP_KEY_TAP_LATE_MS - 70, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 1 && !r->taps.taps && r->taps.late == 1);
  /* the bound is the message's own time: taken TFP_KEY_TAP_LATE_MS after the press was shown it is still that
   * press's; one millisecond later it is a tap (stated in key_tap.h) */
  r = rig(MESSAGES);
  r->board.message_delay = TFP_KEY_TAP_LATE_MS + 10;
  tap(&r->board, 1090, 'U', 30);                                   /* shown at 1100; the message taken at 1850 */
  CHECK(run(r, 0, 3000, 100) == 1);
  r = rig(MESSAGES);
  r->board.message_delay = TFP_KEY_TAP_LATE_MS + 11;
  tap(&r->board, 1090, 'U', 30);
  CHECK(run(r, 0, 3000, 100) == 2);
  /* two taps close together, each between two updates: two presses (a tap from its message leaves nothing to wait for) */
  r = rig(MESSAGES);
  tap(&r->board, 1030, 'U', 20); tap(&r->board, 1230, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 2);
  /* ... the low bit alone keeps its quiet time: the second is given up, a later one is not */
  r = rig(LOW_WINDOWS);
  tap(&r->board, 1030, 'U', 20); tap(&r->board, 1230, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 1);
  r = rig(LOW_WINDOWS);
  tap(&r->board, 1030, 'U', 20); tap(&r->board, 1030 + TFP_KEY_TAP_QUIET_MS + 100, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 2);
}
/* The independent check's two reproductions (~/.cache/tfp-v67-fuzz/det.c), through the toggle key's guard. */
static void one_press_never_toggles_twice(void) {
  /* blocking 1: the level shows the press at 1110 and its end at 1130; the window's thread takes the WM_KEYDOWN at
   * 1135, 30 ms late; the next update comes after a 470 ms hitch, at 1600 */
  Rig *r = rig(LOW_PROTON | MESSAGES);
  r->board.message_delay = 30;
  ev(&r->board, 1105, 'U', true); ev(&r->board, 1120, 'U', false);
  CHECK(RUN_AT(r, 1000, 1100, 1110, 1130, 1600, 1700, 1800) == 1);
  CHECK(!r->taps.taps && r->taps.late == 1);
  /* the same with Windows' low bit beside the messages */
  r = rig(LOW_WINDOWS | MESSAGES);
  r->board.message_delay = 30;
  ev(&r->board, 1105, 'U', true); ev(&r->board, 1120, 'U', false);
  CHECK(RUN_AT(r, 1000, 1100, 1110, 1130, 1600, 1700, 1800) == 1);
  /* a key held through the hitch and let go, its message later still */
  r = rig(MESSAGES);
  r->board.message_delay = 600;
  ev(&r->board, 1105, 'U', true); ev(&r->board, 1500, 'U', false);
  CHECK(RUN_AT(r, 1000, 1100, 1110, 1600, 1650, 1700, 3500, 3600) == 1);
  CHECK(!r->taps.taps);
}
static void a_key_typed_into_another_window_is_no_press(void) {
  /* blocking 2: Windows' low bit is every window's. Updates at 1100 and 2300 both see the game in front; in between
   * the player left, typed "u" elsewhere at 1500 and came back */
  Rig *r = rig(LOW_WINDOWS);
  tap(&r->board, 1500, 'U', 20);
  CHECK(RUN_AT(r, 1000, 1100, 2300, 2400) == 0);
  CHECK(!r->taps.taps);
  /* the low bit is taken across TFP_KEY_TAP_LOW_GAP_MS and no further */
  r = rig(LOW_WINDOWS);
  tap(&r->board, 1200, 'U', 20);
  CHECK(RUN_AT(r, 1000, 1100, 1100 + TFP_KEY_TAP_LOW_GAP_MS, 1500) == 1);
  r = rig(LOW_WINDOWS);
  tap(&r->board, 1200, 'U', 20);
  CHECK(RUN_AT(r, 1000, 1100, 1101 + TFP_KEY_TAP_LOW_GAP_MS, 1500) == 0);
  /* the window's own message cannot come from elsewhere: it keeps the long gap (the hitch this round is for) */
  r = rig(LOW_PROTON | MESSAGES);
  tap(&r->board, 1500, 'U', 20);
  CHECK(RUN_AT(r, 1000, 1100, 2300, 2400) == 1);
  /* once the hook has seen a key message the low bit is not asked at all: "u" typed elsewhere gives the game window
   * no message, however short the gap */
  r = rig(LOW_WINDOWS | MESSAGES);
  tap(&r->board, 1030, 'U', 20);                    /* in the game: one tap, from its message */
  r->board.silent_before = 0;
  CHECK(run(r, 0, 2000, 100) == 1 && r->taps.hooked);
  r->board.silent_before = 9000;                    /* from here the key goes to another window */
  tap(&r->board, 2130, 'U', 20);
  CHECK(run(r, 2100, 4000, 100) == 0 && r->taps.taps == 1);
}
/* The reader's own focus check: F10's reader (src/mod_panel.c p_tapped) has no other. */
static void the_reader_refuses_a_tap_out_of_focus(void) {
  static const unsigned modes[] = {LOW_WINDOWS, MESSAGES};
  for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
    for (int lost = 0; lost < 4; lost++) {            /* 0: in front throughout; 1: not at 1000; 2: not at 1100; 3: neither */
      Board b = {.mode = modes[m]};
      TfpKeyTaps t = {0};
      unsigned answer = 0;
      tap(&b, 1030, 0x79, 20);
      for (uint64_t now = 0; now <= 1500; now += 100) {
        const bool focused = !((now == 1000 && (lost & 1)) || (now == 1100 && (lost & 2)));
        advance(&b, now);
        tfp_key_taps_begin(&t, now, focused);
        answer |= tfp_key_taps_read(&t, 0x79, raw, &b);
      }
      CHECK(answer == (lost ? 0u : (unsigned)TFP_KEY_TAPPED));
      CHECK(t.taps == (lost ? 0u : 1u));
    }
  }
}
static void a_tap_leaves_the_edge_up(void) {
  /* tapped between two updates, then down at the very next one: two presses (the tap's edge was let go) */
  Rig *r = rig(MESSAGES);
  tap(&r->board, 1030, 'U', 20);
  ev(&r->board, 1150, 'U', true); ev(&r->board, 1450, 'U', false);
  CHECK(run(r, 0, 3000, 100) == 2 && r->taps.taps == 1);
}
static void focus_lost_between_updates_ignores_the_tap(void) {
  static const unsigned modes[] = {LOW_WINDOWS, MESSAGES};
  for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
    const uint64_t before[] = {1000}, after[] = {1100}, around[] = {900, 1000};
    Rig *r = rig(modes[m]);
    tap(&r->board, 1030, 'U', 20);
    CHECK(run_lost(r, 0, 3000, 100, before, 1) == 0);     /* not in front at the update before the tap */
    r = rig(modes[m]);
    tap(&r->board, 1030, 'U', 20);
    CHECK(run_lost(r, 0, 3000, 100, after, 1) == 0);      /* not in front at the update after it */
    CHECK(!r->taps.taps);
    r = rig(modes[m]);
    tap(&r->board, 930, 'U', 20);                         /* typed into another window */
    CHECK(run_lost(r, 0, 3000, 100, around, 2) == 0);
    /* back in front, a tap counts from the second update on */
    r = rig(modes[m]);
    tap(&r->board, 1130, 'U', 20);
    CHECK(run_lost(r, 0, 3000, 100, before, 1) == 1);
  }
}
static void stale_answers_are_ignored(void) {
  Board b = {.mode = LOW_WINDOWS | MESSAGES};
  TfpKeyTaps t = {0};
  /* before the first update nothing is read */
  CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0 && !b.raw_calls);
  /* a key pressed long before its first read (the F11 watch resuming, a key just bound): no tap at that read */
  tap(&b, 500, 0x79, 20);
  for (uint64_t now = 0; now <= 2000; now += 100) { advance(&b, now); tfp_key_taps_begin(&t, now, true); }
  CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0);
  /* read at every update from here: the next tap is caught, once, and asked twice in one update it is one raw read */
  tap(&b, 2130, 0x79, 20);
  advance(&b, 2100); tfp_key_taps_begin(&t, 2100, true); CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0);
  const unsigned calls = b.raw_calls;
  advance(&b, 2200); tfp_key_taps_begin(&t, 2200, true);
  CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == TFP_KEY_TAPPED && tfp_key_taps_read(&t, 0x79, raw, &b) == TFP_KEY_TAPPED);
  CHECK(b.raw_calls == calls + 1 && t.taps == 1);
  advance(&b, 2300); tfp_key_taps_begin(&t, 2300, true); CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0);
  /* a key not read for an update (the watch was off): the tap in between is not answered */
  tap(&b, 3030, 0x79, 20);
  advance(&b, 3000); tfp_key_taps_begin(&t, 3000, true); CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0);
  advance(&b, 3100); tfp_key_taps_begin(&t, 3100, true);
  advance(&b, 3200); tfp_key_taps_begin(&t, 3200, true); CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0);
  /* a game that stood still for more than TFP_KEY_TAP_GAP_MS (a long load): what was pressed meanwhile is not acted on */
  tap(&b, 5000, 0x79, 20);
  advance(&b, 4000); tfp_key_taps_begin(&t, 4000, true); CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0);
  advance(&b, 4000 + TFP_KEY_TAP_GAP_MS + 1); tfp_key_taps_begin(&t, 4000 + TFP_KEY_TAP_GAP_MS + 1, true);
  CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0);
  /* ... a hitch inside the bound is: this is the case the round is for */
  tap(&b, 8000, 0x79, 20);
  advance(&b, 7000); tfp_key_taps_begin(&t, 7000, true); CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == 0);
  advance(&b, 7000 + TFP_KEY_TAP_GAP_MS); tfp_key_taps_begin(&t, 7000 + TFP_KEY_TAP_GAP_MS, true);
  CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == TFP_KEY_TAPPED);
  /* the level is the keyboard's whether or not the game is in front; keys out of range are nothing */
  ev(&b, 9950, 0x79, true);
  advance(&b, 10000); tfp_key_taps_begin(&t, 10000, false);
  CHECK(tfp_key_taps_read(&t, 0x79, raw, &b) == TFP_KEY_DOWN && tfp_key_taps_read(&t, 0, raw, &b) == 0 && tfp_key_taps_read(&t, 256, raw, &b) == 0);
  /* another caller of GetAsyncKeyState took the low bit (what Windows documents): the tap is missed, as before the round */
  Rig *r = rig(LOW_WINDOWS);
  r->board.thief = true;
  tap(&r->board, 1030, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 0);
}
static void guards_still_refuse_a_tap(void) {
  /* chat open: refused, with the guard's line */
  Rig *r = rig(MESSAGES);
  r->chat = 1;
  tap(&r->board, 1030, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 0 && r->says == 1 && !strcmp(r->said, "toggle key ignored: typing in chat"));
  CHECK(r->taps.taps == 1 && r->guard.refused == 1);
  /* a host window, the mod's own panel */
  r = rig(MESSAGES); r->overlay = true;
  tap(&r->board, 1030, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 0 && strstr(r->said, "a Goose Overlay Host window has the keyboard"));
  r = rig(MESSAGES); r->panel = true;
  tap(&r->board, 1030, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 0 && strstr(r->said, "settings panel or placement editor is open"));
  /* the chat flag unreadable: "yo u" typed, the U a tap between two updates: refused as typing */
  r = rig(MESSAGES); r->chat = -1;
  tap(&r->board, 990, 'Y', 120); tap(&r->board, 1190, 'O', 120);
  tap(&r->board, 1330, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 0 && strstr(r->said, "typing (another text key"));
  /* ... and a tap with no typing before it is taken */
  r = rig(MESSAGES); r->chat = -1;
  tap(&r->board, 1330, 'U', 20);
  CHECK(run(r, 0, 3000, 100) == 1 && !r->says);
  /* a key that has just become the toggle key is refused until it was seen up, tapped or not (TfpKeyEdge's rule) */
  r = rig(MESSAGES);
  ev(&r->board, 950, 'U', true); ev(&r->board, 1250, 'U', false);   /* held to bind it */
  CHECK(run(r, 0, 900, 100) == 0);
  r->edge.key = 'K';                                                /* another key until the update at 1000 */
  CHECK(run(r, 1000, 3000, 100) == 0);
  tap(&r->board, 3530, 'U', 20);                                    /* bound in the very update that sees the tap */
  CHECK(run(r, 3100, 3500, 100) == 0);
  r->edge.key = 'K';
  CHECK(run(r, 3600, 3600, 100) == 0 && r->taps.taps == 1);
  tap(&r->board, 4030, 'U', 20);
  CHECK(run(r, 3700, 5000, 100) == 1);
}

/* Round 50c (the independent check's chatdet.c): the last letter of a chat line is the toggle key and Enter sends the
 * line before the next update. The update that answers the tap sees chat closed, but chat had the keyboard at the
 * previous update: the tap is the chat's, refused. The same for a host window or the mod's panel closed in the gap. */
static void text_closed_in_the_gap_keeps_its_tap(void) {
  static const unsigned modes[] = {MESSAGES, LOW_WINDOWS, LOW_WINDOWS | MESSAGES, LOW_PROTON | MESSAGES};
  for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
    const int before = failed;
    /* chatdet.c: updates at 1000, 1100, 1200; "u" 1110-1170; Enter closes chat at 1150 */
    Rig *r = rig(modes[m]);
    r->chat = 1, r->change_at = 1150, r->chat_after = 0;
    tap(&r->board, 1110, 'U', 60);
    CHECK(RUN_AT(r, 1000, 1100, 1200, 1300, 1400) == 0);
    CHECK(r->says == 1 && !strcmp(r->said, "toggle key ignored: typing in chat") && r->guard.refused == 1);
    /* ... and the next tap, after an update that saw chat closed, is the player's: taken */
    tap(&r->board, 1710, 'U', 40);   /* past the low bit's quiet time after the refused tap */
    CHECK(RUN_AT(r, 1500, 1600, 1700, 1800, 1900) == 1);
    /* a long gap (a hitch of 400 ms), "u" let go 150 ms before Enter */
    r = rig(modes[m]);
    r->chat = 1, r->change_at = 1350, r->chat_after = 0;
    tap(&r->board, 1150, 'U', 50);
    CHECK(RUN_AT(r, 900, 1000, 1400, 1500) == 0 && (modes[m] == LOW_WINDOWS || r->guard.refused == 1)); /* the low bit: gap too long */
    /* a host window's text field, and the mod's own panel, closed in the gap */
    r = rig(modes[m]);
    r->overlay = true, r->change_at = 1150, r->overlay_after = false;
    tap(&r->board, 1110, 'U', 60);
    CHECK(RUN_AT(r, 1000, 1100, 1200, 1300) == 0 && strstr(r->said, "a Goose Overlay Host window has the keyboard"));
    r = rig(modes[m]);
    r->panel = true, r->change_at = 1150, r->panel_after = false;
    tap(&r->board, 1110, 'U', 60);
    CHECK(RUN_AT(r, 1000, 1100, 1200, 1300) == 0 && strstr(r->said, "settings panel or placement editor is open"));
    /* a held press is the level's, as before: chat closed by the update that sees it down, it is taken */
    r = rig(modes[m]);
    r->chat = 1, r->change_at = 1150, r->chat_after = 0;
    ev(&r->board, 1160, 'U', true); ev(&r->board, 1400, 'U', false);
    CHECK(RUN_AT(r, 1000, 1100, 1200, 1300, 1400, 1500) == 1);
    /* chat opened in the gap after the tap: refused as before (chat open at the update that answers it) */
    r = rig(modes[m]);
    r->chat = 0, r->change_at = 1150, r->chat_after = 1;
    tap(&r->board, 1110, 'U', 20);
    CHECK(RUN_AT(r, 1000, 1100, 1200, 1300) == 0);
    /* chat opened and closed inside one gap (Enter, "u", Enter): never seen open, but the window took an Enter */
    r = rig(modes[m]);
    r->enter_at[0] = 1105, r->enter_at[1] = 1180, r->enters = 2;
    tap(&r->board, 1130, 'U', 30);
    CHECK(RUN_AT(r, 1000, 1100, 1200, 1300) == 0 && strstr(r->said, "Enter went down since the previous game update") && r->guard.gap_refused == 1);
    /* an Enter before the previous update (a line sent long ago) refuses nothing */
    r = rig(modes[m]);
    r->enter_at[0] = 950, r->enters = 1;
    tap(&r->board, 1130, 'U', 30);
    CHECK(RUN_AT(r, 900, 1000, 1100, 1200, 1300) == 1 && !r->says && !r->guard.gap_refused);
    /* ... nor a press the level saw, Enter or not */
    r = rig(modes[m]);
    r->enter_at[0] = 1105, r->enters = 1;
    ev(&r->board, 1130, 'U', true); ev(&r->board, 1260, 'U', false);
    CHECK(RUN_AT(r, 1000, 1100, 1200, 1300) == 1);
    if (failed != before) printf("  (text closed in the gap: source %u)\n", modes[m]);
  }
}

/* ---- F11's notice (src/f11_hint.h) ---- */
typedef struct Hint { Board board; TfpKeyTaps taps; int chat; unsigned shows, says; } Hint;
static bool h_down(void *u, int vk) { Hint *h = u; return (tfp_key_taps_read(&h->taps, vk, raw, &h->board) & TFP_KEY_DOWN) != 0; }
static bool h_tapped(void *u, int vk) { Hint *h = u; return (tfp_key_taps_read(&h->taps, vk, raw, &h->board) & TFP_KEY_TAPPED) != 0; }
static int h_chat(void *u) { return ((Hint *)u)->chat; }
static bool h_no(void *u) { (void)u; return false; }
static void h_say(void *u, const char *line) { (void)line; ((Hint *)u)->says++; }
static bool h_show(void *u, const char *notice, char *why, size_t n) { (void)notice; (void)why; (void)n; ((Hint *)u)->shows++; return true; }
static void f11_tap_gets_its_notice(void) {
  static Hint h;
  TfpF11Hint hint = {0};
  const TfpF11HintIo io = {&h, h_down, h_chat, h_no, h_no, h_say, h_show, h_tapped};
  int notices = 0;
  memset(&h, 0, sizeof h);
  h.board.mode = LOW_PROTON | MESSAGES;
  tap(&h.board, 1030, TFP_F11_VK, 20);
  tap(&h.board, 2030, TFP_F11_VK, 20);   /* inside the notice's quiet time: F11 is not even read */
  for (uint64_t now = 0; now <= 3000; now += 100) {
    advance(&h.board, now);
    tfp_key_taps_begin(&h.taps, now, true);
    notices += tfp_f11_hint_tick(&hint, &io, now, 'U', "U", true, true);
  }
  CHECK(notices == 1 && h.shows == 1 && h.says == 1 && h.taps.taps == 1);
  /* chat open: nothing, and nothing used up */
  memset(&h, 0, sizeof h); memset(&hint, 0, sizeof hint);
  h.board.mode = MESSAGES, h.chat = 1, notices = 0;
  tap(&h.board, 1030, TFP_F11_VK, 20);
  for (uint64_t now = 0; now <= 3000; now += 100) {
    advance(&h.board, now);
    tfp_key_taps_begin(&h.taps, now, true);
    notices += tfp_f11_hint_tick(&hint, &io, now, 'U', "U", true, true);
  }
  CHECK(!notices && !h.shows && !hint.shown);
}

int main(void) {
  one_press_never_toggles_twice();
  a_key_typed_into_another_window_is_no_press();
  the_reader_refuses_a_tap_out_of_focus();
  a_tap_leaves_the_edge_up();
  tap_between_two_updates_is_one_press();
  held_key_is_one_edge();
  late_key_message_is_not_a_second_press();
  focus_lost_between_updates_ignores_the_tap();
  stale_answers_are_ignored();
  guards_still_refuse_a_tap();
  text_closed_in_the_gap_keeps_its_tap();
  f11_tap_gets_its_notice();
  printf("key_tap: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
