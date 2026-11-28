"""Round 49: the judge of the direction shown at a change of camera (tools/harness/matrix.py turn_snaps) and the veil's
bound (veil_faults), on made-up rows and on rows cut from the soaks of 2026-11-27 (feel.59 + rounds 40, 43:
tests/reference/turn; soak 60 is Stable, soak 62 Bodycam)."""
import math
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import matrix  # noqa: E402
import trace  # noqa: E402

REFERENCE = ROOT / "tests/reference/turn"
DIVE_SWAP = "soak60-A-L1990-2070.log"      # the matrix's dive-swap: 29.0 degrees out of the native view, 39.5 back into it
PRONE_BACK = "soak62-C-L1960-2012.log"     # the primary back, prone after a dive: 72.7 degrees
FOOT_SWAP = "soak60-A-L3280-3420.log"      # a swap on foot: 0.05 and 0.02 degrees at its two changes
FLICK = "soak60-A-L6760-6800.log"          # the primary back, prone: the veil 0.248 m, then nothing
MELEE = "soak62-C-L2760-2815.log"          # #13 melee, prone: the veil 0.196, 0.081 m, then the native view
UNDER = "soak62-C-L2155-2200.log"          # a dive + swap: the veil 0.120 m, then 0.055 with the head below the view
DIVE = "soak60-A-L6590-6715.log"           # a dive + swap on open ground, Stable: the float 0.355 m after the landing
STIM = "soak60-A-L6985-7015.log"           # #20 stim, prone: the end of an orphan run, the camera's matrix 90 degrees off


def cut(name):
    return (REFERENCE / name).read_text().splitlines()


def yaw(degrees, pitch=0.0):
    a, p = math.radians(degrees), math.radians(pitch)
    return [-math.sin(a) * math.cos(p), math.cos(a) * math.cos(p), math.sin(p)]


def rows(frames, dt=33.0):
    """(kind, direction or None, veil or None) a frame."""
    return [{"tick": 1000 + dt * i, "at": f"f{i}", "kind": kind, "turn": turn, "veil": veil, "eye": [0, 0, 0]} for i, (kind, turn, veil) in enumerate(frames)]


