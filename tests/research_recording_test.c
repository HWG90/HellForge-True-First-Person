#define TFP_READONLY_SDK_CENSUS 1
#include "../src/sdk_inventory.c"
#include <assert.h>
static uint64_t fake_generation = 1;
static bool ready;
static unsigned observed, logs, censuses;
static char folder[MAX_PATH];
uint64_t eng_state_generation(void) { return fake_generation; }
bool eng_ready(void) { return ready; }
bool eng_sdk_census(EngSdkVisitor visitor, void *user, unsigned maximum,
                    EngSdkCensus *out) {
  assert(maximum == 32768);
  censuses++;
  *out = (EngSdkCensus){.rows = 1, .tables = 1, .complete = true};
  return visitor(user, "Camera", 5, 0, false);
}
bool fp_native_observe(const FpMemory *m, FpNativeObservation *out) {
  (void)m;
  observed++;
  memset(out, 0, sizeof *out);
  out->available = FP_OBSERVE_CORE;
  out->unavailable = FP_OBSERVE_ALL & ~FP_OBSERVE_CORE;
  out->owner.kind = 6;
  out->owner.body = 401;
  return true;
}
static const char *logs_folder(HfMod *mod) {
  (void)mod;
  return folder;
}
static void log_line(HfMod *mod, const char *format, ...) {
  (void)mod;
  (void)format;
  logs++;
}
static uintptr_t module(const char *name, size_t *size) {
  (void)name;
  *size = 0;
  return 0;
}
static bool unreadable(void *user, uintptr_t at, void *out, size_t size) {
  (void)user;
  (void)at;
  (void)out;
  (void)size;
  return false;
}
static unsigned tabs(const char *s) {
  unsigned n = 0;
  for (; *s; s++)
    if (*s == '\t')
      n++;
  return n;
}
static void check_file(const char *name, bool compare_columns) {
  char path[1500];
  snprintf(path, sizeof path, "%s/%s", folder, name);
  FILE *file = fopen(path, "rb");
  assert(file);
  char line[16384];
  unsigned columns = 0, rows = 0;
  while (fgets(line, sizeof line, file)) {
    if (line[0] == '#')
      continue;
    if (!columns)
      columns = tabs(line);
    else {
      assert(!compare_columns || tabs(line) == columns);
      rows++;
    }
  }
  assert(rows);
  fclose(file);
  assert(DeleteFileA(path));
}
int main(void) {
  char temp[MAX_PATH];
  assert(GetTempPathA(MAX_PATH, temp));
  assert(GetTempFileNameA(temp, "tfp", 0, folder));
  assert(DeleteFileA(folder) && CreateDirectoryA(folder, NULL));
  HfApi hf = {0};
  hf.size = sizeof hf;
  hf.logs_folder = logs_folder;
  hf.log = log_line;
  hf.module_base = module;
  FpMemory memory = {.read = unreadable};
  fp_sdk_observer(&hf, NULL, &memory, true, 1000);
  assert(!observer_attempted && !observed);
  ready = true;
  fp_sdk_observer(&hf, NULL, &memory, false, 1000);
  assert(!observer_attempted && !observed);
  fp_sdk_observer(&hf, NULL, &memory, true, 1000);
  assert(observer_attempted && observation.file && observed == 1);
  fp_sdk_observer(&hf, NULL, &memory, true, 1020);
  assert(observed == 1);
  fp_sdk_observer(&hf, NULL, &memory, false, 1060);
  assert(observed == 2);
  fake_generation = 2;
  ready = false;
  fp_sdk_observer(&hf, NULL, &memory, true, 1100);
  assert(observer_attempted && !observation.file && observed == 2);
  fake_generation = 3;
  ready = true;
  fp_sdk_observer(&hf, NULL, &memory, true, 1200);
  assert(!observation.file && observed == 2);
  check_file("true-first-person-native-g1.tsv", true);
  /* A new process/session is simulated explicitly, not a VM replacement. */
  observer_attempted = false;
  memset(&observation, 0, sizeof observation);
  fake_generation = 4;
  fp_sdk_observer(&hf, NULL, &memory, true, 10000);
  assert(observed == 3);
  fp_sdk_observer(&hf, NULL, &memory, true, 30000);
  assert(!observation.file && observed == 3);
  check_file("true-first-person-native-g4.tsv", true);
  assert(fp_sdk_inventory_pending());
  fp_sdk_inventory(&hf, NULL, &memory);
  assert(censuses == 1 && !fp_sdk_inventory_pending());
  fp_sdk_inventory(&hf, NULL, &memory);
  assert(censuses == 1);
  check_file("true-first-person-sdk-g4.tsv", true);
  assert(RemoveDirectoryA(folder));
  puts("C research recorder: manual start only, 20Hz/window limits, "
       "flush/finalize, matching TSV columns, VM-close stops without rearming, "
       "session-only latch, one-shot SDK census and no game calls passed");
  return 0;
}
