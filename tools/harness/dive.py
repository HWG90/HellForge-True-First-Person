#!/usr/bin/env python3
"""A dive + swap that leaves the game's view, to measure the head in a dive (round 30, the build order's fix 5).

The helmet veil (round 14 on) opens on the first head-camera frame after the game's own view (kind 6 to kind 4), and
from round 26 every veiled Trace row says where the head node was from the eye before the veil: veil_head=(ahead up),
metres. Round 26 estimated the head of a diving body 0.55 m below the eye; no soak has measured it, since no dive left
the game's view through the veil (feel.53's soak 36: lowest up -0.223 m, a crouch; its one "w + dive + swap" played
inside a 15 s head-camera wait that a stratagem and a grenade had opened, so no veil). A dive alone holds the game's
view ("State: holding the native view through an action"); a swap late in the dive leaves it (feel.52's soak 34,
02:22:23.454: "hold ended: the gun has left the hands", then "left the native view", 1.3 s into the dive), and an
early swap can be held through as the dive is (feel.52's overlap with the swap 0.3 s in: kinds [6] only).

dive_swap() therefore brings the primary up, stands the body up, waits for the game's view (the log's View lines,
kind=6), dives (w held, alt) and swaps (2) SWAP_AFTER_S into the dive, for each delay in turn until the log says the
view was left in the dive; the primary comes back after every try. Its result lists every try and the veil_head of
every veiled frame.

Round 32: feel.54's matrix (tfp-soak-37.modlog 09:56:25-09:56:47) measured nothing. Two causes, both the script's:
  - the body was still prone from the try before. Tries 2 and 4 never dived: their alt stood the body up from prone
    (Motion: action=idle at 09:56:32.503 and 09:56:47.259, no dive line), so stand_up() now brings the body to its
    feet first (z from prone, c from a crouch: the matrix's prone and crouch phases) and reads the log until a
    standing action (idle) comes;
  - 1.0 s and 1.3 s were too late: the dives ran 0.93 s and 1.27 s (dive at 09:56:25.125, prone at 26.060; dive at
    39.514, prone at 40.783) and the game held the swap until it had landed, the gun leaving the hands about 0.53 s
    after action=prone (26.594, 41.317), so both leaves came after the dive. The swap now goes down 0.1, 0.15, 0.2,
    then 0.3 s after the dive key went down.

  dive.py                 one dive + swap in the game, printed
  dive.py --log LOG       no game: the veil_head rows of a log and the lowest head, then every leave in a dive
                          (round 32) with its own lowest head
"""
import re
import sys
import time

import aim

STAMP = aim.STAMP
LEFT = re.compile(STAMP + r"State: left the native view\b")
ENTERED = re.compile(STAMP + r"State: entered the native view\b")
VIEW_KIND = re.compile(STAMP + r"View: .*?\bkind=(\d+)")
MOTION = re.compile(STAMP + r"Motion: action=([\w-]+)")
DIVE_ACTION = "dive"   # the mod's name for a dive (src/action_select.c), forward or back; never "dive-back"
GAP_MS = 80.0          # as matrix.GAP_MS
VEIL_HEAD = re.compile(STAMP + r"Trace: t=\S+ kind=(\d+)\b.*? veil_head=\((-?[\d.]+) (-?[\d.]+)\)")

# Round 32: the swap's key goes down this long after the dive's key went down, tried in turn until a leave comes in the
# dive. feel.54's dives lasted 0.93 and 1.27 s and a swap at 1.0 / 1.3 s was held until the landing; the short ones
# come first. 0.4 s last: the soak's overlap "down w; wait 0.2; key alt; wait 0.3; key 2" (the swap 0.41 s after the
# dive key) left the view in the dive in both feel.54 soaks (tfp-soak-37 10:04:58.182, -38 10:24:38.160, 0.77 s after
# the dive's line) with the deepest heads yet, -0.626 and -0.690 m.
SWAP_AFTER_S = (0.1, 0.15, 0.2, 0.3, 0.4)
RUN_IN_S = 0.3                        # w held this long before the dive key: a dive forward, as the matrix's "dive"
DIVE_TAP_S = 0.06                     # the dive key down to up (two frames at the harness's 30 a second), so a swap
                                      # 0.1 s in can go down after it is let go
