/* Round 50c (src/key_guard.h, src/key_tap.h): the toggle key typed as text, after the independent check's chat.c
 * ("u" as the last letter of a chat line, Enter sending it before the next update, toggled the view).
 *
 *   key_chat_fuzz LOW HOOK MAXLAG GAPMIN GAPMAX TEXT [SEEDS]
 *     LOW     0: no low bit; 1: Windows' (pressed since the last call); 2: Proton's (down now and not at the last call)
 *     HOOK    1: every key-down gives the game window a message, taken up to MAXLAG ms later (in order), stamped then;
 *             the window takes Enter the same way, and the text opens and closes when its Enter is taken. 0: no
 *             messages; Enter is seen by the low bit at the first update after it (src/wire_win.c tfp_win_enter_ms)
 *     GAPMIN, GAPMAX   game updates this far apart (uniform)
 *     TEXT    0: the game's chat; 1: a Goose Overlay Host text field (input_captured); 2: the mod's own panel
 *
 * The keyboard, two minutes a schedule: text lines (Enter opens, 1 to 8 letters, some of them U, the last always U, let
 * go 0 to 200 ms before the Enter that closes it; a line of one U opened and closed inside one gap happens too) and,
 * at least 1000 ms from any line and from each other, the player's own presses of U (taps of 5 to 150 ms, holds of
 * 200 to 900 ms). Asserted:
 *   - no toggle comes from a U typed as text (chat toggles 0). Not counted, and reported apart ("before the text
 *     opened"): a U an update saw down before the window took the Enter that opens the text (MAXLAG longer than the
 *     typist's Enter-to-U): a level press with nothing open yet, judged by now as main judges it
 *   - every press of the player's is acted on once: each one a tick saw down, and with HOOK each tap too (spaced past
 *     TFP_KEY_TAP_LATE_MS, every tap has its message); without a hook only the level's are owed (the low bit's gap
 *     rule may give up a tap)
 *   - no toggle without a press (phantom 0)
 * Exit 1 on any violation. */
#include "key_guard.h"
#include "key_tap.h"
#include <stdio.h>
#include <stdlib.h>
#define VK 'U'
#define MAXP 2000
#define MAXL 400
#define MAXE 20000
enum { E_DOWN, E_UP, E_MESSAGE, E_ENTER, E_OPEN, E_CLOSE };
typedef struct Ev { long t; int kind; } Ev;
static uint64_t rng_state;
static unsigned rnd(void) { rng_state ^= rng_state << 13, rng_state ^= rng_state >> 7, rng_state ^= rng_state << 17; return (unsigned)(rng_state >> 16); }
static long between(long a, long b) { return a + (long)(rnd() % (unsigned long)(b - a + 1)); }

static int low_mode, text_kind;
static int phys_down, low_bit, prev_call_down, text_open;
static bool latch;
static uint64_t latch_ms, enter_ms;
static bool enter_low;   /* no hook: Enter pressed since the last update (its low bit) */
static unsigned raw(void *u, int vk, uint64_t *message_ms) {
  (void)u, (void)vk;
  const int low = low_mode == 1 ? low_bit : low_mode == 2 ? (phys_down && !prev_call_down) : 0;
  low_bit = 0, prev_call_down = phys_down;
  const bool message = latch;
  *message_ms = latch_ms, latch = false;
  return (phys_down ? TFP_KEY_RAW_DOWN : 0u) | (low ? TFP_KEY_RAW_LOW : 0u) | (message ? TFP_KEY_RAW_MESSAGE : 0u);
}
static TfpKeyTaps taps;
static bool held_edge;
static bool k_down(void *u, int vk) { (void)u; return (tfp_key_taps_read(&taps, vk, raw, NULL) & TFP_KEY_DOWN) != 0; }
static bool k_tap(void *u, int vk) { (void)u; return (tfp_key_taps_read(&taps, vk, raw, NULL) & TFP_KEY_TAPPED) != 0; }
static bool k_edge(void *u, int k, bool d) { (void)u, (void)k; const bool p = d && !held_edge; held_edge = d; return p; }
static bool k_kb(void *u, uint8_t k[256]) { (void)u; memset(k, 0, 256); return false; }
static int k_chat(void *u) { (void)u; return text_kind == 0 ? text_open : 0; }
static bool k_overlay(void *u) { (void)u; return text_kind == 1 && text_open; }
static bool k_panel(void *u) { (void)u; return text_kind == 2 && text_open; }
static void k_say(void *u, const char *l) { (void)u, (void)l; }
static uint64_t k_enter(void *u) { (void)u; return enter_ms; }
static int cmp(const void *a, const void *b) {
  const Ev *x = a, *y = b;
  return x->t < y->t ? -1 : x->t > y->t ? 1 : x->kind - y->kind;
}

