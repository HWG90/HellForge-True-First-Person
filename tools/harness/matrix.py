#!/usr/bin/env python3
"""The test matrix: play every action the harness can produce with the view on and judge each from the mod's log.

Needs a harness session standing in a mission with player control (session.sh up). Takes the keyboard and mouse.

  matrix.py OUT_DIR [phase ...]      phases default to all, in order; see PHASES

A phase passes when the mod logged no frame of the view that was not first person ("+0 missed" on every Lock line
of the phase), the largest camera jump it logged is under LIMIT_M, and nothing was refused. The result is printed
and written to OUT_DIR/matrix.json; one capture per phase goes beside it. A pass says the camera stayed first
person and did not jump; it does not say the picture looks right: look at the captures.

Round 24: a phase also fails on a float, the presented eye held more than trace.FLOAT_M off the raw target (the wanted
eye less the damping) for trace.FLOAT_MS (trace.held_offsets): the step judge does not see an eye that drifts 0.6 m off
and back at 0.04 m a frame, which is how the clambers of the user's log of 2026-11-24 floated while "jump-vault" passed.
The phases clamber-low and clamber-high need the body standing facing a ledge (a low wall for the first, one at
head height or more for the second, about 1.5 m ahead: tools/harness/walk.py to it first); the input is the same, the
ledge decides which the game does, and the result says which mask the log shows (0x20 a vault held in the native view,
0x22 a clamber in the head camera). Run them once in each camera style: the style is the log's, not a phase's.

Round 26: the aim phases reach full aim. The right button alone is the game's shoulder aim; full aim (the sights on the
eye, the log's "State: aim in") is the game's other aim mode, switched with the middle button while aiming. The phases
in AIMED are played by aim.full_aim(): hold the aim, press the aim-mode button, read the log until it says full aim
(a second press where it does not), play the phase's action in full aim, hold, let go. Such a phase also fails where
full aim was not reached or the "Aim:" lines are missing (aim.verdict). aim-full holds full aim 2 s; aim-scope-zoom
steps the scope's range (round 30: hold R and the mouse wheel up, the game's PC input; in full aim first, then before
aiming, each range held 2 s in full aim: aim.scope_zoom) and needs a scoped rifle with ranges in hand; a capture of
each range goes beside the phase's. docs/guides/AIM-TEST.md "The harness".

Round 30: dive-swap is a dive and a swap that leave the game's view, to measure the head of a diving body through the
veil (veil_head on the Trace rows): played by dive.dive_swap(), which brings the primary up, waits for the game's view,
and tries the swap at several delays into the dive until the log says the view was left. It also fails where no try
left the view or no veiled row was written (dive.verdict).

The camera style the run was in (Bodycam or Stable) is read from the log's own "Camera style:" lines and printed at
the top, where it changes, and in the summary; each phase's result carries it. It is never taken from an argument or a
file name: the two feel.49 soaks of 2026-11-24 were named the wrong way round.

Round 44: the throw phases (THROWS: grenade, grenade-held, stratagem-menu) also fail where a head-camera frame of a
body on foot showed the eye less than FRONT_M in front of the head node along the level view (behind_head): the
feel.58 matrix's "grenade" passed while every throw's held eye stood 0.18 to 0.31 m behind the head (the cape and
the back in view), because the veil's frames were left out of the step judge and the eye was never measured against
the head. A build from round 44 on writes the head on every head-camera row (head_shown=); on an older log the head is
taken where the leave's veiled row put it (veil_head=), carried in the body root's frame (estimated).

Round 39: the reload phases (RELOADS) are judged on the traced eye only (a phase with no Trace rows fails), and also
fail on a slide: the eye's offset from the body root along the level view changing by more than SLIDE_M within
SLIDE_MS while the root stays within ROOT_STILL_M (reload_slide). The feel.44 matrix's "reload" passed while every
reload slid the eye 0.21 to 0.26 m back and forward again over 75 ms: five frames of 0.045 m, none of them a jump.
"""
import json
import re
import sys
import time
from pathlib import Path