STEP_S = 0.03                         # drive.events: what a "down" or an "up" takes of its own
LAND_S = 2.5                          # after the swap: the dive lands and the swap ends (the matrix's dive settles 2.5 s)
NATIVE_WAIT_S = 4.0                   # how long the log is read for the game's view before a try (a swap takes ~2 s)
PRIMARY = "key 1; wait 0.3"

# Round 32: the body is stood up before every try and after the last. Motion: actions with the body off its feet;
# any other (idle, aim, ads, reload, sprint) is standing. The key that stands it up from each, from the matrix's own
# phases in feel.54's log: prone "key z" again (09:56:09.901 idle), crouch "key c" again (09:56:04.459 idle). A dive
# lands prone; the dive key from prone also stood it up (09:56:19.015, 09:56:32.503), the fallback.
LOWERED = {"prone", "crouch", DIVE_ACTION}
STAND_KEYS = {"prone": "key z", "crouch": "key c"}
STAND_FALLBACK = "key alt"            # from prone, where z did not stand it up (only ever pressed while the log says prone)
STAND_PRESSES = 3                     # presses at most (prone -> crouch -> idle where z gives a crouch)
STAND_WAIT_S = 2.0                    # how long the log is read for a new Motion: line after a press
LANDING_WAIT_S = 2.0                  # where the last action is the dive: how long the log is read for its landing
LOOKBACK_BYTES = 1 << 20              # the log before the harness began reading it, searched for the last Motion: line


# Round 55C: the game's dive launch, for matrix.measure (whose "launch").
LAUNCH_SOURCE = re.compile(r" Trace: .*? source=(\w+)\b.*? tick=([\d.]+)")
LAUNCH_AFTER = (2, 3)      # the frame judged is this many frames after the dive's own (its Motion: line's frame)
LAUNCH_STAMP_S = 0.0025    # the dive's frame: a row written within this of the Motion: line ...
LAUNCH_EDGE_MS = 5.0       # ... at t= this near 0 (the trace counts from the dive's line: -0.8 to -1.1 ms logged)


def launch_marks(lines):
    """What launch() reads of the log `lines`: {dives: [seconds of the day of every "Motion: action=dive" line],
    sources: {tick: the Trace row's source=}}; None where the log has no dive line (nothing can be a launch)."""
    dives = [stamp_s(m[1]) for m in map(MOTION.match, lines) if m and m[2] == DIVE_ACTION]
    if not dives:
        return None
    sources = {}
    for line in lines:
        m = LAUNCH_SOURCE.search(line)
        if m:
            sources[float(m[2])] = m[1]
    return {"dives": dives, "sources": sources}


def launch(rows, i, shown, root, marks, limit):
    """Round 55C: whether frame i of `rows` (trace.frames, with source= set from `marks`), whose eye changed its step
    by `shown` and whose root's own change of step is `root` (m, or None), is the game's dive launch: an untouched
    native frame of the game's own eye, where the mod can have had no part. All of this must hold:
      - the frame is LAUNCH_AFTER frames after the dive's own: the row written with the dive's "Motion: action=dive"
        line (within LAUNCH_STAMP_S, at t= within LAUNCH_EDGE_MS of 0). The root leaves its run in the frame after the
        dive's and the rig eye is the root a frame late, so the eye's step changes one or two frames after that;
      - in the dive's frame and every frame from it to frame i: kind 6, jumps not looked for, nothing taken up but
        "root", under stance.NOTHING_M carried, the wanted eye the eye shown, no veil (stance._untouched), source native,
        and no two of them further apart than GAP_MS;
      - the root's own change of step in frame i is under `limit` (POP_M): a lunge of the body is measure()'s own rule;
      - `shown` is under stance.CAP_M.
    The feel.63 cases: soak 67-A #54 (0.070 m) and #85 (0.071), 68-C #5 (0.069), #22 (0.090) and #218 (0.072), the
    matrix's "dive" (0.073); every one two frames after the dive's line, the root's change 0.026 to 0.052 m. matrix.py
    prints such a frame and does not count it, as it does the stance ramp's (tools/harness/stance.py)."""
    import stance
    if not marks or root is None or root >= limit or shown >= stance.CAP_M:
        return False
    for back in LAUNCH_AFTER:
        e = i - back
        if e < 0:
            continue
        edge = rows[e]
        if abs(edge.get("t", 1e9)) > LAUNCH_EDGE_MS or not any(abs(stamp_s(edge["written"]) - d) <= LAUNCH_STAMP_S for d in marks["dives"]):
            continue
        span = rows[e:i + 1]
        if any(not 0 < b["tick"] - a["tick"] <= GAP_MS for a, b in zip(span, span[1:])):
            return False
        return all(stance._untouched(row) and row.get("source") == "native" for row in span)
    return False


