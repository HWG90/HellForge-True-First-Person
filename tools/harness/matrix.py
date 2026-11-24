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
    "toggle-fast": ("key f11; wait 0.08; key f11; wait 0.08; key f11; wait 0.08; key f11", 2.0),   # the key is polled once a frame: 0.11 s up, so no press falls between two polls at 30 frames a second
    "ragdoll": ("look 0 900; wait 0.2; key g; wait 0.4; look 0 -900; hold s 0.4", 6.0),
    "death": ("look 0 900; wait 0.2; key g; wait 0.6; key g; wait 0.4; look 0 -900", 9.0),   # the view is put back level for what runs after
}
LOCK = re.compile(r"Lock: \+(\d+) frames held \(\+(\d+) in another camera, kind now (\d+)\), \+(\d+) missed \(kind (\d+), helper error (\d+)\), largest kick ([\d.]+)")
CUT = re.compile(r"Cut: largest camera jump ([\d.]+) m")
POP_M = 0.06   # the presented eye's step may change by this much between two frames (30 a second in the harness)


def log_path():
    sessions = sorted((HF / "logs/sessions").iterdir(), key=lambda p: p.stat().st_mtime)
    return sessions[-1] / "mod-goose.true-first-person.log"


GAP_MS = 80   # two traced frames further apart than this are not consecutive (30 frames a second in the harness)
OWN_M = 0.03  # under this carried in both frames, and the wanted eye making 0.8 of the change itself: the eye's own


def _sub(a, b):
    return [x - y for x, y in zip(a, b)]


def _length(v):
    return sum(x * x for x in v) ** 0.5


def measure(rows):
    """One dict for every three consecutive frames of `rows` (trace.frames), named for the third:
      shown    how much the presented eye's step changed, as a vector: |(c-b) - (b-a)|
      lengths  the same by the lengths of the two steps, ||c-b| - |b-a||: the measure until round 10 (2026-11-24).
               It does not see a change of direction: 0.04 m forwards and then 0.04 m back is 0.08 m and counted 0;
               the arc of 08:12:09.5 in the Stable soak of that day, eight frames of 0.07 to 0.10 m, read as one 0.100.
      wanted   the same vector change of the wanted eye; root: of the body root
      body     what was taken off for the body's own change of step (0: nothing)
      after    how many frames after the root's that is: 0 (with the root), 1 (with the rig eye), None (nothing)
      beyond   shown less that: what is judged against POP_M
      whose    "body" (shown is POP_M or more and beyond is not), "own" (the wanted eye makes 0.8 of it and under
               OWN_M is carried: the eye's own start or stop, the filter did nothing), "filter" (anything else), or
               None under POP_M
    The body's own: a climb lifts the root 1.2 m in eight frames, its step 0.03, 0.13, 0.29, 0.38, 0.30, 0.15, 0.02 m
    (the vaults of that soak, 5 of 5 flagged at 0.09 to 0.17 m), and the rig eye is the root a frame late: the game's
    own lag, in every native frame. The eye moves with the body and no more; a camera fixed to a body shows it
    whatever the mod does. So where the root's own step changes by POP_M or more, that vector is taken off the shown
    eye's change in the frame in which the body's lunge reaches the eye, which is one of two (round 11):
      the root's own frame, where the wanted eye makes that change (to 0.03 m or half of it) in that frame (the head
      camera) or the next (the rig eye): the shown eye went with the root;
      the frame after the root's, where the wanted eye makes that change in that same frame: the shown eye went with
      the rig eye.
    One change of the root is taken off in one frame only, and only frames over POP_M ask for one. A frame whose
    own wanted eye makes the change comes before one whose wanted eye makes it a frame later; then the better fit;
    of two that fit alike the root's own frame. Round 10 looked for the wanted eye a
    frame either side of the frame judged, whichever frame the root's change was from, and let both frames have the
    one change: an eye that took the body's lunge and lurched as far again in the next frame read as 0.05 m, and an
    eye given the root's change a frame after the wanted eye had made it (R2: 08:34:07.181 in the Bodycam soak, 0.55 m
    carried) read as the body's. That eye has nothing taken off now, nor one that moves a frame before the root does.
    Not where the root's change is under POP_M: a marginal step is not argued down by a few centimetres of the
    root. And not where the wanted eye does not make it (the head camera of a body checked in a dive: the root
    stops, the head goes on, and an eye slowed with the root is the filter's doing). Rows without "root=" (before
    0.6.0-feel.38) have nothing taken off.
    What this lets through: a step the filter makes in the very frame the body's lunge reaches the eye, of that
    lunge's size and way (within POP_M), and no other that frame or the next. And the body itself: in a native frame
    the rig eye is given the root's change a frame after the root, by the game; where the shown eye makes it with the
    rig eye that is the body's, whatever was carried. What the body did stays in view as "body" in every line.
    Each dict also has at (the frame's own time), written (the stamp of its line) and tick (the helper's clock: what
    tells two frames apart; frames from before an edge are all written with it, under one stamp)."""
    changes = {}
    for i in range(2, len(rows)):
        a, b, c = rows[i - 2:i + 1]
        if not (0 < b["tick"] - a["tick"] <= GAP_MS and 0 < c["tick"] - b["tick"] <= GAP_MS):
            continue
        one, two = _sub(b["eye"], a["eye"]), _sub(c["eye"], b["eye"])
        change = {"eye": _sub(two, one), "lengths": abs(_length(two) - _length(one)), "wanted": None, "root": None}
        for key in ("wanted", "root"):
            if a[key] and b[key] and c[key]:
                change[key] = _sub(_sub(c[key], b[key]), _sub(b[key], a[key]))
        changes[i] = change
    fits = []
    for i, change in changes.items():
        if _length(change["eye"]) < POP_M:
            continue
        for j, k in ((i, i), (i - 1, i), (i, i + 1)):   # the root's change of frame j with the wanted eye's of frame k
            root = changes.get(j, {}).get("root")
            wanted = changes.get(k, {}).get("wanted")
            if not root or not wanted or _length(root) < POP_M:
                continue
            off = _length(_sub(wanted, root))
            if off <= max(0.03, 0.5 * _length(root)):
                fits.append((k - i, round(off, 3), i - j, i, j))
    lunge, given = {}, set()   # frame -> the frame of the root's change that is its body's lunge; each given once
    for _, _, _, i, j in sorted(fits):
        if i not in lunge and j not in given:
            lunge[i] = j
            given.add(j)
    found = []
    for i, change in changes.items():
        c, b = rows[i], rows[i - 1]
        shown = _length(change["eye"])
        beyond, body, after = shown, 0.0, None
        if i in lunge:
            root = changes[lunge[i]]["root"]
            left = _length(_sub(change["eye"], root))
            if left < beyond:
                beyond, body, after = left, _length(root), i - lunge[i]
        wanted = _length(change["wanted"]) if change["wanted"] else None
        whose = None
        if shown >= POP_M:
            if beyond < POP_M:
                whose = "body"
            elif wanted is not None and wanted >= 0.8 * shown and b["carried"] < OWN_M and c["carried"] < OWN_M:
                whose = "own"
            else:
                whose = "filter"
        found.append({"at": c["at"], "written": c["written"], "tick": c["tick"], "kind": c["kind"], "looked": c["looked"], "taken": c["taken"], "carried": c["carried"], "shown": shown,
                      "lengths": change["lengths"], "wanted": wanted, "root": _length(change["root"]) if change["root"] else None,
                      "body": body, "after": after, "beyond": beyond, "whose": whose})
    return found