class Turn(unittest.TestCase):
    def test_the_angle_is_exact_for_rounded_vectors(self):
        v = [-0.5582, 0.8295, 0.0159]   # four places: its length is 0.99995, and acos of its dot with itself read 0.8 degrees
        self.assertLess(matrix._degrees(v, v), 1e-6)
        self.assertAlmostEqual(matrix._degrees(yaw(0), yaw(29)), 29.0, places=6)

    def test_a_turn_at_a_change_of_kind_is_found(self):
        found = matrix.turn_snaps(rows([(6, yaw(0), None), (6, yaw(1), None), (4, yaw(30), None), (4, yaw(31), None), (6, yaw(31), None)]))
        self.assertEqual([(t["before"], t["kind"], round(t["degrees"], 3)) for t in found], [(6, 4, 29.0), (4, 6, 0.0)])

    def test_the_same_kind_a_pause_and_an_unknown_direction_are_not(self):
        self.assertEqual(matrix.turn_snaps(rows([(4, yaw(0), None), (4, yaw(50), None)])), [])              # the mouse within one camera
        self.assertEqual(matrix.turn_snaps(rows([(6, yaw(0), None), (4, yaw(50), None)], dt=200.0)), [])    # not neighbours
        self.assertEqual(matrix.turn_snaps(rows([(6, None, None), (4, yaw(50), None)])), [])                # an orphan row before round 49

    def test_the_dive_swap_of_soak_60_fails_twice(self):
        j = matrix.judge(cut(DIVE_SWAP))
        self.assertEqual([round(t["degrees"], 1) for t in j["turn_snaps"]], [29.0, 39.5])
        self.assertEqual([(t["before"], t["kind"]) for t in j["turn_snaps"]], [(6, 4), (4, 6)])
        self.assertFalse(j["pass"])
        self.assertAlmostEqual(j["turn_deg"], 39.5, places=1)
        self.assertIn("turn 39.5 deg at a change of camera", matrix.describe(j))
        self.assertTrue(any("29.0 degrees" in n and "over 10 without the mouse" in n for n in j["notes"]))

    def test_the_prone_primary_back_of_soak_62_fails(self):
        snaps = matrix.judge(cut(PRONE_BACK))["turn_snaps"]
        self.assertEqual([round(t["degrees"], 1) for t in snaps], [72.7])

    def test_a_swap_on_foot_passes(self):
        lines = cut(FOOT_SWAP)
        j = matrix.judge(lines)
        self.assertEqual(j["turn_snaps"], [])
        self.assertLess(j["turn_deg"], 0.1)
        self.assertEqual(len(matrix.turn_snaps(trace.frames(lines))), 2)   # both changes were measured

    def test_a_phase_that_moved_the_mouse_is_printed_not_failed(self):
        lines = cut(PRONE_BACK)
        with_mouse, without = matrix.judge(lines, mouse=True), matrix.judge(lines)
        self.assertEqual(len(with_mouse["turn_snaps"]), 1)
        self.assertTrue(any("not counted" in n for n in with_mouse["notes"]))
        self.assertFalse(any("72.7" in n and "not counted" in n for n in without["notes"]))

    def test_a_turn_alone_fails_a_phase(self):
        """Every reference window with a turn fails for another reason too. A swap on foot, which passes, with the
        ordinary camera's rows turned 26 degrees about up: nothing else is wrong with it."""
        lines = cut(FOOT_SWAP)
        self.assertTrue(matrix.judge(lines)["pass"])
        c, n = math.cos(math.radians(26)), math.sin(math.radians(26))

        def turned(line):
            if "Trace:" not in line or " kind=4 " not in line:
                return line
            return re.sub(r" turn=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)",
                          lambda m: f" turn=({float(m[1]) * c - float(m[2]) * n:.4f} {float(m[1]) * n + float(m[2]) * c:.4f} {m[3]})", line)
        j = matrix.judge([turned(line) for line in lines])
        self.assertEqual([round(t["degrees"]) for t in j["turn_snaps"]], [23, 23])   # 26 about up, under a view pitched 28 degrees
        self.assertFalse(j["pass"])
        self.assertTrue(matrix.judge([turned(line) for line in lines], mouse=True)["pass"])   # a phase that looks around

    def test_a_reload_phase_keeps_the_turn_and_the_veil(self):
        """The reload judge replaced what the turn and the veil judges had found; a reader of turn_snaps raised."""
        j = matrix.judge(cut(DIVE_SWAP), reload=True)
        self.assertEqual([round(t["degrees"], 1) for t in j["turn_snaps"]], [29.0, 39.5])
        for key in ("turn_deg", "turn_at", "veil_open_m", "veil_faults", "said", "slide_m", "slide_at"):
            self.assertIn(key, j)


def veiled(moves, kinds=None, dt=33.0):
    kinds = kinds or [4] * len(moves)
    return rows([(k, yaw(0), [0.0, m, 0.0] if m else None) for k, m in zip(kinds, moves)], dt)


