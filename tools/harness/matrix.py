#!/usr/bin/env python3
"""The test matrix: play every action the harness can produce with the view on and judge each from the mod's log.

Needs a harness session standing in a mission with player control (session.sh up). Takes the keyboard and mouse.

  matrix.py OUT_DIR [phase ...]      phases default to all, in order; see PHASES

A phase passes when the mod logged no frame of the view that was not first person ("+0 missed" on every Lock line
of the phase), the largest camera jump it logged is under LIMIT_M, and nothing was refused. The result is printed
and written to OUT_DIR/matrix.json; one capture per phase goes beside it. A pass says the camera stayed first
person and did not jump; it does not say the picture looks right: look at the captures.
"""
import json
import re
import sys
import time
from pathlib import Path

import drive

HF = Path.home() / ".local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge"
LIMIT_M = 0.05
# name -> (input script, seconds the action needs after the input, capture at this second or None for the end)
PHASES = {
    "idle": ("wait 2", 0),
    "walk": ("hold w 1.5", 0.5),
    "back": ("hold s 1.5", 0.5),
    "strafe": ("hold a 0.8; hold d 0.8", 0.5),
    "sprint": ("down shift; hold w 2.0; up shift", 0.8),
    "look": ("look 400 0; look -800 0; look 400 0; look 0 -300; look 0 600; look 0 -300", 0.5),
    "crouch": ("key c; wait 1.0; hold w 0.8; key c", 1.0),
    "prone": ("key z; wait 1.5; hold w 0.8; key z", 1.8),
    "dive": ("down w; wait 0.3; key alt; wait 0.6; up w", 2.5),
    "dive-back": ("down s; wait 0.3; key alt; wait 0.6; up s", 2.5),
    "stand-up": ("key c; wait 0.2; key c", 1.5),
    "fire": ("click left 0.6", 1.0),
    "aim": ("click right 1.5", 1.0),
    "aim-fire": ("down right; wait 0.7; click left 0.5; wait 0.4; up right", 1.0),
    "aim-walk": ("down right; wait 0.5; hold w 1.0; up right", 1.0),
    "reload": ("click left 0.3; wait 0.3; key r", 4.0),
    "ammo-check": ("hold r 0.8", 2.5),
    "swap-secondary": ("key 2", 2.0),
    "pistol-fire": ("click left 0.3", 1.0),
    "pistol-aim": ("click right 1.5", 1.0),
    "pistol-reload": ("key r", 3.5),
    "swap-support": ("key 3", 2.5),
    "swap-primary": ("key 1", 2.0),
    "quick-swaps": ("key 2; wait 0.25; key 1; wait 0.25; key 2; wait 0.25; key 1", 3.0),
    "grenade": ("key g", 3.5),
    "grenade-held": ("hold g 1.5", 3.5),
    "stim": ("key v", 3.5),
    "melee": ("key f", 2.0),
    "stratagem-menu": ("key ctrl; wait 1.5; key ctrl", 3.0),   # a tap opens the list and a second tap closes it; holding does neither
    "emote": ("key b", 4.0),
    "jump-vault": ("hold w 0.4; key space; wait 0.2; hold w 0.6", 2.5),
    "toggle-off-on": ("key f11; wait 1.0; key f11", 1.5),
    "toggle-fast": ("key f11; key f11; key f11; key f11", 2.0),
    "ragdoll": ("look 0 900; wait 0.2; key g; wait 0.4; look 0 -900; hold s 0.4", 6.0),
    "death": ("look 0 900; wait 0.2; key g; wait 0.6; key g", 9.0),
}
LOCK = re.compile(r"Lock: \+(\d+) frames held \(\+(\d+) in another camera, kind now (\d+)\), \+(\d+) missed \(kind (\d+), helper error (\d+)\), largest kick ([\d.]+)")
CUT = re.compile(r"Cut: largest camera jump ([\d.]+) m")
POP_M = 0.06   # the presented eye's step may change by this much between two frames (30 a second in the harness)


def log_path():
    sessions = sorted((HF / "logs/sessions").iterdir(), key=lambda p: p.stat().st_mtime)
    return sessions[-1] / "mod-goose.true-first-person.log"


def judge(lines, toggles=False):
    held = other = missed = 0
    kick = jump = 0.0
    kinds, notes = set(), []
    for line in lines:
        m = LOCK.search(line)
        if m:
            held += int(m[1]); other += int(m[2]); kinds.add(int(m[3])); kick = max(kick, float(m[7]))
            if int(m[4]):
                missed += int(m[4]); notes.append(f"missed in kind {m[5]} (helper error {m[6]})")
        m = CUT.search(line)
        if m:
            jump = max(jump, float(m[1]))
        if "Refused" in line or "State: left" in line or "waiting:" in line:
            notes.append(line.split(" ", 1)[-1].strip()[:140])
    refused = any("Refused" in n for n in notes)
    # With the Trace lines (0.6.0-feel.23 on) the judge is the presented eye itself, frame by frame: the Cut line
    # measures before the eye continuity and calls an eased 0.7 m a jump.
    import trace as trace_reader
    pop, traced = 0.0, False
    for _, rows in trace_reader.windows(lines):
        steps = [sum((x - y) ** 2 for x, y in zip(a[2], b[2])) ** 0.5 for a, b in zip(rows, rows[1:])]
        pop = max([pop] + [abs(q - s) for s, q in zip(steps, steps[1:])]); traced = True
    if traced:
        jump = pop
    ok = held > 0 and missed == 0 and (jump < (POP_M if traced else LIMIT_M) or toggles) and not refused   # F11 itself is a cut by design
    return {"pass": ok, "frames": held, "other_camera_frames": other, "missed": missed, "kinds": sorted(kinds),
            "largest_jump_m": jump, "from_trace": traced, "largest_kick_deg": kick, "notes": sorted(set(notes))[:6]}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out = Path(argv[1]); out.mkdir(parents=True, exist_ok=True)
    names = argv[2:] or list(PHASES)
    if not drive.focus():
        print("the game window could not be focused"); return 1
    log = log_path(); results = {}
    with drive.Input() as device:
        if "frames held" not in log.read_text(errors="replace")[-4000:]:
            device.run("key f11; wait 2.5")   # the view on
        for name in names:
            script, settle = PHASES[name]
            start = log.stat().st_size
            device.run(script)
            time.sleep(min(settle, 1.0))
            try:
                drive.shot(out / f"{name}.png")
            except drive.DriveError as error:
                print(f"  (no capture: {error})")
            time.sleep(max(0.0, settle - 1.0) + 1.2)   # the Lock line is written once a second
            with open(log, errors="replace") as handle:
                handle.seek(start); lines = handle.read().splitlines()
            results[name] = judge(lines, name.startswith('toggle'))
            r = results[name]
            print(f"{'pass' if r['pass'] else 'FAIL':4} {name:16} frames {r['frames']:4} other-camera {r['other_camera_frames']:4} missed {r['missed']:3} "
                  f"jump {r['largest_jump_m']:.3f} m kick {r['largest_kick_deg']:.1f} deg kinds {r['kinds']}")
            for note in r["notes"]:
                print(f"       {note}")
    (out / "matrix.json").write_text(json.dumps(results, indent=1))
    failed = [n for n, r in results.items() if not r["pass"]]
    print(f"{len(results) - len(failed)} of {len(results)} passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