import aim
import dive
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
    "dive-swap": (dive.script(dive.SWAP_AFTER_S[0]), 2.0),   # played by dive.dive_swap() (DIVES): this is one of its tries
    "stand-up": ("key c; wait 0.2; key c", 1.5),
    "fire": ("click left 0.6", 1.0),
    # The aim phases are played by aim.full_aim() (AIMED below); the scripts are what a plain run of them sends.
    "aim": ("down right; wait 0.35; click middle; wait 1.5; up right", 1.0),
    "aim-fire": ("down right; wait 0.35; click middle; wait 0.7; click left 0.5; wait 0.4; up right", 1.0),
    "aim-walk": ("down right; wait 0.35; click middle; wait 0.5; hold w 1.0; up right", 1.0),
    "aim-full": ("down right; wait 0.35; click middle; wait 2.5; up right", 1.0),
    "aim-scope-zoom": ("down right; wait 0.35; click middle; wait 1.0; down r; wait 0.8; wheel 1; wait 0.3; up r; wait 2.0; up right", 1.0),
    "reload": ("click left 0.3; wait 0.3; key r", 4.0),
    "ammo-check": ("hold r 0.8", 2.5),
    "swap-secondary": ("key 2", 2.0),
    "pistol-fire": ("click left 0.3", 1.0),
    "pistol-aim": ("down right; wait 0.35; click middle; wait 1.5; up right", 1.0),
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
    "clamber-low": ("down w; wait 0.4; key space; wait 1.0; up w", 2.5),    # facing a low wall: a vault, mask 0x20 (round 24)
    "clamber-high": ("down w; wait 0.4; key space; wait 1.4; up w", 3.0),   # facing a ledge at head height: a clamber, mask 0x22 then 0x20
    "toggle-off-on": ("key u; wait 1.0; key u", 1.5),   # U: the mod's toggle key (drive.TOGGLE)
    "toggle-fast": ("key u; wait 0.08; key u; wait 0.08; key u; wait 0.08; key u", 2.0),   # the key is polled once a frame: 0.11 s up, so no press falls between two polls at 30 frames a second
    "ragdoll": ("look 0 900; wait 0.2; key g; wait 0.4; look 0 -900; hold s 0.4", 6.0),
    "death": ("look 0 900; wait 0.2; key g; wait 0.6; key g; wait 0.4; look 0 -900", 9.0),   # the view is put back level for what runs after
}
# Round 26: phases played in full aim by aim.full_aim(): name -> (what is played once the log says full aim: an input
# script, "zoom" for aim.scope_zoom, or None; seconds held after it). aim.verdict() judges the aim with that hold.
AIMED = {
    "aim": (None, 1.5),
    "aim-fire": ("click left 0.5; wait 0.4", 0.3),
    "aim-walk": ("hold w 1.0", 0.3),
    "aim-full": (None, aim.HOLD_S),
    "aim-scope-zoom": ("zoom", aim.ZOOM_HOLD_S),
    "pistol-aim": (None, 1.5),
}
# Round 30: phases played by dive.dive_swap(), judged by dive.verdict().
DIVES = {"dive-swap"}
DRIVEN = set(AIMED) | DIVES   # the phases play() hands to a function that reads the log as it goes
# Round 39: phases judged on the traced eye and its slide along the view (reload_slide).
RELOADS = {"reload", "pistol-reload"}
SLIDE_M = 0.05      # the eye's offset from the root along the level view may change by this much ...
SLIDE_MS = 500.0    # ... within this (a reload's edge is 75 ms; the user's slowest, R2, 0.4 s) ...
ROOT_STILL_M = 0.02 # ... where the root moved less than this in the same time
# Round 44: phases judged on the eye against the head node too (behind_head).
THROWS = {"grenade", "grenade-held", "stratagem-menu"}
FRONT_M = 0.05      # the least in front of the head node an eye shown on foot may be, along the level view
LOCK = re.compile(r"Lock: \+(\d+) frames held \(\+(\d+) in another camera, kind now (\d+)\), \+(\d+) missed \(kind (\d+), helper error (\d+)\), largest kick ([\d.]+)")
CUT = re.compile(r"Cut: largest camera jump ([\d.]+) m")
MASK = re.compile(r" (?:Hold|Return|Drop|Wait): .*?\bmask=([0-9a-fA-F]+)")
STYLE = re.compile(r"^(\d+:\d+:\d+\.\d+) Camera style: (\w+)")
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


