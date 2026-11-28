/* Round 50: the Windows half of the tap reader (src/wire_win.c tfp_win_keys_begin, tfp_win_key_level,
 * tfp_win_key_tapped, tfp_win_keys_end; the rule itself is src/key_tap.h, tests/key_tap_test.c).
 *
 * Built for Windows with zig (tests/run-key-hook.sh) and run under Wine in a headless gamescope (no window on the desktop). A window of this process on a
 * thread of its own takes the place of the game window; SendInput plays the keys; the "game updates" are 100 ms apart:
 *   - F10 pressed and let go inside 20 ms between two updates: one tap, once
 *   - U held across four updates: its level, no tap, none as it is let go
 *   - F9 tapped while it is not read: nothing when it is read again later
 *   - the hook taken off: true, and the window's keys still arrive
 * It also prints what this Wine's GetAsyncKeyState answers after a tap that is over (1: the low bit remembers it, as
 * Windows; 0: Proton's rule), and whether the hook saw the tap, so a run under either says which source carried it.
 * Without a foreground window (a Wine build whose headless desktop gives none) it says so and passes nothing. */
#include <windows.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hellforge_mod.h"
#include "wire_win.h"

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("key hook: FAILED %s (line %d)\n", #c, __LINE__); } } while (0)

static HWND window;
static HANDLE ready;
static volatile LONG key_downs;
static LRESULT CALLBACK proc(HWND w, UINT message, WPARAM wp, LPARAM lp) {
  if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lp & 0x40000000)) InterlockedIncrement(&key_downs); /* F10 is a system key */
  if (message == WM_DESTROY) PostQuitMessage(0);
  return DefWindowProcW(w, message, wp, lp);
}
static DWORD WINAPI window_thread(void *unused) {
  (void)unused;
  WNDCLASSW cls = {0};
  cls.lpfnWndProc = proc, cls.hInstance = GetModuleHandleW(NULL), cls.lpszClassName = L"tfp_key_hook_test";
  RegisterClassW(&cls);
  window = CreateWindowExW(0, cls.lpszClassName, L"keys", WS_POPUP | WS_VISIBLE, 0, 0, 200, 100, NULL, NULL, cls.hInstance, NULL);
  if (window) { SetForegroundWindow(window); SetFocus(window); }
  SetEvent(ready);
  MSG m;
  while (window && GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
  return 0;
}
static void key(unsigned vk, bool down) {
  INPUT in = {0};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = (WORD)vk, in.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC), in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  SendInput(1, &in, sizeof in);
}
static void notes(void) {
  char line[320];
  while (tfp_win_keys_note(line, sizeof line)) printf("key hook: log: %s\n", line);
}
/* One game update: the level and the tap of each watched key. */
static void update(const int *keys, int count, unsigned *levels, unsigned *taps) {
  tfp_win_keys_begin(tfp_win_focused());
  for (int i = 0; i < count; i++) {
    levels[i] += tfp_win_key_level(keys[i]);
    taps[i] += tfp_win_key_tapped(keys[i]);
    taps[i] += tfp_win_key_tapped(keys[i]) ? 0 : 0; /* asked twice in one update: still one raw read (key_tap_test) */
  }
  notes();
}
static void fake_log(HfMod *mod, const char *format, ...) { (void)mod; (void)format; }
static uintptr_t fake_module(const char *name, size_t *size) { (void)name; *size = 0; return 0x10000; }
static int fake_read(uintptr_t at, void *out, size_t size) { (void)at; (void)out; (void)size; return 1; }

