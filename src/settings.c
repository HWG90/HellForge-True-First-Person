#define WIN32_LEAN_AND_MEAN
#include "settings.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <windows.h>
void fp_settings_defaults(FpSettings *s) {
  memset(s, 0, sizeof *s);
  s->toggle_key = VK_F11;
  s->editor_key = VK_F8;
  s->show_markers = true;
  s->near_cm = 5;
  s->eye.forward = .18f;
  s->eye.up = .04f;
  s->eye.kick = true;
  s->eye.motion = (FpMotionOptions){.enabled = true,
                                    .scale = 1,
                                    .bob_strength = 1,
                                    .bob_speed = 1,
                                    .sway_strength = 1,
                                    .sway_speed = 1,
                                    .sprint_strength = 1,
                                    .camera_bob_strength = .25,
                                    .camera_bob_speed = 1,
                                    .camera_sway_strength = .25,
                                    .camera_sway_speed = 1};
  fp_placements_init(&s->placements);
}
bool fp_settings_path(wchar_t *out, size_t count) {
  wchar_t folder[1024];
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", folder, 1024);
  if (!n || n >= 1024 || count < 1100)
    return false;
  if (swprintf(out, count, L"%ls\\HellForge", folder) < 0)
    return false;
  CreateDirectoryW(out, NULL);
  if (swprintf(out, count, L"%ls\\HellForge\\TrueFirstPerson", folder) < 0)
    return false;
  CreateDirectoryW(out, NULL);
  return swprintf(out, count, L"%ls\\HellForge\\TrueFirstPerson\\settings.ini",
                  folder) > 0;
}
static double numeric(const wchar_t *path, const wchar_t *key, double fallback,
                      double min, double max) {
  wchar_t raw[64], *end = NULL;
  if (!GetPrivateProfileStringW(L"settings", key, L"", raw, 64, path))
    return fallback;
  double v = wcstod(raw, &end);
  return end && !*end && isfinite(v) && v >= min && v <= max ? v : fallback;
}
static bool placement(const wchar_t *path, const wchar_t *section,
                      const wchar_t *key, FpPlacement *out) {
  wchar_t raw[512];
  if (!GetPrivateProfileStringW(section, key, L"", raw, 512, path))
    return false;
  double v[6];
  wchar_t *at = raw;
  for (unsigned i = 0; i < 6; i++) {
    wchar_t *end = NULL;
    v[i] = wcstod(at, &end);
    if (end == at || !isfinite(v[i]) || (i < 5 ? *end != L',' : *end != 0))
      return false;
    at = end + 1;
  }
  *out = (FpPlacement){v[0], v[1], v[2], v[3], v[4], v[5]};
  return fp_placement_valid(out);
}
bool fp_settings_load(FpSettings *s, const wchar_t *path) {
  fp_settings_defaults(s);
  if (!path || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    return true;
  s->toggle_key = (int)numeric(path, L"toggle_key", VK_F11, 0, 255);
  s->editor_key = (int)numeric(path, L"editor_key", VK_F8, 0, 255);
  s->native_ads = numeric(path, L"native_ads", 0, 0, 1) == 1;
  s->show_markers = numeric(path, L"show_markers", 1, 0, 1) == 1;
  s->near_cm = (int)numeric(path, L"near_cm", 5, 5, 40);
  s->near_aim = numeric(path, L"near_aim", 0, 0, 1) == 1;
  s->eye.motion.enabled = numeric(path, L"motion_enabled", 1, 0, 1) == 1;
  s->eye.motion.scale = numeric(path, L"motion_strength", 1, 0, 2);
  s->eye.motion.bob_strength = numeric(path, L"weapon_bob_strength", 1, 0, 3);
  s->eye.motion.bob_speed = numeric(path, L"weapon_bob_speed", 1, .25, 2);
  s->eye.motion.sway_strength = numeric(path, L"weapon_sway_strength", 1, 0, 3);
  s->eye.motion.sway_speed = numeric(path, L"weapon_sway_speed", 1, .25, 2);
  s->eye.motion.sprint_strength = numeric(path, L"sprint_strength", 1, 0, 2);
  s->eye.motion.camera_bob_strength =
      numeric(path, L"camera_bob_strength", .25, 0, 2);
  s->eye.motion.camera_bob_speed =
      numeric(path, L"camera_bob_speed", 1, .25, 2);
  s->eye.motion.camera_sway_strength =
      numeric(path, L"camera_sway_strength", .25, 0, 2);
  s->eye.motion.camera_sway_speed =
      numeric(path, L"camera_sway_speed", 1, .25, 2);
  s->eye.kick = numeric(path, L"camera_kick", 1, 0, 1) == 1;
  s->marker_legacy = numeric(path, L"marker_legacy", 0, 0, 1) == 1;
  static wchar_t entries[8192];
  DWORD used =
      GetPrivateProfileSectionW(L"legacy_markers", entries, 8192, path);
  if (used >= 8190)
    return false;
  for (wchar_t *at = entries; *at; at += wcslen(at) + 1) {
    wchar_t *eq = wcschr(at, L'=');
    if (!eq || eq - at != 16 || wcscmp(eq + 1, L"1"))
      continue;
    wchar_t key[17];
    memcpy(key, at, 16 * sizeof *key);
    key[16] = 0;
    wchar_t *end = NULL;
    uint64_t resource = wcstoull(key, &end, 16);
    if (end && !*end && resource && s->marker_count < 256)
      s->marker_allow[s->marker_count++] = resource;
  }
  used = GetPrivateProfileSectionW(L"action_map", entries, 8192, path);
  if (used >= 8190)
    return false;
  for (wchar_t *at = entries; *at; at += wcslen(at) + 1) {
    wchar_t *end = NULL;
    long l1 = wcstol(at, &end, 10);
    if (end == at || *end != L':' || l1 < -1 || l1 > 2047)
      continue;
    wchar_t *next = NULL;
    long l3 = wcstol(end + 1, &next, 10);
    if (next == end + 1 || *next != L'=' || l3 < -1 || l3 > 2047)
      continue;
    for (FpAction a = FP_IDLE; a < FP_ACTION_COUNT; a++) {
      wchar_t name[32];
      MultiByteToWideChar(CP_UTF8, 0, fp_action_name(a), -1, name, 32);
      if (!wcscmp(next + 1, name) && s->mapping_count < 128)
        s->action_mapping[s->mapping_count++] =
            (FpActionMapping){(int)l1, (int)l3, a};
    }
  }
  static wchar_t sections[16384];
  DWORD n = GetPrivateProfileSectionNamesW(sections, 16384, path);
  if (n >= 16382)
    return false;
  for (const wchar_t *at = sections; *at; at += wcslen(at) + 1) {
    if (wcsncmp(at, L"placement:", 10))
      continue;
    bool all = !wcscmp(at + 10, L"__all");
    uint64_t weapon = 1;
    if (!all) {
      if (wcslen(at + 10) != 16)
        continue;
      wchar_t *end = NULL;
      weapon = wcstoull(at + 10, &end, 16);
      if (!end || *end || !weapon)
        continue;
    }
    FpPlacement legacy;
    if (placement(path, at, L"legacy", &legacy)) {
      FpWeaponProfile *profile = all ? &s->placements.shared : NULL;
      for (unsigned i = 0; !all && i < s->placements.count; i++)
        if (s->placements.weapon[i].weapon == weapon)
          profile = s->placements.weapon + i;
      if (!profile) {
        if (s->placements.count == FP_PROFILE_LIMIT)
          return false;
        profile = s->placements.weapon + s->placements.count++;
        memset(profile, 0, sizeof *profile);
        profile->weapon = weapon;
      }
      profile->legacy = legacy;
      profile->legacy_present = true;
    }
    for (FpAction a = FP_IDLE; a < FP_ACTION_COUNT; a++) {
      FpPlacement p;
      wchar_t key[32];
      MultiByteToWideChar(CP_UTF8, 0, fp_action_name(a), -1, key, 32);
      if (placement(path, at, key, &p)) {
        if (all) {
          s->placements.shared.action[a] = p;
          s->placements.shared.present[a] = true;
        } else if (!fp_placement_save(&s->placements, weapon, a, &p, false))
          return false;
      }
    }
  }
  return true;
}
typedef struct Text {
  char *at;
  size_t remaining;
  bool ok;
} Text;
static void append(Text *t, const char *format, ...) {
  if (!t->ok)
    return;
  va_list ap;
  va_start(ap, format);
  int n = vsnprintf(t->at, t->remaining, format, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= t->remaining) {
    t->ok = false;
    return;
  }
  t->at += n;
  t->remaining -= (size_t)n;
}
static void profile(Text *t, const FpWeaponProfile *p, bool shared) {
  bool present = p->legacy_present;
  for (FpAction a = FP_IDLE; a < FP_ACTION_COUNT; a++)
    present |= p->present[a];
  if (!present)
    return;
  if (shared)
    append(t, "\r\n[placement:__all]\r\n");
  else
    append(t, "\r\n[placement:%016llx]\r\n", (unsigned long long)p->weapon);
  if (p->legacy_present && fp_placement_valid(&p->legacy)) {
    const FpPlacement *v = &p->legacy;
    append(t, "legacy=%.12g,%.12g,%.12g,%.12g,%.12g,%.12g\r\n", v->x, v->y,
           v->z, v->yaw, v->pitch, v->roll);
  }
  for (FpAction a = FP_IDLE; a < FP_ACTION_COUNT; a++)
    if (p->present[a] && fp_placement_valid(p->action + a)) {
      const FpPlacement *v = p->action + a;
      append(t, "%s=%.12g,%.12g,%.12g,%.12g,%.12g,%.12g\r\n", fp_action_name(a),
             v->x, v->y, v->z, v->yaw, v->pitch, v->roll);
    }
}
bool fp_settings_save(const FpSettings *s, const wchar_t *path) {
  if (!s || !path || s->placements.count > FP_PROFILE_LIMIT)
    return false;
  size_t capacity = 512000;
  char *buffer = HeapAlloc(GetProcessHeap(), 0, capacity);
  if (!buffer)
    return false;
  Text t = {buffer, capacity, true};
  append(&t, "; True First Person C settings. Positions are metres, angles "
             "degrees.\r\n[settings]\r\n");
  append(&t,
         "toggle_key=%d\r\neditor_key=%d\r\nnative_ads=%d\r\nshow_markers=%"
         "d\r\nnear_cm=%d\r\nnear_aim=%d\r\n",
         s->toggle_key, s->editor_key, s->native_ads, s->show_markers,
         s->near_cm, s->near_aim);
  const FpMotionOptions *m = &s->eye.motion;
  append(&t,
         "motion_enabled=%d\r\nmotion_strength=%.9g\r\nweapon_bob_strength=%."
         "9g\r\nweapon_bob_speed=%.9g\r\nweapon_sway_strength=%.9g\r\nweapon_"
         "sway_speed=%.9g\r\nsprint_strength=%.9g\r\ncamera_bob_strength=%."
         "9g\r\ncamera_bob_speed=%.9g\r\ncamera_sway_strength=%.9g\r\ncamera_"
         "sway_speed=%.9g\r\ncamera_kick=%d\r\n",
         m->enabled, m->scale, m->bob_strength, m->bob_speed, m->sway_strength,
         m->sway_speed, m->sprint_strength, m->camera_bob_strength,
         m->camera_bob_speed, m->camera_sway_strength, m->camera_sway_speed,
         s->eye.kick);
  append(&t, "marker_legacy=%d\r\n\r\n[legacy_markers]\r\n", s->marker_legacy);
  for (unsigned i = 0; i < s->marker_count && i < 256; i++)
    append(&t, "%016llx=1\r\n", (unsigned long long)s->marker_allow[i]);
  append(&t, "\r\n[action_map]\r\n");
  for (unsigned i = 0; i < s->mapping_count && i < 128; i++)
    append(&t, "%d:%d=%s\r\n", s->action_mapping[i].l1, s->action_mapping[i].l3,
           fp_action_name(s->action_mapping[i].action));
  profile(&t, &s->placements.shared, true);
  for (unsigned i = 0; i < s->placements.count; i++)
    profile(&t, s->placements.weapon + i, false);
  wchar_t temporary[1200];
  bool ok = t.ok && swprintf(temporary, 1200, L"%ls.tmp", path) > 0;
  if (ok) {
    HANDLE file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    ok = file != INVALID_HANDLE_VALUE;
    if (ok) {
      DWORD done = 0, n = (DWORD)(t.at - buffer);
      ok = WriteFile(file, buffer, n, &done, NULL) && done == n &&
           FlushFileBuffers(file);
      CloseHandle(file);
    }
    if (ok)
      ok = MoveFileExW(temporary, path,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    else
      DeleteFileW(temporary);
  }
  HeapFree(GetProcessHeap(), 0, buffer);
  return ok;
}