def camera_styles(lines):
    """Every "Camera style:" line of `lines`: (its stamp, the style). The mod writes one at the start and at every change."""
    return [(m[1], m[2]) for m in map(STYLE.match, lines) if m]


def style_in_force(lines):
    """The style after the last of `lines`, or None where they say none."""
    found = camera_styles(lines)
    return found[-1][1] if found else None


def style_header(lines):
    """The first line of a run's output, from the log up to where the run begins."""
    found = camera_styles(lines)
    if not found:
        return "camera style: not in the log (a build before the camera styles, or the log's start is missing)"
    return f"camera style: {found[-1][1]} (the log's line of {found[-1][0]})"


def style_changes(lines):
    """One line for every "Camera style:" line inside a stretch of a run."""
    return [f"camera style from here: {style} (the log's line of {stamp})" for stamp, style in camera_styles(lines)]


def style_summary(start, lines):
    """The summary's line: the style at the run's start (None: not known) and every other style its log has."""
    names = [(None, start)] if start else []
    for stamp, style in camera_styles(lines):
        if not names or names[-1][1] != style:
            names.append((stamp, style))
    if not names:
        return "camera style: not in the log"
    if len(names) == 1:
        return f"camera style: {names[0][1]} throughout" if names[0][0] is None else f"camera style: {names[0][1]} from {names[0][0]} (not in the log before)"
    text = ", then ".join(style if stamp is None else f"{style} from {stamp}" for stamp, style in names)
    return f"camera style: {text}: NOT ONE STYLE, judge the stretches apart"


def whose_counts(pops):
    """How many of the listed frames are whose: own, filter, body, veil (round 19); along: those of the eye's own that
    lie along the view."""
    counts = {"own": 0, "filter": 0, "body": 0, "veil": 0, "stance": 0, "along": 0}
    for p in pops:
        counts[p["whose"]] += 1
        counts["along"] += _along_named(p)
    return counts


def _along_named(p):
    """The eye's own, at or over POP_M along the view and under it across: what the arm drive's written socket did to
    the eye until round 13 (the game seats the eye by the socket, and the socket moved along the view)."""
    return p["whose"] == "own" and p.get("along") is not None and p["along"] >= POP_M and p["across"] < POP_M


def _shown(row):
    """The eye shown in a frame: the row's eye plus the helmet veil's move of it (round 19; trace.frames "veil")."""
    veil = row.get("veil")
    return [e + v for e, v in zip(row["eye"], veil)] if veil else row["eye"]


def _veil(row):
    return _length(row["veil"]) if row.get("veil") else 0.0


