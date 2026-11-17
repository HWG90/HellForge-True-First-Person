/* See wire_win_engine.h. Every function is src/engine.h's of the same name; game thread only. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "wire_win_engine.h"
#include "engine.h"

static const HfApi *hf;
static bool hooked;

static bool api_ready(void *user) { (void)user; return hooked && eng_ready(); }
static uint64_t api_generation(void *user) { (void)user; return eng_state_generation(); }
static const char *api_error(void *user) { (void)user; return eng_error(); }
static bool api_has(void *user, const char *path) { (void)user; return eng_has(path); }
static uintptr_t api_cfunction(void *user, const char *path) { (void)user; return eng_cfunction(path); }
static bool api_call(void *user, const char *path, const char *sig, va_list args) { (void)user; return eng_callv(path, sig, args); }
static void api_unref(void *user, TfpEngRef *ref) { (void)user; eng_unref(ref); }
static uintptr_t api_pointer(void *user, TfpEngRef ref) { (void)user; return eng_pointer(ref); }
static int api_type(void *user, TfpEngRef ref) { (void)user; return eng_type(ref); }
static bool api_boolean(void *user, TfpEngRef ref, bool *out) { (void)user; return eng_toboolean(ref, out); }
static int api_len(void *user, TfpEngRef table) { (void)user; return eng_len(table); }
static bool api_index(void *user, TfpEngRef table, int i, TfpEngRef *out) { (void)user; return eng_index(table, i, out); }
static int api_userdata_array(void *user, TfpEngRef table, uintptr_t *out, int max) { (void)user; return eng_userdata_array(table, out, max); }
/* Retail head_visibility.lua's clock: GetTickCount64() / 1000 (hv:47, 53). */
static double api_ticks(void *user) { (void)user; return (double)GetTickCount64() / 1000; }
static uint64_t api_exe(void *user) {
  size_t size = 0;
  (void)user;
  return hf && hf->module_base ? (uint64_t)hf->module_base("helldivers2.exe", &size) : 0;
}

bool tfp_win_engine_init(const HfApi *api, HfMod *mod) {
  hf = api;
  hooked = eng_init(api, mod);
  return hooked;
}
const TfpWireEngineApi *tfp_win_engine_api(void) {
  static const TfpWireEngineApi table = {NULL, api_ready, api_generation, api_error, api_has, api_cfunction, api_call, api_unref, api_pointer, api_type,
                                         api_boolean, api_len, api_index, api_userdata_array, api_ticks, api_exe};
  return &table;
}