class VeilBound(unittest.TestCase):
    def test_an_opening_and_a_let_out_within_the_bound_pass(self):
        opened, row, faults = matrix.veil_faults(veiled([0, 0.25, 0.24, 0.20, 0.15, 0.10, 0.05, 0.01, 0]))
        self.assertAlmostEqual(opened, 0.25)
        self.assertEqual(row["at"], "f1")
        self.assertEqual(faults, [])

    def test_an_opening_over_the_cap_fails(self):
        _, _, faults = matrix.veil_faults(veiled([0, 0.43, 0.40, 0.36]))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in faults], [("opening", 0.43)])

    def test_the_let_out_of_a_large_move_is_not_a_flick(self):
        # the LAS-5 Scythe's 0.32 m let out rest to rest over 300 ms at 30 frames a second: 0.053 m in its steepest frame
        moves = [0, 0.32] + [0.32 * (1 - (x * x * (3 - 2 * x))) for x in (i * 33.3 / 300 for i in range(1, 10))] + [0]
        self.assertGreater(max(abs(a - b) for a, b in zip(moves[1:], moves[2:])), matrix.VEIL_STEP_M)
        self.assertEqual(matrix.veil_faults(veiled(moves, dt=33.3))[2], [])
        # the same move taken back in three frames is
        self.assertEqual(len(matrix.veil_faults(veiled([0, 0.32, 0.22, 0.11, 0], dt=33.3))[2]), 3)

    def test_a_veil_that_falls_to_nothing_in_one_frame_fails(self):
        _, _, faults = matrix.veil_faults(veiled([0, 0.248, 0, 0]))
        self.assertEqual([(f["what"], round(f["m"], 3), f["at"]) for f in faults], [("step", 0.248, "f2")])

    def test_the_native_view_cutting_a_move_fails(self):
        _, _, faults = matrix.veil_faults(veiled([0, 0.196, 0.15, 0, 0], kinds=[6, 4, 4, 6, 6]))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in faults], [("cut", 0.15)])

    def test_a_let_in_that_grows_too_fast_fails(self):
        _, _, faults = matrix.veil_faults(veiled([0, 0.05, 0.15, 0.15]))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in faults], [("step", 0.10)])

    def test_the_first_veiled_frame_after_a_pause_is_an_opening(self):
        opened, _, faults = matrix.veil_faults(veiled([0.2, 0.2], dt=200.0))
        self.assertAlmostEqual(opened, 0.2)
        self.assertEqual(faults, [])

    def test_the_flick_of_soak_60_fails(self):
        j = matrix.judge(cut(FLICK))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in j["veil_faults"]], [("step", 0.248)])
        self.assertAlmostEqual(j["veil_open_m"], 0.248, places=3)
        self.assertFalse(j["pass"])
        self.assertIn("veil opened 0.248 m", matrix.describe(j))

    def test_the_melee_of_soak_62_fails_on_the_step_and_on_the_cut(self):
        j = matrix.judge(cut(MELEE))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in j["veil_faults"]], [("step", 0.115), ("cut", 0.081)])
        self.assertFalse(j["pass"])

    def test_a_swap_on_foot_stays_inside_the_bound(self):
        j = matrix.judge(cut(FOOT_SWAP))
        self.assertEqual(j["veil_faults"], [])
        self.assertAlmostEqual(j["veil_open_m"], 0.191, places=3)


