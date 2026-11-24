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
# What later builds put after "tick=", read apart so that the line may grow: which way the view pointed (0.6.0-feel.49),
# and what the arm drive drew in the frame and whether the eye opened the window itself (round 13: sock, arms, eye_edge).
VIEW = re.compile(r" view=\(([-\d. ]+)\)")
DRAWN = re.compile(r" sock=(-?[\d.]+) arms=(\d+)(?: eye_edge=(\d))?")
# Round 19: the helmet veil's move of the eye handed to the game, after eye_edge=: "veil=0" or "veil=(x y z)".
VEIL = re.compile(r" veil=(?:\(([-\d. ]+)\)|0\b)")
# What the head camera's eye damping added to the wanted eye (0.6.0-feel.48 on): the raw target is wanted less it.
DAMP = re.compile(r" damp=\(([-\d. ]+)\)")
# Round 24: a float. The presented eye's offset from the raw target (the wanted eye less the damping: the head camera's
# own eye, or the rig eye), the least it is over FLOAT_MS: a step judge (POP_M between two frames) does not see an eye
# that drifts 0.6 m off and back at 0.04 m a frame, which is how the clambers of the user's log of 2026-11-24 floated
# (feel.51; the matrix's vault passed). FLOAT_M: Stable's damping holds the eye up to 0.25 m behind the head by design.
FLOAT_MS = 300.0
FLOAT_M = 0.26


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


def _clock(ms):
    ms = int(round(ms)) % 86400000
    return "%02d:%02d:%02d.%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def frames(lines):
    """Every traced frame of `lines` in order of time, as dicts, whatever falls between them: at (the frame's own
    time), written (the stamp of its line), tick (ms), kind, eye, wanted, carried, root (None where the line has none), looked (None likewise), taken,
    view (the unit vector the view pointed along; None where the line has none), sock (m: the weapon socket as the arm
    drive drew it less the game's own, along the view), arms (0 not driven or not known, 1 driven and the game's joint
    values put back, 2 driven and the written values left, 3 driven in a hold) and eye_edge (1: the eye opened the
    window itself); those three are None on a line from before round 13. veil: how far the helmet veil moved the eye
    handed to the game in that frame (round 19), a vector, or None where it moved nothing or the line does not say
    (before round 19). "eye" is the eye before the veil; the eye shown is eye + veil.
    windows() ends a window at every State line, and frames on either side of one were never measured against each
    other (the Stable soak of 2026-11-24: 0.120 m at a landing, 08:18:55.525, with "State: left the native view"
    between its frames); nor were the frames a stretch of the log begins with before its first State line. Here a
    State line only says that "t=" counts from a new edge. The time is "tick=" (the helper's clock, 0.6.0-feel.41 on);
    without it the edge is placed by the line's own stamp, as tools/harness/replay.py does, to a millisecond or two.
    Two rows of one frame are one, the later, as in windows().
    The stamp of a line is when it was written, not when its frame was: the frames from before an edge are written
    with the edge, six or so under one stamp (08:10:44.575 in that soak is four frames over 130 ms). So "at" is the
    edge's time and "t=" where that is more than SAME_FRAME_MS before the stamp, and the stamp otherwise (a frame
    after the edge is written as it happens; the two differ by a millisecond or two and the stamp is what the log
    can be searched for). What tells two frames apart is the tick."""
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
        row = {"written": line[:12], "stamp": ((int(m[1]) * 60 + int(m[2])) * 60 + int(m[3])) * 1000 + int(m[4]), "t": float(m[5]), "kind": int(m[6]),
               "eye": numbers(m[7]), "wanted": numbers(m[8]), "carried": float(m[9] or 0), "root": numbers(m[10]),
               "looked": int(m[11]) if m[11] else None, "taken": m[12] or "-", "clock": float(m[13]) if m[13] else None}
        view, drawn = VIEW.search(line), DRAWN.search(line)
        row["view"] = numbers(view[1]) if view else None
        row["sock"], row["arms"] = (float(drawn[1]), int(drawn[2])) if drawn else (None, None)
        row["eye_edge"] = int(drawn[3]) if drawn and drawn[3] else None
        veil = VEIL.search(line)
        row["veil"] = numbers(veil[1]) if veil and veil[1] else None
        damp = DAMP.search(line)
        row["damp"] = numbers(damp[1]) if damp else None
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
            own = edge + row["t"]
            row["at"] = row["written"] if own >= row["stamp"] - SAME_FRAME_MS else _clock(own)
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


def raw_target(row, wanted="wanted"):
    """The eye the frame's camera gives before the mod keeps it (the continuity) or damps it: the wanted eye less what the
    damping added (round 24). None where the row has no wanted eye."""
    if not row.get(wanted):
        return None
    damp = row.get("damp")
    return [w - d for w, d in zip(row[wanted], damp)] if damp else list(row[wanted])


def held_offsets(rows, eye="eye", ms=FLOAT_MS, gap_ms=GAP_MS):
    """For every frame (rows: frames(), or replay.py's), the least offset of rows[i][eye] from raw_target() over the
    `ms` that end at it, where the frames there are no more than `gap_ms` apart and span at least `ms`: [(row, held)].
    A float is a frame whose held offset is over FLOAT_M."""
    out, start = [], 0
    offsets = []
    for row in rows:
        target = raw_target(row)
        offsets.append(None if target is None or not row.get(eye) else sum((a - b) ** 2 for a, b in zip(row[eye], target)) ** 0.5)
    for i, row in enumerate(rows):
        if offsets[i] is None or (i and row["tick"] - rows[i - 1]["tick"] > gap_ms) or (i and offsets[i - 1] is None):
            start = i
        if offsets[i] is None or row["tick"] - rows[start]["tick"] < ms:
            continue
        first = i
        while first > start and row["tick"] - rows[first - 1]["tick"] <= ms:
            first -= 1
        if first > start:  # the frame just before the span, so the span covers the whole time
            first -= 1
        out.append((row, min(offsets[first:i + 1])))
    return out


def largest_float(rows, eye="eye"):
    """(the largest held offset, its row) or (0.0, None)."""
    found = held_offsets(rows, eye)
    if not found:
        return 0.0, None
    row, held = max(found, key=lambda f: f[1])
    return held, row


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
