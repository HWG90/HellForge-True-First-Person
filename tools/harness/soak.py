#!/usr/bin/env python3
"""Soak: random actions back to back for MINUTES, with short random pauses and overlaps, judged from the mod's log.

  soak.py [MINUTES] [SEED]        default 10 minutes, seed 1
  soak.py --log LOG               no game: judge a log that is there, style by style

Needs a harness session in a mission with player control; takes the keyboard and mouse. One action at a time is what
matrix.py checks; this looks for what one action leaves behind for the next: a frame out of first person, a camera
step over POP_M between two traced frames, a refusal, the arm drive refusing, an action after which the view does
not come back. Every action and what the judge said of its stretch of the log go to stdout, with one line for every
frame at or over the limit (its own time, kind, whether jumps were looked for, what was carried, whose it is); at the end
the whole log is judged once more in one piece, for the frames that lie across two actions. The seed repeats a run.
The view is switched on at the start if it is not, and is not toggled (toggling is a cut by design). No death: that ends the body.

The camera style (Bodycam or Stable) is the log's: the last "Camera style:" line before the first action is printed
at the top, a change of style inside the run where it falls, and the summary says whether the run had one style. No
argument and no file name says it (the two feel.49 soaks of 2026-11-24 were named the wrong way round). The summary
also counts the flagged frames by whose they are, and how many of the eye's own lie along the view. Round 19: the eye
judged is the eye shown (the row's eye plus the helmet veil's move, veil=), and a frame over the limit for the veil
alone is "the veil's": printed, with its own largest, and neither a flag nor the eye's largest (the eye's is measured
without the veil's move).

Round 26: the aim actions (matrix.AIMED) go into full aim: aim.full_aim() holds the aim, presses the game's aim-mode
button (middle) and reads the log until it says "State: aim in". Each such line says whether full aim was reached and
the summary counts them; before round 26 no soak reached it (one "State: aim in" in soaks 8 to 34).

Round 30: the dive-swap action (matrix.DIVES) is played by dive.dive_swap(): a dive and a swap that leave the game's
view, for the head of a diving body through the veil. Each says whether the view was left and how low the head was;
the summary counts them and gives the lowest head (round 26's estimate: 0.55 m below the eye).
"""
import random
import sys
import time

import aim
import dive
import drive
import matrix

SKIP = {"toggle-off-on", "toggle-fast", "ragdoll", "death", "idle", "aim-scope-zoom"}   # the zoom needs a scoped rifle
# What is left pressed or changed by a phase and has to be undone before the next (a crouch that is left on).
OVERLAPS = [   # two inputs at once, as a player does
    "down w; key g; wait 1.5; up w", "down w; key v; wait 2.0; up w", "down shift; down w; wait 0.8; key 2; wait 1.2; up w; up shift",
    "key 2; wait 0.3; key g", "key g; wait 0.4; key 1", "key r; wait 0.5; key 2", "key v; wait 0.6; click right 0.8",
    "down right; wait 0.35; click middle; wait 0.6; key 2; wait 1.0; up right",   # a swap in full aim (round 26)
    "key c; wait 0.4; key g; wait 2.5; key c", "key z; wait 1.0; key v; wait 3.0; key z",
    "key ctrl; wait 0.6; key g", "key 2; wait 0.15; key 2; wait 0.15; key 1", "down w; wait 0.2; key alt; wait 0.3; key 2; wait 1.0; up w",
    "key b; wait 0.8; key g", "look 300 200; key g; look -300 -200", "key f; wait 0.2; key f; wait 0.2; key r",
]


def verdict(frames, flagged):
    """The exit code: 2 when no frame was judged (the view was not on: nothing was looked at), 1 when flagged."""
    if frames <= 0:
        return 2
    return 1 if flagged else 0


def listed(pops):
    """What names a frame: its tick (the helper's clock). Not its time: that is the stamp of its line, and the frames
    from before an edge are written with the edge, several under one stamp."""
    return {p["tick"] for p in pops}