class Phases(unittest.TestCase):
    """Round 49: the phases that have to say they were played, and the body stood up before a dive."""
    NEW = ("prone-swap", "aim-swap", "stand-up-supine", "stand-up-prone-head")

    def test_the_new_phases_are_scripts_the_driver_reads(self):
        import drive
        for name in self.NEW:
            script, settle = matrix.PHASES[name]
            lines, seconds = drive.events(script)
            self.assertTrue(lines and seconds > 2.0, name)
            self.assertNotIn("look", script)   # no mouse: a turn at a change of camera fails them
            self.assertIn(name, matrix.NEEDS)
        self.assertNotIn("prone-swap", matrix.DRIVEN)

    def test_a_dive_phase_without_a_dive_fails(self):
        # the feel.59 matrix's "dive-back", 19:34:44 to 19:34:50: a stand-up and a walk back, judged as a dive
        stood = ["19:34:45.100 Motion: action=idle placement=idle>imported(own)", "19:34:46.000 Lock: +30 frames held (+0 in another camera, kind now 6), +0 missed (kind 0, helper error 0), largest kick 0.0"]
        for name in ("dive", "dive-back"):
            self.assertEqual(len(matrix.unplayed(name, stood)), 1)
            judged = matrix.judge_played(name, stood, {"pass": True, "notes": ["x"]})
            self.assertFalse(judged["pass"])
            self.assertFalse(judged["played"])
            self.assertTrue(judged["notes"][0].startswith("NOT PLAYED: no dive in the log"))
            dived = stood + ["19:34:45.300 Motion: action=dive placement=idle>imported(own)"]
            self.assertEqual(matrix.judge_played(name, dived, {"pass": True, "notes": []}), {"pass": True, "notes": [], "played": True})
        self.assertEqual(matrix.unplayed("walk", stood), [])

    def test_the_dive_swap_of_soak_60_counts_as_played_for_what_it_shows(self):
        lines = cut(DIVE)   # a dive, a swap, the head camera
        self.assertEqual(matrix.unplayed("dive", lines), [])
        self.assertEqual(matrix.unplayed("stand-up-supine", lines), [])          # (the log does not tell lying on the back from lying prone)
        flying = [line for line in lines if " Motion: action=prone" not in line]   # the same stretch cut before the landing
        self.assertEqual([n[:35] for n in matrix.unplayed("stand-up-supine", flying)], ["NOT PLAYED: the body never lay down"])
        self.assertEqual(len(matrix.unplayed("aim-swap", lines)), 1)             # no full aim
        self.assertEqual(len(matrix.unplayed("prone-swap", cut(FOOT_SWAP))), 1)  # a swap on foot: the head camera, never prone

    def test_the_body_is_stood_up_before_a_dive_and_not_before_a_walk(self):
        calls = []
        real = matrix.dive.stand_up
        matrix.dive.stand_up = lambda device, tail: calls.append(device) or {"before": "prone", "presses": ["key z"], "after": "idle", "standing": True}
        try:
            with tempfile.NamedTemporaryFile(dir=os.environ.get("TMPDIR")) as log:
                self.assertIsNone(matrix.stand_first("device", "walk", log.name))
                self.assertEqual(calls, [])
                for name in ("dive", "dive-back", "prone-swap", "stand-up-supine", "stand-up-prone-head"):
                    self.assertTrue(matrix.stand_first("device", name, log.name)["standing"])
                self.assertEqual(len(calls), 5)
        finally:
            matrix.dive.stand_up = real


def let_out(ms):
    """src/camera_helper.h tfp_camera_veil_let_out: what is left of the veil's move `ms` after its frame."""
    x = min(1.0, max(0.0, ms / 300.0))
    return 1 - x * x * (3 - 2 * x)


def veil_kept(lines):
    """The rows of `lines` (trace.frames) with the veil's move as round 49 keeps it (src/camera_helper.c): a veiled
    frame (veil_head= on its line) whose own move is less than what is left of the move kept from an earlier frame,
    less what the eye has gained on the head since, shows what is left; the let-out after the veil is of the move
    kept. The eye before the veil, the head and the veil's own moves are the logged ones."""
    ahead = {}
    for line in lines:
        tick, head = matrix._TICK.search(line), matrix._VEIL_HEAD.search(line)
        if " Trace: " in line and tick and head:
            ahead[float(tick[1])] = -float(head[1])   # the eye in front of the head, before the veil
    out, kept, before = [], None, None   # kept: (the move, its tick, the eye ahead of the head then)
    for row in trace.frames(lines):
        row = dict(row)
        near = before is not None and 0 < row["tick"] - before["tick"] <= matrix.GAP_MS
        if row["kind"] != 4 or not near:
            kept = None
        own = row["veil"] or [0.0, 0.0, 0.0]
        if row["kind"] == 4 and row["tick"] in ahead:   # a veiled frame
            left = 0.0
            if kept:
                left = matrix._length(kept[0]) * let_out(row["tick"] - kept[1]) - max(0.0, ahead[row["tick"]] - kept[2])
            if kept and left > matrix._length(own) + 1e-4:
                row["veil"] = [c * left / matrix._length(kept[0]) for c in kept[0]]
            else:
                kept = (own, row["tick"], ahead[row["tick"]]) if matrix._length(own) > 0 else None
        elif row["kind"] == 4 and kept:
            w = let_out(row["tick"] - kept[1])
            row["veil"] = [c * w for c in kept[0]] if w > 0 else None
            kept = kept if w > 0 else None
        out.append(row)
        before = row
    return out


