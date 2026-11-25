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

dive_swap() therefore brings the primary up and waits for the game's view (the log's View lines, kind=6), dives (w
held, alt) and swaps (2) SWAP_AFTER_S into the dive, for each delay in turn until the log says the view was left; the
primary comes back after every try. Its result lists every try and the veil_head of every veiled frame.

  dive.py                 one dive + swap in the game, printed
  dive.py --log LOG       no game: the veil_head rows of a log and the lowest head
"""
import re
import sys
import time

import aim

STAMP = aim.STAMP
LEFT = re.compile(STAMP + r"State: left the native view\b")
ENTERED = re.compile(STAMP + r"State: entered the native view\b")
VIEW_KIND = re.compile(STAMP + r"View: .*?\bkind=(\d+)")
DIVING = re.compile(r"\baction=dive\b")
VEIL_HEAD = re.compile(STAMP + r"Trace: t=\S+ kind=(\d+)\b.*? veil_head=\((-?[\d.]+) (-?[\d.]+)\)")

SWAP_AFTER_S = (1.0, 0.6, 1.3, 0.3)   # the swap this long after the dive key, tried in turn (soak 34: 1.3 s left the view)
RUN_IN_S = 0.3                        # w held this long before the dive key: a dive forward, as the matrix's "dive"
LAND_S = 2.5                          # after the swap: the dive lands and the swap ends (the matrix's dive settles 2.5 s)
NATIVE_WAIT_S = 4.0                   # how long the log is read for the game's view before a try (a swap takes ~2 s)
PRIMARY = "key 1; wait 0.3"


def script(after):
    """One try: w held, the dive, the swap `after` seconds into it, w let go once it has landed."""
    return f"down w; wait {RUN_IN_S}; key alt; wait {after}; key 2; wait {LAND_S}; up w"


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


def veil_heads(lines):
    """Every veiled Trace row of `lines`: {at, kind, ahead, up} (veil_head: the head from the eye before the veil)."""
    out = []
    for line in lines:
        m = VEIL_HEAD.match(line)
        if m:
            out.append({"at": m[1], "kind": int(m[2]), "ahead": float(m[3]), "up": float(m[4])})
    return out


def dive_swap(device, tail, delays=SWAP_AFTER_S, sleep=time.sleep, clock=time.monotonic):
    """Dives and swaps from the game's view until the log says the view was left. Returns:
      left        a try left the game's view ("State: left the native view" in its stretch of the log)
      swap_after  the delay of that try (None: none left)
      attempts    every try: {swap_after_s, from_native, left, dived, veil_heads}
      veil_heads  the veiled rows of the try that left; lowest_up their lowest head (m from the eye, below < 0)
    The primary is brought back after every try, also when one fails."""
    result = {"left": False, "swap_after": None, "attempts": [], "veil_heads": [], "lowest_up": None, "notes": []}
    for after in delays:
        device.run(PRIMARY)
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
        attempt = {"swap_after_s": after, "from_native": native, "left": aim.first(LEFT, part) is not None,
                   "dived": any(DIVING.search(line) for line in part), "veil_heads": veil_heads(part)}
        result["attempts"].append(attempt)
        if attempt["left"]:
            result.update(left=True, swap_after=after, veil_heads=attempt["veil_heads"])
            if attempt["veil_heads"]:
                result["lowest_up"] = min(r["up"] for r in attempt["veil_heads"])
            break
    if not result["left"]:
        result["notes"].append(f"no dive + swap left the game's view (swaps {', '.join(f'{d:.1f}' for d in delays)} s into "
                               "the dive): the head in a dive is still unmeasured")
    elif not result["veil_heads"]:
        result["notes"].append("the view was left in the dive and no veiled Trace row was written: the veil is off, or "
                               "a build before round 26 (no veil_head)")
    if not any(a["from_native"] for a in result["attempts"]):
        result["notes"].append("the game's view was never seen before a try (View: kind=6): the primary was not in hand")
    return result


def verdict(result):
    """The dive's own pass: the view was left in a dive and the head measured through the veil."""
    return {"pass": bool(result.get("left") and result.get("veil_heads")), "notes": list(result.get("notes", []))}


def text(result):
    """One line for the dive + swap phase."""
    tries = len(result["attempts"])
    if not result["left"]:
        return f"dive + swap: the game's view never left ({tries} tries)"
    out = f"dive + swap: left the game's view with the swap {result['swap_after']:.1f} s into the dive (try {tries})"
    if result["veil_heads"]:
        out += f"; {len(result['veil_heads'])} veiled frames, the head lowest {result['lowest_up']:+.3f} m from the eye"
    return out


def main(argv):
    if len(argv) > 2 and argv[1] == "--log":
        with open(argv[2], errors="replace") as handle:
            rows = veil_heads(handle.read().splitlines())
        print({"veiled": len(rows), "lowest_up": min((r["up"] for r in rows), default=None),
               "lowest": min(rows, key=lambda r: r["up"]) if rows else None})
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
