/* Plays a timed list of input events (SendInput) while the HellForge game window is in front.
 * Built and started by drive.py inside the game's own Wine prefix; the game must have focus.
 *
 *   win_input.exe            reads script.txt from the current directory, writes result.txt
 *
 * script.txt, one event per line, times in ms from the start, ascending:
 *   <ms> d <vk>     key down (decimal virtual key)      <ms> u <vk>     key up
 *   <ms> d L|R|M    mouse button down                   <ms> u L|R|M    mouse button up
 *   <ms> m <dx> <dy>  move the mouse by this many counts
 * Everything still held is released at the end, also after an interrupt. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HWND target;
static unsigned targets;
static unsigned char key_down[256];
static WPARAM buttons;
static RECT client;

static BOOL CALLBACK collect(HWND window, LPARAM unused) {
  char name[64];
  (void)unused;
  if (IsWindowVisible(window) && GetClassNameA(window, name, sizeof name) && !strcmp(name, "stingray_window")) target = window, targets++;
  return TRUE;
}
/* SendInput goes through Wine's own input queue: the focused game window gets the messages and
 * GetAsyncKeyState sees the keys, which is how True First Person reads its toggle and the aim button. */
static int key(unsigned vk, int down) {
  unsigned scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
  if (!vk || vk > 255 || !scan || scan > 255) return 0;
  INPUT in = {0};
  in.type = INPUT_KEYBOARD;
  in.ki.wVk = (WORD)vk, in.ki.wScan = (WORD)scan, in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  key_down[vk] = (unsigned char)down;
  return SendInput(1, &in, sizeof in) == 1;
}
static int button(char which, int down) {
  INPUT in = {0};
  WPARAM flag;
  in.type = INPUT_MOUSE;
  if (which == 'L') in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP, flag = MK_LBUTTON;
  else if (which == 'R') in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP, flag = MK_RBUTTON;
  else if (which == 'M') in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP, flag = MK_MBUTTON;
  else return 0;
  buttons = down ? buttons | flag : buttons & ~flag;
  return SendInput(1, &in, sizeof in) == 1;
}
static int move(int dx, int dy) {
  INPUT in = {0};
  in.type = INPUT_MOUSE;
  in.mi.dx = dx, in.mi.dy = dy, in.mi.dwFlags = MOUSEEVENTF_MOVE;
  return SendInput(1, &in, sizeof in) == 1;
}
static void release_all(void) {
  if (!target) return;
  for (unsigned vk = 1; vk < 256; vk++) if (key_down[vk]) key(vk, 0);
  if (buttons & MK_LBUTTON) button('L', 0);
  if (buttons & MK_RBUTTON) button('R', 0);
  if (buttons & MK_MBUTTON) button('M', 0);
}
static BOOL WINAPI interrupted(DWORD reason) { (void)reason; release_all(); return FALSE; }

int main(void) {
  FILE *log = fopen("result.txt", "w"), *script = fopen("script.txt", "r");
  if (!log || !script) return 2;
  EnumWindows(collect, 0);
  if (targets != 1 || !GetClientRect(target, &client)) {
    fprintf(log, "game windows=%u\n", targets);
    fclose(log);
    return 3;
  }
  if (GetForegroundWindow() != target) {
    fprintf(log, "the game window is not in front (foreground=%p)\n", (void *)GetForegroundWindow());
    fclose(log);
    return 5;
  }
  fprintf(log, "window=%p foreground=%p client=%ldx%ld\n", (void *)target, (void *)GetForegroundWindow(), client.right, client.bottom);
  atexit(release_all);
  SetConsoleCtrlHandler(interrupted, TRUE);
  ULONGLONG start = GetTickCount64();
  char line[128];
  unsigned played = 0, failed = 0;
  while (fgets(line, sizeof line, script)) {
    unsigned long at;
    char kind, code[16];
    int dx, dy, ok = 0;
    if (sscanf(line, "%lu m %d %d", &at, &dx, &dy) == 3) kind = 'm';
    else if (sscanf(line, "%lu %c %15s", &at, &kind, code) != 3 || (kind != 'd' && kind != 'u')) continue;
    ULONGLONG now = GetTickCount64() - start;
    if (at > now) Sleep((DWORD)(at - now));
    if (!IsWindow(target)) break;
    if (kind == 'm') ok = move(dx, dy);
    else if (code[0] == 'L' || code[0] == 'R' || code[0] == 'M') ok = button(code[0], kind == 'd');
    else ok = key((unsigned)strtoul(code, NULL, 10), kind == 'd');
    played++, failed += !ok;
  }
  release_all();
  fprintf(log, "played=%u failed=%u foreground after=%p\n", played, failed, (void *)GetForegroundWindow());
  fclose(log);
  fclose(script);
  return failed ? 4 : 0;
}
