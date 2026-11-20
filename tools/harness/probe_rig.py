#!/usr/bin/env python3
"""Measure what the game's first-person arm rig does during ordinary actions.

Needs a harness session standing in a mission with player control. Each phase restarts the in-game
recorder (rig_motion.lua), plays real input, then prints one line per rig: a strip with one character per
0.1 s ('.' frozen, ':' under 0.001, '+' under 0.01, '#' above) and where the rig sits in the world.

  probe_rig.py OUT_DIR [phase ...]      phases default to all; see PHASES
"""
import json
import sys
import time
from pathlib import Path

import drive

HERE = Path(__file__).resolve().parent
RECORDER = (HERE / "rig_motion.lua").read_text()

# name -> (input script, seconds to record)
PHASES = {
    "idle": ("", 3),
    "walk": ("wait 0.4; hold w 1.5", 3),
    "sprint": ("wait 0.4; down shift; hold w 1.5; up shift", 3),
    "strafe": ("wait 0.4; hold a 0.8; hold d 0.8", 3),
    "look": ("wait 0.4; look 300 0; look -300 0; look 0 -120; look 0 120", 3),
    "fire": ("wait 0.5; click left 0.5", 3),
    "reload": ("wait 0.5; key r", 5),
    "aim": ("wait 0.5; click right 1.2", 3.5),
    "aim-fire": ("wait 0.5; down right; wait 0.6; click left 0.4; wait 0.4; up right", 3.5),
    "crouch": ("wait 0.5; key c; wait 1.0; key c", 3.5),
    "swap": ("wait 0.5; key 2; wait 1.5; key 1", 4.5),
    "grenade": ("wait 0.5; key g", 4),
    "stim": ("wait 0.5; key v", 4),
    "dive": ("wait 0.4; down w; wait 0.3; key alt; wait 0.5; up w", 4.5),
}


def strip(text):
    out = []
    for value in map(float, text.split()):
        out.append("?" if value < 0 else "." if value == 0 else ":" if value < 0.001 else "+" if value < 0.01 else "#")
    return "".join(out)


def phase(device, name, out_dir, capture):
    script, seconds = PHASES[name]
    if drive.lua(RECORDER) != "recording":
        raise drive.DriveError("the recorder did not start")
    start = time.monotonic()
    device.run(script)
    if capture:
        try:
            drive.shot(out_dir / f"{name}.png")
        except drive.DriveError as error:
            print(f"  (no capture: {error})")
    time.sleep(max(0, seconds - (time.monotonic() - start)))
    report = drive.lua("return TFPRigMotion.report()")
    (out_dir / f"{name}.json").write_text(json.dumps(report, indent=1))
    print(f"{name}: {report['frames']} frames in {report['seconds']:.1f} s" + (f"  ERROR {report['err']}" if report.get("err") else ""))
    for side in ("fp", "body"):
        row = report[side]
        print(f"  {side:4} {strip(row['bins'])}  moved {row['moved']} frames, missing {row['missing']}")
        if side == "fp" and row.get("joints"):
            print(f"       joints: {row['joints']}")
    return report


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out_dir = Path(argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    names = argv[2:] or list(PHASES)
    capture = True
    if not drive.focus():
        print("the game window could not be focused")
        return 1
    with drive.Input() as device:
        for name in names:
            if name.startswith("key:"):  # e.g. key:f11 between phases
                device.hold(name[4:])
                time.sleep(1.5)
                print(f"-- pressed {name[4:]}")
                continue
            phase(device, name, out_dir, capture)
    # The recorder must not keep walking the unit list into a level change: see HARNESS.md.
    drive.lua("TFPRigMotion.on = false return true")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