def measure(rows, stance=True):
    """One dict for every three consecutive frames of `rows` (trace.frames), named for the third:
      shown    how much the presented eye's step changed, as a vector: |(c-b) - (b-a)|. The presented eye is the eye
               shown: the row's eye plus veil= where the line has it (round 19: the helmet veil moved the eye handed
               to the game up to 0.32 m forward for a frame or two at a leave of the native view, after the row was
               written, and no judge saw it)
      lengths  the same by the lengths of the two steps, ||c-b| - |b-a||: the measure until round 10 (2026-11-24).
               It does not see a change of direction: 0.04 m forwards and then 0.04 m back is 0.08 m and counted 0;
               the arc of 08:12:09.5 in the Stable soak of that day, eight frames of 0.07 to 0.10 m, read as one 0.100.
      wanted   the same vector change of the wanted eye; root: of the body root
      body     what was taken off for the body's own change of step (0: nothing)
      after    how many frames after the root's that is: 0 (with the root), 1 (with the rig eye), None (nothing)
      beyond   shown less that: what is judged against POP_M
      whose    "body" (shown is POP_M or more and beyond is not), "veil" (one of the three frames was moved by the
               veil, and the same measure of the eye before it is under POP_M: the veil's alone), "own" (the wanted eye
               makes 0.8 of it, of the eye before the veil where the veil moved one of the frames, and under OWN_M is carried: the eye's own start or stop, the filter did nothing),
               "filter" (anything else), or None under POP_M
      veil     the largest move of the veil in the three frames (m; 0: none)
      bare     the eye's own measure: beyond, of the eye before the veil (the same as beyond where the veil moved none
               of the three frames). The eye's largest is taken from it, the veil's from the beyond of its own frames.
      along, across  the vector that is judged (the shown eye's change, less the body's where that is taken off) split
               along the third frame's view= and across it; None where the line has no view=. Printed, and "the eye's
               own" is named "along the view" where that is where it lies. Nothing is excused or counted otherwise
               for it (round 13).
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
    Round 49, whose "stance" (with `stance`; tools/harness/stance.py): a frame that is the eye's own, in which the
    filter did nothing, and which is the first of the game's stance ramp (prone to standing or back: the eye against
    the root at 1.6 m/s from its first frame) with the root's own change of step of the frame before, under POP_M,
    on top. That change is taken off as the body's is (body, after 1), what is left (beyond, bare) is under POP_M,
    and the frame is printed and not counted. matrix.judge leaves the class out of a reload phase.
    Each dict also has at (the frame's own time), written (the stamp of its line) and tick (the helper's clock: what
    tells two frames apart; frames from before an edge are all written with it, under one stamp)."""
    import stance as stance_ramp
    changes = {}
    for i in range(2, len(rows)):
        a, b, c = rows[i - 2:i + 1]
        if not (0 < b["tick"] - a["tick"] <= GAP_MS and 0 < c["tick"] - b["tick"] <= GAP_MS):
            continue
        one, two = _sub(_shown(b), _shown(a)), _sub(_shown(c), _shown(b))
        change = {"eye": _sub(two, one), "lengths": abs(_length(two) - _length(one)), "wanted": None, "root": None,
                  "bare": _sub(_sub(c["eye"], b["eye"]), _sub(b["eye"], a["eye"])), "veil": max(_veil(a), _veil(b), _veil(c))}
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
        beyond, body, after, judged = shown, 0.0, None, change["eye"]
        bare = _length(change["bare"])   # the same measure of the eye before the veil
        if i in lunge:
            root = changes[lunge[i]]["root"]
            left = _length(_sub(change["eye"], root))
            if left < beyond:
                beyond, body, after, judged = left, _length(root), i - lunge[i], _sub(change["eye"], root)
                bare = min(bare, _length(_sub(change["bare"], root)))
        along = across = None
        view = c.get("view")
        if view and _length(view) > 0.5:
            along = abs(sum(x * y for x, y in zip(judged, view))) / _length(view)
            across = max(0.0, beyond * beyond - along * along) ** 0.5
        wanted = _length(change["wanted"]) if change["wanted"] else None
        whose = None
        if shown >= POP_M:
            if beyond < POP_M:
                whose = "body"
            elif change["veil"] > 0 and bare < POP_M:
                whose = "veil"
            elif wanted is not None and wanted >= 0.8 * (bare if change["veil"] > 0 else shown) and b["carried"] < OWN_M and c["carried"] < OWN_M:
                whose = "own"
            else:
                whose = "filter"
        if whose == "own" and stance:   # round 49: the game's stance ramp with the root's small change of step on top
            ramp = stance_ramp.ramp(rows, i, shown, POP_M)
            if ramp:
                whose, beyond, body, after, bare = "stance", ramp[0], ramp[1], 1, min(bare, ramp[0])
                along = across = None   # they were split from the whole change, not from what is left
        found.append({"at": c["at"], "written": c["written"], "tick": c["tick"], "kind": c["kind"], "looked": c["looked"], "taken": c["taken"], "carried": c["carried"], "shown": shown,
                      "lengths": change["lengths"], "wanted": wanted, "root": _length(change["root"]) if change["root"] else None,
                      "body": body, "after": after, "beyond": beyond, "whose": whose, "along": along, "across": across,
                      "veil": change["veil"], "bare": bare})
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
    if result.get("largest_veil_m"):
        text += f" (veil {result['largest_veil_m']:.3f} not counted)"
    if result.get("stance_frames"):
        text += f" (stance ramp {result['stance_frames']}, largest {result['largest_stance_m']:.3f} shown, not counted)"
    text += f" [by lengths {result['by_lengths_m']:.3f}]"
    if result.get("largest_float_m"):
        text += f" float {result['largest_float_m']:.3f} m ({result['float_at']})"
    if "slide_m" in result:
        text += f" slide {result['slide_m']:.3f} m"
    return text


