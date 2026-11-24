#!/usr/bin/env bash
# The placement editor's keys (Page Up / Page Down no longer depth; the wheel is): tests/editor_keys_test.c.
set -euo pipefail
TFP_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TFP_CC="${CC:-cc}"
mkdir -p "$TFP_ROOT/work/tests"
# The shipped source: TFP_ED_RETAIL_PAGE_KEYS (retail's Page Up / Page Down depth keys, for the retail
# comparison only) is set by tests/run-vectors-placement-editor.sh and nothing else, never by build.sh; and
# src/placement_editor.c preprocessed as build.sh's feel build compiles it binds no key 33 or 34 and has no
# PgUp/PgDn help.
tfp_setters="$(grep -rl --include='*.sh' --include='*.py' --include='*.json' --include='Makefile' --exclude=run-editor-keys.sh \
  -e TFP_ED_RETAIL_PAGE_KEYS "$TFP_ROOT/build.sh" "$TFP_ROOT/tests" "$TFP_ROOT/tools" "$TFP_ROOT/manifests" 2>/dev/null | sort || true)"
if [ "$tfp_setters" != "$TFP_ROOT/tests/run-vectors-placement-editor.sh" ] \
   || grep -rq '#[[:space:]]*define[[:space:]]*TFP_ED_RETAIL_PAGE_KEYS' "$TFP_ROOT/src"; then
  echo "editor keys: FAILED TFP_ED_RETAIL_PAGE_KEYS is set outside tests/run-vectors-placement-editor.sh: $tfp_setters"; exit 1
fi
tfp_shipped="$("$TFP_CC" -std=c11 -E -P -DTFP_FEEL=1 -I "$TFP_ROOT/src" "$TFP_ROOT/src/placement_editor.c")"
tfp_bindings="$(printf '%s\n' "$tfp_shipped" | tr -d ' \n' | grep -o 'BINDINGS\[\]={[^;]*;')"
if [ "$tfp_bindings" != 'BINDINGS[]={{37,0,-1},{39,0,1},{38,2,1},{40,2,-1}};' ] || printf '%s' "$tfp_shipped" | grep -q 'PgUp\|PgDn'; then
  echo "editor keys: FAILED the shipped editor still has Page Up / Page Down: $tfp_bindings"; exit 1
fi
echo "editor keys: the shipped source binds only the four arrows (no 33/34, no PgUp/PgDn help); the retail switch is set only by the retail comparison"
"$TFP_CC" -std=c11 -Wall -Wextra -Werror -ffp-contract=off -fsanitize=address,undefined -fno-sanitize-recover=all -g \
  -I "$TFP_ROOT/src" "$TFP_ROOT/src/placement_editor.c" "$TFP_ROOT/tests/editor_keys_test.c" -lm -o "$TFP_ROOT/work/tests/editor_keys"
ASAN_OPTIONS=detect_leaks=0 "$TFP_ROOT/work/tests/editor_keys"
