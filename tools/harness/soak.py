#!/usr/bin/env python3
"""Soak: random actions back to back for MINUTES, with short random pauses and overlaps, judged from the mod's log.

  soak.py [MINUTES] [SEED]        default 10 minutes, seed 1

Needs a harness session in a mission with player control; takes the keyboard and mouse. One action at a time is what
matrix.py checks; this looks for what one action leaves behind for the next: a frame out of first person, a camera
step over POP_M between two traced frames, a refusal, the arm drive refusing, an action after which the view does
not come back. Every action and what the judge said of its stretch of the log go to stdout; the seed repeats a run.
The view is on at the start and is not toggled (toggling is a cut by design). No death: that ends the body.
"""
import random
import sys
import time

import drive
import matrix

SKIP = {"toggle-off-on", "toggle-fast", "ragdoll", "death", "idle"}
# What is left pressed or changed by a phase and has to be undone before the next (a crouch that is left on).
OVERLAPS = [   # two inputs at once, as a player does
    "down w; key g; wait 1.5; up w", "down w; key v; wait 2.0; up w", "down shift; down w; wait 0.8; key 2; wait 1.2; up w; up shift",
    "key 2; wait 0.3; key g", "key g; wait 0.4; key 1", "key r; wait 0.5; key 2", "key v; wait 0.6; click right 0.8",
    "down right; wait 0.4; key 2; wait 1.0; up right", "key c; wait 0.4; key g; wait 2.5; key c", "key z; wait 1.0; key v; wait 3.0; key z",
    "key ctrl; wait 0.6; key g", "key 2; wait 0.15; key 2; wait 0.15; key 1", "down w; wait 0.2; key alt; wait 0.3; key 2; wait 1.0; up w",
    "key b; wait 0.8; key g", "look 300 200; key g; look -300 -200", "key f; wait 0.2; key f; wait 0.2; key r",
]


def main(argv):
    minutes = float(argv[1]) if len(argv) > 1 else 10.0
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 1)
    names = [n for n in matrix.PHASES if n not in SKIP]
    d = drive.Input()
    log = matrix.log_path()
    whole = log.stat().st_size
    end = time.time() + minutes * 60
    worst, bad, count = 0.0, [], 0
    while time.time() < end:
        if rng.random() < 0.3:
            name, script, settle = "overlap", rng.choice(OVERLAPS), 3.0
        else:
            name = rng.choice(names)
            script, settle = matrix.PHASES[name]
        settle *= rng.choice((0.15, 0.4, 1.0))   # often the next action begins before this one is over
        start = log.stat().st_size
        try:
            d.run(f"{script}; wait {settle:.2f}")
        except drive.DriveError as error:
            print(f"stopped: {error}")
            break
        with open(log, errors="replace") as handle:
            handle.seek(start)
            lines = handle.read().splitlines()
        j = matrix.judge(lines)
        count += 1
        worst = max(worst, j["largest_jump_m"])
        flag = j["missed"] or j["largest_jump_m"] >= matrix.POP_M or any("Refused" in n for n in j["notes"])
        if flag:
            bad.append((count, name, script, j["missed"], round(j["largest_jump_m"], 3)))
        print(f"{count:3d} {'BAD ' if flag else 'ok  '}{name:16s} missed {j['missed']} pop {j['largest_jump_m']:.3f} kinds {j['kinds']}  {script[:60]}", flush=True)
    d.run("up w; up s; up shift; up right; wait 2.0")
    with open(log, errors="replace") as handle:
        handle.seek(whole)
        lines = handle.read().splitlines()
    j = matrix.judge(lines)
    refused = sorted({l.split(" ", 1)[-1][:120] for l in lines if "Refused" in l or "refused 1" in l or " error " in l.lower()})[:8]
    arms = [l for l in lines if " Arms:" in l and "refused 0" not in l][:3]
    print(f"\n{count} actions, {j['frames']} frames held, {j['missed']} missed, largest step {worst:.3f} m; {len(bad)} flagged")
    for row in bad:
        print("  flagged:", row)
    for text in refused:
        print("  line:", text)
    for text in arms:
        print("  arms:", text[13:170])
    last = [l for l in lines if " State:" in l or " Status:" in l][-2:]
    print("  ends:", [l[13:110] for l in last])
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