def script(after):
    """One try: w held, the dive, the swap's key down `after` seconds after the dive's key went down, w let go once it
    has landed."""
    held = round(DIVE_TAP_S - STEP_S, 3)        # drive.events: a down or an up takes 0.03 s of its own
    gap = round(after - DIVE_TAP_S - STEP_S, 3)
    if gap < 0:
        raise ValueError(f"a swap {after} s into the dive is before the dive key is let go ({DIVE_TAP_S + STEP_S:.2f} s)")
    return f"down w; wait {RUN_IN_S}; down alt; wait {held}; up alt; wait {gap}; key 2; wait {LAND_S}; up w"


def stamp_s(stamp):
    """Seconds of the day of a log stamp HH:MM:SS.mmm."""
    h, m, s = stamp.split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def last_action(lines):
    """The action of the last "Motion: action=" line of `lines`, or None."""
    for line in reversed(lines):
        m = MOTION.match(line)
        if m:
            return m[2]
    return None


def body_action(tail):
    """The body's last Motion: action: from the lines read so far, else from the log before the harness began reading
    it (the phase before this one left it there); None where the log never said."""
    action = last_action(tail.lines)
    if action is not None:
        return action
    try:
        with open(tail.path, "rb") as handle:
            start = max(0, tail.offset - LOOKBACK_BYTES)
            handle.seek(start)
            before = handle.read(tail.offset - start).decode(errors="replace").splitlines()
    except OSError:
        return None
    return last_action(before[1:] if start else before)   # the first line of a cut read may be half a line


def stand_up(device, tail, sleep=time.sleep, clock=time.monotonic):
    """Brings the body to its feet where the log says it is not: waits for a dive to land, then presses the key that
    stands it up from where it is, and reads the log for the next Motion: line, up to STAND_PRESSES times. Returns
    {before, presses, after, standing}; standing is True where the last action is not in LOWERED (or the log never
    named one: nothing is pressed then)."""
    before = action = body_action(tail)
    if action == DIVE_ACTION:
        start = len(tail.lines)
        aim.wait_for(tail, lambda lines: last_action(lines[start:]) is not None, LANDING_WAIT_S, sleep, clock)
        action = last_action(tail.lines[start:]) or "prone"   # no landing line: a dive that has landed is prone
    presses = []
    while action in LOWERED and len(presses) < STAND_PRESSES:
        key = STAND_KEYS.get(action, STAND_KEYS["prone"])
        if action == "prone" and presses and presses[-1] == STAND_KEYS["prone"]:
            key = STAND_FALLBACK   # z did not stand it up: the dive key did in feel.54
        start = len(tail.lines)
        device.run(key)
        presses.append(key)
        aim.wait_for(tail, lambda lines: last_action(lines[start:]) is not None, STAND_WAIT_S, sleep, clock)
        action = last_action(tail.lines[start:]) or action
    return {"before": before, "presses": presses, "after": action, "standing": action not in LOWERED}


def in_native_view(lines):
    """True where the last word of `lines` on the view is the game's: a View line with kind=6 or an "entered the
    native view" after the last "left"; None where they say nothing."""
    state = None
    for line in lines:
        m = VIEW_KIND.match(line)
        if m:
            state = m[2] == "6"
        elif ENTERED.match(line):
            state = True
        elif LEFT.match(line):
            state = False
    return state


def dive_leave(lines, action=DIVE_ACTION):
    """The index in `lines` of the first "left the native view" whose last "Motion: action=" before it is `action` (a
    dive): the view left in the dive, not after it. None where no leave is.  A leave after another action (the dive
    landed and the swap came after it) is not one."""
    last = None
    for i, line in enumerate(lines):
        m = MOTION.match(line)
        if m:
            last = m[2]
        elif LEFT.match(line) and last == action:
            return i
    return None


