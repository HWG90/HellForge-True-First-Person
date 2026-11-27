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


class Soak(unittest.TestCase):
    def test_the_summary_counts_them_apart(self):
        import soak
        result = matrix.judge(LOG.read_text().splitlines())
        self.assertEqual(soak.unlisted(result["pops"], set()), [])
        text = soak.counted(result["pops"])
        self.assertIn("0 frames flagged", text)
        self.assertIn("1 the game's stance ramp, not counted", text)


if __name__ == "__main__":
    unittest.main()
