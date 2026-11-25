"""Round 39: the matrix's reload phases judged on the traced eye, with the eye's slide along the view summed across a
reload's edges (tools/harness/matrix.py reload_slide). The feel.44 matrix's "reload" passed while every reload slid the
eye 0.21 to 0.26 m back and forward again: it judged single-frame jumps only, and each frame of the slide is under them.

The user's reload of 2026-11-25 (0.6.0-feel.56, tests/reference/reload/) is judged as it was (main: fails) and as the
hold of src/reload_hold.h puts it (the C replay of tests/reload_hold_test.c --rewrite: passes). Needs a C compiler for
the second; without one that test is skipped."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import matrix  # noqa: E402

WINDOW = ROOT / "tests/reference/reload/feel56-L6985-7100.log"
LOCK = "18:47:04.275 Lock: +71 frames held (+0 in another camera, kind now 6), +0 missed (kind 0, helper error 0), largest kick 0.7 degrees"


def window():
    return WINDOW.read_text().splitlines()


def trace(stamp_ms, tick, eye, root=(0.0, 0.0, 0.0), view=(0.0, 1.0, 0.0)):
    stamp = "22:00:%02d.%03d" % (stamp_ms // 1000, stamp_ms % 1000)
    return ("%s Trace: t=0.0 kind=6 eye_world=(%.5f %.5f %.5f) step=0.0000 wanted=(%.5f %.5f %.5f) correction=0.0000 lease=0x1 pose_armed=1 "
            "entry_ease=1.00 source=native root=(%.5f %.5f %.5f) looked=0 taken=- quiet=0 tick=%.3f off_foot=0 damped=0 damp=(0.00000 0.00000 0.00000) "
            "view=(%.5f %.5f %.5f)" % (stamp, *eye, *eye, *root, tick, *view))


class ReloadSlide(unittest.TestCase):
    def test_the_users_reload_fails_as_main_had_it(self):
        lines = window() + [LOCK]
        plain = matrix.judge(lines)
        self.assertTrue(plain["pass"])                       # what the matrix said of it before: every frame under the jump limit
        self.assertTrue(plain["from_trace"])
        judged = matrix.judge(lines, reload=True)
        self.assertFalse(judged["pass"])
        self.assertGreater(judged["slide_m"], 0.2)           # 0.233 m back onto the root and forward again
        self.assertIn("slid", " ".join(judged["notes"]))

    def test_a_reload_phase_with_no_traced_eye_fails(self):
        judged = matrix.judge([LOCK], reload=True)
        self.assertFalse(judged["pass"])
        self.assertFalse(judged["from_trace"])
        self.assertIn("not traced", " ".join(judged["notes"]))

    def test_a_slow_slide_is_summed(self):
        # 0.24 m in 0.4 s (the user's second reload: R2), 0.02 m a frame: no frame is a jump, the slide is
        rows = [trace(1000 + 16 * i, 5000.0 + 16 * i, (0.0, 0.233 - 0.24 * min(1.0, max(0.0, (i - 10) / 25.0)), 1.6)) for i in range(60)]
        judged = matrix.judge(rows + [LOCK], reload=True)
        self.assertLess(judged["largest_jump_m"], matrix.POP_M)
        self.assertFalse(judged["pass"])
        self.assertAlmostEqual(judged["slide_m"], 0.24, places=3)

    def test_the_body_walking_is_not_a_slide(self):
        # the root goes with the eye: the eye's offset from it does not change
        rows = [trace(1000 + 16 * i, 5000.0 + 16 * i, (0.0, 0.233 + 0.03 * i, 1.6), root=(0.0, 0.03 * i, 0.0)) for i in range(40)]
        judged = matrix.judge(rows + [LOCK], reload=True)
        self.assertTrue(judged["pass"])
        self.assertLess(judged["slide_m"], 1e-6)

    def test_the_eye_turning_with_the_view_is_not_a_slide(self):
        import math
        rows = []
        for i in range(40):
            a = 0.02 * i
            view = (math.sin(a), math.cos(a), 0.0)
            rows.append(trace(1000 + 16 * i, 5000.0 + 16 * i, (0.233 * view[0], 0.233 * view[1], 1.6), view=view))
        judged = matrix.judge(rows + [LOCK], reload=True)
        self.assertTrue(judged["pass"])

    def test_other_phases_are_judged_as_before(self):
        lines = window() + [LOCK]
        self.assertNotIn("slide_m", matrix.judge(lines))
        self.assertIn("reload", matrix.RELOADS)
        self.assertIn("pistol-reload", matrix.RELOADS)


@unittest.skipUnless(shutil.which(os.environ.get("CC", "cc")), "no C compiler")
class ReloadHeld(unittest.TestCase):
    def test_the_users_reload_passes_as_the_hold_puts_it(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as work:
            binary, held = Path(work) / "reload_hold_test", Path(work) / "held.log"
            subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O1", "-ffp-contract=off", "-I", str(ROOT / "src"), "-I", str(ROOT / "tests"),
                            str(ROOT / "tests/reload_hold_test.c"), "-lm", "-o", str(binary)], check=True)
            subprocess.run([str(binary), "--rewrite", str(WINDOW), str(held)], check=True)
            lines = held.read_text().splitlines()
        self.assertEqual(len(lines), len(window()))
        judged = matrix.judge(lines + [LOCK], reload=True)
        self.assertTrue(judged["pass"], judged["notes"])
        self.assertLess(judged["slide_m"], 0.02)
        unchanged = [a for a, b in zip(window(), lines) if a == b]
        self.assertGreater(len(unchanged), 50)             # the frames before the reload, and its header and other lines, as written


if __name__ == "__main__":
    unittest.main()
