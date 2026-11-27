#!/usr/bin/env python3
"""Replay a log's "Trace:" lines through the continuity filter alone and count the steps the filter makes.

  replay.py LOG [--logged REV] [--present REV] [--worst N] [--from HH:MM:SS.mmm --to HH:MM:SS.mmm [--emit NAME]]

With --from/--to: the frames of that time, one line each (the log, the logged filter, the present filter). With
--emit as well: the same frames as a C array NAME for a test (tests/reference/soak-6-steps.h and soak-8-steps.h were
written so). --worst N: the N rows where the logged filter, replayed, is farthest from the log.

The filter (src/camera_continuity.h) is run twice over the logged rows (wanted eye, body root and its axes, source,
looked): once as it was in the build that wrote the log (REV, a git revision; default 9a3470b, 0.6.0-feel.40, for a
log with axes and b7ce1ab, 0.6.0-feel.39, for one without) and once as it is in this tree (or in --present REV, to
see what a change to the filter did to a log).

A log whose Trace lines carry "axes=" (0.6.0-feel.40 on) is replayed as the helper ran the filter: every Trace row is
one call of it, the second row of a frame the game set twice too, with the logged axes. The filter's state is known
from the first row of a stretch (rows no more than 250 ms apart, the helper's frame gap) at which the log carries no
correction; from there the replay is called exact in a row when it shows the eye the log shows within 0.005 m, and
the steps of the replays are judged only where all three frames are exact.

The Trace lines of 0.6.0-feel.39 and before do not carry the axes. For those the first run also finds them: in each
frame the turn with which the old filter presents the eye the log shows (replay_filter.c), and the second run is
given those axes. There the frames are replayed one row each and judged where the logged filter is within 0.02 m.

A step is counted for frames a, b, c (5 to 80 ms apart) by how much the presented eye's step changed, as a vector:
| (c.eye-b.eye) - (b.eye-a.eye) |, and the wanted change is the same of the wanted eye. At 0.06 m or more of shown
change it is the game's own when the wanted change is at least 0.8 of it and under 0.03 m is carried in b and c,
"reduced" when the wanted change is over 1.2 of it, and otherwise the filter's.
The log's own judge, and this tool until 2026-11-22, took the difference of the two steps' lengths instead:
| |c.eye-b.eye| - |b.eye-a.eye| |. That is blind to a change of direction: an eye that goes 0.04 m forwards and then
0.04 m back has changed its step by 0.08 m and counts as nothing. A filter that throws the eye backwards was counted
as better than one that does not (docs/design/specs/2026-11-22-locked-view-design.md, "What the count did not see").
The count by lengths is still printed beside the other, for the figures written down before.

Beside the steps, for each filter, what it carries where the replay is exact: the correction summed over the frames
(metre-frames: a filter can hide a step by carrying more for longer), and the longest time more than 0.05 m is
carried on after a change of camera (to the first frame at or under it, or to the last frame that is known). The line
says which of the two the figure is: "which settled" (a frame at or under 0.05 m came), or "cut off by the end of the
trace" with what was still carried in the last frame there is (the Trace window ended, or the replay stopped being
exact: the true time is longer and not known), and gives the longest of the other kind and how many there were of
each. Most of the figures written down before 2026-11-23 ("467 ms", "468 ms", "835 ms") were cut off: a window of half
a second is 14 frames at 30 frames a second.

What this leaves out:
 - Only the rows in the Trace windows are there (0.5 s after each State edge and six frames before it). What the
   filter carried into a stretch is not known, nor the eye's speed it had kept: the first row of a stretch starts the
   replay from nothing, and its second row cannot be a jump in the replay as it can in the game.
 - What a line carries after "tick=" (off_foot, damped, damp, view; from round 13 sock, arms, eye_edge) is not read
   here: the filter was not given it. tools/harness/trace.py reads it for the judge.
 - The helmet veil (round 19: "veil=", the last field) moves the eye handed to the game after the filter, in a frame
   or two at a leave of the native view. It is read into row["veil"] (None where it moved nothing or the line does
   not say), and nothing here uses it: eye_world is the filter's own eye, before the veil, and that is what is
   replayed and judged. tools/harness/matrix.py judges the eye shown, eye + veil.
 - Of what a line carries after "tick=" only off_foot, damp and view are read (round 19: the head the helper gives the
   filter, head_node); damped, sock, arms and eye_edge are not. tools/harness/trace.py reads them for the judge.
 - A row's time is its "tick=" where the line has one (0.6.0-feel.41 on). Before that it is "t=", to 0.1 ms and from
   the window's edge, and the edge is placed by the line's own stamp (to 1 ms, and written up to a frame late): the
   time between the last row of one window and the first of the next can be a millisecond or two off.
 - Full aim (quiet) is taken from "quiet=" where the line has it and as off where not.
 - The filter forgotten by the helper (the lock let go) shows as "start" in taken= where the line can say so; before
   that only as a pause.
 - The fitted axes are a guess: where nothing is carried and nothing taken up no turn can be seen and the axes stay
   as they were, so a root that turns evenly there and is caught turning later looks as if it began to turn then.
 - The damping (src/camera_damp.h) is not run again: the wanted eye of a head-camera frame is logged after it.
   (Round 24: it is where the traversal rule changed it. Round 49: --damp-frame runs it again in the test switch's
   frame and --root-frame in the root's axes in the same rows; replay_filter.c says how far that can be trusted.)
 - The helper's own decision to look (smooth_detect) is taken from the log; the present filter might have kept a run
   of jumps open for a different time.
"""
import argparse
import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace as trace_reader  # noqa: E402  (tools/harness/trace.py: the raw target and the float, round 24)
SAME_FRAME_MS = 5.0
SAME_EYE_M = 0.002
STEP_M = 0.06
NEAR_M = 0.02    # fitted axes: the logged filter, replayed, "follows the log"
EXACT_M = 0.005  # logged axes: the replay is exact
GAP_MS = 250.0   # the helper's frame gap: a longer pause starts the filter again
BEYOND_M = 0.05
CARRY_M = 0.05   # a correction over this, carried on after a change of camera, is timed
TRACE = re.compile(r"^(\d+):(\d+):(\d+)\.(\d+) Trace: t=(-?[\d.]+) kind=(\d+) eye_world=\(([-\d. ]+)\) step=[-\d.]+ wanted=\(([-\d. ]+)\) "
                   r"correction=([-\d.]+).*?source=(\S+) root=\(([-\d. ]+)\)(?: axes=\(([-\d. ]+)\))? looked=(\d) taken=(\S+)"
                   r"(?: quiet=(\d))?(?: tick=([\d.]+))?(?: off_foot=(\d) damped=(\d) damp=\(([-\d. ]+)\))?(?: view=\(([-\d. ]+)\))?")
