#!/usr/bin/env python3
"""Full aim for the harness (round 26).

Why the harness never reached full aim before: the right button is the game's aim, and on its own it gives the
over-the-shoulder aim of the third-person camera (the log's Return/Hold lines say shoulder=1, Motion says action=aim)
and never full aim (controls[4], the log's "State: aim in", View "ads=1", the "Aim:" lines). Full aim, the sights on
the eye, is the game's other aim mode: on PC it is switched with the middle mouse button while the aim is held (the
game's "switch aim mode" binding; the mod's aim watch calls it the aim-mode button, src/wire_action.h). The user
presses it; no harness script ever did (the 26 harness soak logs 8 to 34: 1,332 "Motion: action=aim", 1 "State: aim
in", 0 "Aim:" lines; the user's own log 31: 13 "State: aim in").

full_aim() therefore holds the right button, presses the aim-mode button, and reads the mod's log until it says
"State: aim in"; where it does not within WAIT_S it presses the aim-mode button once more (the game can remember the
mode per weapon: a press then takes the aim out of first person), then holds full aim for HOLD_S and lets go. The
right button stays down between the helper's runs (drive.py `keep`); Input.close() lets go of it whatever happens.

  aim.py full [HOLD_S]          one full aim with the weapon in hand: prints what the log said
  aim.py zoom                   each scope range in full aim (hold R, the mouse wheel up), each held ZOOM_HOLD_S
  aim.py --log LOG              no game: what a log says of full aim (the counts of round 26's "before")

Round 30: the scope's range. Round 26 held R and pressed the arrow keys; the game never changed its zoom (feel.53's
soak: one field of view, 30.0, in every "Aim:" line of the BR-14). On PC the game's input is hold R (the weapon
functions menu, "open weapon wheel") and the mouse wheel up for the scope range (Steam, Destructoid; the wheel down
is the flashlight, the right button the fire mode). A Steam reply says it may need doing outside the sights (in third
person) or with the aim on hold, not tap. scope_zoom() therefore steps the range in full aim first (zoom_cycle: R
held, ZOOM_STEP, each range held ZOOM_HOLD_S in full aim), and where no step changed the field of view, steps it
before aiming (R and the wheel with no aim held, then full aim ZOOM_HOLD_S, for each range). A range is judged by the
field of view of the "Aim:" lines (a change of ZOOM_FOV_DEG or more); the near clip ("sight") and the game's depth of
the same lines are kept with each step, since the game reseats both at a new zoom level, and say where the range
moved and the field of view did not. Whether a weapon's range changes its zoom at all is the game's: a step that
changes nothing is reported, not taken as a fault of the mod.
"""
import re
import sys
import time
from pathlib import Path

STAMP = r"^(\d+:\d+:\d+\.\d+) "
AIM_IN = re.compile(STAMP + r"State: aim in\b")
AIM_OUT = re.compile(STAMP + r"State: aim out\b")
AIM_LINE = re.compile(STAMP + r"Aim: (in|changed|second|out)\b(.*)")
FOV = re.compile(r"; fov ([\d.]+) shown ([\d.]+)")
SIGHT = re.compile(r"; sight ([\d.]+) m\b")
DEPTH = re.compile(r"; depth ([\d.]+) m\b")
HELD = re.compile(r"^ after ([\d.]+) s")
LEVEL = re.compile(r"([\d.]+): sight ")
ADS = re.compile(STAMP + r"View: .*\bads=1\b")
SHOULDER = re.compile(r"\bshoulder=1\b")
MOTION_AIM = re.compile(r"Motion: action=aim\b")
NOT_KEPT = re.compile(STAMP + r"Aim: release not kept")

