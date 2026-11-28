"""Round 49: the game's own stance ramp (tools/harness/stance.py), named by the judge and not counted.

The evidence is tests/reference/stance/feel59-prone-stand.log: the Trace, Motion, Steady, Lock and Smooth lines of
19:34:38.3 to 38.9 in ~/.cache/tfp-soak-60-A.modlog (the harness run of 2026-11-27 on feel.59, phase "prone": a
crawl, and z pressed while the body was still slowing). The phase read 0.066 m at 19:34:38.419 and failed."""
import math
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import matrix  # noqa: E402
import stance  # noqa: E402
import trace  # noqa: E402

LOG = ROOT / "tests/reference/stance/feel59-prone-stand.log"
AT = "19:34:38.419"


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def carrying(line, metres):
    """The Trace line with `metres` carried (the eye left where it is: the wanted eye is then not the eye shown)."""
    return re.sub(r"correction=[\d.]+", f"correction={metres:.4f}", line)


class Evidence(unittest.TestCase):
    """What the rows say, before any class: whose motion the 0.066 m is."""

    @classmethod
    def setUpClass(cls):
        cls.lines = LOG.read_text().splitlines()
        cls.rows = trace.frames(cls.lines)
        cls.i = next(i for i, r in enumerate(cls.rows) if r["at"] == AT)

    def test_the_filter_did_nothing(self):
        for row in self.rows:
            self.assertEqual((row["kind"], row["looked"], row["carried"]), (6, 0, 0.0), row["at"])
            self.assertIn(row["taken"], ("-", "root"), row["at"])
            self.assertEqual(row["wanted"], row["eye"], row["at"])
            self.assertIsNone(row["veil"], row["at"])

    def test_the_mods_hold_of_the_rig_stood_still(self):
        steady = [line for line in self.lines if " Steady: " in line]
        self.assertEqual(len(steady), 1)
        self.assertIn("held at 0.0561 to 0.0561 m", steady[0])
        self.assertIn("by -0.0794 to -0.0794 m", steady[0])

    def test_the_eyes_step_changed_by_0_066_and_the_roots_by_0_012_the_frame_before(self):
        rows, i = self.rows, self.i
        eye = sub(sub(rows[i]["eye"], rows[i - 1]["eye"]), sub(rows[i - 1]["eye"], rows[i - 2]["eye"]))
        self.assertAlmostEqual(math.hypot(*eye), 0.0664, places=3)
        # the root goes on slowing, evenly: its step 0.0309, 0.0187, 0.0126, 0.0080 m. In the frame itself its change
        # is 0.006 m; the frame before, 0.012 m: the rig eye is the root a frame late
        steps = [math.dist(rows[k]["root"], rows[k - 1]["root"]) for k in range(i - 2, i + 2)]
        for got, want in zip(steps, (0.0309, 0.0187, 0.0126, 0.0080)):
            self.assertAlmostEqual(got, want, places=3)
        before = sub(sub(rows[i - 1]["root"], rows[i - 2]["root"]), sub(rows[i - 2]["root"], rows[i - 3]["root"]))
        self.assertAlmostEqual(math.hypot(*before), 0.0123, places=3)

    def test_against_the_root_a_frame_late_the_eye_goes_at_one_speed_from_its_first_frame(self):
        # the eye's step less the root's step of the frame before: under 0.005 m while the body crawls, then 0.0534 m a
        # frame, the same vector to 0.0002 m, in the frame judged and the four after it
        rows, i = self.rows, self.i
        for k in range(i - 3, i):
            self.assertLess(math.hypot(*stance.against_root(rows, k)), 0.005, rows[k]["at"])
        first = stance.against_root(rows, i)
        for k in range(i, i + 5):
            step = stance.against_root(rows, k)
            self.assertAlmostEqual(math.hypot(*step), 0.0534, places=3, msg=rows[k]["at"])
            self.assertLess(math.dist(step, first), 0.0002, rows[k]["at"])
        # so what is left of the 0.066 m with the root's 0.012 m taken off is the ramp's own first frame, under POP_M
        left, root = stance.ramp(rows, i, 0.0664, matrix.POP_M)
        self.assertAlmostEqual(left, 0.0542, places=3)
        self.assertAlmostEqual(root, 0.0123, places=3)
        self.assertLess(left, matrix.POP_M)