def leave_rows(lines, at):
    """The veiled rows of the leave at index `at`: from it to the next "entered the native view" (the veil of that
    leave only)."""
    end = next((i for i in range(at + 1, len(lines)) if ENTERED.match(lines[i])), len(lines))
    return veil_heads(lines[at:end])


def dive_leaves(lines):
    """Every leave of the game's view in a dive in `lines` (dive_leave's rule: the last Motion: action before it is the
    dive): {at, after_dive_s (from the dive's line), veiled, lowest_up, ahead (at the lowest)}. For a whole log, the
    soak's own dives and overlaps too."""
    out, last, dive_at = [], None, None
    for i, line in enumerate(lines):
        m = MOTION.match(line)
        if m:
            last = m[2]
            if last == DIVE_ACTION:
                dive_at = stamp_s(m[1])
        elif last == DIVE_ACTION and (m := LEFT.match(line)):
            rows = leave_rows(lines, i)
            low = min(rows, key=lambda r: r["up"]) if rows else None
            out.append({"at": m[1], "after_dive_s": round(stamp_s(m[1]) - dive_at, 3), "veiled": len(rows),
                        "lowest_up": low["up"] if low else None, "ahead": low["ahead"] if low else None})
    return out


def veil_heads(lines):
    """Every veiled Trace row of `lines`: {at, kind, ahead, up} (veil_head: the head from the eye before the veil)."""
    out = []
    for line in lines:
        m = VEIL_HEAD.match(line)
        if m:
            out.append({"at": m[1], "kind": int(m[2]), "ahead": float(m[3]), "up": float(m[4])})
    return out


def dive_timing(lines):
    """When the try's dive and its leave came, from the stamps of `lines` (a try's stretch of the log): {dive_s (the
    dive's length, its Motion line to the next one: the landing), leave_after_s (the first leave, after the dive's line;
    > dive_s: after the landing)}, None where the log has no such line."""
    dive_at = land_at = leave_at = None
    for line in lines:
        m = MOTION.match(line)
        if m:
            if m[2] == DIVE_ACTION and dive_at is None:
                dive_at = stamp_s(m[1])
            elif dive_at is not None and land_at is None and m[2] != DIVE_ACTION:
                land_at = stamp_s(m[1])
        elif dive_at is not None and leave_at is None and (m := LEFT.match(line)):
            leave_at = stamp_s(m[1])
    return {"dive_s": round(land_at - dive_at, 3) if land_at is not None else None,
            "leave_after_s": round(leave_at - dive_at, 3) if leave_at is not None else None}


def dive_swap(device, tail, delays=SWAP_AFTER_S, sleep=time.sleep, clock=time.monotonic):
    """Dives and swaps from the game's view, standing, until the log says the view was left in the dive. Returns:
      left        a try left the game's view in the dive: a "State: left the native view" in its stretch of the log
                  whose last "Motion: action=" before it is a dive (dive_leave)
      swap_after  the delay of that try (None: none left)
      attempts    every try: {swap_after_s, stood (stand_up before it), from_native, left, left_any (a leave not in
                  the dive too), dived, dive_s, leave_after_s (dive_timing), veil_heads}
      veil_heads  the veiled rows of that leave (to the next "entered the native view"); lowest_up their lowest head
                  (m from the eye, below < 0)
      stood       stand_up after the last try (the next phase starts on its feet)
    The primary is brought back after every try, also when one fails."""
    result = {"left": False, "swap_after": None, "attempts": [], "veil_heads": [], "lowest_up": None, "notes": [],
              "stood": None}
    for after in delays:
        device.run(PRIMARY)
        stood = stand_up(device, tail, sleep, clock)
        native = aim.wait_for(tail, lambda lines: in_native_view(lines) is True, NATIVE_WAIT_S, sleep, clock)
        start = len(tail.lines)
        try:
            device.run(script(after))
            aim.wait_for(tail, lambda lines: aim.first(LEFT, lines[start:]) is not None, 1.0, sleep, clock)
            sleep(0.6)   # the Trace rows of the edge are written for 0.5 s after it
            tail.read()
        finally:
            device.run(PRIMARY + "; wait 1.7")   # the primary back for what comes next
        part = tail.lines[start:]
        at = dive_leave(part)
        attempt = {"swap_after_s": after, "stood": stood, "from_native": native, "left": at is not None,
                   "left_any": aim.first(LEFT, part) is not None,
                   "dived": any((m := MOTION.match(line)) and m[2] == DIVE_ACTION for line in part),
                   **dive_timing(part),
                   "veil_heads": leave_rows(part, at) if at is not None else []}
        result["attempts"].append(attempt)
        if attempt["left"]:
            result.update(left=True, swap_after=after, veil_heads=attempt["veil_heads"])
            if attempt["veil_heads"]:
                result["lowest_up"] = min(r["up"] for r in attempt["veil_heads"])
            break
    result["stood"] = stand_up(device, tail, sleep, clock)
    if not result["left"]:
        result["notes"].append(f"no dive + swap left the game's view (swaps {', '.join(f'{d:.2f}' for d in delays)} s into "
                               "the dive): the head in a dive is still unmeasured")
        late = [a["swap_after_s"] for a in result["attempts"] if a["left_any"]]
        if late:
            result["notes"].append(f"the view was left after the dive, not in it (swaps {', '.join(f'{d:.2f}' for d in late)} s): "
                                   "not counted")
    elif not result["veil_heads"]:
        result["notes"].append("the view was left in the dive and no veiled Trace row was written: the veil is off, or "
                               "a build before round 26 (no veil_head)")
    flat = [a["swap_after_s"] for a in result["attempts"] if not a["dived"]]
    if flat:
        result["notes"].append(f"no dive in the try (swaps {', '.join(f'{d:.2f}' for d in flat)} s): the body was "
                               "not on its feet or the dive key was not heard")
    down = [a["stood"]["after"] for a in result["attempts"] if not a["stood"]["standing"]]
    if down:
        result["notes"].append(f"the body could not be stood up before a try (the log said {', '.join(down)})")
    if not any(a["from_native"] for a in result["attempts"]):
        result["notes"].append("the game's view was never seen before a try (View: kind=6): the primary was not in hand")
    return result


