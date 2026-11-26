"""Round 44: the matrix's throw phases judged on the eye against the head node (tools/harness/matrix.py behind_head).
The feel.58 matrix's "grenade" passed while every throw held the eye 0.18 to 0.31 m behind the head: the veil's frames
were left out of the step judge, and nothing measured the eye against the head.

The three throws of tests/reference/throw/ (the user's of 2026-11-25 on feel.58, session b9c6, Bodycam; the harness
soaks 7a3c, Stable, and 5608, prone) are judged as they were (main: fail) and as this build shows them (the C++ replay
of tests/camera_throw_eye_test.cpp --rewrite: pass). Needs g++ for the second; without it those tests are skipped."""
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

WINDOWS = ROOT / "tests/reference/throw"
NAMES = ["feel58-b9c6-L11795-12010.log", "soak-7a3c-L17118-17225.log", "soak-5608-L18433-18515.log"]
LOCK = "23:59:59.000 Lock: +71 frames held (+0 in another camera, kind now 4), +0 missed (kind 0, helper error 0), largest kick 0.7 degrees"


def window(name):
    return (WINDOWS / name).read_text().splitlines()


def row(tick, eye, head_shown=None, veil_head=None, kind=4, source="head", off_foot=0, veil=None, root=(0.0, 0.0, 0.0), view=(0.0, 1.0, 0.0)):
    ms = int(tick)
    stamp = "22:00:%02d.%03d" % (ms // 1000 % 60, ms % 1000)
    v = "(%.5f %.5f %.5f)" % veil if veil else "0"
    return ("%s Trace: t=0.0 kind=%d eye_world=(%.5f %.5f %.5f) step=0.0000 wanted=(%.5f %.5f %.5f) correction=0.0000 lease=0x0 pose_armed=0 "
            "entry_ease=1.00 source=%s root=(%.5f %.5f %.5f) axes=(1.000000 0.000000 0.000000 0.000000 1.000000 0.000000 0.000000 0.000000 1.000000) "
            "looked=0 taken=- quiet=0 tick=%.3f off_foot=%d damped=0 damp=(0.00000 0.00000 0.00000) view=(%.5f %.5f %.5f) sock=0.00000 arms=0 eye_edge=0 "
            "veil=%s turn=(0.0000 0.0000 0.0000) veil_head=%s%s"
            % (stamp, kind, *eye, *eye, source, *root, tick, off_foot, *view, v,
               "(%.4f %.4f)" % veil_head if veil_head else "0", " head_shown=(%.4f %.4f) clear=0.0000" % head_shown if head_shown else ""))


class BehindHead(unittest.TestCase):
    def test_the_three_throws_fail_as_main_had_them(self):
        for name in NAMES:
            lines = window(name) + [LOCK]
            plain = matrix.judge(lines)
            self.assertNotIn("behind_frames", plain)
            judged = matrix.judge(lines, throw=True)
            self.assertFalse(judged["pass"], name)
            self.assertGreater(judged["behind_frames"], 0, name)
            self.assertLess(judged["front_least_m"], -0.15, name)       # 0.18 to 0.31 m behind the head node
            self.assertIn("the cape and the back in view", " ".join(judged["notes"]))
            # main's rows carry no head_shown: the head is estimated from the leave's veiled rows
            self.assertGreater(judged["head_estimated"], 0, name)
        # main's matrix passed the grenade on the step judge alone: the user's throw is under it
        self.assertTrue(matrix.judge(window(NAMES[0]) + [LOCK])["pass"])

    def test_head_shown_is_taken_as_it_is(self):
        rows = [row(5000 + 33 * i, (0.0, 0.10 * i, 1.6), head_shown=(-0.10, -0.12)) for i in range(10)]
        out = matrix.behind_head(rows)
        self.assertEqual((out["frames"], out["measured"], out["estimated"], out["behind"]), (10, 10, 0, 0))
        self.assertAlmostEqual(out["least_m"], 0.10)
        rows[4] = row(5000 + 33 * 4, (0.0, 0.4, 1.6), head_shown=(0.02, -0.12))
        out = matrix.behind_head(rows)
        self.assertEqual(out["behind"], 1)
        self.assertAlmostEqual(out["least_m"], -0.02)

    def test_the_veiled_row_places_the_head_for_the_rows_after_it(self):
        # the head 0.21 m in front of the eye before the veil; the veil moves the eye 0.31 m; the rows after it are
        # the held eye: 0.21 m behind the head, carried with the root
        rows = [row(5000, (0.0, 0.0, 1.6), veil_head=(0.21, -0.12), veil=(0.0, 0.31, 0.0))]
        rows += [row(5000 + 33 * i, (0.0, 0.05 * i, 1.6), root=(0.0, 0.05 * i, 0.0)) for i in range(1, 6)]
        out = matrix.behind_head(rows)
        self.assertEqual((out["measured"], out["estimated"], out["behind"]), (6, 5, 5))
        self.assertAlmostEqual(out["least_m"], -0.21, places=4)

    def test_off_foot_native_and_unplaced_frames_are_not_measured(self):
        rows = [row(5000, (0.0, 0.0, 1.6), head_shown=(0.3, 0.0), off_foot=1),
                row(5033, (0.0, 0.0, 1.6), head_shown=(0.3, 0.0), kind=6, source="native"),
                row(5066, (0.0, 0.0, 1.6))]
        out = matrix.behind_head(rows)
        self.assertEqual((out["frames"], out["measured"], out["behind"]), (1, 0, 0))
        judged = matrix.judge(rows + [LOCK], throw=True)
        self.assertIn("the head was not measured", " ".join(judged["notes"]))

    def test_a_native_frame_forgets_the_head(self):
        rows = [row(5000, (0.0, 0.0, 1.6), veil_head=(0.21, -0.12)), row(5033, (0.0, 0.0, 1.6), kind=6, source="native"), row(5066, (0.0, 0.0, 1.6))]
        self.assertEqual(matrix.behind_head(rows)["measured"], 1)

    def test_the_throw_phases(self):
        self.assertEqual(matrix.THROWS, {"grenade", "grenade-held", "stratagem-menu"})
        for name in matrix.THROWS:
            self.assertIn(name, matrix.PHASES)


@unittest.skipUnless(shutil.which("g++") and sys.platform.startswith("linux"), "no g++ (or not Linux)")
class BehindHeadNow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR"))
        work = Path(cls.work.name)
        cc = os.environ.get("CC", "cc")
        subprocess.run([cc, "-std=c11", "-O1", "-ffp-contract=off", "-I", str(ROOT / "src"), "-c", str(ROOT / "src/camera_helper.c"),
                        "-o", str(work / "camera_helper.o")], check=True)
        cls.binary = work / "camera_throw_eye_test"
        subprocess.run(["g++", "-std=c++17", "-O1", "-ffp-contract=off", "-I", str(ROOT / "src"), "-I", str(ROOT / "tests"),
                        str(ROOT / "tests/camera_throw_eye_test.cpp"), str(work / "camera_helper.o"), "-lm", "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def test_the_three_throws_pass_as_this_build_shows_them(self):
        for name in NAMES:
            out = Path(self.work.name) / ("now-" + name)
            subprocess.run([str(self.binary), "--rewrite", str(WINDOWS / name), str(out)], check=True)
            lines = out.read_text().splitlines()
            head = matrix.behind_head(lines)
            self.assertGreater(head["measured"], 10, name)
            self.assertEqual(head["estimated"], 0, name)               # every head-camera row says where the head was
            self.assertEqual(head["behind"], 0, name)
            self.assertGreaterEqual(head["least_m"], 0.099, name)
            judged = matrix.judge(lines + [LOCK], throw=True)
            self.assertEqual(judged["behind_frames"], 0, name)
            self.assertFalse(any("cape" in n for n in judged["notes"]), name)
            # the lines other than Trace are as written
            self.assertEqual([x for x in lines if " Trace: " not in x], [x for x in window(name) if " Trace: " not in x])


if __name__ == "__main__":
    unittest.main()