class Judge(unittest.TestCase):
    def setUp(self):
        self.lines = LOG.read_text().splitlines()
        self.held = "19:34:38.619 Lock: +30 frames held (+0 in another camera, kind now 6), +0 missed (kind 0, helper error 0), largest kick 0.0 degrees"
        self.assertIn(self.held, self.lines)

    def frame(self, lines, **more):
        return next(f for f in matrix.measure(trace.frames(lines), **more) if f["at"] == AT)

    def test_the_rows_as_written_are_the_stance_ramps_and_not_counted(self):
        f = self.frame(self.lines)
        self.assertEqual(f["whose"], "stance")
        self.assertAlmostEqual(f["shown"], 0.0664, places=3)   # what the eye did stays in view
        self.assertAlmostEqual(f["body"], 0.0123, places=3)    # the root's change taken off, a frame after the root
        self.assertEqual(f["after"], 1)
        self.assertAlmostEqual(f["bare"], 0.0542, places=3)    # what is judged against POP_M
        result = matrix.judge(self.lines)
        self.assertLess(result["largest_jump_m"], matrix.POP_M)
        self.assertAlmostEqual(result["largest_jump_m"], 0.0542, places=3)
        self.assertTrue(result["pass"])
        self.assertEqual(matrix.whose_counts(result["pops"])["stance"], 1)
        self.assertIn("the game's stance ramp", "\n".join(matrix.pop_lines(result)))
        self.assertIn("stance", matrix.describe(result))

    def test_without_the_class_it_is_the_eyes_own_and_fails(self):
        f = self.frame(self.lines, stance=False)
        self.assertEqual(f["whose"], "own")
        self.assertAlmostEqual(f["bare"], 0.0664, places=3)

    def test_a_reload_phase_is_judged_without_it(self):
        result = matrix.judge(self.lines, reload=True)
        self.assertAlmostEqual(result["largest_jump_m"], 0.0664, places=3)
        self.assertFalse(result["pass"])

    def test_with_0_03_m_carried_it_still_fails(self):
        for at in (AT, "19:34:38.451"):   # in the frame judged, or in one of the frames the ramp is read from
            lines = [carrying(line, 0.03) if line.startswith(at) else line for line in self.lines]
            result = matrix.judge(lines)
            self.assertGreaterEqual(result["largest_jump_m"], matrix.POP_M, at)
            self.assertFalse(result["pass"], at)
            self.assertNotEqual(self.frame(lines)["whose"], "stance", at)

    def test_where_jumps_were_looked_for_or_one_was_taken_up_it_still_fails(self):
        for old, new in ((" looked=0 ", " looked=1 "), (" taken=- ", " taken=jump ")):
            lines = [line.replace(old, new) if line.startswith(AT) else line for line in self.lines]
            self.assertEqual(self.frame(lines)["whose"], "own", new)
            self.assertFalse(matrix.judge(lines)["pass"], new)

    def moved(self, at, dy):
        """The eye (and the wanted eye with it) of the frame at `at` moved by dy along the body: not the ramp's speed."""
        def shift(line):
            return re.sub(r"\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", lambda m: f"({m[1]} {float(m[2]) + dy:.5f} {m[3]})", line, count=2)
        return [shift(line) if line.startswith(at) else line for line in self.lines]

    def test_an_eye_that_does_not_go_on_at_the_ramps_speed_still_fails(self):
        # the next frame 0.01 m short: 1.3 m/s, then 1.9
        lines = self.moved("19:34:38.451", 0.01)
        self.assertEqual(self.frame(lines)["whose"], "own")
        self.assertFalse(matrix.judge(lines)["pass"])

    def test_a_larger_step_in_that_frame_still_fails(self):
        # 0.02 m further back in the frame judged: what is left is over POP_M whatever the root did
        lines = self.moved(AT, -0.02)
        self.assertNotEqual(self.frame(lines)["whose"], "stance")
        self.assertFalse(matrix.judge(lines)["pass"])

    def test_what_is_left_is_judged_against_the_limit(self):
        # the eye 0.01 m further on from the frame before the ramp: the ramp runs as it did, but what is left of the
        # frame's 0.076 m with the root's 0.012 m taken off is 0.064 m, over POP_M
        def shift(line):
            return re.sub(r"\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", lambda m: f"({m[1]} {float(m[2]) + 0.01:.5f} {m[3]})", line, count=2)
        lines = [shift(line) if " Trace: " in line and " tick=" in line and float(line.split(" tick=")[1].split()[0]) > 482783300 else line for line in self.lines]
        f = self.frame(lines)
        self.assertEqual(f["whose"], "own")
        self.assertAlmostEqual(f["bare"], 0.0764, places=3)
        self.assertFalse(matrix.judge(lines)["pass"])
        import stance as rule
        rows = trace.frames(lines)
        i = next(k for k, r in enumerate(rows) if r["at"] == AT)
        self.assertIsNone(rule.ramp(rows, i, f["shown"], matrix.POP_M))
        self.assertAlmostEqual(rule.ramp(rows, i, f["shown"], 1.0)[0], 0.0643, places=3)   # the ramp itself is found

    def test_the_limits_are_as_they_were(self):
        self.assertEqual((matrix.POP_M, matrix.OWN_M, trace.FLOAT_M, matrix.SLIDE_M), (0.06, 0.03, 0.26, 0.05))
        self.assertLessEqual(stance.CAP_M, 0.10)


