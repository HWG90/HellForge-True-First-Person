#ifndef TFP_SETTINGS_H
#define TFP_SETTINGS_H
#include "camera_policy.h"
#include "placement.h"
typedef struct FpActionMapping {
  int l1, l3;
  FpAction action;
} FpActionMapping;
typedef struct FpSettings {
  int toggle_key, editor_key;
  bool native_ads, show_markers, marker_legacy;
  uint64_t marker_allow[256];
  unsigned marker_count;
  FpActionMapping action_mapping[128];
  unsigned mapping_count;
  int near_cm;
  bool near_aim;
  FpEyeOptions eye;
  FpPlacements placements;
} FpSettings;
void fp_settings_defaults(FpSettings *);
bool fp_settings_load(FpSettings *, const wchar_t *path);
bool fp_settings_save(const FpSettings *, const wchar_t *path);
bool fp_settings_path(wchar_t *out, size_t count);
#endif