def pop_lines(result, indent="       "):
    """One line for every frame at or over POP_M: when (the frame's own time, and the stamp of its line where that
    is another: frames from before an edge are written with it), what the eye did, how much of what is judged lies
    along the view and across it (where the line says which way the view pointed), whose it is."""
    out = []
    for p in result.get("pops", []):
        body = "the body's own" + {0: "", 1: " a frame after the root, with the wanted eye"}.get(p.get("after"), "") + ", not counted"
        who = {"body": body, "own": "the eye's own, along the view" if _along_named(p) else "the eye's own (nothing carried)", "filter": "the filter's",
               "veil": "the veil's (the eye moved in front of the head at a leave of the native view)",
               "stance": "the game's stance ramp begun on the root's own change of step, not counted"}[p["whose"]]
        split = "" if p.get("along") is None else f" (along the view {p['along']:.3f}, across {p['across']:.3f})"
        if p.get("veil"):
            split += f", veil {p['veil']:.3f}"
        wanted = "-" if p["wanted"] is None else f"{p['wanted']:.3f}"
        root = "-" if p["root"] is None else f"{p['root']:.3f}"
        when = p["at"] if p.get("written", p["at"]) == p["at"] else f"{p['at']} (written {p['written']})"
        out.append(f"{indent}{when} kind {p['kind']} looked {'-' if p['looked'] is None else p['looked']} taken {p['taken']}: shown {p['shown']:.3f} m "
                   f"(by lengths {p['lengths']:.3f}), wanted {wanted}, root {root}, carried {p['carried']:.3f}, beyond the body {p['beyond']:.3f}{split}: {who}")
    return out


def _level_forward(row):
    """The shown eye's offset from the root along the row's level view (the view's horizontal part as a unit); None
    without a root or a view."""
    view, root = row.get("view"), row.get("root")
    if not view or not root:
        return None
    length = (view[0] ** 2 + view[1] ** 2) ** 0.5
    if length < 0.5:
        return None
    eye = _shown(row)
    return ((eye[0] - root[0]) * view[0] + (eye[1] - root[1]) * view[1]) / length


def reload_slide(rows):
    """(the largest slide, the row it ends at): the change of the eye's offset from the root along the level view
    between two traced frames at most SLIDE_MS apart, the root within ROOT_STILL_M of where it was. Summed over the
    frames between, so a slide made of steps that are each under the jump limit is seen whole. (0.0, None) where no
    two frames have a root and a view."""
    best, where = 0.0, None
    known = [(row, _level_forward(row)) for row in rows]
    known = [(row, f) for row, f in known if f is not None]
    for j, (b, fb) in enumerate(known):
        for i in range(j - 1, -1, -1):
            a, fa = known[i]
            if b["tick"] - a["tick"] > SLIDE_MS:
                break
            if _length(_sub(b["root"], a["root"])) >= ROOT_STILL_M:
                continue
            if abs(fb - fa) > best:
                best, where = abs(fb - fa), b
    return best, where