AIM_BUTTON = "right"     # the game's aim (hold)
MODE_KEY = "middle"      # the game's "switch aim mode": third person <-> the sights (full aim)
RAISE_S = 0.35           # the aim held this long before the mode switch: the switch is heard only while aiming
WAIT_S = 1.5             # how long the log is read for "State: aim in" after a switch (the line is the tick's)
SWITCHES = 2             # a second press for a mode the game remembered as first person (the first took it out)
HOLD_S = 2.0             # full aim held this long once the log said so: the "Aim: in" line (0.3 s) and a "second"
MENU_S = 0.8             # R held this long before the wheel: the weapon functions menu (a tap is a reload; the
                         # matrix's ammo-check holds R 0.8 s)
ZOOM_STEP = f"down r; wait {MENU_S}; wheel 1; wait 0.3; up r"   # one scope range on (round 30: the game's PC input)
ZOOM_STEPS = 4           # ranges stepped at most: a scope's ranges come round again (50/100/200 m, then 50)
ZOOM_HOLD_S = 2.0        # each range held this long in full aim (the testers' "shake" is in a range held)
ZOOM_WAIT_S = 1.2        # an "Aim: changed" line comes 0.3 s after a change has settled
ZOOM_FOV_DEG = 0.5       # a change of the field of view this large is another zoom level
SEAT_M = 0.005           # a change of the sight or the depth this large is another seat
POLL_S = 0.1


class LogTail:
    """The mod's log from a point on: whole lines only, kept in `lines`."""

    def __init__(self, path, start=None):
        self.path = Path(path)
        self.offset = self.path.stat().st_size if start is None else start
        self.lines = []

    def read(self):
        with open(self.path, "rb") as handle:
            handle.seek(self.offset)
            data = handle.read()
        end = data.rfind(b"\n") + 1   # a line being written is read the next time
        self.offset += end
        new = data[:end].decode(errors="replace").splitlines()
        self.lines += new
        return new


def first(pattern, lines):
    for line in lines:
        m = pattern.match(line)
        if m:
            return m
    return None


def in_full_aim(lines):
    """True while the last of "State: aim in" / "State: aim out" in `lines` is an in."""
    state = False
    for line in lines:
        if AIM_IN.match(line):
            state = True
        elif AIM_OUT.match(line):
            state = False
    return state


def aim_lines(lines):
    """Every "Aim:" line: {at, kind, fov, shown, held (out only), levels (out only)}."""
    out = []
    for line in lines:
        m = AIM_LINE.match(line)
        if not m:
            continue
        row = {"at": m[1], "kind": m[2], "fov": None, "shown": None, "held": None, "levels": []}
        f = FOV.search(m[3])
        if f:
            row["fov"], row["shown"] = float(f[1]), float(f[2])
        sight, depth = SIGHT.search(m[3]), DEPTH.search(m[3])
        row["sight"], row["depth"] = (float(sight[1]) if sight else None), (float(depth[1]) if depth else None)
        if m[2] == "out":
            h = HELD.search(m[3])
            row["held"] = float(h[1]) if h else None
            text = m[3].split(" | ", 1)[0]
            if "zoom levels" in text:
                row["levels"] = [float(x) for x in LEVEL.findall(text.split("):", 1)[-1])]
            row["tap"] = "a tap" in text
        out.append(row)
    return out


def zoom_levels(lines):
    """The distinct fields of view (degrees, to 0.1) of full aim in `lines`: the "in", "changed" and "second" lines'
    and every level the "out" lines list."""
    seen = set()
    for row in aim_lines(lines):
        if row["fov"] is not None:
            seen.add(round(row["fov"], 1))
        seen.update(round(v, 1) for v in row["levels"])
    return sorted(seen)


def last_fov(lines):
    rows = [r for r in aim_lines(lines) if r["fov"] is not None]
    return rows[-1]["fov"] if rows else None


def last_seat(lines):
    """(sight, depth) of the last "Aim:" line in `lines` that has them: what the game seated the scope at."""
    rows = [r for r in aim_lines(lines) if r["sight"] is not None or r["depth"] is not None]
    return (rows[-1]["sight"], rows[-1]["depth"]) if rows else (None, None)


