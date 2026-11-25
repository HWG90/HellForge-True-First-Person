/* A stand-in for <windows.h> so tools/harness/win_input.c builds and runs on Linux in a test
 * (tests/test_harness_aim.py): one game window, always in front; every SendInput is written to sent.txt as
 * "<ms> key <vk> down|up", "<ms> button L|R|M down|up", "<ms> move dx dy" or "<ms> wheel delta"; Sleep moves a fake clock. */
#ifndef TFP_HARNESS_WIN_STUB_H
#define TFP_HARNESS_WIN_STUB_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned long long ULONGLONG;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef void *HWND;
typedef struct { long left, top, right, bottom; } RECT;
typedef struct { long x, y; } POINT;
#define TRUE 1
#define FALSE 0
#define CALLBACK
#define WINAPI
#define MAPVK_VK_TO_VSC 0
#define INPUT_MOUSE 0
#define INPUT_KEYBOARD 1
#define KEYEVENTF_EXTENDEDKEY 0x1
#define KEYEVENTF_KEYUP 0x2
#define MOUSEEVENTF_MOVE 0x1
#define MOUSEEVENTF_LEFTDOWN 0x2
#define MOUSEEVENTF_LEFTUP 0x4
#define MOUSEEVENTF_RIGHTDOWN 0x8
#define MOUSEEVENTF_RIGHTUP 0x10
#define MOUSEEVENTF_MIDDLEDOWN 0x20
#define MOUSEEVENTF_MIDDLEUP 0x40
#define MOUSEEVENTF_WHEEL 0x800
#define WHEEL_DELTA 120
#define MK_LBUTTON 0x1
#define MK_RBUTTON 0x2
#define MK_MBUTTON 0x10
#define VK_PRIOR 0x21
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define WM_MOUSEMOVE 0x200
#define WM_LBUTTONDOWN 0x201
#define WM_LBUTTONUP 0x202
#define MAKELPARAM(l, h) ((LPARAM)(((uint32_t)(l) & 0xffff) | ((uint32_t)(h) << 16)))
typedef struct { WORD wVk, wScan; DWORD dwFlags; } KEYBDINPUT;
typedef struct { long dx, dy; DWORD mouseData, dwFlags; } MOUSEINPUT;
typedef struct { DWORD type; struct { KEYBDINPUT ki; MOUSEINPUT mi; }; } INPUT;
typedef BOOL (*WNDENUMPROC)(HWND, LPARAM);
typedef BOOL (*PHANDLER_ROUTINE)(DWORD);

static int stub_window;
static ULONGLONG stub_clock;
static FILE *stub_sent(void) {
  static FILE *f;
  if (!f) f = fopen("sent.txt", "w");
  return f;
}
static inline BOOL EnumWindows(WNDENUMPROC f, LPARAM l) { f((HWND)&stub_window, l); return TRUE; }
static inline BOOL IsWindowVisible(HWND w) { return w == (HWND)&stub_window; }
static inline int GetClassNameA(HWND w, char *name, int size) { (void)w; snprintf(name, (size_t)size, "stingray_window"); return 1; }
static inline BOOL GetClientRect(HWND w, RECT *r) { (void)w; r->left = r->top = 0, r->right = 1920, r->bottom = 1080; return TRUE; }
static inline HWND GetForegroundWindow(void) { return (HWND)&stub_window; }
static inline BOOL IsWindow(HWND w) { return w == (HWND)&stub_window; }
static inline unsigned MapVirtualKeyW(unsigned vk, unsigned type) { (void)type; return vk ? 0x10 + (vk & 0x7f) : 0; }
static inline void Sleep(DWORD ms) { stub_clock += ms; }
static inline ULONGLONG GetTickCount64(void) { return stub_clock; }
static inline BOOL ClientToScreen(HWND w, POINT *p) { (void)w; (void)p; return TRUE; }
static inline BOOL SetCursorPos(int x, int y) { (void)x; (void)y; return TRUE; }
static inline BOOL PostMessageW(HWND w, unsigned m, WPARAM wp, LPARAM lp) { (void)w; (void)m; (void)wp; (void)lp; return TRUE; }
static inline BOOL SetConsoleCtrlHandler(PHANDLER_ROUTINE f, BOOL add) { (void)f; (void)add; return TRUE; }
static inline unsigned SendInput(unsigned n, INPUT *in, int size) {
  (void)size;
  FILE *f = stub_sent();
  if (in->type == INPUT_KEYBOARD)
    fprintf(f, "%llu key %u %s\n", stub_clock, in->ki.wVk, in->ki.dwFlags & KEYEVENTF_KEYUP ? "up" : "down");
  else if (in->mi.dwFlags == MOUSEEVENTF_WHEEL)   /* mouseData is a DWORD carrying a signed delta, as in Windows */
    fprintf(f, "%llu wheel %d\n", stub_clock, (int)(int32_t)(uint32_t)in->mi.mouseData);
  else if (in->mi.dwFlags == MOUSEEVENTF_MOVE)
    fprintf(f, "%llu move %ld %ld\n", stub_clock, in->mi.dx, in->mi.dy);
  else {
    const DWORD d = in->mi.dwFlags;
    const char *which = d & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP) ? "L" : d & (MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP) ? "R" : "M";
    fprintf(f, "%llu button %s %s\n", stub_clock, which, d & (MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_MIDDLEDOWN) ? "down" : "up");
  }
  fflush(f);
  return n;
}
#endif
