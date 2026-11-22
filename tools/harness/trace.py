#!/usr/bin/env python3
"""Read the mod's per-frame "Trace:" lines (written for 0.5 s after each State edge) and judge continuity.

  trace.py [LOG]        default: the newest session's mod log

For each edge window: frames, the largest step of the rendered eye between two frames (m), the largest change of
that step from one frame to the next (a pop shows here; an ease does not), and the largest correction applied.
A window is called a pop when the step changes by more than POP_M between two frames.

The game can set the camera twice in one frame (seen on the frame the view leaves the native view: kind 6, then
kind 4, 0.7 ms apart). Those are two Trace rows of one frame; the later one is what is drawn and is the one kept.
Read as two frames, the second "stands still" and the body's own travel on the next frame reads as a pop.
"""
import re
import sys
from pathlib import Path

HF = Path.home() / ".local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge"
POP_M = 0.03
SAME_FRAME_MS = 5.0   # two rows closer than this are one frame set twice
TRACE = re.compile(r"Trace: t=(-?[\d.]+) kind=(\d+) eye_world=\(([-\d. ]+)\) step=([-\d.]+)(?:.*?correction=([-\d.]+))?")


def newest():
    sessions = sorted((HF / "logs/sessions").iterdir(), key=lambda p: p.stat().st_mtime)
    return sessions[-1] / "mod-goose.true-first-person.log"


def windows(lines):
    edge, rows = None, []
    for line in lines:
        if " State: " in line:
            if rows:
                yield edge, rows
            edge, rows = line.split(" State: ", 1)[1].strip()[:70], []
            continue
        m = TRACE.search(line)
        if m and edge is not None:
            eye = [float(x) for x in m[3].split()]
            row = (float(m[1]), int(m[2]), eye, float(m[4]), float(m[5] or 0))
            if rows and 0 <= row[0] - rows[-1][0] < SAME_FRAME_MS:
                rows[-1] = row
            else:
                rows.append(row)
    if rows:
        yield edge, rows


def main(argv):
    log = Path(argv[1]) if len(argv) > 1 else newest()
    pops = total = 0
    for edge, rows in windows(log.read_text(errors="replace").splitlines()):
        steps = []
        for a, b in zip(rows, rows[1:]):
            steps.append(sum((x - y) ** 2 for x, y in zip(a[2], b[2])) ** 0.5)
        change = max((abs(q - p) for p, q in zip(steps, steps[1:])), default=0.0)
        kinds = "".join(str(k) if k < 10 else "x" for _, k, *_ in rows)
        pop = change > POP_M
        pops += pop; total += 1
        print(f"{'POP ' if pop else 'ok  '} {edge:70} frames {len(rows):3} max step {max(steps, default=0):.3f} m  step change {change:.3f} m  "
              f"correction {max((abs(r[4]) for r in rows), default=0):.3f} m  kinds {kinds[:40]}")
    print(f"{total - pops} of {total} edge windows without a pop (limit {POP_M} m between two frames' steps)")
    return 1 if pops else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