int main(void) {
  static HfApi api;
  api.version = 16, api.size = sizeof api, api.log = fake_log, api.module_base = fake_module, api.read = fake_read, api.read_code = fake_read;
  tfp_win_setup(&api, NULL);
  ready = CreateEventW(NULL, TRUE, FALSE, NULL);
  HANDLE thread = CreateThread(NULL, 0, window_thread, NULL, 0, NULL);
  WaitForSingleObject(ready, 10000);
  Sleep(300);
  if (!window || !tfp_win_focused()) {
    printf("Key hook: not run (no foreground window in this Wine without a display: window=%p foreground=%p)\n", (void *)window, (void *)GetForegroundWindow());
    return 0;
  }
  /* what this Wine's GetAsyncKeyState says of a tap that is over */
  GetAsyncKeyState(VK_F8);
  key(VK_F8, true); Sleep(20); key(VK_F8, false); Sleep(60);
  const SHORT word = GetAsyncKeyState(VK_F8);
  printf("key hook: GetAsyncKeyState after a 20 ms tap that is over: 0x%04x (%s)\n", (unsigned)(uint16_t)word,
         (word & 1) ? "the low bit remembers it: Windows' rule" : "nothing: Proton's rule, only the key messages can show it");

  const int keys[] = {VK_F10, 'U'};
  unsigned levels[2] = {0}, taps[2] = {0};
  for (int i = 0; i < 3; i++) { update(keys, 2, levels, taps); Sleep(100); }
  CHECK(!levels[0] && !taps[0] && !levels[1] && !taps[1]);
  /* F10: down and up between two updates */
  const LONG before = InterlockedCompareExchange(&key_downs, 0, 0);
  const unsigned hooked = tfp_win_key_messages();
  Sleep(30); key(VK_F10, true); Sleep(20); key(VK_F10, false); Sleep(50);
  for (int i = 0; i < 6; i++) { update(keys, 2, levels, taps); Sleep(100); }
  printf("key hook: F10 tapped for 20 ms between two updates: level seen %u times, taps %u, the window got %ld key message(s), the hook saw %u\n",
         levels[0], taps[0], (long)(InterlockedCompareExchange(&key_downs, 0, 0) - before), tfp_win_key_messages() - hooked);
  CHECK(levels[0] == 0 && taps[0] == 1 && tfp_win_key_taps() == 1);
  CHECK(InterlockedCompareExchange(&key_downs, 0, 0) - before == 1 && tfp_win_key_messages() - hooked == 1); /* the hook alone would have carried it */
  /* U: held across four updates */
  key('U', true);
  for (int i = 0; i < 4; i++) { Sleep(100); update(keys, 2, levels, taps); }
  key('U', false);
  for (int i = 0; i < 6; i++) { Sleep(100); update(keys, 2, levels, taps); }
  printf("key hook: U held for 400 ms: level seen %u times, taps %u, the hook saw %u fresh key message(s)\n", levels[1], taps[1], tfp_win_key_messages() - hooked - 1);
  CHECK(tfp_win_key_messages() - hooked == 2);
  CHECK(levels[1] >= 3 && taps[1] == 0 && taps[0] == 1);
  /* F9 tapped while nobody reads it, read from two updates later on: an old answer, no tap */
  key(VK_F9, true); Sleep(20); key(VK_F9, false);
  for (int i = 0; i < 3; i++) { Sleep(100); update(keys, 2, levels, taps); }
  const int late[] = {VK_F9};
  unsigned late_levels = 0, late_taps = 0;
  for (int i = 0; i < 4; i++) { Sleep(100); update(late, 1, &late_levels, &late_taps); }
  CHECK(!late_levels && !late_taps);
  /* the hook off: the window still gets its keys */
  CHECK(tfp_win_keys_end());
  const LONG mid = InterlockedCompareExchange(&key_downs, 0, 0);
  const unsigned seen = tfp_win_key_messages();
  key(VK_F10, true); Sleep(20); key(VK_F10, false); Sleep(200);
  CHECK(InterlockedCompareExchange(&key_downs, 0, 0) == mid + 1 && tfp_win_key_messages() == seen);
  CHECK(tfp_win_keys_end()); /* twice is fine */
  PostMessageW(window, WM_CLOSE, 0, 0);
  WaitForSingleObject(thread, 3000);
  printf("Key hook: %d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
