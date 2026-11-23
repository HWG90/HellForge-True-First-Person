#!/usr/bin/env python3
"""Made-up scenes through the continuity filter, held against the numbers recorded for main.

  probes.py [--present REV] [--against REV] [--record]

The soaks say what the filter does to the play that was logged; they ran at 30 frames a second and cannot say what a
change does at another frame time or to a motion that did not happen to be in them (a fix that looked good on the
soaks threw the eye backwards in a rig re-seated in two frames: docs/design/specs/2026-11-22-locked-view-design.md,
"What the count did not see"). These scenes are the ones that check was made with, at 8, 16.7, 33.3 and 50 ms a
frame; tools/harness/probe_filter.c says what each is. For each: the largest change of the presented eye's step in
the world as a vector (m), the largest correction carried (m), and the time to settle (ms from the event to the last
frame more than 0.005 m is carried).

Without options: this tree's filter (src/camera_continuity.h) against tools/harness/probes-main.txt, the numbers of
the filter of 0.6.0-feel.41 (66ee111). Every scene is listed that differs, and the exit code is 1 if one is worse:
more than 0.0005 m more change or more carried, or later settled. --present REV: a git revision (or a folder with
camera_continuity.h and camera_math.h) in place of this tree's. --against REV: that revision's filter in place of
the recorded numbers. --record: write this tree's (or --present's) numbers as the new record; only for a filter
that is to be the bound from now on.

tests/test_harness_probes.py runs the comparison with the other harness tests."""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RECORD = ROOT / "tools/harness/probes-main.txt"
SLACK_M = 0.0005


def header(revision, work, name):
    """The filter's header as it was in `revision` (or the folder `revision`), in a folder of its own."""
    if Path(revision).is_dir():
        return Path(revision).resolve()
    folder = Path(work) / name
    folder.mkdir()
    for file in ("camera_continuity.h", "camera_math.h"):
        (folder / file).write_text(subprocess.run(["git", "-C", str(ROOT), "show", f"{revision}:src/{file}"], capture_output=True, text=True,
                                                  check=True).stdout)
    return folder


def parse(text):
    """{(scene, ms a frame): (change, carried, settled)} from probe_filter's lines; lines beginning with # are notes."""
    out = {}
    for line in text.splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, dt, change, carried, settled = line.split()
        out[(name, float(dt))] = (float(change), float(carried), float(settled))
    return out


def measure(revision=None):
    """The scenes through the filter of `revision` (None: this tree's): (the numbers, probe_filter's own lines)."""
    with tempfile.TemporaryDirectory() as work:
        folder = header(revision, work, "filter") if revision else ROOT / "src"
        binary = Path(work) / "probe_filter"
        subprocess.run(["cc", "-std=c11", "-O1", "-ffp-contract=off", "-I", str(folder), str(ROOT / "tools/harness/probe_filter.c"), "-lm", "-o",
                        str(binary)], check=True)
        text = subprocess.run([str(binary)], capture_output=True, text=True, check=True).stdout
    return parse(text), text


def compare(bound, now):
    """[(scene, ms, bound, now, worse)] for every scene whose numbers differ; worse: any of the three is over the bound."""
    out = []
    for key in sorted(bound, key=lambda k: (k[0], k[1])):
        b, n = bound[key], now.get(key)
        if n is None:
            out.append((key[0], key[1], b, None, True))
        elif n != b:
            out.append((key[0], key[1], b, n, n[0] > b[0] + SLACK_M or n[1] > b[1] + SLACK_M or n[2] > b[2]))
    return out


def group(numbers):
    """The flicker scenes as one (the worst of 2 to 12 frames), the others as they are."""
    out = {}
    for (name, dt), values in numbers.items():
        key = ("flicker (2 to 12 frames)" if name.startswith("flicker-") else name, dt)
        out[key] = tuple(max(a, b) for a, b in zip(out[key], values)) if key in out else values
    return out


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--present", default=None, help="git revision (or a folder with the two headers) in place of this tree's filter")
    parser.add_argument("--against", default=None, help="git revision whose filter is the bound, in place of the recorded numbers")
    parser.add_argument("--record", action="store_true", help="write the numbers as the new record")
    parser.add_argument("--table", action="store_true", help="print every scene's numbers beside the bound's (the flicker scenes as one)")
    args = parser.parse_args(argv[1:])
    now, text = measure(args.present)
    if args.record:
        RECORD.write_text("# tools/harness/probes.py --record: scene, ms a frame, largest change of the presented step as a vector (m),\n"
                          "# largest correction carried (m), settled (ms). The bound for the filter: see probes.py.\n" + text)
        print(f"recorded {len(now)} scenes in {RECORD}")
        return 0
    bound = measure(args.against)[0] if args.against else parse(RECORD.read_text())
    if args.table:
        b, n = group(bound), group(now)
        print("scene | ms a frame | change (m), bound / now | carried (m), bound / now | settled (ms), bound / now")
        for key in b:
            print(f"{key[0]} | {key[1]:g} | {b[key][0]:.4f} / {n[key][0]:.4f} | {b[key][1]:.4f} / {n[key][1]:.4f} | {b[key][2]:.0f} / {n[key][2]:.0f}")
    differ = compare(bound, now)
    worse = [d for d in differ if d[4]]
    for name, dt, b, n, bad in differ:
        print(f"{'WORSE ' if bad else 'differs'} {name} at {dt:g} ms: change {b[0]:.4f} -> {n[0]:.4f} m, carried {b[1]:.4f} -> {n[1]:.4f} m, "
              f"settled {b[2]:.0f} -> {n[2]:.0f} ms" if n else f"WORSE  {name} at {dt:g} ms: not measured")
    print(f"{len(bound)} scenes: {len(differ)} differ from the bound, {len(worse)} worse")
    return 1 if worse else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
