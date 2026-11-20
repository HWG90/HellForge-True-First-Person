#!/usr/bin/env bash
# One in-game test session for True First Person.
#   session.sh up [DIST]   install DIST (default dist/true-first-person-feel) with a backup, switch the mod on and
#                          the rig trial off, launch, reach a solo difficulty 1 mission
#   session.sh down [--kill]   quit through the harness and put the mod list back
# Run outside the tool sandbox. The game and launcher must be closed for `up`.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
HF="$HOME/.local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge"
HARNESS="${HD2H_DIR:-$HOME/hellforge-research/tools/BingusDevelopmentHarness}"
KEEP="$HF/backport-backups/tfp-harness-mods.json"
export HD2H_TARGET=hellforge
running() { pgrep -f 'helldivers2\.exe|HellForge\.(Session|LocalBackend)' >/dev/null; }
case "${1:-}" in
up)
  if running; then echo "the game is running; close it first" >&2; exit 1; fi
  dist="${2:-$REPO/dist/true-first-person-feel}"
  stamp="$(date -u +%Y%m%dT%H%M%SZ)"
  mkdir -p "$HF/backport-backups/true-first-person-$stamp"
  cp -a "$HF/mods/true-first-person/." "$HF/backport-backups/true-first-person-$stamp/"
  cp "$dist/goose_true_first_person.dll" "$dist/hellforge-mod.json" "$dist/settings.json" "$HF/mods/true-first-person/"
  [ -f "$KEEP" ] || cp "$HF/mods/hellforge-mods.json" "$KEEP"
  python3 - "$HF/mods/hellforge-mods.json" <<'PY'
import json, sys
path = sys.argv[1]
data = json.load(open(path))
off = set(data["disabled"])
off.discard("goose.true-first-person")
off.add("goose.true-first-person-rig-trial")
data["disabled"] = sorted(off)
json.dump(data, open(path, "w"), indent=2)
PY
  cd "$HARNESS"
  python3 harness.py recover >/dev/null 2>&1 || true
  python3 harness.py start --preset light
  python3 harness.py ship
  python3 harness.py mission --difficulty 1
  ;;
down)
  cd "$HARNESS"
  python3 harness.py stop ${2:+"$2"} || true
  if running; then echo "the game is still running; mod list left as it is" >&2; exit 1; fi
  if [ -f "$KEEP" ]; then cp "$KEEP" "$HF/mods/hellforge-mods.json" && rm "$KEEP"; fi
  ;;
*) sed -n 2,7p "$0"; exit 2 ;;
esac
