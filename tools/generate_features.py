"""Emit runtime C contracts from reviewed build25480438 evidence, no runtime Python."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
doc=json.loads((root/'docs/feature-contract.json').read_text())
lines=['/* Generated from reviewed original-build evidence. */','#ifndef TFP_FEATURES_H','#define TFP_FEATURES_H','#include "backend.h"','typedef enum FpFeature {FP_FEATURE_AIM,FP_FEATURE_MOVEMENT,FP_FEATURE_MARKERS,FP_FEATURE_MESH,FP_FEATURE_EFFECTS,FP_FEATURE_BLUR} FpFeature;','typedef struct FpFeatureSite {FpFeature feature;bool exe;uint32_t rva;const char *hex;} FpFeatureSite;','static const FpFeatureSite fp_feature_sites[]={']
for s in doc['sites']:lines.append(' {%s,%s,UINT32_C(%s),"%s"},'%(s['feature'],'true' if s['exe'] else 'false',s['rva'],s['hex']))
lines+=['};','static inline bool fp_feature_contract(const FpMemory *m,FpFeature feature){',' for(unsigned i=0;i<sizeof fp_feature_sites/sizeof *fp_feature_sites;i++){const FpFeatureSite *s=fp_feature_sites+i;if(s->feature==feature&&!fp_bytes(m,(s->exe?m->exe:m->game)+s->rva,s->hex))return false;}return true;','}','#endif']
(root/'src/feature_contract.h').write_text('\n'.join(lines)+'\n')