def counts(lines):
    """What a stretch of log says of aiming: the numbers of round 26's before and after."""
    rows = aim_lines(lines)
    return {"aim_in": sum(1 for l in lines if AIM_IN.match(l)), "aim_out": sum(1 for l in lines if AIM_OUT.match(l)),
            "ads_seconds": sum(1 for l in lines if ADS.match(l)), "shoulder": sum(1 for l in lines if SHOULDER.search(l)),
            "motion_aim": sum(1 for l in lines if MOTION_AIM.search(l)),
            "aim_lines": {k: sum(1 for r in rows if r["kind"] == k) for k in ("in", "changed", "second", "out")},
            "not_kept": sum(1 for l in lines if NOT_KEPT.match(l)), "zoom_levels": zoom_levels(lines)}


def wait_for(tail, test, seconds, sleep=time.sleep, clock=time.monotonic):
    """Reads the log until test(all lines read so far) is true or `seconds` pass; the result of the last test."""
    started = clock()
    while True:
        tail.read()
        if test(tail.lines):
            return True
        if clock() - started >= seconds:
            return False
        sleep(POLL_S)


def full_aim(device, tail, hold=HOLD_S, during=None, sleep=time.sleep, clock=time.monotonic):
    """Holds the aim, switches to full aim, waits for the log to say so, runs `during(device, tail, result)` (an
    action in full aim), holds `hold` seconds and lets go. Returns what happened:
      reached   the log said "State: aim in" while the button was held
      switches  how many aim-mode presses it took (0: none was made: the aim could not even be raised)
      to_full_s seconds from the aim button to the log's "State: aim in" (None: not reached)
      lost      the log said "State: aim out" before the harness let go
    The aim button is let go in every case, also when `during` or the input fails."""
    result = {"reached": False, "switches": 0, "to_full_s": None, "lost": False, "notes": []}
    begun = clock()
    device.run(f"down {AIM_BUTTON}; keep {AIM_BUTTON}; wait {RAISE_S}")
    try:
        for _ in range(SWITCHES):
            device.run(f"click {MODE_KEY}")
            result["switches"] += 1
            if wait_for(tail, lambda lines: first(AIM_IN, lines) is not None, WAIT_S, sleep, clock):
                result["reached"], result["to_full_s"] = True, round(clock() - begun, 2)
                break
        if not result["reached"]:
            result["notes"].append(f"no \"State: aim in\" after {result['switches']} aim-mode presses: the game did not go into full aim")
            return result
        if during:
            during(device, tail, result)
        sleep(hold)
        tail.read()
        result["lost"] = not in_full_aim(tail.lines)
        if result["lost"]:
            result["notes"].append("the log said \"State: aim out\" before the harness let go of the aim")
    finally:
        device.run(f"up {AIM_BUTTON}")
    return result


def _step(device, tail, result, where, before, seat, sleep, clock, capture=None):
    """One range on (ZOOM_STEP) and ZOOM_HOLD_S held from the moment R is let go (the weapon functions menu shut: the
    hold is free full aim, not the menu's time); the step's record, appended to result["zoom_steps"]."""
    start = len(tail.lines)
    device.run(ZOOM_STEP)   # blocks until R is up (~1.26 s with the real helper)
    begun = clock()
    changed = wait_for(tail, lambda lines: _zoomed(lines[start:], before), ZOOM_WAIT_S, sleep, clock)
    sleep(max(0.0, ZOOM_HOLD_S - (clock() - begun)))
    tail.read()
    return _record(tail, result, where, start, before, seat, changed, capture)