class Narrow(unittest.TestCase):
    """Round 49g: what the independent check of round49d found open in the class (results of 2026-11-27, findings 3
    to 5): the root's change was not held under the limit, and four mutants of tools/harness/stance.py passed."""

    def setUp(self):
        self.lines = LOG.read_text().splitlines()
        self.rows = trace.frames(self.lines)
        self.i = next(i for i, r in enumerate(self.rows) if r["at"] == AT)

    def ticked(self, line):
        return float(line.split(" tick=")[1].split()[0]) if " Trace: " in line and " tick=" in line else None

    def edited(self, change, first, last=None):
        """The Trace lines of the frames first..last (indices into self.rows; last None: to the end) through `change`."""
        lo = self.rows[first]["tick"]
        hi = self.rows[last]["tick"] if last is not None else float("inf")
        return [change(line) if self.ticked(line) is not None and lo <= self.ticked(line) <= hi else line for line in self.lines]

    def test_a_root_change_at_or_over_the_limit_is_not_argued_off(self):
        # the root, from the frame before the ramp on, and the eye a frame later, 0.065 m to the side: the root's own
        # change of step is then 0.066 m, the eye shows 0.093 m, the ramp runs as it did and 0.054 m is left. A change
        # of the root that size is the body's lunge (measure's own rule, which wants the wanted eye to make it to half)
        # or nothing: this class is for the few centimetres of a body slowing down
        def side(which):
            return lambda line: re.sub(which + r"\(([-\d.]+) ", lambda m: f"{which}({float(m[1]) + 0.065:.5f} ", line)
        lines = self.edited(side("root="), self.i - 1)
        self.lines = lines
        lines = self.edited(lambda line: side("wanted=")(side("eye_world=")(line)), self.i)
        rows = trace.frames(lines)
        f = next(f for f in matrix.measure(rows) if f["at"] == AT)
        self.assertAlmostEqual(f["shown"], 0.093, places=2)
        self.assertLess(f["shown"], stance.CAP_M)
        before = sub(sub(rows[self.i - 1]["root"], rows[self.i - 2]["root"]), sub(rows[self.i - 2]["root"], rows[self.i - 3]["root"]))
        self.assertAlmostEqual(math.hypot(*before), 0.067, places=3)
        self.assertGreaterEqual(math.hypot(*before), matrix.POP_M)
        self.assertAlmostEqual(math.dist(stance.against_root(rows, self.i), stance.against_root(rows, self.i - 1)), 0.0542, places=3)
        self.assertIsNone(stance.ramp(rows, self.i, f["shown"], matrix.POP_M))
        self.assertEqual(f["whose"], "own")
        self.assertFalse(matrix.judge(lines)["pass"])

    def test_a_steady_eye_at_another_speed_is_not_the_ramp(self):
        # 0.008 m a frame slower from the ramp's first frame on: 1.36 m/s, steady. (What is left is then under the
        # limit too, so only the speed says it is not the game's ramp.)
        def slower(line, n=[0]):
            n[0] += 1
            return re.sub(r"\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", lambda m: f"({m[1]} {float(m[2]) + 0.008 * n[0]:.5f} {m[3]})", line, count=2)
        rows = trace.frames(self.edited(slower, self.i))
        step = stance.against_root(rows, self.i)
        self.assertAlmostEqual(math.hypot(*step) * 1000.0 / (rows[self.i]["tick"] - rows[self.i - 1]["tick"]), 1.36, places=1)
        self.assertLess(math.dist(step, stance.against_root(rows, self.i + 3)), stance.STEADY_M)
        self.assertIsNone(stance.ramp(rows, self.i, 0.0664, matrix.POP_M))
        self.assertIsNotNone(stance.ramp(self.rows, self.i, 0.0664, matrix.POP_M))

    def test_a_frame_of_another_camera_or_one_the_veil_moved_is_not_the_ramps(self):
        for old, new in ((" kind=6 ", " kind=4 "), (" veil=0 ", " veil=(0.00000 0.00000 0.00050) ")):
            for k in (self.i + 1, self.i + 3, self.i - 1):
                self.assertIn(old, self.lines[0] + " ".join(l for l in self.lines if " Trace: " in l), new)
                lines = self.edited(lambda line: line.replace(old, new), k, k)
                rows = trace.frames(lines)
                self.assertEqual(len(rows), len(self.rows))
                self.assertIsNone(stance.ramp(rows, self.i, 0.0664, matrix.POP_M), (new, k))

    def test_frames_too_far_apart_are_not_read_as_one_run(self):
        # the frames before the root's change 100 ms earlier: the root's change of the frame before would be read
        # across a pause of the trace
        def earlier(line):
            return re.sub(r" tick=([\d.]+)", lambda m: f" tick={float(m[1]) - 100.0:.3f}", line)
        rows = trace.frames(self.edited(earlier, 0, self.i - 3))
        self.assertEqual([r["at"] for r in rows], [r["at"] for r in self.rows])
        self.assertGreater(rows[self.i - 2]["tick"] - rows[self.i - 3]["tick"], stance.GAP_MS)
        self.assertLessEqual(rows[self.i - 2]["tick"] - rows[self.i - 3]["tick"], 8000)
        self.assertIsNone(stance.ramp(rows, self.i, 0.0664, matrix.POP_M))


