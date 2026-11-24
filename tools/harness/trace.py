#!/usr/bin/env python3
"""Read the mod's per-frame "Trace:" lines (written for 0.5 s after each State edge) and judge continuity.

  trace.py [LOG]        default: the newest session's mod log

For each edge window: frames, the largest step of the rendered eye between two frames (m), the largest change of
that step from one frame to the next (a pop shows here; an ease does not), and the largest correction applied.
A window is called a pop when the step changes by more than POP_M between two frames.

The game can set the camera twice in one frame (seen on the frame the view leaves the native view: kind 6, then
kind 4, 0.7 ms apart). Those are two Trace rows of one frame; the later one is what is drawn and is the one kept.
Read as two frames, the second "stands still" and the body's own travel on the next frame reads as a pop.
A row is the same frame again only when it comes under SAME_FRAME_MS after that frame's FIRST row and either its
kind differs or its eye is where the kept row's is. The time alone does not say it: above 200 frames a second every
frame is under 5 ms after the one before, and measured against the row just kept a whole window became one row.
"""
import re
import sys
from pathlib import Path

HF = Path.home() / ".local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge"
POP_M = 0.03
SAME_FRAME_MS = 5.0   # a row this soon after a frame's first row can be that frame set again
SAME_EYE_M = 0.002    # "the same eye": the rows of one frame differed by 0.0001 m in the game
TRACE = re.compile(r"Trace: t=(-?[\d.]+) kind=(\d+) eye_world=\(([-\d. ]+)\) step=([-\d.]+)(?:.*?correction=([-\d.]+))?")
# The same line with what 0.6.0-feel.23 and later builds add to it, each part only where the line has it.
WHOLE = re.compile(r"^(\d+):(\d+):(\d+)\.(\d+) Trace: t=(-?[\d.]+) kind=(\d+) eye_world=\(([-\d. ]+)\) step=[-\d.]+(?: wanted=\(([-\d. ]+)\))?"
                   r"(?:.*? correction=([-\d.]+))?(?:.*? root=\(([-\d. ]+)\))?(?:.*? looked=(\d))?(?:.*? taken=(\S+))?(?:.*? tick=([\d.]+))?")
GAP_MS = 80.0   # two traced frames further apart than this are not consecutive (30 frames a second in the harness)


def newest():
    sessions = sorted((HF / "logs/sessions").iterdir(), key=lambda p: p.stat().st_mtime)
    return sessions[-1] / "mod-goose.true-first-person.log"


def windows(lines):
    edge, rows, first = None, [], None   # first: the time of the first row of the frame rows[-1] belongs to
    for line in lines:
        if " State: " in line:
            if rows:
                yield edge, rows
            edge, rows, first = line.split(" State: ", 1)[1].strip()[:70], [], None
            continue
        m = TRACE.search(line)
        if m and edge is not None:
            eye = [float(x) for x in m[3].split()]
            row = (float(m[1]), int(m[2]), eye, float(m[4]), float(m[5] or 0))
            again = bool(rows) and 0 <= row[0] - first < SAME_FRAME_MS and (
                row[1] != rows[-1][1] or sum((x - y) ** 2 for x, y in zip(row[2], rows[-1][2])) ** 0.5 < SAME_EYE_M)
            if again:
                rows[-1] = row
            else:
                rows.append(row); first = row[0]
    if rows:
        yield edge, rows


def frames(lines):
    """Every traced frame of `lines` in order of time, as dicts, whatever falls between them: at (the line's stamp),
    tick (ms), kind, eye, wanted, carried, root (None where the line has none), looked (None likewise), taken.
    windows() ends a window at every State line, and frames on either side of one were never measured against each
    other (the Stable soak of 2026-11-24: 0.120 m at a landing, 08:18:55.525, with "State: left the native view"
    between its frames); nor were the frames a stretch of the log begins with before its first State line. Here a
    State line only says that "t=" counts from a new edge. The time is "tick=" (the helper's clock, 0.6.0-feel.41 on);
    without it the edge is placed by the line's own stamp, as tools/harness/replay.py does, to a millisecond or two.
    Two rows of one frame are one, the later, as in windows()."""
    groups, group = [], []
    for line in lines:
        if " State: " in line:
            if group:
                groups.append(group)
            group = []
            continue
        m = WHOLE.match(line)
        if not m:
            continue
        def numbers(text):
            return [float(x) for x in text.split()] if text else None
        row = {"at": line[:12], "stamp": ((int(m[1]) * 60 + int(m[2])) * 60 + int(m[3])) * 1000 + int(m[4]), "t": float(m[5]), "kind": int(m[6]),
               "eye": numbers(m[7]), "wanted": numbers(m[8]), "carried": float(m[9] or 0), "root": numbers(m[10]),
               "looked": int(m[11]) if m[11] else None, "taken": m[12] or "-", "clock": float(m[13]) if m[13] else None}
        if group and row["t"] < group[-1]["t"] - 1:   # an edge without a State line: t starts again
            groups.append(group)
            group = []
        group.append(row)
    if group:
        groups.append(group)
    rows = []
    for group in groups:
        edge = min(row["stamp"] - row["t"] for row in group)   # a line is written at or after its frame
        for row in group:
            row["tick"] = row["clock"] if row["clock"] is not None else edge + row["t"]
        rows.extend(group)
    rows.sort(key=lambda row: row["tick"])
    out, first = [], None
    for row in rows:
        again = bool(out) and 0 <= row["tick"] - first < SAME_FRAME_MS and (
            row["kind"] != out[-1]["kind"] or sum((x - y) ** 2 for x, y in zip(row["eye"], out[-1]["eye"])) ** 0.5 < SAME_EYE_M)
        if again:
            out[-1] = row
        else:
            out.append(row)
            first = row["tick"]
    return out


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