def unlisted(whole, seen):
    """The frames over the limit in the whole log that no action's own stretch listed (`seen`: their ticks)."""
    return [p for p in whole if p["whose"] not in ("body", "veil") and p["tick"] not in seen]


def counted(pops):
    """The summary's words for the frames at or over the limit: how many, whose, the largest."""
    counts = matrix.whose_counts(pops)
    flagged = [p for p in pops if p["whose"] not in ("body", "veil")]
    veiled = [p["beyond"] for p in pops if p["whose"] == "veil"]
    veil = f"{len(veiled)} the veil's" + (f", largest {max(veiled):.3f} m" if veiled else "") + ", not counted"
    return (f"{len(flagged)} frames flagged ({counts['own']} the eye's own, {counts['along']} of them along the view; {counts['filter']} the filter's), "
            f"largest {max([0.0] + [p.get('bare', p['beyond']) for p in flagged]):.3f} m; {veil}; {counts['body']} the body's own, not counted")


def aim_summary(aims, lines):
    """The summary's line on full aim: how many aim actions reached it, and what the log's aim lines count."""
    c = aim.counts(lines)
    reached = sum(1 for _, _, ok in aims if ok)
    missed = [f"{n} {name}" for n, name, ok in aims if not ok][:6]
    text = (f"full aim: {reached} of {len(aims)} aim actions reached it; the log: {c['aim_in']} \"State: aim in\", "
            f"{c['ads_seconds']} View seconds with ads=1, Aim: lines {c['aim_lines']}, {c['not_kept']} releases not kept")
    return text + (f"; not reached: {', '.join(missed)}" if missed else "")


def dive_summary(dives):
    """The summary's line on the dive + swap actions: how many left the game's view, and the lowest head measured."""
    left = [r for _, r in dives if r["left"]]
    lows = [r["lowest_up"] for r in left if r["lowest_up"] is not None]
    text = f"dive + swap: {len(left)} of {len(dives)} left the game's view"
    return text + (f"; {sum(len(r['veil_heads']) for r in left)} veiled frames, the head lowest {min(lows):+.3f} m from the eye"
                   if lows else "; the head in a dive not measured")


def act(device, name, script, settle, log, sleep=time.sleep):
    """One action: a phase of matrix.DRIVEN (the aims, the dive + swap) by matrix.play(), which reads the log as it
    goes, then `settle`; any other its script with the settle as a wait. Returns matrix.play()'s result, or None."""
    if name in matrix.DRIVEN:
        aimed = matrix.play(device, name, log)
        sleep(settle)
        return aimed
    device.run(f"{script}; wait {settle:.2f}")
    return None


def judge_log(lines):
    """A log judged in one piece for each camera style it ran in: one line of text for each stretch between two
    "Camera style:" lines in which a frame was held or traced. Frames on either side of such a line are not measured
    against each other."""
    cuts = [i for i, line in enumerate(lines) if matrix.STYLE.match(line)]
    out = []
    for begin, end in zip([0] + cuts, cuts + [len(lines)]):
        stretch = lines[begin:end]
        j = matrix.judge(stretch)
        if not j["frames"] and not j["judged"]:
            continue
        out.append(f"{matrix.style_header(stretch)}, {j['frames']} frames held, {j['missed']} missed: {counted(j['pops'])}")
    return out