def describe(result):
    """What a soak or matrix line says of the jump: the figure judged, the body's own beside it, the old figure."""
    if not result.get("from_trace"):
        return "not traced" if not result.get("judged") else f"jump {result['largest_jump_m']:.3f} m"
    if not result["judged"]:
        return "no frames traced"
    text = f"jump {result['largest_jump_m']:.3f} m"
    if result["body_m"] >= POP_M:
        text += f" (body {result['body_m']:.3f})"
    return text + f" [by lengths {result['by_lengths_m']:.3f}]"


def pop_lines(result, indent="       "):
    """One line for every frame at or over POP_M: when (the frame's own time, and the stamp of its line where that
    is another: frames from before an edge are written with it), what the eye did, whose it is."""
    out = []
    for p in result.get("pops", []):
        body = "the body's own" + {0: "", 1: " a frame after the root, with the wanted eye"}.get(p.get("after"), "") + ", not counted"
        who = {"body": body, "own": "the eye's own (nothing carried)", "filter": "the filter's"}[p["whose"]]
        wanted = "-" if p["wanted"] is None else f"{p['wanted']:.3f}"
        root = "-" if p["root"] is None else f"{p['root']:.3f}"
        when = p["at"] if p.get("written", p["at"]) == p["at"] else f"{p['at']} (written {p['written']})"
        out.append(f"{indent}{when} kind {p['kind']} looked {'-' if p['looked'] is None else p['looked']} taken {p['taken']}: shown {p['shown']:.3f} m "
                   f"(by lengths {p['lengths']:.3f}), wanted {wanted}, root {root}, carried {p['carried']:.3f}, beyond the body {p['beyond']:.3f}: {who}")
    return out


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
    # measures before the eye continuity and calls an eased 0.7 m a jump. The trace stops half a second after an edge
    # and starts again at the next: frames on either side of such a pause are not neighbours (a dive begun inside one
    # window and over before the next read as 0.12 m), and measure() takes none of them together.
    import trace as trace_reader
    rows = trace_reader.frames(lines)
    traced = bool(rows)
    found = measure(rows)
    if traced:
        jump = max([0.0] + [f["beyond"] for f in found])
    ok = held > 0 and missed == 0 and (jump < (POP_M if traced else LIMIT_M) or toggles) and not refused   # F11 itself is a cut by design
    return {"pass": ok, "frames": held, "other_camera_frames": other, "missed": missed, "kinds": sorted(kinds),
            "largest_jump_m": jump, "from_trace": traced, "judged": len(found),
            "by_lengths_m": max([0.0] + [f["lengths"] for f in found]), "body_m": max([0.0] + [f["body"] for f in found]),
            "pops": [f for f in found if f["whose"]], "largest_kick_deg": kick, "notes": sorted(set(notes))[:6]}


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
                  f"{describe(r)} kick {r['largest_kick_deg']:.1f} deg kinds {r['kinds']}")
            for note in r["notes"] + pop_lines(r, ""):
                print(f"       {note}")
    (out / "matrix.json").write_text(json.dumps(results, indent=1))
    failed = [n for n, r in results.items() if not r["pass"]]
    print(f"{len(results) - len(failed)} of {len(results)} passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