def _record(tail, result, where, start, before, seat, changed, capture=None):
    after = last_fov(tail.lines[start:])
    new_seat = last_seat(tail.lines[start:])
    step = {"step": len(result["zoom_steps"]) + 1, "where": where, "fov_before": before,
            "fov_after": after if after is not None else before, "zoomed": bool(changed),
            "seat_before": list(seat), "seat_after": list(new_seat if new_seat != (None, None) else seat),
            "full_aim": in_full_aim(tail.lines)}
    step["seat_moved"] = any(a is not None and b is not None and abs(a - b) >= SEAT_M
                             for a, b in zip(step["seat_before"], step["seat_after"]))
    if capture:
        try:
            step["shot"] = str(capture(step["step"]))
        except Exception as error:   # a capture is a help for the eye, never the verdict
            step["shot"] = f"no capture: {error}"
    result["zoom_steps"].append(step)
    if step["zoomed"] and not result.get("zoom_where"):
        result["zoom_where"] = where
    return step


def _round_again(result, where, first):
    """True once the ranges stepped `where` came back to the first field of view after another."""
    steps = [z for z in result["zoom_steps"] if z["where"] == where]
    return (any(z["zoomed"] for z in steps) and first is not None and steps[-1]["fov_after"] is not None
            and abs(steps[-1]["fov_after"] - first) < ZOOM_FOV_DEG)


def zoom_cycle(device, tail, result, sleep=time.sleep, clock=time.monotonic, capture=None):
    """In full aim (`during` of full_aim()): each scope range in turn, R held and the wheel up one notch (ZOOM_STEP),
    each held ZOOM_HOLD_S, up to ZOOM_STEPS or until the field of view comes round to the first again. Every step is
    kept in result["zoom_steps"] ({step, where "in aim", fov_before, fov_after, zoomed, seat_before, seat_after,
    seat_moved, full_aim}); result["zoom_where"] is "in aim" once a step changed the field of view."""
    result.setdefault("zoom_steps", []); result.setdefault("zoom_where", None)
    wait_for(tail, lambda lines: any(r["kind"] == "in" for r in aim_lines(lines)), 1.0, sleep, clock)
    first = last_fov(tail.lines)
    for _ in range(ZOOM_STEPS):
        if not in_full_aim(tail.lines):
            result["notes"].append("full aim was lost while the scope's range was stepped (R held in the sights)")
            return result
        _step(device, tail, result, "in aim", last_fov(tail.lines), last_seat(tail.lines), sleep, clock, capture)
        if _round_again(result, "in aim", first):
            break
    return result


def zoom_before_aim(device, tail, result, sleep=time.sleep, clock=time.monotonic, capture=None):
    """With no aim held: R and the wheel up (ZOOM_STEP), then full aim ZOOM_HOLD_S, for each range up to ZOOM_STEPS.
    The field of view compared is the one of the full aim after each step against the one before it."""
    result.setdefault("zoom_steps", []); result.setdefault("zoom_where", None)
    tail.read()
    first = before = last_fov(tail.lines)
    seat = last_seat(tail.lines)
    for _ in range(ZOOM_STEPS):
        start = len(tail.lines)
        device.run(ZOOM_STEP + "; wait 0.5")
        # a tail of its own: full_aim() looks for "State: aim in" in every line it has read, and the earlier aims' are there
        own = LogTail(tail.path, tail.offset)
        aimed = full_aim(device, own, ZOOM_HOLD_S, sleep=sleep, clock=clock)
        own.read()
        tail.lines += own.lines
        tail.offset = own.offset
        if not aimed["reached"]:
            result["notes"].append("no full aim after a range step before aiming")
            break
        after = last_fov(tail.lines[start:])
        changed = before is not None and after is not None and abs(after - before) >= ZOOM_FOV_DEG
        step = _record(tail, result, "before aim", start, before, seat, changed, capture)
        step["full_aim"] = aimed["reached"] and not aimed["lost"]
        before, seat = step["fov_after"], tuple(step["seat_after"])
        if _round_again(result, "before aim", first):
            break
    return result


