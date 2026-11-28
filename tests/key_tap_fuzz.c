/* Round 50b (src/key_tap.h): random schedules of presses, hitches, focus changes and late key messages against a
 * model of the physical keyboard, after the independent check's fuzz (which found one press answered twice and a key
 * typed into another window answered).
 *
 *   key_tap_fuzz LOW HOOK MAXLAG MINAWAY CONSUMER [SEEDS]
 *     LOW       0: no low bit; 1: Windows' (pressed since the last call, any window, auto-repeat sets it too);
 *               2: Proton's (down now and not at this process's last call)
 *     HOOK      1: every key-down with the game in front gives the window a message, taken up to MAXLAG ms later (in
 *               order), stamped when taken
 *     MINAWAY   the shortest time the game is out of focus, ms (the low bit's rule stands on nobody leaving, typing
 *               and returning inside TFP_KEY_TAP_LOW_GAP_MS; the messages' rule needs no such thing: give 1)
 *     CONSUMER  0: the toggle key (tfp_key_guard_toggle: the key is read only with the game in front);
 *               1: F10's way (src/mod_panel.c: the level asked in front only, the tap always)
 *
 * The keyboard: a key held 300 to 1800 ms, tapped 5 to 150 ms, or tapped in bursts 20 to 120 ms apart. Updates 20 to
 * 150 ms apart, one in 40 a hitch of 300 to 2000 ms. Asserted, press by press:
 *   - every press the level rule of main acts on is acted on (nothing lost), and nothing else comes from the level
 *   - every other press reported is the tap of one real press: over by now, begun with the game in front, begun no
 *     earlier than the previous update less MAXLAG, and not reported before: so no press is reported twice, none
 *     without a real key-down in the game, and no more are reported than were made
 * Exit 1 on any violation. */
#include "key_guard.h"
#include "key_tap.h"
#include <stdio.h>
#include <stdlib.h>
#define VK 'U'
#define MAXP 4000
#define MAXE 40000
enum { E_DOWN, E_UP, E_FOCUS_OFF, E_FOCUS_ON, E_MESSAGE, E_REPEAT };
typedef struct Ev { long t; int kind; } Ev;
static uint64_t rng_state;
static unsigned rnd(void) { rng_state ^= rng_state << 13, rng_state ^= rng_state >> 7, rng_state ^= rng_state << 17; return (unsigned)(rng_state >> 16); }
static long between(long a, long b) { return a + (long)(rnd() % (unsigned long)(b - a + 1)); }

static int low_mode;
static int phys_down, low_bit, prev_call_down, focus;
static bool latch;
static uint64_t latch_ms;
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
static int k_chat(void *u) { (void)u; return 0; }
static bool k_no(void *u) { (void)u; return false; }
static void k_say(void *u, const char *l) { (void)u, (void)l; }
static int cmp(const void *a, const void *b) {
  const Ev *x = a, *y = b;
  return x->t < y->t ? -1 : x->t > y->t ? 1 : x->kind - y->kind;
}

