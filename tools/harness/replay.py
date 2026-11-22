#!/usr/bin/env python3
"""Replay a log's "Trace:" lines through the continuity filter alone and count the steps the filter makes.

  replay.py LOG [--logged REV] [--from HH:MM:SS.mmm --to HH:MM:SS.mmm [--emit NAME]]

With --from/--to: the frames of that time, one line each (the log, the logged filter, the present filter). With
--emit as well: the same frames as a C array NAME for a test (tests/reference/soak-6-steps.h was written so).

The filter (src/camera_continuity.h) is run twice over the logged frames (wanted eye, body root, source, looked):
once as it was in the build that wrote the log (REV, a git revision; default b7ce1ab, 0.6.0-feel.39) and once as it
is in this tree. The first run also finds the body root's axes, which the Trace lines of feel.39 and before do not
carry: in each frame the turn with which the old filter presents the eye the log shows (replay_filter.c). The second
run is given those axes. A log whose Trace lines carry "axes=" needs no fit; those axes are used as they are.

A step is counted as the log's judge counts it: for frames a, b, c (5 to 80 ms apart) the shown change is
| |c.eye-b.eye| - |b.eye-a.eye| |, the wanted change the same of the wanted eye. At 0.06 m or more of shown change it
is the game's own when the wanted change is at least 0.8 of it and under 0.03 m is carried in b and c, "reduced" when
the wanted change is over 1.2 of it, and otherwise the filter's.

What this leaves out:
 - Only the frames in the Trace windows are there (0.5 s after each State edge and six frames before it). The filter
   starts from nothing at each window; what it carried into one is not known. So the replays are judged only where
   the logged filter, replayed, shows the eye the log shows (within 0.02 m, in all three frames of a step).
 - The fitted axes are a guess: where nothing is carried and nothing taken up no turn can be seen and the axes stay
   as they were, so a root that turns evenly there and is caught turning later looks as if it began to turn then.
 - Full aim (quiet) is not in the Trace lines; it is taken as off.
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
NEAR_M = 0.02
BEYOND_M = 0.05
TRACE = re.compile(r"^(\d+):(\d+):(\d+)\.(\d+) Trace: t=(-?[\d.]+) kind=(\d+) eye_world=\(([-\d. ]+)\) step=[-\d.]+ wanted=\(([-\d. ]+)\) "
                   r"correction=([-\d.]+).*?source=(\S+) root=\(([-\d. ]+)\)(?: axes=\(([-\d. ]+)\))? looked=(\d) taken=(\S+)")
SOURCES = {"head": 1, "native": 2, "repeat": 2}
TAKEN = ((1, "jump"), (2, "edge"), (4, "root"), (8, "restart"))


def frames(lines):
    """The log's frames in order of time: one for each frame presented (two Trace rows of one frame are one)."""
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
        window.append({"stamp": stamp, "t": float(m[5]), "kind": int(m[6]), "eye": [float(x) for x in m[7].split()],
                       "wanted": [float(x) for x in m[8].split()], "carried": float(m[9]), "source": m[10],
                       "root": [float(x) for x in m[11].split()], "axes": [float(x) for x in m[12].split()] if m[12] else None,
                       "looked": int(m[13]), "taken": m[14], "at": line[:12]})
    if window:
        windows.append(window)
    rows = []
    for window in windows:
        # t is from the window's edge; the line is written at or after its frame, so the edge is the earliest (stamp - t)
        edge = min(row["stamp"] - row["t"] for row in window)
        for row in window:
            row["tick"] = edge + row["t"]
        rows.extend(window)
    rows.sort(key=lambda row: row["tick"])
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


def judge(rows, eye="eye", carried="carried", known=False):
    """[(whose, shown change, wanted change, a, b, c)] for every step of STEP_M or more. `known`: only where the
    logged filter, replayed, is within NEAR_M of the log in all three frames."""
    found = []
    for a, b, c in zip(rows, rows[1:], rows[2:]):
        if not (5 <= b["tick"] - a["tick"] <= 80 and 5 <= c["tick"] - b["tick"] <= 80):
            continue
        if known and max(a["misfit"], b["misfit"], c["misfit"]) >= NEAR_M:
            continue
        shown = abs(math.dist(c[eye], b[eye]) - math.dist(b[eye], a[eye]))
        wanted = abs(math.dist(c["wanted"], b["wanted"]) - math.dist(b["wanted"], a["wanted"]))
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


def build(header_dir, name, work):
    binary = Path(work) / name
    subprocess.run(["cc", "-std=c11", "-O1", "-ffp-contract=off", "-I", str(header_dir), str(ROOT / "tools/harness/replay_filter.c"), "-lm",
                    "-o", str(binary)], check=True)
    return binary


