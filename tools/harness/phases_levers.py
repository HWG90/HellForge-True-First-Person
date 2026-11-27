#!/usr/bin/env python3
"""Phases that set their scene with the levers first (levers.py), then play input and read the mod's log.

  phases_levers.py OUT [NAME ...]     run the named phases (default: all) in a harness session with the game focused

Each phase: `setup` (levers: a loadout, a stratagem, an exosuit), an input script, then the log lines the phase is
about (Weapon:, Grip:, Seat:, the throw's eye) and matrix.judge's verdict on the camera. Results go to
OUT/levers.json and OUT/NAME.png. matrix.py plays the same phases when they are named on its command line
(`prepare(name)` runs the setup there).

  foregrip-liberator    AR-23 Liberator with a Vertical Grip: hip, full aim, a shot, reload
  foregrip-adjudicator  BR-14 Adjudicator with a Vertical Grip: the same
  support-weapon        MG-206 Heavy Machine Gun in the hands: hip, full aim, a burst, reload
  stratagem-throw       the MG-206 stratagem added to the equipped ones, its code entered, the ball thrown
  exosuit-seat          an EXO-45 called in with its stratagem (else spawned), the diver put at its front: enter,
                        look around, walk, leave
  prone-melee           prone, a melee strike, up again
"""
import json
import re
import sys
import time
from pathlib import Path

import levers

AIM = "down right; wait 0.35; click middle; wait 1.5; up right"
# name -> (input script, seconds the action needs after the input): matrix.PHASES' shape
PHASES = {
    "foregrip-liberator": (f"wait 1.5; {AIM}; wait 0.6; click left 0.3; wait 0.3; key r", 4.0),
    "foregrip-adjudicator": (f"wait 1.5; {AIM}; wait 0.6; click left 0.3; wait 0.3; key r", 4.0),
    "support-weapon": (f"wait 1.5; {AIM}; wait 0.6; click left 0.5; wait 0.3; key r", 6.0),
    # MG-206's code: down, left, up, down, down; a tap of ctrl opens the list
    "stratagem-throw": ("key ctrl; wait 0.6; key arrowdown; wait 0.15; key arrowleft; wait 0.15; key arrowup; wait 0.15; "
                        "key arrowdown; wait 0.15; key arrowdown; wait 0.8; click left 0.2", 3.0),
    "exosuit-seat": ("hold e 1.5; wait 4.5; look 300 0; look -600 0; look 300 0; wait 0.5; hold w 1.5; wait 0.8; hold e 1.5", 3.5),
    "prone-melee": ("key z; wait 1.5; key f; wait 2.0; key z", 1.8),
}
# EXO-45 Patriot's code: left, down, right, up, left, down, down
CALL_EXOSUIT = ("key ctrl; wait 0.6; key arrowleft; wait 0.15; key arrowdown; wait 0.15; key arrowright; wait 0.15; key arrowup; "
                "wait 0.15; key arrowleft; wait 0.15; key arrowdown; wait 0.15; key arrowdown; wait 0.8; click left 0.2")
EXOSUIT_WAIT_S = 30
KEEP = {"foregrip-liberator": ("Weapon:", "Grip:"), "foregrip-adjudicator": ("Weapon:", "Grip:"), "support-weapon": ("Weapon:", "Grip:"),
        "stratagem-throw": ("Throw:", "State:"), "exosuit-seat": ("Seat:", "State:"), "prone-melee": ("Motion: action=",)}


def front_of(vehicle, metres=1.9):
    """Where to stand to face a vehicle's front: `metres` along its facing."""
    return vehicle["x"] + vehicle["facing_x"] * metres, vehicle["y"] + vehicle["facing_y"] * metres, vehicle["z"] + 0.1


def newest_exosuit(vehicles, known=()):
    suits = [v for v in vehicles if v["resource"] == levers.content()["entity"][levers.EXOSUIT] and v["entity"] not in known]
    return max(suits, key=lambda v: v["entity"]) if suits else None


def setup(name, tool=None, device=None, sleep=time.sleep):
    """Sets the scene of a phase with the levers; returns what the levers reported (a dict), {} for a phase that
    needs none. A refusal comes back as {"refused": text}: the phase is then played as it stands."""
    tool = tool or levers.Levers(memory=levers.Path(levers.tempfile.gettempdir()) / ("tfp-levers-%d.json" % levers.os.getuid()))
    try:
        if name == "foregrip-liberator":
            return tool.loadout("primary", "AR-23 Liberator", ["Vertical Grip"])
        if name == "foregrip-adjudicator":
            return tool.loadout("primary", "BR-14 Adjudicator", ["Vertical Grip"])
        if name == "support-weapon":
            return tool.loadout("support", "MG-206 Heavy Machine Gun")
        if name == "stratagem-throw":
            return {"stratagems": tool.stratagems(["MG-206 Heavy Machine Gun"])}
        if name == "exosuit-seat":
            before = [v["entity"] for v in tool.vehicles()]
            report = {"stratagems": tool.stratagems([levers.EXOSUIT]), "how": "called in"}
            suit = None
            if device is not None:
                device.run(CALL_EXOSUIT)
                end = time.monotonic() + EXOSUIT_WAIT_S
                while suit is None and time.monotonic() < end:
                    sleep(2.0)
                    suit = newest_exosuit(tool.vehicles(), before)
                if suit is not None:
                    sleep(6.0)                                 # the Pelican lets go and leaves
                    suit = newest_exosuit(tool.vehicles(), before) or suit
            if suit is None:
                report["how"] = "spawned (no exosuit came from the stratagem)"
                made = tool.spawn_exosuit()
                sleep(1.5)
                suit = next((v for v in tool.vehicles() if v["entity"] == made["entity"]), None)
            if suit is None:
                return dict(report, refused="no exosuit in the world")
            report["exosuit"] = suit
            report["teleport"] = tool.teleport(*front_of(suit))
            return report
    except levers.LeverError as error:
        return {"refused": str(error)}
    return {}