VEIL = re.compile(r" veil=\(([-\d. ]+)\)")
SOURCES = {"head": 1, "native": 2, "repeat": 2}
TAKEN = ((1, "jump"), (2, "edge"), (4, "root"), (8, "restart"), (16, "cut"), (32, "settle"))


MASK = re.compile(r"^(\d+):(\d+):(\d+)\.(\d+) (?:Hold|Return|Drop|Wait): .*?\bmask=([0-9a-fA-F]+)")
STYLE = re.compile(r"^(\d+):(\d+):(\d+)\.(\d+) Camera style: (\w+)")
WINDOW_MS = 600.0  # src/camera_helper.c SMOOTH_EVENT_MS
SETTLE_ARM_MS = 2000.0  # src/camera_helper.c SETTLE_ARM_MS
TRAVERSAL = 0x20   # src/camera_traversal.h


def _stamp(m):
    return ((int(m[1]) * 60 + int(m[2])) * 60 + int(m[3])) * 1000 + int(m[4])


def context(lines, rows):
    """Round 24: what the helper knew around the filter that the Trace lines do not say, from the log's other lines,
    into each row: "mask" (the control mask of the last Hold/Return/Drop/Wait line at or before the row's frame, within
    1.5 s; -1 not known), "stable" (1 Stable, 0 Bodycam, -1 not known; the last "Camera style:" line) and "rearm_only"
    (1: jumps were looked for in the row only because a change of mask into or out of a traversal mask opened the window
    again, which camera_helper.c no longer does; found from the State lines, the changes of kind and the masks, so it is
    the window as near as the log tells it) and "settle_arm" (the stamp of the last "left the native view: Native camera
    request or firearm changed" within 2 s, which arms THE SETTLE; -1 none). A row's frame is placed on the log's clock by the median of stamp less
    tick over the rows written as they happened."""
    masks, styles, states, arms = [], [], [], []
    for line in lines:
        m = MASK.match(line)
        if m:
            masks.append((_stamp(m), int(m[5], 16)))
            continue
        m = STYLE.match(line)
        if m:
            styles.append((_stamp(m), 1 if m[5] == "Stable" else 0))
            continue
        if " State: " in line and line[:2].isdigit():
            try:
                states.append(_stamp(re.match(r"^(\d+):(\d+):(\d+)\.(\d+)", line)))
                if "left the native view: Native camera request or firearm changed" in line:  # mod_view.c tfp_camera_settle_arm
                    arms.append(states[-1])
            except TypeError:
                pass
    live = sorted(row["stamp"] - row["tick"] for row in rows if row.get("clock") is not None and row["t"] >= 0)
    offset = live[len(live) // 2] if live else 0.0
    events = [(t, "other") for t in states]
    before = None
    for stamp, mask in masks:
        if before is not None and mask != before:
            events.append((stamp, "trav" if (mask & TRAVERSAL or before & TRAVERSAL) else "other"))
        before = mask
    kind = None
    for row in rows:
        at = row["tick"] + offset if row.get("clock") is not None else row["tick"]
        row["at_ms"] = at
        if kind is not None and row["kind"] != kind:
            events.append((at, "other"))
        kind = row["kind"]
    events.sort()
    mi = si = ei = ai = 0
    arm = None
    mask = style = None
    last = {"other": -1e18, "trav": -1e18}
    for row in sorted(rows, key=lambda r: r["at_ms"]):
        at = row["at_ms"]
        while mi < len(masks) and masks[mi][0] <= at + 2:
            mask = masks[mi]
            mi += 1
        while si < len(styles) and styles[si][0] <= at + 2:
            style = styles[si][1]
            si += 1
        while ai < len(arms) and arms[ai] <= at + 2:
            arm = arms[ai]
            ai += 1
        while ei < len(events) and events[ei][0] <= at + 2:
            last[events[ei][1]] = events[ei][0]
            ei += 1
        row["mask"] = mask[1] if mask and at - mask[0] <= 1500 else -1
        row["stable"] = -1 if style is None else style
        row["settle_arm"] = int(arm) if arm is not None and at - arm <= SETTLE_ARM_MS else -1
        row["rearm_only"] = int(bool(row["looked"]) and at >= last["other"] + WINDOW_MS and at < last["trav"] + WINDOW_MS)


def read(lines):
    """Every Trace row in order of time: one for each call of the filter (round 24: with context())."""
    windows, window = [], []
    for line in lines:
        if " State: " in line:
            if window:
                windows.append(window)
            window = []
            continue
        m = TRACE.match(line)
        if not m:
            continue
        stamp = ((int(m[1]) * 60 + int(m[2])) * 60 + int(m[3])) * 1000 + int(m[4])
        row = {"stamp": stamp, "t": float(m[5]), "kind": int(m[6]), "eye": [float(x) for x in m[7].split()],
               "wanted": [float(x) for x in m[8].split()], "carried": float(m[9]), "source": m[10],
               "root": [float(x) for x in m[11].split()], "axes": [float(x) for x in m[12].split()] if m[12] else None,
               "looked": int(m[13]), "taken": m[14], "quiet": int(m[15] or 0), "clock": float(m[16]) if m[16] else None, "at": line[:12],
               "off_foot": int(m[17]) if m[17] else None, "damped": int(m[18]) if m[18] else None,
               "damp": [float(x) for x in m[19].split()] if m[19] else None, "view": [float(x) for x in m[20].split()] if m[20] else None}
        veil = VEIL.search(line)
        row["veil"] = [float(x) for x in veil[1].split()] if veil else None
        if window and row["t"] < window[-1]["t"] - 1:  # an edge without a State line (the orphaned view): t starts again
            windows.append(window)
            window = []
        window.append(row)
    if window:
        windows.append(window)
    rows = []
    for window in windows:
        # t is from the window's edge; the line is written at or after its frame, so the edge is the earliest (stamp - t)
        edge = min(row["stamp"] - row["t"] for row in window)
        for row in window:
            row["tick"] = row["clock"] if row["clock"] is not None else edge + row["t"]
        rows.extend(window)
    rows.sort(key=lambda row: row["tick"])
    context(lines, rows)
    return rows


def merge(rows):
    """The frames presented: two Trace rows of one frame are one, the later (what is drawn)."""
    out, first = [], None
    for row in rows:
        again = bool(out) and 0 <= row["tick"] - first < SAME_FRAME_MS and (
            row["kind"] != out[-1]["kind"] or math.dist(row["eye"], out[-1]["eye"]) < SAME_EYE_M)
        if again:
            out[-1] = row
        else:
            out.append(row)
            first = row["tick"]
    return out


def frames(lines):
    """The log's frames in order of time: one for each frame presented (two Trace rows of one frame are one)."""
    return merge(read(lines))


def stretches(rows):
    """Marks each row: "known" (the filter's state can be had from the log: the stretch has had a row without a
    correction) and "fresh" (1: the helper forgot the filter before this row, "start" in taken=; 2: the first row
    without a correction of a stretch that began with one: the replay lets its own correction go there and keeps the
    rest). A stretch is rows no more than GAP_MS apart."""
    known, before = False, None
    for row in rows:
        row["fresh"] = 1 if "start" in row["taken"] else 0
        if before is None or row["tick"] - before["tick"] > GAP_MS or row["fresh"]:
            known = False
        if not known and row["carried"] == 0:
            known = True
            row["fresh"] = row["fresh"] or (2 if before is not None and row["tick"] - before["tick"] <= GAP_MS else 0)
        row["known"] = known
        before = row


def change(a, b, c, vector):
    """How much the step from b to c differs from the step from a to b: as a vector, or (the old measure) in length."""
    one, two = [y - x for x, y in zip(a, b)], [y - x for x, y in zip(b, c)]
    return math.dist(two, one) if vector else abs(math.hypot(*two) - math.hypot(*one))


def judge(rows, eye="eye", carried="carried", known=False, vector=False):
    """[(whose, shown change, wanted change, a, b, c)] for every step of STEP_M or more. `known`: only where the
    logged filter, replayed, shows what the log shows in all three frames (row["follows"]). `vector`: the change of
    the step as a vector; without it the difference of the steps' lengths, which does not see a change of direction."""
    found = []
    for a, b, c in zip(rows, rows[1:], rows[2:]):
        if not (5 <= b["tick"] - a["tick"] <= 80 and 5 <= c["tick"] - b["tick"] <= 80):
            continue
        if known and not (follows(a) and follows(b) and follows(c)):
            continue
        shown = change(a[eye], b[eye], c[eye], vector)
        wanted = change(a["wanted"], b["wanted"], c["wanted"], vector)
        if shown < STEP_M:
            continue
        if wanted >= 0.8 * shown and b[carried] < 0.03 and c[carried] < 0.03:
            whose = "game"
        elif wanted > 1.2 * shown:
            whose = "reduced"
        else:
            whose = "filter"
        found.append((whose, shown, wanted, a, b, c))
    return found


def carries(rows, carried="carried", known=False):
    """What is carried on after the changes of camera, as a dict:
      "metres":  the correction summed over the frames (metre-frames)
      "settled": (ms, row) the longest time more than CARRY_M was carried on from a change of camera (the row of that
                 change) to the first frame at or under it
      "cut":     (ms, row, still) the longest such time that the trace cut off: no frame at or under CARRY_M came before
                 a pause, the end of the rows or (with `known`) a frame that is not exact; ms to the last frame there
                 is, and `still` what that frame carried
      "counts":  (how many settled, how many were cut off)
    (0, None) and (0, None, 0.0) where there was none. `known`: only the frames where the replay is exact."""
    found = {"metres": 0.0, "settled": (0, None), "cut": (0, None, 0.0), "counts": (0, 0)}
    for i, row in enumerate(rows):
        if known and not follows(row):
            continue
        found["metres"] += row[carried]
        before = rows[i - 1] if i else None
        if before is None or (known and not follows(before)) or row["tick"] - before["tick"] > GAP_MS:
            continue
        if SOURCES.get(row["source"], 0) == SOURCES.get(before["source"], 0) or not row[carried] > CARRY_M:
            continue
        last, settled = row, False
        for later in rows[i + 1:]:
            if later["tick"] - last["tick"] > GAP_MS or (known and not follows(later)):
                break
            last = later
            if not later[carried] > CARRY_M:
                settled = True
                break
        ms = last["tick"] - row["tick"]
        if settled:
            found["counts"] = (found["counts"][0] + 1, found["counts"][1])
            if found["settled"][1] is None or ms > found["settled"][0]:
                found["settled"] = (ms, row)
        else:
            found["counts"] = (found["counts"][0], found["counts"][1] + 1)
            if found["cut"][1] is None or ms > found["cut"][0]:
                found["cut"] = (ms, row, last[carried])
    return found


def carry(rows, carried="carried", known=False):
    """(metre-frames, (ms, row)): the correction summed over the frames, and the longest time more than CARRY_M was
    carried on from a change of camera (the row of that change; None where there was none), to the first frame at or
    under it or to the last frame before a pause (or, with `known`, before a frame that is not exact): the longer of
    the two that carries() tells apart. `known`: only the frames where the replay is exact."""
    found = carries(rows, carried, known)
    settled, cut = found["settled"], found["cut"]
    if cut[1] is not None and (settled[1] is None or cut[0] > settled[0]):
        return found["metres"], (cut[0], cut[1]) if cut[0] > 0 else (0, None)
    return found["metres"], settled if settled[0] > 0 else (0, None)


def carry_line(metres, found):
    """The "carried:" line: as it was printed (the longest time, whichever kind), then which kind that was and the
    longest of the other kind."""
    settled, cut, (settled_count, cut_count) = found["settled"], found["cut"], found["counts"]
    cut_longest = cut[1] is not None and (settled[1] is None or cut[0] > settled[0])
    longest, where = (cut[0], cut[1]) if cut_longest else settled
    line = f"  carried: {metres:.1f} metre-frames; more than {CARRY_M} m for {longest:.0f} ms at most after a change of camera"
    if where is None:
        return line
    line += f" ({where['at']})"
    if cut_longest:
        line += f", cut off by the end of the trace with {cut[2]:.3f} m still carried; "
        line += f"the longest that settled: {settled[0]:.0f} ms ({settled[1]['at']})" if settled[1] is not None else "none settled"
    else:
        line += ", which settled; "
        line += (f"the longest cut off by the end of the trace: {cut[0]:.0f} ms ({cut[1]['at']}), {cut[2]:.3f} m still carried"
                 if cut[1] is not None else "none was cut off by the end of the trace")
    return line + f"; {settled_count} settled, {cut_count} cut off"


def follows(row):
    return row["follows"] if "follows" in row else row["misfit"] < NEAR_M


def build(header_dir, name, work):
    binary = Path(work) / name
    subprocess.run(["cc", "-std=c11", "-O1", "-ffp-contract=off", "-I", str(header_dir), str(ROOT / "tools/harness/replay_filter.c"), "-lm",
                    "-o", str(binary)], check=True)
    return binary


EYE_FORWARD_M = 0.18  # the head camera's eye from its node: along the level view (src/camera_policy.c), and up
EYE_UP_M = 0.04


def head_node(row, axes=None):
    """The point the head camera's eye is built on (the head node as the head camera's own smoothing left it), in the
    world, before the eye damping: the wanted eye less the damping and less the eye's place in front of and above it
    (src/camera_helper.c: c.forward along the level view direction, c.up; the side the placement can give is taken as
    0). The level view is the logged view's; at a vertical view the root's forward axis (`axes`, rows), as the helper
    does. None where the line does not say the damping and the view (before 0.6.0-feel.48)."""
    damp, view = row.get("damp"), row.get("view")
    if damp is None or view is None:
        return None
    level = [view[0], view[1]]
    length = math.hypot(*level)
    if length < 1e-4 and axes:
        level = [axes[3], axes[4]]
        length = math.hypot(*level)
    if length < 1e-4:
        return None
    return [row["wanted"][0] - damp[0] - level[0] / length * EYE_FORWARD_M, row["wanted"][1] - damp[1] - level[1] / length * EYE_FORWARD_M,
            row["wanted"][2] - damp[2] - EYE_UP_M]


def head_given(row):
    """Whether the helper gives the filter the head in this row (THE CUT, src/camera_continuity.h; src/camera_helper.c
    smooth_present): a head-camera frame (kind 4, the head source) of a body on foot, not in full aim, where the line
    says enough to find the head (head_node: 0.6.0-feel.48 on). Not known from a line: the helper also leaves out a
    body that is down (its head 3 m from the root) and the ADS gap. A line before feel.48 is replayed without the
    head: the eye is no stand-in for it (a turn of the view swings the eye 0.18 m about the head, which is no cut)."""
    return (row["kind"] == 4 and row["source"] == "head" and row.get("off_foot") != 1 and not row.get("quiet", 0)
            and row.get("damp") is not None and row.get("view") is not None)


def row_text(row, axes):
    """One row for replay_filter.c: tick source detect quiet fresh, wanted, root, logged eye, axes, (round 19) whether
    the head is given and the head node in the world (head_node; zeros where it is not given), and (round 24) mask,
    kind, damped, off_foot, damp, stable, rearm_only (context(); -1 where not known), and (round 49) the logged view
    (zeros where the line does not say)."""
    node = head_node(row, row[axes]) if head_given(row) else None
    given = node is not None
    damped = row.get("damped")
    return "%.3f %d %d %d %d %s %s %s %s %d %s %d %d %d %d %s %d %d %d %s\n" % (
        row["tick"], SOURCES.get(row["source"], 0), row["looked"], row.get("quiet", 0), row.get("fresh", False), " ".join(map(str, row["wanted"])),
        " ".join(map(str, row["root"])), " ".join(map(str, row["eye"])), " ".join(map(str, row[axes])), given, " ".join(map(str, node or [0.0, 0.0, 0.0])),
        row.get("mask", -1), row["kind"], -1 if damped is None or row.get("damp") is None else damped, row.get("off_foot") or 0,
        " ".join(map(str, row.get("damp") or [0.0, 0.0, 0.0])), row.get("stable", -1), row.get("rearm_only", 0), row.get("settle_arm", -1),
        " ".join(map(str, row.get("view") or [0.0, 0.0, 0.0])))


def emit_head(name, rows):
    """The rows as a C array of HeadFrame (tests/reference/soak-30-cut.h): SoakFrame with the kind, off_foot, what
    the damping added and the view, for scenes of THE CUT."""
    first = rows[0]["tick"]

    def group(values, digits):
        return "{" + ", ".join(f"{v:.{digits}f}f" for v in values) + "}"
    out = [f"/* {rows[0]['at']} to {rows[-1]['at']}: ms, kind, source, looked, off_foot, wanted eye, damp, view, root, root axes (logged), "
           f"eye shown in the log */",
           f"static const HeadFrame {name}[] = {{"]
    for row in rows:
        off = row.get("off_foot")
        out.append(f"  {{{row['tick'] - first:.1f}f, {row['kind']}, {SOURCES.get(row['source'], 0)}, {row['looked']}, {1 if off == 1 else 0}, "
                   f"{group(row['wanted'], 4)}, {group(row.get('damp') or [0, 0, 0], 4)}, {group(row.get('view') or [0, 0, 0], 5)},\n"
                   f"    {group(row['root'], 4)}, {group(row['fit'], 6)}, "
                   f"{group(row['eye'], 4)}}},")
    out.append("};")
    return "\n".join(out) + "\n"


def run(binary, mode, rows, axes, settle=False, flags=()):
    text = "".join(row_text(row, axes) for row in rows)
    out = subprocess.run([str(binary), mode] + (["settle"] if settle else []) + list(flags), input=text, capture_output=True, text=True, check=True).stdout.splitlines()
    assert len(out) == len(rows)
    return [[float(x) for x in line.split()] for line in out]


STILL_M_S = 0.5    # the root counts as stopped under this speed (src/camera_traversal.h TFP_TRAVERSAL_ROOT_M_S) ...
STILL_MS = 100.0   # ... for this long


def window_figures(rows, eye, carried, taken_key):
    """Round 24, for --from/--to: what a window's frames say of one filter (`eye`: its presented eye, `carried`, `taken_key`):
      offset   the most the presented eye is from the raw target (trace.raw_target: the head camera's own eye, before the
               damping; the rig eye in the native view)
      stopped  the same, only in frames where the body root has gone under STILL_M_S for STILL_MS
      held     the largest float: the least offset over trace.FLOAT_MS, at its largest (trace.held_offsets)
      regrown  how far what is carried grew again after it had come down, with nothing taken up (a jump, a change of
               camera, a cut, THE SETTLE) in between: the backpack pick-ups' drift"""
    out = {"offset": 0.0, "stopped": 0.0, "held": 0.0, "regrown": 0.0}
    still_since = None
    low = None
    for i, row in enumerate(rows):
        target = trace_reader.raw_target(row)
        off = math.dist(row[eye], target)
        out["offset"] = max(out["offset"], off)
        if i and row["tick"] > rows[i - 1]["tick"]:
            speed = math.dist(row["root"], rows[i - 1]["root"]) * 1000 / (row["tick"] - rows[i - 1]["tick"])
            if speed < STILL_M_S:
                still_since = rows[i - 1]["tick"] if still_since is None else still_since
            else:
                still_since = None
        if still_since is not None and row["tick"] - still_since >= STILL_MS:
            out["stopped"] = max(out["stopped"], off)
        if any(word in row[taken_key] for word in ("jump", "edge", "cut", "settle")):
            low = None
        else:
            low = row[carried] if low is None else min(low, row[carried])
            out["regrown"] = max(out["regrown"], row[carried] - low)
    shown = [dict(row, eye=row[eye]) for row in rows]
    out["held"] = trace_reader.largest_float(shown)[0]
    return out


def taken(bits):
    return "+".join(name for bit, name in TAKEN if int(bits) & bit) or "-"


def header(revision, work, name):
    """The filter's header as it was in `revision`, in a folder of its own."""
    folder = Path(work) / name
    if Path(revision).is_dir():  # a folder with the two headers in place of a revision
        return Path(revision).resolve()
    folder.mkdir()
    for file in ("camera_continuity.h", "camera_math.h", "camera_damp.h", "camera_traversal.h", "camera_clear.h"):
        shown = subprocess.run(["git", "-C", str(ROOT), "show", f"{revision}:src/{file}"], capture_output=True, text=True,
                               check=file in ("camera_continuity.h", "camera_math.h"))
        if shown.returncode == 0:  # the damping and the round-24 rules only where the revision has both (replay_filter.c RULES)
            (folder / file).write_text(shown.stdout)
    return folder


def replay(rows, logged=None, present=None, settle=False, flags=()):
    """Runs both filters over `rows` (read(), all of them with axes or none) and returns (the frames, whether the axes
    were given). `present`: a git revision, or a folder with camera_continuity.h and camera_math.h, in place of this
    tree's filter. `settle`: THE SETTLE on in this tree's filter (the helper's settle_on.txt; off by default, as in the
    helper). `flags` (round 49): further arguments for this tree's filter, "dampframe" (the eye damping run again
    in the frame of tfp_damp_frame_step, the helper's damp_frame_on.txt), "rootframe" (run again in the same rows in
    the root's own axes: what "dampframe" is compared with) and "floor" (round 44's FLOOR on the eye presented);
    replay_filter.c says what each does. Each row gets old_eye, old_carried, old_taken (the logged build's filter), new_eye, new_carried,
    new_taken (this tree's), fit (the axes used), misfit (m, the logged filter from the log) and follows."""
    given = bool(rows) and all(row["axes"] for row in rows)
    logged = logged or ("9a3470b" if given else "b7ce1ab")
    identity = [1, 0, 0, 0, 1, 0, 0, 0, 1]
    if given:
        stretches(rows)
    else:
        rows = merge(rows)  # the fit was made for one row a frame
    with tempfile.TemporaryDirectory() as work:
        old = build(header(logged, work, "logged"), "logged_filter", work)
        new = build(header(present, work, "present") if present else ROOT / "src", "present_filter", work)
        for row in rows:
            row["start"] = row["axes"] if given else identity
        for row, out in zip(rows, run(old, "run" if given else "fit", rows, "start")):
            row["old_eye"], row["old_carried"], row["old_taken"], row["fit"], row["misfit"] = out[0:3], out[3], taken(out[4]), out[5:14], out[14]
            row["old_wanted"] = out[15:18]
            row["follows"] = row["known"] and row["misfit"] < EXACT_M if given else row["misfit"] < NEAR_M
        for row, out in zip(rows, run(new, "run", rows, "fit", settle, flags)):
            row["new_eye"], row["new_carried"], row["new_taken"] = out[0:3], out[3], taken(out[4])
            row["new_wanted"] = out[15:18]
    return (merge(rows) if given else rows), given


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log")
    parser.add_argument("--logged", default=None, help="git revision of the build that wrote the log")
    parser.add_argument("--present", default=None, help="git revision (or a folder with the two headers) to run in place of this tree's filter")
    parser.add_argument("--settle", action="store_true", help="THE SETTLE on in this tree's filter (settle_on.txt; off by default)")
    parser.add_argument("--damp-frame", action="store_true", help="this tree's eye damping run again in the frame of tfp_damp_frame_step (damp_frame_on.txt; off by default)")
    parser.add_argument("--root-frame", action="store_true", help="the same rows run again in the root's own axes: what --damp-frame is compared with")
    parser.add_argument("--floor", action="store_true", help="round 44's FLOOR on the eye this tree's filter presents (src/camera_clear.h)")
    parser.add_argument("--from", dest="start", default=None)
    parser.add_argument("--to", dest="end", default=None)
    parser.add_argument("--emit", default=None, help="write the rows of --from/--to as a C array of this name")
    parser.add_argument("--emit-head", default=None, help="the same as HeadFrame rows, with the kind, off_foot, the damping and the view (round 19)")
    parser.add_argument("--worst", type=int, default=0, help="list this many rows where the logged filter, replayed, is farthest from the log")
    args = parser.parse_args(argv[1:])
    calls = read(Path(args.log).read_text(errors="replace").splitlines())
    rows, given = replay(calls, args.logged, args.present, args.settle,
                         (["dampframe"] if args.damp_frame else []) + (["rootframe"] if args.root_frame else []) + (["floor"] if args.floor else []))
    if not given:
        calls = rows

    def chosen(row):
        return not (args.start and row["at"] < args.start) and not (args.end and row["at"] > args.end)
    if args.emit_head:
        print(emit_head(args.emit_head, [row for row in calls if chosen(row)]), end="")
        return 0
    if args.emit:
        picked = [row for row in calls if chosen(row)]
        print(f"/* {picked[0]['at']} to {picked[-1]['at']}: ms, source, looked, wanted eye, root, root axes "
              f"({'logged' if given else 'fitted'}), eye shown in the log */")
        print(f"static const SoakFrame {args.emit}[] = {{")
        for row in picked:
            def group(values, digits):
                return "{" + ", ".join(f"{v:.{digits}f}f" for v in values) + "}"
            print(f"  {{{row['tick'] - picked[0]['tick']:.1f}f, {SOURCES.get(row['source'], 0)}, {row['looked']}, {group(row['wanted'], 4)}, "
                  f"{group(row['root'], 4)},\n    {group(row['fit'], 6)}, {group(row['eye'], 4)}}},")
        print("};")
        return 0
    if args.start or args.end:
        before = None
        picked = [row for row in calls if chosen(row)]
        for row in calls:
            if not chosen(row):
                before = None
                continue
            if before:
                a = [x - y for x, y in zip(row["fit"], before["fit"])]
                print(f"{row['at']} +{row['tick'] - before['tick']:5.1f} ms kind {row['kind']} looked {row['looked']} wanted "
                      f"{math.dist(row['wanted'], before['wanted']):.3f} "
                      f"root {math.dist(row['root'], before['root']):.3f} | log {math.dist(row['eye'], before['eye']):.3f} carried {row['carried']:.3f} "
                      f"{row['taken']:9} | logged filter {math.dist(row['old_eye'], before['old_eye']):.3f} carried {row['old_carried']:.3f} "
                      f"{row['old_taken']:9} misfit {row['misfit']:.3f} axes turned {math.sqrt(sum(x * x for x in a) / 2):.3f} | present filter "
                      f"{math.dist(row['new_eye'], before['new_eye']):.3f} carried {row['new_carried']:.3f} {row['new_taken']}")
            before = row
        if picked:
            for title, eye, carried, key in (("log", "eye", "carried", "taken"), ("logged filter", "old_eye", "old_carried", "old_taken"),
                                             ("present", "new_eye", "new_carried", "new_taken")):
                f = window_figures(merge(picked), eye, carried, key)
                print(f"{title:14}: shown eye from the raw target at most {f['offset']:.3f} m, {f['stopped']:.3f} m once the root stopped; "
                      f"held over {trace_reader.FLOAT_MS:.0f} ms {f['held']:.3f} m; carried grew again by {f['regrown']:.3f} m")
        return 0
    if given:
        known = [row for row in calls if row["known"]]
        exact = sum(row["misfit"] < EXACT_M for row in known)
        print(f"{len(calls)} rows, {len(rows)} frames; the filter's state is known from the log in {len(known)} rows, and the logged filter, replayed "
              f"with the logged axes, is within {EXACT_M} m of the log in {exact} of them ({sum(follows(row) for row in rows)} of the frames)")
    else:
        close = sum(row["misfit"] < NEAR_M for row in rows)
        print(f"{len(rows)} frames; the logged filter with the fitted axes is within {NEAR_M} m of the log in {close} of them")
    for row in sorted(calls, key=lambda row: -row["misfit"] if row.get("known", True) else 0)[:args.worst]:
        print(f"  {row['at']} kind {row['kind']} looked {row['looked']}: {row['misfit']:.3f} m from the log; the log carried {row['carried']:.3f} m "
              f"{row['taken']}, the replay {row['old_carried']:.3f} m {row['old_taken']}")
    where = "where the replay is exact" if given else "where the replay follows it"
    for title, eye, carried, known in (("the log, all frames", "eye", "carried", False),
                                       ("the logged filter, replayed, all frames", "old_eye", "old_carried", False),
                                       ("the present filter, replayed, all frames", "new_eye", "new_carried", False),
                                       (f"the log, {where}", "eye", "carried", True),
                                       ("the logged filter, replayed, there", "old_eye", "old_carried", True),
                                       ("the present filter, replayed, there", "new_eye", "new_carried", True)):
        found = judge(rows, eye, carried, known, vector=True)
        counts = {whose: sum(1 for f in found if f[0] == whose) for whose in ("filter", "game", "reduced")}
        lengths = sorted((f[1] for f in judge(rows, eye, carried, known) if f[0] == "filter"), reverse=True)

        def root_change(a, b, c):
            return change(a["root"], b["root"], c["root"], True)
        beyond = sum(1 for f in found if f[0] == "filter" and f[1] - root_change(*f[3:]) > BEYOND_M)
        print(f"{title}: {counts['filter']} steps of {STEP_M} m or more are the filter's ({beyond} of them more than {BEYOND_M} m beyond the "
              f"root's own change of step), {counts['game']} the game's own, {counts['reduced']} reduced; by the lengths of the steps "
              f"(the old count) {len(lengths)} the filter's" + (f", the largest {lengths[0]:.3f} m" if lengths else ""))
        found_carried = carries(rows, carried, known)
        print(carry_line(found_carried["metres"], found_carried))
        for whose, shown, wanted, a, b, c in found:
            if whose == "filter":
                print(f"  {c['at']} kind {c['kind']}: shown change {shown:.3f} m (by lengths {change(a[eye], b[eye], c[eye], False):.3f} m), "
                      f"wanted {wanted:.3f} m, root {root_change(a, b, c):.3f} m, carried {c[carried]:.3f} m")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