_ROW = re.compile(r"^(\d+):(\d+):(\d+)\.(\d+) Trace: .*? kind=(\d+) eye_world=\(([-\d. ]+)\).*? source=(\w+) root=\(([-\d. ]+)\) axes=\(([-\d. ]+)\)")
_TICK = re.compile(r" tick=([\d.]+)")
_OFF_FOOT = re.compile(r" off_foot=(\d)")
_VIEW = re.compile(r" view=\(([-\d. ]+)\)")
_VEIL = re.compile(r" veil=\(([-\d. ]+)\)")
_VEIL_HEAD = re.compile(r" veil_head=\((-?[\d.]+) (-?[\d.]+)\)")
_HEAD_SHOWN = re.compile(r" head_shown=\((-?[\d.]+) (-?[\d.]+)\)")


def behind_head(lines):
    """Round 44: every head-camera frame (kind 4, source head) of a body on foot (off_foot=0) measured against the head
    node: how far the eye shown (eye_world + veil) was in front of it along the level view. head_shown= says it for
    the frame; without it the head is where the last veiled row (veil_head=, the head from the eye before the veil) put
    it, carried in the body root's frame until the next native frame (estimated); a frame with neither is not measured.
    Rows are taken once each, in the order of their tick. Returns {frames, measured, estimated, behind, least_m,
    least_at}: behind, the frames under FRONT_M; least_m, the least in front (None where nothing was measured)."""
    rows, seen = [], set()
    for line in lines:
        m = _ROW.match(line)
        tick = _TICK.search(line)
        if not m or not tick or tick[1] in seen:
            continue
        seen.add(tick[1])
        nums = lambda text: [float(x) for x in text.split()]
        off, view, veil = _OFF_FOOT.search(line), _VIEW.search(line), _VEIL.search(line)
        vh, hs = _VEIL_HEAD.search(line), _HEAD_SHOWN.search(line)
        rows.append({"tick": float(tick[1]), "at": line[:12], "kind": int(m[5]), "head": m[7] == "head", "eye": nums(m[6]), "root": nums(m[8]),
                     "axes": nums(m[9]), "off_foot": int(off[1]) if off else 0, "view": nums(view[1]) if view else None,
                     "veil": nums(veil[1]) if veil else [0.0, 0.0, 0.0], "veil_head": (float(vh[1]), float(vh[2])) if vh else None,
                     "head_shown": (float(hs[1]), float(hs[2])) if hs else None})
    rows.sort(key=lambda r: r["tick"])
    out = {"frames": 0, "measured": 0, "estimated": 0, "behind": 0, "least_m": None, "least_at": None}
    local = None   # the head in the root's frame, from the last veiled row
    for r in rows:
        if r["kind"] != 4 or not r["head"]:
            local = None
            continue
        if r["off_foot"] or not r["view"] or len(r["axes"]) != 9:
            continue
        n = (r["view"][0] ** 2 + r["view"][1] ** 2) ** 0.5
        if n < 1e-3:
            continue
        h = (r["view"][0] / n, r["view"][1] / n)
        shown = [e + v for e, v in zip(r["eye"], r["veil"])]
        out["frames"] += 1
        a = r["axes"]
        if r["veil_head"] is not None:
            ahead, up = r["veil_head"]
            head = [r["eye"][0] + h[0] * ahead, r["eye"][1] + h[1] * ahead, r["eye"][2] + up]
            d = _sub(head, r["root"])
            local = [sum(d[c] * a[k * 3 + c] for c in range(3)) for k in range(3)]
        if r["head_shown"] is not None:
            front = -r["head_shown"][0]
        elif local is not None:
            head = [r["root"][c] + sum(local[k] * a[k * 3 + c] for k in range(3)) for c in range(3)]
            front = (shown[0] - head[0]) * h[0] + (shown[1] - head[1]) * h[1]
            out["estimated"] += r["veil_head"] is None   # the veiled row's own head is the row's
        else:
            continue
        out["measured"] += 1
        if front < FRONT_M:
            out["behind"] += 1
        if out["least_m"] is None or front < out["least_m"]:
            out["least_m"], out["least_at"] = front, r["at"]
    return out