def prepare(name):
    """For matrix.py: the setup of a lever phase before it is played; nothing for any other phase."""
    if name in PHASES:
        report = setup(name)
        print(f"  levers: {summary(report)}", flush=True)
        return report
    return None


def summary(report):
    if not report:
        return "no setup"
    if "refused" in report:
        return report["refused"]
    if "name" in report:
        hand = "in the hand" if report.get("in_hand") else "equipped"
        return f"{report['name']} {hand} as entity {report['entity']}" + (f" with {', '.join(report['attachments'])}" if report.get("attachments") else "")
    if "exosuit" in report:
        return f"exosuit {report['exosuit']['entity']} {report['how']}; the diver at its front ({'moved' if report['teleport']['moved'] else 'not moved'})"
    if "stratagems" in report:
        return "equipped: " + ", ".join(r["name"] for r in report["stratagems"])
    return json.dumps(report)[:200]


def kept(name, lines):
    """The log lines a phase is about: for each kind it keeps, how many came and the last of them."""
    out = {}
    for kind in KEEP.get(name, ()):
        found = [l for l in lines if kind in l]
        out[kind] = {"count": len(found), "last": [l[:400] for l in found[-3:]]}
    return out


def seat_facts(lines):
    """What the Seat: lines of a stretch say: entered, left, frames from the seat, glides, the suit seen through."""
    text = "\n".join(l for l in lines if "Seat:" in l)
    frames = [int(n) for n in re.findall(r"Seat: \+(\d+) frames from the seat", text)]
    return {"entered": "in a vehicle's seat" in text, "left": "left the seat" in text, "seat_frames": sum(frames),
            "gliding_frames": sum(int(n) for n in re.findall(r"\+(\d+) gliding", text)),
            "seen_through": "cockpit: seen through" in text, "no_suit_proven": "no suit proven" in text}


def main(argv):
    import drive
    import matrix
    if len(argv) < 2:
        print(__doc__); return 2
    out = Path(argv[1]); out.mkdir(parents=True, exist_ok=True)
    names = argv[2:] or list(PHASES)
    unknown = [n for n in names if n not in PHASES]
    if unknown:
        print("no lever phase called " + ", ".join(unknown)); return 2
    if not drive.focus():
        print("the game window could not be focused"); return 1
    log = matrix.log_path(); results = {}
    with drive.Input() as device:
        if "frames held" not in log.read_text(errors="replace")[-4000:]:
            device.run(f"key {drive.TOGGLE}; wait 2.5")
        for name in names:
            start = log.stat().st_size
            report = setup(name, device=device)
            print(f"{name}: levers: {summary(report)}", flush=True)
            time.sleep(1.0)
            script, settle = PHASES[name]
            played = log.stat().st_size
            try:
                device.run(script)
                time.sleep(min(settle, 1.0))
                try:
                    drive.shot(out / f"{name}.png")
                except drive.DriveError as error:
                    print(f"  (no capture: {error})")
                time.sleep(max(0.0, settle - 1.0) + 1.2)
            except drive.DriveError as error:
                results[name] = {"pass": False, "levers": report, "error": str(error)}
                print(f"FAIL {name}: {error}"); break
            with open(log, errors="replace") as handle:
                handle.seek(start); whole = handle.read().splitlines()
                handle.seek(played); lines = handle.read().splitlines()
            judged = matrix.judge(lines, throw=name == "stratagem-throw")
            result = {"pass": bool(judged["pass"]) and "refused" not in report, "levers": report, "lines": kept(name, whole),
                      "camera": {k: judged[k] for k in ("frames", "other_camera_frames", "missed", "kinds", "notes") if k in judged},
                      "describe": matrix.describe(judged)}
            if name == "exosuit-seat":
                result["seat"] = seat_facts(whole)
                result["pass"] = result["pass"] and result["seat"]["entered"] and result["seat"]["left"]
            results[name] = result
            print(f"{'pass' if result['pass'] else 'FAIL':4} {name:22} frames {result['camera'].get('frames')} other-camera "
                  f"{result['camera'].get('other_camera_frames')} missed {result['camera'].get('missed')} {result['describe']}")
            for kind, found in result["lines"].items():
                print(f"       {kind} {found['count']} lines" + (f"; last: {found['last'][-1][:260]}" if found["last"] else ""))
            if "seat" in result:
                print(f"       seat: {result['seat']}")
            for note in result["camera"].get("notes", []):
                print(f"       {note}")
    (out / "levers.json").write_text(json.dumps(results, indent=1, default=str))
    failed = [n for n, r in results.items() if not r["pass"]]
    print(f"{len(results) - len(failed)} of {len(results)} passed" + (f"; failed: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