class VeilKept(unittest.TestCase):
    """Round 49, the veil's dropped move, on the logged rows."""

    def shown(self, rows):
        """The changes of the shown eye's step at or over the limit, in the frames the veil had a part in."""
        return [round(f["shown"], 3) for f in matrix.measure(rows) if f["veil"] > 0 and f["shown"] >= matrix.POP_M]

    def test_the_rule_gives_the_log_back_where_no_move_was_dropped(self):
        lines = cut(FOOT_SWAP)
        for a, b in zip(trace.frames(lines), veil_kept(lines)):
            self.assertLess(matrix._length(matrix._sub(a["veil"] or [0, 0, 0], b["veil"] or [0, 0, 0])), 0.002)   # the let-out as the build made it

    def test_the_flick_of_soak_60_is_let_out(self):
        lines = cut(FLICK)
        logged, kept = trace.frames(lines), veil_kept(lines)
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in matrix.veil_faults(logged)[2]], [("step", 0.248)])
        self.assertEqual(matrix.veil_faults(kept)[2], [])
        self.assertEqual(self.shown(logged), [0.248, 0.496, 0.248])   # forward 0.248 m and back again in the next frame, the root still
        self.assertEqual(self.shown(kept), [0.248, 0.256])            # the opening frame alone (its going and its stopping): no way back ...
        moves = [round(matrix._length(r["veil"]), 3) for r in kept if r["veil"]]
        self.assertEqual(moves[:3], [0.248, 0.240, 0.217])            # ... the move let out over 300 ms
        self.assertTrue(all(a >= b for a, b in zip(moves, moves[1:])))

    def test_the_smaller_second_move_of_soak_62_is_let_out(self):
        lines = cut(UNDER)
        logged, kept = trace.frames(lines), veil_kept(lines)
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in matrix.veil_faults(logged)[2]], [("step", 0.065)])
        self.assertEqual(matrix.veil_faults(kept)[2], [])
        moves = [round(matrix._length(r["veil"]), 3) for r in kept if r["veil"]]
        self.assertEqual(moves[:2], [0.120, 0.116])


class Dive(unittest.TestCase):
    """Round 49, in a dive (src/camera_helper.h tfp_camera_set_dive): the Stable style's eye damping is not run in the
    head camera's frames of a dive. The logged rows with the damping taken off those frames (the lag was built in them:
    the leave is 470 ms into the dive), through this tree's continuity (tools/harness/replay.py)."""
    MOTION = re.compile(r"^(\d+):(\d+):(\d+)\.(\d+) Motion: action=([\w-]+)")

    def replayed(self, told):
        import replay
        lines = cut(DIVE)
        spans, start = [], None
        for line in lines:
            m = self.MOTION.match(line)
            if m:
                ms = ((int(m[1]) * 60 + int(m[2])) * 60 + int(m[3])) * 1000 + int(m[4])
                if m[5] == "dive":
                    start = ms
                elif start is not None:
                    spans.append((start, ms))
                    start = None
        self.assertEqual(len(spans), 1)   # 19:38:33.398 to 34.569
        rows, changed = replay.read(lines), 0
        for row in rows:
            if told and row["kind"] == 4 and row["source"] == "head" and row.get("damp") and any(a <= row["stamp"] <= b for a, b in spans):
                row["wanted"] = [w - d for w, d in zip(row["wanted"], row["damp"])]
                row["damp"] = [0.0, 0.0, 0.0]
                changed += 1
        out, given = replay.replay(rows, logged="85f18b8")
        self.assertTrue(given)
        floated = max(held for _, held in trace.held_offsets([dict(r) for r in out], eye="new_eye"))
        judged = matrix.measure([dict(r, written=r["at"], eye=r["new_eye"], carried=r["new_carried"], veil=None) for r in out])
        left = min(r["tick"] for r in out if r["kind"] == 4)   # from the leave on (the launch before it is the game's own, 0.073 m)
        judged = [f for f in judged if f["tick"] >= left]
        return changed, floated, max(f["beyond"] for f in judged), sum(f["beyond"] >= matrix.POP_M for f in judged)

    def test_the_float_after_the_landing(self):
        changed, floated, step, over = self.replayed(False)
        self.assertEqual(changed, 0)
        self.assertAlmostEqual(floated, 0.355, delta=0.002)   # as logged
        self.assertAlmostEqual(step, 0.065, delta=0.002)
        self.assertEqual(over, 1)
        changed, floated, step, over = self.replayed(True)
        self.assertEqual(changed, 11)                      # the head camera's frames of the dive
        self.assertAlmostEqual(floated, 0.142, delta=0.003)
        self.assertLess(floated, trace.FLOAT_M)
        # Not all gain: the two head-camera frames over the limit go (0.065, 0.054 to 0.035, 0.030) and the second
        # native frame of the entry comes in their place, larger (0.043 to 0.083): one frame over the limit either way.
        self.assertAlmostEqual(step, 0.083, delta=0.002)
        self.assertEqual(over, 1)


