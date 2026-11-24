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
 - A row's time is its "tick=" where the line has one (0.6.0-feel.41 on). Before that it is "t=", to 0.1 ms and from
   the window's edge, and the edge is placed by the line's own stamp (to 1 ms, and written up to a frame late): the
   time between the last row of one window and the first of the next can be a millisecond or two off.
 - Full aim (quiet) is taken from "quiet=" where the line has it and as off where not.
 - The filter forgotten by the helper (the lock let go) shows as "start" in taken= where the line can say so; before
   that only as a pause.
 - The fitted axes are a guess: where nothing is carried and nothing taken up no turn can be seen and the axes stay
   as they were, so a root that turns evenly there and is caught turning later looks as if it began to turn then.
 - The damping (src/camera_damp.h) is not run again: the wanted eye of a head-camera frame is logged after it.
 - The helper's own decision to look (smooth_detect) is taken from the log; the present filter might have kept a run
   of jumps open for a different time.
"""
import argparse
import math
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
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
                   r"(?: quiet=(\d))?(?: tick=([\d.]+))?")
SOURCES = {"head": 1, "native": 2, "repeat": 2}
TAKEN = ((1, "jump"), (2, "edge"), (4, "root"), (8, "restart"))


def read(lines):
    """Every Trace row in order of time: one for each call of the filter."""
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
               "looked": int(m[13]), "taken": m[14], "quiet": int(m[15] or 0), "clock": float(m[16]) if m[16] else None, "at": line[:12]}
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


def run(binary, mode, rows, axes):
    text = "".join("%.3f %d %d %d %d %s %s %s %s\n" % (row["tick"], SOURCES.get(row["source"], 0), row["looked"], row.get("quiet", 0),
                                                       row.get("fresh", False), " ".join(map(str, row["wanted"])), " ".join(map(str, row["root"])),
                                                       " ".join(map(str, row["eye"])), " ".join(map(str, row[axes])))
                   for row in rows)
    out = subprocess.run([str(binary), mode], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
    assert len(out) == len(rows)
    return [[float(x) for x in line.split()] for line in out]


def taken(bits):
    return "+".join(name for bit, name in TAKEN if int(bits) & bit) or "-"


def header(revision, work, name):
    """The filter's header as it was in `revision`, in a folder of its own."""
    folder = Path(work) / name
    if Path(revision).is_dir():  # a folder with the two headers in place of a revision
        return Path(revision).resolve()
    folder.mkdir()
    for file in ("camera_continuity.h", "camera_math.h"):
        (folder / file).write_text(subprocess.run(["git", "-C", str(ROOT), "show", f"{revision}:src/{file}"], capture_output=True, text=True,
                                                  check=True).stdout)
    return folder


def replay(rows, logged=None, present=None):
    """Runs both filters over `rows` (read(), all of them with axes or none) and returns (the frames, whether the axes
    were given). `present`: a git revision, or a folder with camera_continuity.h and camera_math.h, in place of this
    tree's filter. Each row gets old_eye, old_carried, old_taken (the logged build's filter), new_eye, new_carried,
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
            row["follows"] = row["known"] and row["misfit"] < EXACT_M if given else row["misfit"] < NEAR_M
        for row, out in zip(rows, run(new, "run", rows, "fit")):
            row["new_eye"], row["new_carried"], row["new_taken"] = out[0:3], out[3], taken(out[4])
    return (merge(rows) if given else rows), given


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log")
    parser.add_argument("--logged", default=None, help="git revision of the build that wrote the log")
    parser.add_argument("--present", default=None, help="git revision (or a folder with the two headers) to run in place of this tree's filter")
    parser.add_argument("--from", dest="start", default=None)
    parser.add_argument("--to", dest="end", default=None)
    parser.add_argument("--emit", default=None, help="write the rows of --from/--to as a C array of this name")
    parser.add_argument("--worst", type=int, default=0, help="list this many rows where the logged filter, replayed, is farthest from the log")
    args = parser.parse_args(argv[1:])
    calls = read(Path(args.log).read_text(errors="replace").splitlines())
    rows, given = replay(calls, args.logged, args.present)
    if not given:
        calls = rows

    def chosen(row):
        return not (args.start and row["at"] < args.start) and not (args.end and row["at"] > args.end)
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