def judge(lines, toggles=False, reload=False, throw=False):
    held = other = missed = 0
    masks = set()
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
        m = MASK.search(line)
        if m:
            masks.add(int(m[1], 16))
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
    found = measure(rows, stance=not reload)   # round 49: a reload's eye is never the stance ramp's (tools/harness/stance.py)
    stances = [f["shown"] for f in found if f["whose"] == "stance"]
    veil_m = max([0.0] + [f["beyond"] for f in found if f["whose"] == "veil"])
    if traced:
        # Round 19: the eye's own largest, without the veil's move; the veil's frames have their own largest and do
        # not fail a phase (a soak has some 70 leaves, each with a frame or two the veil moved up to 0.32 m).
        jump = max([0.0] + [f["bare"] for f in found])
    floated, float_row = trace_reader.largest_float(rows) if traced else (0.0, None)   # round 24
    ok = held > 0 and missed == 0 and (jump < (POP_M if traced else LIMIT_M) or toggles) and not refused and (floated <= trace_reader.FLOAT_M or toggles)   # the toggle itself is a cut by design
    extra, first = {}, []
    if reload:   # round 39: on the traced eye only, and its slide along the view across the reload's edges
        slide, slide_row = reload_slide(rows)
        extra = {"slide_m": slide, "slide_at": slide_row["at"] if slide_row else None}
        if not traced:
            ok = False
            first.append("not traced: a reload phase is judged on the traced eye only")
        elif slide > SLIDE_M:
            ok = False
            first.append(f"the eye slid {slide:.3f} m along the view with the root still (to {slide_row['at']}): a reload's step, summed over its frames")
    if throw:   # round 44: the eye against the head node in the head camera
        head = behind_head(lines)
        extra.update({"head_measured": head["measured"], "head_estimated": head["estimated"], "behind_frames": head["behind"],
                      "front_least_m": head["least_m"], "front_least_at": head["least_at"]})
        if head["behind"]:
            ok = False
            first.append(f"the eye was less than {FRONT_M:.2f} m in front of the head in {head['behind']} of {head['measured']} head-camera frames on foot"
                         f" (least {head['least_m']:+.3f} m, {head['least_at']}): the cape and the back in view")
        elif head["frames"] and not head["measured"]:
            first.append("the head was not measured in the head camera (no head_shown or veil_head on the rows)")
    return {**extra, "pass": ok, "frames": held, "other_camera_frames": other, "missed": missed, "kinds": sorted(kinds),
            "largest_jump_m": jump, "largest_veil_m": veil_m, "stance_frames": len(stances), "largest_stance_m": max([0.0] + stances),
            "from_trace": traced, "judged": len(found),
            "by_lengths_m": max([0.0] + [f["lengths"] for f in found]), "body_m": max([0.0] + [f["body"] for f in found]),
            "pops": [f for f in found if f["whose"]], "largest_kick_deg": kick, "notes": first + sorted(set(notes))[:6],
            "largest_float_m": floated, "float_at": float_row["at"] if float_row else None, "masks": sorted(masks)}


def play(device, name, log, out=None):
    """Plays phase `name`: its script, or for a phase of AIMED full aim read from the log `log` (aim.full_aim). Returns
    the aim's result, or None for a plain phase. With `out`, the zoom captures each range there. A phase of DIVES is
    played by dive.dive_swap() and returns its result."""
    if name in DIVES:
        return dive.dive_swap(device, aim.LogTail(log))
    if name not in AIMED:
        device.run(PHASES[name][0])
        return None
    during, hold = AIMED[name]
    if during == "zoom":
        return aim.scope_zoom(device, aim.LogTail(log), capture=_capture(out, name))
    return aim.full_aim(device, aim.LogTail(log), hold, aim.during_script(during) if during else None)


def _capture(out, name):
    """A capture of the game for each step of a phase, OUT/NAME-N.png; None without `out`."""
    if out is None:
        return None
    return lambda step: drive.shot(Path(out) / f"{name}-{step}.png")