def verdict(result):
    """The dive's own pass: the view was left in a dive and the head measured through the veil."""
    return {"pass": bool(result.get("left") and result.get("veil_heads")), "notes": list(result.get("notes", []))}


def try_text(attempt):
    """A try in a few words: the swap's delay, the dive's length and when the view was left, from the dive's line."""
    if "swap_after_s" not in attempt:
        return ""
    out = f"{attempt['swap_after_s']:.2f} s"
    if not attempt.get("dived"):
        return out + " no dive"
    if attempt.get("dive_s") is not None:
        out += f" dive {attempt['dive_s']:.2f} s"
    if attempt.get("leave_after_s") is not None:
        out += f" left at {attempt['leave_after_s']:.2f} s"
    return out


def text(result):
    """One line for the dive + swap phase."""
    tries = len(result["attempts"])
    if not result["left"]:
        told = "; ".join(filter(None, (try_text(a) for a in result["attempts"])))
        return f"dive + swap: the game's view never left ({tries} tries{': ' + told if told else ''})"
    out = f"dive + swap: left the game's view with the swap {result['swap_after']:.2f} s into the dive (try {tries})"
    if result["veil_heads"]:
        out += f"; {len(result['veil_heads'])} veiled frames, the head lowest {result['lowest_up']:+.3f} m from the eye"
    return out


def main(argv):
    if len(argv) > 2 and argv[1] == "--log":
        with open(argv[2], errors="replace") as handle:
            lines = handle.read().splitlines()
        rows = veil_heads(lines)
        print({"veiled": len(rows), "lowest_up": min((r["up"] for r in rows), default=None),
               "lowest": min(rows, key=lambda r: r["up"]) if rows else None})
        for leave in dive_leaves(lines):   # round 32: the leaves in a dive, with the head of each
            print(f"  left in a dive at {leave['at']}, {leave['after_dive_s']:.3f} s after the dive's line: "
                  f"{leave['veiled']} veiled, the head lowest "
                  + (f"{leave['lowest_up']:+.3f} m ({leave['ahead']:+.3f} ahead)" if leave["veiled"] else "not measured"))
        return 0
    if len(argv) > 1:
        print(__doc__)
        return 2
    import drive
    import matrix
    if not drive.focus():
        print("the game window could not be focused")
        return 1
    tail = aim.LogTail(matrix.log_path())
    with drive.Input() as device:
        result = dive_swap(device, tail)
    print(text(result))
    print({**result, **verdict(result)})
    return 0 if verdict(result)["pass"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