def main(argv):
    if len(argv) > 2 and argv[1] == "--log":
        with open(argv[2], errors="replace") as handle:
            text = judge_log(handle.read().splitlines())
        print("\n".join(text) if text else "no frame was held or traced in this log")
        return 0 if text else 2
    minutes = float(argv[1]) if len(argv) > 1 else 10.0
    rng = random.Random(int(argv[2]) if len(argv) > 2 else 1)
    names = [n for n in matrix.PHASES if n not in SKIP]
    if not drive.focus():
        print("the game window could not be focused"); return 2
    d = drive.Input()
    log = matrix.log_path()
    if "frames held" not in log.read_text(errors="replace")[-4000:]:
        d.run(f"key {drive.TOGGLE}; wait 2.5")   # the view on, as matrix.py does
    whole = log.stat().st_size
    before = log.read_text(errors="replace").splitlines()
    print(matrix.style_header(before), flush=True)
    end = time.time() + minutes * 60
    worst, veiled, bad, count, seen = 0.0, 0.0, [], 0, set()
    aims = []   # (action number, name, reached) of every aim action
    dives = []  # (action number, dive.dive_swap's result) of every dive + swap
    while time.time() < end:
        if rng.random() < 0.3:
            name, script, settle = "overlap", rng.choice(OVERLAPS), 3.0
        else:
            name = rng.choice(names)
            script, settle = matrix.PHASES[name]
        settle *= rng.choice((0.15, 0.4, 1.0))   # often the next action begins before this one is over
        start = log.stat().st_size
        try:
            aimed = act(d, name, script, settle, log)
        except drive.DriveError as error:
            print(f"stopped: {error}")
            break
        with open(log, errors="replace") as handle:
            handle.seek(start)
            lines = handle.read().splitlines()
        for text in matrix.style_changes(lines):
            print(text, flush=True)
        j = matrix.judge(lines, mouse="look" in script)
        count += 1
        worst = max(worst, j["largest_jump_m"])
        veiled = max(veiled, j.get("largest_veil_m", 0.0))
        flag = j["missed"] or j["largest_jump_m"] >= matrix.POP_M or any("Refused" in n for n in j["notes"])
        flag = flag or bool(j["veil_faults"]) or (bool(j["turn_snaps"]) and "look" not in script)   # round 49
        if flag:
            bad.append((count, name, script, j["missed"], round(j["largest_jump_m"], 3)))
        print(f"{count:3d} {'BAD ' if flag else 'ok  '}{name:16s} missed {j['missed']} {matrix.describe(j)} kinds {j['kinds']}  {script[:60]}", flush=True)
        if aimed is not None and name in matrix.DIVES:
            dives.append((count, aimed))
            print(f"      {dive.text(aimed)}", flush=True)
        elif aimed is not None:
            aims.append((count, name, aimed["reached"]))
            print(f"      {matrix.aim_text({**aimed, **aim.counts(lines)})}", flush=True)
        for text in [f"       {said}" for said in j["said"]] + matrix.pop_lines(j):
            print(text, flush=True)
        seen |= listed(j["pops"])
    d.run("up w; up s; up shift; up right; wait 2.0")
    with open(log, errors="replace") as handle:
        handle.seek(whole)
        lines = handle.read().splitlines()
    j = matrix.judge(lines)
    refused = sorted({l.split(" ", 1)[-1][:120] for l in lines if "Refused" in l or "refused 1" in l or " error " in l.lower()})[:8]
    arms = [l for l in lines if " Arms:" in l and "refused 0" not in l][:3]
    print(f"\n{count} actions, {j['frames']} frames held, {j['missed']} missed, largest step {worst:.3f} m (the veil's {veiled:.3f} m, not counted); {len(bad)} flagged")
    print("  " + matrix.style_summary(matrix.style_in_force(before), lines))
    print("  the whole log: " + counted(j["pops"]))
    print("  " + aim_summary(aims, lines))
    print("  " + dive_summary(dives))
    for row in bad:
        print("  flagged:", row)
    across = unlisted(j["pops"], seen)
    if across:
        print(f"  {len(across)} frames over the limit lie across two actions' stretches of the log (judged from the whole log):")
        for text in matrix.pop_lines({"pops": across}, "    "):
            print(text)
    for text in refused:
        print("  line:", text)
    for text in arms:
        print("  arms:", text[13:170])
    last = [l for l in lines if " State:" in l or " Status:" in l][-2:]
    print("  ends:", [l[13:110] for l in last])
    if j["frames"] <= 0:
        print("  NOT A PASS: no frame was held in first person: the view was not on")
    return verdict(j["frames"], len(bad) + len(across))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