def judge_aim(name, result, lines, judged):
    """Adds the aim's verdict to a phase's camera verdict `judged` (in place)."""
    if result is None:
        return judged
    if name in DIVES:
        v = dive.verdict(result)
        judged["dive"] = result
        judged["pass"] = judged["pass"] and v["pass"]
        judged["notes"] = judged["notes"] + v["notes"]
        return judged
    hold = AIMED[name][1]
    v = aim.verdict(result, lines, hold, zoom=name == "aim-scope-zoom")
    judged["aim"] = {**result, **aim.counts(lines)}
    judged["pass"] = judged["pass"] and v["pass"]
    judged["notes"] = judged["notes"] + v["notes"]
    return judged


def aim_text(result):
    """One line for an aim phase: whether full aim was reached, how fast, and the field of view of each range step
    (* where it changed)."""
    if not result:
        return ""
    if "attempts" in result:   # a dive + swap (DIVES)
        return dive.text(result)
    text = (f"full aim after {result['to_full_s']:.2f} s ({result['switches']} aim-mode press{'es' if result['switches'] > 1 else ''})"
            if result["reached"] else f"NO full aim ({result['switches']} aim-mode presses)")
    if "zoom_steps" in result:
        steps = result["zoom_steps"]
        text += (f", range steps {len(steps)}: the zoom changed {result['zoom_where']}" if result["zoom_where"] else
                 f", range steps {len(steps)}: the zoom never changed")
        text += " (fov " + " ".join(f"{z['fov_after']}" + ("*" if z["zoomed"] else "") for z in steps) + ")"
    if "aim_lines" in result:
        text += f"; Aim: lines {result['aim_lines']}, zoom levels {result['zoom_levels']}"
    return text


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    out = Path(argv[1]); out.mkdir(parents=True, exist_ok=True)
    names = argv[2:] or list(PHASES)
    import phases_levers                                   # round 51: phases that set their scene with the levers
    PHASES.update({n: phases_levers.PHASES[n] for n in names if n in phases_levers.PHASES})
    if not drive.focus():
        print("the game window could not be focused"); return 1
    log = log_path(); results = {}
    with drive.Input() as device:
        if "frames held" not in log.read_text(errors="replace")[-4000:]:
            device.run(f"key {drive.TOGGLE}; wait 2.5")   # the view on
        begun = log.stat().st_size
        before = log.read_text(errors="replace").splitlines()
        style = style_in_force(before)
        print(style_header(before), flush=True)
        for name in names:
            script, settle = PHASES[name]
            phases_levers.prepare(name)                    # nothing for a phase of this file's own
            start = log.stat().st_size
            aimed = play(device, name, log, out)
            time.sleep(min(settle, 1.0))
            try:
                drive.shot(out / f"{name}.png")
            except drive.DriveError as error:
                print(f"  (no capture: {error})")
            time.sleep(max(0.0, settle - 1.0) + 1.2)   # the Lock line is written once a second
            with open(log, errors="replace") as handle:
                handle.seek(start); lines = handle.read().splitlines()
            results[name] = judge_aim(name, aimed, lines, judge(lines, name.startswith('toggle'), reload=name in RELOADS, throw=name in THROWS))
            r = results[name]
            for text in style_changes(lines):
                print(text)
            style = style_in_force(lines) or style
            r["camera_style"] = style
            print(f"{'pass' if r['pass'] else 'FAIL':4} {name:16} frames {r['frames']:4} other-camera {r['other_camera_frames']:4} missed {r['missed']:3} "
                  f"{describe(r)} kick {r['largest_kick_deg']:.1f} deg kinds {r['kinds']}")
            if aimed is not None:
                print(f"       {aim_text(r['dive'] if name in DIVES else r['aim'])}")
            for note in r["notes"] + pop_lines(r, ""):
                print(f"       {note}")
    (out / "matrix.json").write_text(json.dumps(results, indent=1))
    failed = [n for n, r in results.items() if not r["pass"]]
    with open(log, errors="replace") as handle:
        handle.seek(begun); run = handle.read().splitlines()
    print(style_summary(style_in_force(before), run))
    print(f"{len(results) - len(failed)} of {len(results)} passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