class Orphan(unittest.TestCase):
    """The orphaned native frames' eye (src/camera_helper.c orphan_head) stood 0.18 m from the head along the camera
    node's world direction (the rows' view=), which was not the view. The same rows with the eye along the direction
    of the frames either side (round 49: the rig eye's axes times the argument): the first head-camera frame wants the
    point the last orphaned frame had. Arithmetic on the logged rows; the helper itself is run in
    tests/camera_turn_helper_test.cpp."""
    FORWARD = 0.18   # c.forward
    SOURCE = re.compile(r" kind=(\d+) .*? wanted=\(([-\d. ]+)\).*? source=(\w+) .*? tick=([\d.]+) .*? view=\(([-\d. ]+)\)")

    def leave(self, name):
        """(the last orphaned row, the head-camera row after it): each (wanted eye, view)."""
        rows = []
        for line in cut(name):
            m = self.SOURCE.search(line) if " Trace: " in line else None
            if m:
                rows.append((float(m[4]), int(m[1]), m[3], [float(x) for x in m[2].split()], [float(x) for x in m[5].split()]))
        rows.sort()
        orphans = [r for r in rows if r[1] == 6 and r[2] == "head"]
        head = next(r for r in rows if r[1] == 4 and r[2] == "head" and r[0] > orphans[-1][0])
        self.assertLess(head[0] - orphans[-1][0], 5.0)   # the two calls of one game frame: the head has not moved between them
        return orphans[-1][3:], head[3:]

    def level(self, v):
        n = (v[0] ** 2 + v[1] ** 2) ** 0.5
        return [v[0] / n, v[1] / n, 0.0]

    def check(self, name, degrees, jump):
        (wanted, wrong), (after, view) = self.leave(name)
        self.assertAlmostEqual(matrix._degrees(self.level(wrong), self.level(view)), degrees, delta=0.6)
        self.assertAlmostEqual(matrix._length(matrix._sub(after, wanted)), jump, delta=0.002)   # as logged: the jump the leave made
        along = [w + self.FORWARD * (t - o) for w, t, o in zip(wanted, self.level(view), self.level(wrong))]
        self.assertLess(matrix._length(matrix._sub(after, along)), 0.001)   # along the view: the same point, to a millimetre

    def test_the_melee_of_soak_62(self):
        self.check(MELEE, 117.2, 0.307)

    def test_the_stim_of_soak_60(self):
        self.check(STIM, 90.0, 0.254)


