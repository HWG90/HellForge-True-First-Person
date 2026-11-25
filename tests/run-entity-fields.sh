#!/usr/bin/env bash
# HellForge 0.5's moved fields (src/entity_fields.h) and the two snapshots that read through them
# (tests/entity_fields_test.c): built as the tests are, then with -DTFP_FIELDS_REQUIRED as the shipped DLLs are.
# Needs only a C compiler.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
for tfp_mode in tests shipped; do
  tfp_defines=()
  if [ "$tfp_mode" = shipped ]; then tfp_defines=(-DTFP_FIELDS_REQUIRED=1); fi
  "$TFP_CC" -std=c11 -O1 -Wall -Wextra -Werror -Wno-unused-function "${tfp_defines[@]}" -I "$TFP_ROOT/src" -I "$TFP_ROOT/tests" \
    "$TFP_ROOT/src/backend.c" "$TFP_ROOT/src/snapshot.c" "$TFP_ROOT/tests/entity_fields_test.c" -lm \
    -o "$TFP_ROOT/work/tests/entity_fields_$tfp_mode"
  "$TFP_ROOT/work/tests/entity_fields_$tfp_mode"
done
# The fifteen instructions and the two masked pins against build 25480438's image ("skipped" without it).
python3 "$TFP_ROOT/tools/verify_entity_sites.py" | tail -1
