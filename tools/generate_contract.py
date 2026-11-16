"""Emit the C witness table from reviewed target-build metadata; no game access."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
data = json.loads((ROOT / 'docs/native-contract.json').read_text())
assert data['build'] == 25480438
lines = [
    '/* Exact build25480438 native-policy witnesses; reviewed target captures. */',
    '#ifndef TFP_NATIVE_CONTRACT_H', '#define TFP_NATIVE_CONTRACT_H',
    '#include "backend.h"',
    'typedef struct FpNativeSite {const char *name;bool exe;uint32_t rva;const char *hex;} FpNativeSite;',
    'static const FpNativeSite fp_native_sites[]={',
]
names = set()
for row in data['native_sites']:
    assert row['name'] not in names
    names.add(row['name'])
    assert row['module'] in {'exe', 'game'}
    assert 0 < int(row['rva'], 16) < 0x4744000
    assert bytes.fromhex(row['hex']) and len(bytes.fromhex(row['hex'])) <= 128
    lines.append('{"%s",%s,UINT32_C(%s),"%s"},' % (
        row['name'], 'true' if row['module'] == 'exe' else 'false', row['rva'], row['hex']))
lines += ['};', '#define FP_NATIVE_SITE_COUNT (sizeof fp_native_sites/sizeof fp_native_sites[0])', '#endif']
(ROOT / 'src/native_contract.h').write_text('\n'.join(lines) + '\n')