int main(int argc, char **argv) {
  if (argc < 7) return 2;
  low_mode = atoi(argv[1]);
  const int hook = atoi(argv[2]), maxlag = atoi(argv[3]), gapmin = atoi(argv[4]), gapmax = atoi(argv[5]);
  text_kind = atoi(argv[6]);
  const int seeds = argc > 7 ? atoi(argv[7]) : 200;
  const bool verbose = getenv("V") != NULL;
  long lines = 0, line_us = 0, own = 0, own_taps = 0, acted_own = 0, chat_toggles = 0, lost = 0, phantom = 0, twice = 0, one_gap = 0, early = 0;
  static Ev ev[MAXE];
  static long start[MAXP], end[MAXP];
  static int is_text[MAXP], acted[MAXP], seen[MAXP];
  for (int seed = 1; seed <= seeds; seed++) {
    rng_state = 0x9E3779B97F4A7C15ull * (uint64_t)seed + (uint64_t)(low_mode * 7 + hook * 3 + text_kind * 11 + gapmax);
    int n = 0, np = 0;
    long t = 1000, message_at = 0;
    const long T = 120000;
    /* a key-down's message (and the window's handling of it) in queue order */
    #define MESSAGE(at_, kind_) do { const long m_ = (at_) + (hook ? between(0, maxlag) : 0); \
      message_at = m_ > message_at ? m_ : message_at; ev[n++] = (Ev){message_at, (kind_)}; } while (0)
    while (t < T && np < MAXP - 12 && n < MAXE - 80) {
      if (rnd() % 2) {   /* a text line ending in U */
        const bool tiny = rnd() % 6 == 0;                    /* Enter, U, Enter: fast, inside one gap often */
        const int letters = tiny ? 1 : (int)between(1, 8);
        ev[n++] = (Ev){t, E_ENTER};                          /* the key; the window opens the text when it takes it */
        MESSAGE(t, E_OPEN);
        long k = t + (tiny ? between(30, 90) : between(120, 400));
        for (int i = 0; i < letters; i++) {
          const bool last = i == letters - 1, is_u = last || rnd() % 5 == 0;
          const long dur = between(40, 110);
          if (is_u) {
            start[np] = k, end[np] = k + dur, is_text[np] = 1, acted[np] = seen[np] = 0, np++, line_us++;
            ev[n++] = (Ev){k, E_DOWN}, ev[n++] = (Ev){k + dur, E_UP};
            MESSAGE(k, E_MESSAGE);
          }
          if (last) k += dur + between(0, tiny ? 60 : 200);  /* let go 0 to 200 ms before Enter */
          else k += between(80, 250);
        }
        ev[n++] = (Ev){k, E_ENTER};
        MESSAGE(k, E_CLOSE);
        lines++, one_gap += tiny;
        t = k + maxlag + between(1000, 2500);
      } else {           /* the player's own press of U, away from any text */
        const bool held = rnd() % 3 == 0;
        const long dur = held ? between(200, 900) : between(5, 150);
        start[np] = t, end[np] = t + dur, is_text[np] = 0, acted[np] = seen[np] = 0, np++, own++;
        ev[n++] = (Ev){t, E_DOWN}, ev[n++] = (Ev){t + dur, E_UP};
        MESSAGE(t, E_MESSAGE);
        t += dur + maxlag + between(1000, 2500);
      }
    }
    #undef MESSAGE
    qsort(ev, (size_t)n, sizeof *ev, cmp);
    memset(&taps, 0, sizeof taps);
    TfpKeyGuard g;
    memset(&g, 0, sizeof g);
    enter_low = false, held_edge = false, phys_down = low_bit = prev_call_down = text_open = 0, latch = false, latch_ms = enter_ms = 0;
    const TfpKeyGuardIo io = {NULL, k_down, k_edge, k_kb, k_chat, k_overlay, k_panel, k_say, k_tap, k_enter};
    int i = 0, cur = -1;
    long tick = 0, prev_tick = 0;
    bool main_held = false;
    while (tick < T + 4000) {
      tick += between(gapmin, gapmax);
      for (; i < n && ev[i].t <= tick; i++) {
        switch (ev[i].kind) {
          case E_DOWN: phys_down = 1, low_bit = 1, cur++; break;
          case E_UP: phys_down = 0; break;
          case E_MESSAGE: if (hook) latch = true, latch_ms = (uint64_t)ev[i].t; break;
          case E_OPEN: text_open = 1; if (hook) enter_ms = (uint64_t)ev[i].t; break;
          case E_CLOSE: text_open = 0; if (hook) enter_ms = (uint64_t)ev[i].t; break;
          case E_ENTER: enter_low = true; break;               /* the key itself: the window acts on its message */
          default: break;
        }
      }
      if (phys_down && cur >= 0) seen[cur] = 1;
      if (!hook && enter_low) enter_ms = (uint64_t)tick;
      enter_low = false;
      tfp_key_taps_begin(&taps, (uint64_t)tick, true);
      const bool act = tfp_key_guard_toggle(&g, &io, (uint64_t)tick, VK, true, false);
      const bool md = phys_down != 0, mp = md && !main_held;
      main_held = md;
      if (!act) { prev_tick = tick; continue; }
      int p = -1;
      if (mp) p = cur;
      else {
        const long earliest = prev_tick - maxlag - 1;
        for (int k = 0; k <= cur; k++)
          if (!acted[k] && end[k] <= tick && start[k] >= earliest) { p = k; break; }
      }
      if (p < 0) { phantom++; if (verbose) printf("  no press: seed %d update %ld\n", seed, tick); }
      else if (acted[p]) twice++;
      else {
        acted[p] = 1;
        if (is_text[p] && mp && !text_open) early++;
        else if (is_text[p]) { chat_toggles++; if (verbose) printf("  text U toggled: seed %d update %ld (previous %ld) press %ld-%ld\n", seed, tick, prev_tick, start[p], end[p]); }
        else acted_own++;
      }
      prev_tick = tick;
    }
    for (int k = 0; k < np; k++) {
      if (is_text[k]) continue;
      own_taps += !seen[k];
      if (!acted[k] && (seen[k] || hook)) { lost++; if (verbose) printf("  lost: seed %d press %ld-%ld%s\n", seed, start[k], end[k], seen[k] ? " (seen down)" : ""); }
    }
  }
  const long bad = chat_toggles + lost + phantom + twice;
  static const char *const kinds[] = {"chat", "a host text field", "the mod's panel"};
  printf("key_chat fuzz: %s; low bit %s, messages %s (late up to %d ms), updates %d-%d ms apart; %d schedules: %ld lines (%ld inside a moment) with %ld U's typed, "
         "%ld own presses (%ld no update saw down), acted on %ld; text toggles %ld (before the text opened %ld), own press lost %ld, no press %ld, twice %ld: %s\n",
         kinds[text_kind], low_mode == 1 ? "Windows" : low_mode == 2 ? "Proton" : "none", hook ? "yes" : "no", maxlag, gapmin, gapmax, seeds, lines, one_gap,
         line_us, own, own_taps, acted_own, chat_toggles, early, lost, phantom, twice, bad ? "FAILED" : "ok");
  return bad ? 1 : 0;
}
