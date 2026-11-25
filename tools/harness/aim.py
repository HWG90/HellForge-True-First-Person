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
  aim.py zoom                   full aim, and each scope range through the weapon functions menu (hold R)
  aim.py --log LOG              no game: what a log says of full aim (the counts of round 26's "before")

Only the game can say which direction of the weapon functions menu is the scope's range: zoom_cycle() tries
ZOOM_KEYS in turn and keeps the first after which the log's "Aim:" lines show another field of view.
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
MENU_S = 0.6             # R held this long before a direction: the weapon functions menu (a tap is a reload)
ZOOM_KEYS = ("arrowright", "arrowleft", "arrowup", "arrowdown")   # the menu's directions, tried in turn
ZOOM_WAIT_S = 1.2        # an "Aim: changed" line comes 0.3 s after a change has settled
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


def zoom_cycle(device, tail, result, keys=ZOOM_KEYS, sleep=time.sleep, clock=time.monotonic):
    """In full aim: opens the weapon functions menu (hold R) and presses one direction, for each of `keys` until the
    log's "Aim:" lines show another field of view; that key is pressed once more (a scope with three ranges shows all
    three). Sets result["zoom_key"] (None: no key changed the zoom) and result["zoom_tries"]."""
    result["zoom_key"], result["zoom_tries"] = None, []
    wait_for(tail, lambda lines: any(r["kind"] == "in" for r in aim_lines(lines)), 1.0, sleep, clock)
    for key in keys:
        if not in_full_aim(tail.lines):
            result["notes"].append(f"full aim was lost before the zoom key {key} was tried")
            return result
        before, start = last_fov(tail.lines), len(tail.lines)
        device.run(f"down r; wait {MENU_S}; key {key}; wait 0.2; up r")
        result["zoom_tries"].append(key)
        if wait_for(tail, lambda lines: _zoomed(lines[start:], before), ZOOM_WAIT_S, sleep, clock):
            result["zoom_key"] = key
            start, before = len(tail.lines), last_fov(tail.lines)
            device.run(f"down r; wait {MENU_S}; key {key}; wait 0.2; up r")
            wait_for(tail, lambda lines: _zoomed(lines[start:], before), ZOOM_WAIT_S, sleep, clock)
            return result
    result["notes"].append(f"no zoom change after {', '.join(keys)} in the weapon functions menu: the weapon has no "
                           "scope ranges, or the menu's zoom is another input")
    return result


def _zoomed(lines, before):
    return any(r["kind"] == "changed" and r["fov"] is not None and (before is None or abs(r["fov"] - before) >= 0.5)
               for r in aim_lines(lines))


def during_script(script):
    """`during` for full_aim(): an input script played in full aim (aim-fire, aim-walk)."""
    def play(device, tail, result):
        device.run(script)
    return play


def verdict(result, lines, hold=HOLD_S, zoom=False):
    """The aim's own pass: full aim reached and kept until the harness let go, an "Aim: in" line, an "Aim: out" line
    of at least `hold` seconds; with zoom, two zoom levels in the "Aim:" lines."""
    rows = aim_lines(lines)
    notes = list(result.get("notes", []))
    if result.get("reached"):
        if not any(r["kind"] == "in" for r in rows):
            notes.append("full aim was reached and no \"Aim: in\" line was written")
        if not any(r["kind"] == "out" and not r.get("tap") and (r["held"] or 0) >= hold - 0.05 for r in rows):
            notes.append(f"no \"Aim: out\" line after {hold:.1f} s or more of full aim")
        if zoom and len(zoom_levels(lines)) < 2:
            notes.append(f"one zoom level seen ({zoom_levels(lines)}): the scope's range did not change")
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
        result = full_aim(device, tail, hold, zoom_cycle if argv[1] == "zoom" else None)
    time.sleep(1.2)
    tail.read()
    judged = verdict(result, tail.lines, hold, argv[1] == "zoom")
    print({**result, **counts(tail.lines), **judged})
    return 0 if judged["pass"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