class Soak(unittest.TestCase):
    def test_a_reload_action_is_judged_without_the_class(self):
        # round 49g: matrix.py passed reload= for its own phases only, and the soak judged every action with the class
        import soak
        lines = LOG.read_text().splitlines()
        for name, script in (("reload", matrix.PHASES["reload"][0]), ("pistol-reload", "key r"), ("ammo-check", "hold r 0.8"),
                             ("overlap", "key r; wait 0.5; key 2"), ("overlap", "key f; wait 0.2; key f; wait 0.2; key r")):
            self.assertTrue(matrix.reloading(name, script), script)
            self.assertAlmostEqual(soak.judged(name, script, lines)["largest_jump_m"], 0.0664, places=3, msg=script)
        for name, script in (("prone", matrix.PHASES["prone"][0]), ("aim", matrix.PHASES["aim"][0]), ("overlap", "key v; wait 0.6; click right 0.8"),
                             ("stratagem-menu", matrix.PHASES["stratagem-menu"][0])):
            self.assertFalse(matrix.reloading(name, script), script)
            self.assertAlmostEqual(soak.judged(name, script, lines)["largest_jump_m"], 0.0542, places=3, msg=script)
        self.assertEqual(matrix.judge(lines, stance=False)["stance_frames"], 0)

    def test_every_caller_leaves_the_class_out_where_the_script_presses_the_reload_key(self):
        # the independent check's minor finding 3: matrix.py passed reload=name in RELOADS while matrix.reloading()
        # said it covered the ammo check, and phases_levers.py judged its phases that end on a reload with the class
        import phases_levers
        lines = LOG.read_text().splitlines()
        for name in ("reload", "pistol-reload", "ammo-check", "aim-scope-zoom"):
            script = matrix.PHASES[name][0]
            self.assertTrue(matrix.reloading(name, script), name)
            self.assertEqual(matrix.phase_judge(name, script, lines)["stance_frames"], 0, name)
            self.assertAlmostEqual(matrix.phase_judge(name, script, lines)["largest_jump_m"], 0.0664, places=3, msg=name)
        for name in ("prone", "aim", "stratagem-menu"):
            script = matrix.PHASES[name][0]
            self.assertFalse(matrix.reloading(name, script), name)
            self.assertEqual(matrix.phase_judge(name, script, lines)["stance_frames"], 1, name)
        for name in ("foregrip-liberator", "foregrip-adjudicator", "support-weapon"):
            script = phases_levers.PHASES[name][0]
            self.assertTrue(matrix.reloading(name, script), name)
            self.assertEqual(phases_levers.judge_lever(name, script, lines)["stance_frames"], 0, name)
        for name, (script, _) in phases_levers.PHASES.items():
            self.assertEqual(phases_levers.judge_lever(name, script, lines)["stance_frames"], 0 if matrix.reloading(name, script) else 1, name)
        # and the matrix keeps its other arguments: the reload's slide, the vault's landing, the look's turns
        self.assertIn("slide_m", matrix.phase_judge("reload", matrix.PHASES["reload"][0], lines))
        self.assertNotIn("slide_m", matrix.phase_judge("ammo-check", matrix.PHASES["ammo-check"][0], lines))
        self.assertIn("landing_m", matrix.phase_judge("jump-vault", matrix.PHASES["jump-vault"][0], lines))
        self.assertNotIn("landing_m", matrix.phase_judge("prone", matrix.PHASES["prone"][0], lines))

    def test_the_summary_counts_them_apart(self):
        import soak
        result = matrix.judge(LOG.read_text().splitlines())
        self.assertEqual(soak.unlisted(result["pops"], set()), [])
        text = soak.counted(result["pops"])
        self.assertIn("0 frames flagged", text)
        self.assertIn("1 the game's stance ramp, not counted", text)


if __name__ == "__main__":
    unittest.main()