class Carried(unittest.TestCase):
    """THE CARRY (src/camera_turn.h) run again over the logged frames by tests/camera_turn_replay.c: the rows' turn= is
    the game's own direction on the build they were logged on, which carried nothing."""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR"))
        cls.tool = str(Path(cls.dir.name) / "camera_turn_replay")
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-ffp-contract=off", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"),
                        str(ROOT / "tests/camera_turn_replay.c"), "-lm", "-o", cls.tool], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def replay(self, name):
        """(the frames as logged, the same frames as they are shown with the carry; each has "carried" and "began")."""
        logged = trace.frames(cut(name))
        text = "".join("%.3f %d %s\n" % (r["tick"], r["kind"], " ".join(repr(x) for x in (r["turn"] or [0, 0, 0]))) for r in logged)
        out = subprocess.run([self.tool], input=text, capture_output=True, text=True, check=True).stdout.splitlines()
        self.assertEqual(len(out), len(logged))
        shown = []
        for row, line in zip(logged, out):
            x, y, z, carried, began = (float(v) for v in line.split())
            shown.append(dict(row, turn=[x, y, z] if row["turn"] else None, carried=carried, began=began))
        return logged, shown

    def largest_turn(self, rows):
        return max(matrix._degrees(a["turn"], b["turn"]) for a, b in zip(rows, rows[1:])
                   if a["turn"] and b["turn"] and 0 < b["tick"] - a["tick"] <= matrix.GAP_MS)

    def test_the_dive_swap_of_soak_60(self):
        logged, shown = self.replay(DIVE_SWAP)
        self.assertEqual([round(t["degrees"], 1) for t in matrix.turn_snaps(logged) if t["degrees"] > matrix.TURN_DEG], [29.0, 39.5])
        self.assertEqual([t for t in matrix.turn_snaps(shown) if t["degrees"] > matrix.TURN_DEG], [])
        self.assertLess(max(t["degrees"] for t in matrix.turn_snaps(shown)), 0.7)   # the frames of the two changes: the game's own 0.6 of a frame
        # (the carry is the whole rotation between the two views, rebuilt level from the forward axes: 39.7 for 39.5)
        self.assertEqual([round(r["began"]) for r in shown if r["began"]], [29, 40])
        # no frame turns faster than 200 degrees a second (6.9 degrees at 34.5 ms, with the game's own 0.7)
        self.assertGreater(self.largest_turn(logged), 39.0)
        self.assertLess(self.largest_turn(shown), 200 * 0.0345 + 0.8)
        # and it ends at the game's direction: 29 degrees in 218 ms, 39.5 in 296
        first = next(i for i, r in enumerate(shown) if r["began"])
        self.assertTrue(all(r["carried"] == 0 and r["turn"] == l["turn"] for r, l in zip(shown, logged)
                            if 230 < r["tick"] - shown[first]["tick"] < 360))
        self.assertEqual(shown[-1]["turn"], logged[-1]["turn"])

    def test_the_prone_primary_back_of_soak_62(self):
        logged, shown = self.replay(PRONE_BACK)
        self.assertEqual([round(t["degrees"], 1) for t in matrix.turn_snaps(logged) if t["degrees"] > matrix.TURN_DEG], [72.7])
        self.assertEqual([t for t in matrix.turn_snaps(shown) if t["degrees"] > matrix.TURN_DEG], [])
        self.assertEqual([round(r["began"]) for r in shown if r["began"]], [74])   # 72.7 between the forward axes
        self.assertLess(self.largest_turn(shown), 200 * 0.0345 + 1.7)   # the game's own turn there is up to 1.5 degrees a frame
        first = next(i for i, r in enumerate(shown) if r["began"])
        carried = [r["carried"] for r in shown[first:]]
        self.assertTrue(all(a >= b for a, b in zip(carried, carried[1:])))   # monotone
        self.assertTrue(all(r["carried"] == 0 for r in shown if r["tick"] - shown[first]["tick"] > 560))

    def test_a_swap_on_foot_is_unchanged(self):
        logged, shown = self.replay(FOOT_SWAP)
        self.assertEqual(len(matrix.turn_snaps(logged)), 2)
        self.assertTrue(all(r["carried"] == 0 and r["began"] == 0 for r in shown))
        self.assertEqual([r["turn"] for r in shown], [r["turn"] for r in logged])


if __name__ == "__main__":
    unittest.main()