int main(int argc, char **argv) {
  if (argc < 6) return 2;
  low_mode = atoi(argv[1]);
  const int hook = atoi(argv[2]), maxlag = atoi(argv[3]), minaway = atoi(argv[4]), consumer = atoi(argv[5]);
  const int seeds = argc > 6 ? atoi(argv[6]) : 300;
  const bool verbose = getenv("V") != NULL;
  long presses = 0, in_front = 0, level_acts = 0, tap_acts = 0, re_edges = 0, hidden = 0, hidden_caught = 0;
  long twice = 0, unfocused = 0, phantom = 0, lost = 0, hitches = 0, longest = 0;
  static Ev ev[MAXE];
  static long start[MAXP], end[MAXP];
  static int front[MAXP], acted[MAXP], seen[MAXP];
  for (int seed = 1; seed <= seeds; seed++) {
    rng_state = 0x9E3779B97F4A7C15ull * (uint64_t)seed + (uint64_t)(low_mode * 7 + hook * 3 + consumer);
    int n = 0, np = 0, f = 1, burst = 0;
    long t = 500, message_at = 0;
    const long T = 120000;
    while (t < T && np < MAXP - 1 && n < MAXE - 80) {
      if (burst || rnd() % 10 < 7) {
        const int kind = burst ? 1 : (int)(rnd() % 6);          /* 0: held; 5: the first of a burst of taps */
        const long dur = kind == 0 ? between(300, 1800) : between(5, 150);
        start[np] = t, end[np] = t + dur, front[np] = f, acted[np] = seen[np] = 0;
        ev[n++] = (Ev){t, E_DOWN}, ev[n++] = (Ev){t + dur, E_UP};
        if (f && hook) {
          const long at = t + between(0, maxlag);
          message_at = at > message_at ? at : message_at;       /* the window's queue keeps their order */
          ev[n++] = (Ev){message_at, E_MESSAGE};
        }
        for (long q = t + 500; q < t + dur; q += 33) ev[n++] = (Ev){q, E_REPEAT};
        np++;
        if (kind == 5) burst = (int)between(1, 4); else if (burst) burst--;
        t += dur + (burst ? between(20, 120) : between(1, 700));
      } else {
        f = !f;
        ev[n++] = (Ev){t, f ? E_FOCUS_ON : E_FOCUS_OFF};
        const long least = f ? 1 : minaway;                     /* f is the new state: out of focus stays at least minaway */
        t += rnd() % 3 ? between(least, least + 2000) : between(least, least + 60);
      }
    }
    qsort(ev, (size_t)n, sizeof *ev, cmp);
    memset(&taps, 0, sizeof taps);
    TfpKeyGuard g;
    memset(&g, 0, sizeof g);
    held_edge = false, phys_down = low_bit = prev_call_down = 0, latch = false, latch_ms = 0, focus = 1;
    const TfpKeyGuardIo io = {NULL, k_down, k_edge, k_kb, k_chat, k_no, k_no, k_say, k_tap};
    int i = 0, cur = -1;
    long tick = 0, prev_tick = 0;
    bool main_held = false;
    while (tick < T + 4000) {
      long period = between(20, 150);
      if (rnd() % 40 == 0) period = between(300, 2000), hitches++;
      if (period > longest) longest = period;
      tick += period;
      for (; i < n && ev[i].t <= tick; i++) {
        switch (ev[i].kind) {
          case E_DOWN: phys_down = 1, low_bit = 1, cur++; break;
          case E_UP: phys_down = 0; break;
          case E_FOCUS_OFF: focus = 0; break;
          case E_FOCUS_ON: focus = 1; break;
          case E_MESSAGE: latch = true, latch_ms = (uint64_t)ev[i].t; break;
          default: if (phys_down) low_bit = 1;
        }
      }
      if (phys_down && cur >= 0) seen[cur] = 1;                 /* an update saw this press down */
      tfp_key_taps_begin(&taps, (uint64_t)tick, focus);
      bool act;
      if (consumer == 0) act = tfp_key_guard_toggle(&g, &io, (uint64_t)tick, VK, focus, false);
      else {
        const bool down = focus && k_down(NULL, VK);
        const bool tapped = !down && k_tap(NULL, VK);
        act = k_edge(NULL, VK, down || tapped);
        if (tapped) k_edge(NULL, VK, false);
      }
      const bool md = focus && phys_down, mp = md && !main_held;   /* main: the level's edge with the game in front */
      main_held = md;
      if (mp && !act) { lost++; if (verbose) printf("  lost: seed %d update %ld\n", seed, tick); }
      if (act && mp) {
        level_acts++;
        if (acted[cur] & 2) { twice++; if (verbose) printf("  twice (tap, then level): seed %d update %ld press %ld-%ld\n", seed, tick, start[cur], end[cur]); }
        if (acted[cur] & 1) re_edges++;                         /* held across a focus change: main acts again too */
        acted[cur] |= 1;
      } else if (act) {
        const long earliest = hook ? prev_tick - maxlag - 1 : prev_tick;
        int p = -1, was_acted = -1, was_away = -1;
        for (int k = 0; k <= cur; k++) {
          if (start[k] < earliest - 4000) continue;
          const bool here = end[k] <= tick && start[k] >= earliest;
          if (here && front[k] && !acted[k]) { p = k; break; }  /* the oldest that can be it */
          if (start[k] >= earliest - 3000 && front[k] && acted[k]) was_acted = k;
          if (here && !front[k]) was_away = k;
        }
        if (p >= 0) { acted[p] |= 2, tap_acts++; if (!seen[p]) hidden_caught++; }
        else if (was_away >= 0) { unfocused++; if (verbose) printf("  not in front: seed %d update %ld (previous %ld) press %ld-%ld\n", seed, tick, prev_tick, start[was_away], end[was_away]); }
        else if (was_acted >= 0) { twice++; if (verbose) printf("  twice: seed %d update %ld (previous %ld) press %ld-%ld\n", seed, tick, prev_tick, start[was_acted], end[was_acted]); }
        else { phantom++; if (verbose) printf("  no press at all: seed %d update %ld (previous %ld)\n", seed, tick, prev_tick); }
      }
      prev_tick = tick;
    }
    presses += np;
    for (int k = 0; k < np; k++) in_front += front[k], hidden += front[k] && !seen[k];
  }
  const long bad = twice + unfocused + phantom + lost;
  printf("key_tap fuzz: low bit %s, messages %s (late up to %d ms), out of focus at least %d ms, %s; %d schedules, %ld updates' hitches (longest %ld ms): "
         "%ld presses (%ld in front), acted on %ld by the level and %ld as taps (%ld of the %ld in-front presses no update saw down); "
         "twice %ld, typed elsewhere %ld, no press %ld, level press lost %ld: %s\n",
         low_mode == 1 ? "Windows" : low_mode == 2 ? "Proton" : "none", hook ? "yes" : "no", maxlag, minaway, consumer ? "F10's reader" : "the toggle key's guard",
         seeds, hitches, longest, presses, in_front, level_acts - re_edges, tap_acts, hidden_caught, hidden, twice, unfocused, phantom, lost, bad ? "FAILED" : "ok");
  return bad ? 1 : 0;
}