def scope_zoom(device, tail, sleep=time.sleep, clock=time.monotonic, capture=None):
    """The aim-scope-zoom phase: the ranges in full aim (zoom_cycle), and where none changed the field of view, the
    ranges before aiming (zoom_before_aim). Returns full_aim()'s result with zoom_steps and zoom_where."""
    def during(device, tail, result):
        zoom_cycle(device, tail, result, sleep, clock, capture)
    result = full_aim(device, tail, 0.3, during, sleep, clock)
    result.setdefault("zoom_steps", []); result.setdefault("zoom_where", None)
    if result["reached"] and not result["zoom_where"]:
        in_aim_notes = list(result["notes"])
        zoom_before_aim(device, tail, result, sleep, clock, capture)
        if result["zoom_where"] == "before aim":
            # the fallback's own case (R in the sights drops full aim): what the ranges in aim said is kept apart, not
            # held against the ranges that did change the zoom
            result["in_aim_notes"] = in_aim_notes
            result["notes"] = [n for n in result["notes"] if n not in in_aim_notes]
    if result["reached"] and not result["zoom_where"]:
        fovs = sorted({z["fov_after"] for z in result["zoom_steps"] if z["fov_after"] is not None})
        moved = sum(1 for z in result["zoom_steps"] if z["seat_moved"])
        result["notes"].append(f"no range step changed the field of view ({len(result['zoom_steps'])} steps, in aim and "
                               f"before it; fov {fovs}; {moved} moved the sight or depth): the input did not reach the "
                               "menu, or this weapon's range does not change its zoom (look at the step captures)")
    return result


def _zoomed(lines, before):
    return any(r["kind"] in ("changed", "in") and r["fov"] is not None and before is not None and abs(r["fov"] - before) >= ZOOM_FOV_DEG
               for r in aim_lines(lines))


def during_script(script):
    """`during` for full_aim(): an input script played in full aim (aim-fire, aim-walk)."""
    def play(device, tail, result):
        device.run(script)
    return play


def verdict(result, lines, hold=HOLD_S, zoom=False):
    """The aim's own pass: full aim reached and kept until the harness let go, an "Aim: in" line, an "Aim: out" line
    of at least `hold` seconds; with zoom, two zoom levels in the "Aim:" lines and every step where the zoom changed
    (result["zoom_where"]: in aim, or the fallback before aiming) held in full aim."""
    rows = aim_lines(lines)
    notes = list(result.get("notes", []))
    if result.get("reached"):
        if not any(r["kind"] == "in" for r in rows):
            notes.append("full aim was reached and no \"Aim: in\" line was written")
        if not any(r["kind"] == "out" and not r.get("tap") and (r["held"] or 0) >= hold - 0.05 for r in rows):
            notes.append(f"no \"Aim: out\" line after {hold:.1f} s or more of full aim")
        if zoom and len(zoom_levels(lines)) < 2:
            notes.append(f"one zoom level seen ({zoom_levels(lines)}): the scope's range did not change")
        where = result.get("zoom_where")   # the steps that changed the zoom are judged, the others only kept
        loose = [z["step"] for z in result.get("zoom_steps", []) if not z["full_aim"] and (not where or z["where"] == where)]
        if zoom and loose:
            notes.append(f"range steps {loose} were not held in full aim")
    return {"pass": bool(result.get("reached")) and not notes, "notes": notes}


def main(argv):
    if len(argv) > 2 and argv[1] == "--log":
        with open(argv[2], errors="replace") as handle:
            print(counts(handle.read().splitlines()))
        return 0
    if len(argv) < 2 or argv[1] not in ("full", "zoom"):
        print(__doc__)
        return 2
    import drive
    import matrix
    if not drive.focus():
        print("the game window could not be focused")
        return 1
    tail = LogTail(matrix.log_path())
    with drive.Input() as device:
        hold = float(argv[2]) if argv[1] == "full" and len(argv) > 2 else HOLD_S
        result = scope_zoom(device, tail) if argv[1] == "zoom" else full_aim(device, tail, hold)
    time.sleep(1.2)
    tail.read()
    judged = verdict(result, tail.lines, ZOOM_HOLD_S if argv[1] == "zoom" else hold, argv[1] == "zoom")
    print({**result, **counts(tail.lines), **judged})
    return 0 if judged["pass"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