def run(binary, mode, rows, axes):
    text = "".join("%.2f %d %d %s %s %s %s\n" % (row["tick"], SOURCES.get(row["source"], 0), row["looked"], " ".join(map(str, row["wanted"])),
                                                 " ".join(map(str, row["root"])), " ".join(map(str, row["eye"])), " ".join(map(str, row[axes])))
                   for row in rows)
    out = subprocess.run([str(binary), mode], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
    assert len(out) == len(rows)
    return [[float(x) for x in line.split()] for line in out]


def taken(bits):
    return "+".join(name for bit, name in TAKEN if int(bits) & bit) or "-"


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log")
    parser.add_argument("--logged", default="b7ce1ab", help="git revision of the build that wrote the log")
    parser.add_argument("--from", dest="start", default=None)
    parser.add_argument("--to", dest="end", default=None)
    parser.add_argument("--emit", default=None, help="write the frames of --from/--to as a C array of this name")
    args = parser.parse_args(argv[1:])
    rows = frames(Path(args.log).read_text(errors="replace").splitlines())
    identity = [1, 0, 0, 0, 1, 0, 0, 0, 1]
    with tempfile.TemporaryDirectory() as work:
        old_dir = Path(work) / "logged"
        old_dir.mkdir()
        for name in ("camera_continuity.h", "camera_math.h"):
            (old_dir / name).write_text(subprocess.run(["git", "-C", str(ROOT), "show", f"{args.logged}:src/{name}"], capture_output=True, text=True,
                                                       check=True).stdout)
        old, new = build(old_dir, "logged_filter", work), build(ROOT / "src", "present_filter", work)
        given = all(row["axes"] for row in rows)
        for row in rows:
            row["start"] = row["axes"] if given else identity
        fitted = run(old, "run" if given else "fit", rows, "start")
        for row, out in zip(rows, fitted):
            row["old_eye"], row["old_carried"], row["old_taken"], row["fit"], row["misfit"] = out[0:3], out[3], taken(out[4]), out[5:14], out[14]
        for row, out in zip(rows, run(new, "run", rows, "fit")):
            row["new_eye"], row["new_carried"], row["new_taken"] = out[0:3], out[3], taken(out[4])
    if args.emit:
        chosen = [row for row in rows if not (args.start and row["at"] < args.start) and not (args.end and row["at"] > args.end)]
        print(f"/* {chosen[0]['at']} to {chosen[-1]['at']}: ms, source, looked, wanted eye, root, root axes "
              f"({'logged' if given else 'fitted'}), eye shown in the log */")
        print(f"static const SoakFrame {args.emit}[] = {{")
        for row in chosen:
            def group(values, digits):
                return "{" + ", ".join(f"{v:.{digits}f}f" for v in values) + "}"
            print(f"  {{{row['tick'] - chosen[0]['tick']:.1f}f, {SOURCES.get(row['source'], 0)}, {row['looked']}, {group(row['wanted'], 4)}, "
                  f"{group(row['root'], 4)},\n    {group(row['fit'], 6)}, {group(row['eye'], 4)}}},")
        print("};")
        return 0
    if args.start or args.end:
        before = None
        for row in rows:
            if (args.start and row["at"] < args.start) or (args.end and row["at"] > args.end):
                before = None
                continue
            if before:
                a = [x - y for x, y in zip(row["fit"], before["fit"])]
                print(f"{row['at']} kind {row['kind']} looked {row['looked']} wanted {math.dist(row['wanted'], before['wanted']):.3f} "
                      f"root {math.dist(row['root'], before['root']):.3f} | log {math.dist(row['eye'], before['eye']):.3f} carried {row['carried']:.3f} "
                      f"{row['taken']:9} | logged filter {math.dist(row['old_eye'], before['old_eye']):.3f} carried {row['old_carried']:.3f} "
                      f"{row['old_taken']:9} misfit {row['misfit']:.3f} axes turned {math.sqrt(sum(x * x for x in a) / 2):.3f} | present filter "
                      f"{math.dist(row['new_eye'], before['new_eye']):.3f} carried {row['new_carried']:.3f} {row['new_taken']}")
            before = row
        return 0
    close = sum(row["misfit"] < NEAR_M for row in rows)
    print(f"{len(rows)} frames; the logged filter with the {'given' if given else 'fitted'} axes is within {NEAR_M} m of the log in {close} of them")
    for title, eye, carried, known in (("the log, all frames", "eye", "carried", False),
                                       ("the logged filter, replayed, all frames", "old_eye", "old_carried", False),
                                       ("the present filter, replayed, all frames", "new_eye", "new_carried", False),
                                       ("the log, where the replay follows it", "eye", "carried", True),
                                       ("the logged filter, replayed, there", "old_eye", "old_carried", True),
                                       ("the present filter, replayed, there", "new_eye", "new_carried", True)):
        found = judge(rows, eye, carried, known)
        counts = {whose: sum(1 for f in found if f[0] == whose) for whose in ("filter", "game", "reduced")}

        def root_change(a, b, c):
            return abs(math.dist(c["root"], b["root"]) - math.dist(b["root"], a["root"]))
        beyond = sum(1 for f in found if f[0] == "filter" and f[1] - root_change(*f[3:]) > BEYOND_M)
        print(f"{title}: {counts['filter']} steps of {STEP_M} m or more are the filter's ({beyond} of them more than {BEYOND_M} m beyond the "
              f"root's own change of step), {counts['game']} the game's own, {counts['reduced']} reduced")
        for whose, shown, wanted, a, b, c in found:
            if whose == "filter":
                print(f"  {c['at']} kind {c['kind']}: shown change {shown:.3f} m, wanted {wanted:.3f} m, root {root_change(a, b, c):.3f} m, "
                      f"carried {c[carried]:.3f} m")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
