"""The harness's judge of the Trace lines, over a recorded log; never a game."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import matrix  # noqa: E402
import trace  # noqa: E402

# The soak of 2026-11-22 (0.6.0-feel.37), 19:46:57: running at 5 m/s (0.165 m a frame at 30 frames a second), the
# view leaves the native view inside a swap. On the frame it leaves the game sets the camera twice, 0.7 ms apart
# (kind 6, then kind 4), so that frame has two Trace rows with the same eye: "step=0.0001", and "step=0.1628" on
# the next frame, which is the body's own travel as on every frame before.
LOG = ROOT / "tests/reference/trace-two-calls-one-frame.log"
# In the recorded log a State line falls between those two rows and the next frame and ends the window there. It
# need not: the binder writes it on its own tick.
LATE = ("State: hold ended", "State: left the native view", "State: swap")


class SameFrame(unittest.TestCase):
    def lines(self):
        return [line for line in LOG.read_text().splitlines() if not any(word in line for word in LATE)]

    def test_two_rows_of_one_frame_are_one_frame(self):
        # Read as two frames, the second "stood still" and the next "moved 0.163 m": a pop of 0.165 m.
        result = matrix.judge(self.lines())
        self.assertTrue(result["from_trace"])
        self.assertLess(result["largest_jump_m"], 0.03)
        for _, rows in trace.windows(self.lines()):
            for a, b in zip(rows, rows[1:]):
                self.assertFalse(0 <= b[0] - a[0] < trace.SAME_FRAME_MS, (a, b))

    def test_the_recorded_log_as_it_is(self):
        self.assertLess(matrix.judge(LOG.read_text().splitlines())["largest_jump_m"], 0.03)

    def test_a_step_that_changes_is_still_a_pop(self):
        lines = self.lines()
        last = [i for i, line in enumerate(lines) if "Trace:" in line][-1]
        # the last frame 0.3 m further on than it was
        lines[last] = lines[last].replace("eye_world=(-22.6380", "eye_world=(-22.9380")
        self.assertIn("-22.9380", lines[last])
        self.assertGreater(matrix.judge(lines)["largest_jump_m"], 0.25)


def row(t, kind, x, step=0.0):
    return f"19:00:00.000 Trace: t={t:.1f} kind={kind} eye_world=({x:.4f} 1.0000 2.0000) step={step:.4f} wanted=({x:.4f} 1.0000 2.0000) correction=0.0000"


class FastFrames(unittest.TestCase):
    """Above 200 frames a second every frame is under 5 ms after the one before. They are still frames."""

    def test_frames_4_ms_apart_with_a_moving_eye_are_all_kept(self):
        lines = ["19:00:00.000 State: swap"] + [row(4.0 * i, 4, 0.02 * i, 0.02) for i in range(50)]
        (_, rows), = trace.windows(lines)
        self.assertEqual(len(rows), 50)
        self.assertEqual([r[0] for r in rows], [4.0 * i for i in range(50)])

    def test_a_pop_at_250_frames_a_second_is_seen(self):
        lines = ["19:00:00.000 State: swap"] + [row(4.0 * i, 4, 0.02 * i + (0.3 if i >= 25 else 0)) for i in range(50)]
        self.assertGreater(matrix.judge(lines)["largest_jump_m"], 0.25)

    def test_a_frame_set_twice_at_250_frames_a_second_is_one_frame(self):
        # the view leaves the native view on frame 25: that frame is set twice, 0.7 ms apart, kind 6 and then kind 4
        # with the same eye, as in the recorded log
        lines = ["19:00:00.000 State: swap"]
        for i in range(50):
            if i == 25:
                lines.append(row(4.0 * i - 0.7, 6, 0.02 * i))
            lines.append(row(4.0 * i, 6 if i < 25 else 4, 0.02 * i))
        (_, rows), = trace.windows(lines)
        self.assertEqual(len(rows), 50)
        self.assertEqual([r[1] for r in rows], [6] * 25 + [4] * 25)  # the later row is the one kept
        self.assertLess(matrix.judge(lines)["largest_jump_m"], 0.03)

    def test_a_third_row_is_measured_from_the_frames_first_row(self):
        # 0, 3 (the same frame again, same eye), 6 ms (the next frame: 6 ms after the first row, though 3 after the second)
        lines = ["19:00:00.000 State: swap", row(0.0, 6, 0.0), row(3.0, 4, 0.0), row(6.0, 4, 0.02), row(12.0, 4, 0.04)]
        (_, rows), = trace.windows(lines)
        self.assertEqual([r[0] for r in rows], [3.0, 6.0, 12.0])

    def test_the_same_kind_with_a_moved_eye_is_another_frame(self):
        lines = ["19:00:00.000 State: swap", row(0.0, 4, 0.0), row(2.0, 4, 0.05), row(4.0, 4, 0.10)]
        (_, rows), = trace.windows(lines)
        self.assertEqual(len(rows), 3)


def full(ms, eye, wanted=None, root=(0.0, 0.0, 0.0), carried=0.0, kind=6, looked=1, taken="-", tick=True, t=None):
    """A Trace line as 0.6.0-feel.41 on writes it: the root, what was looked for and the helper's own clock."""
    wanted = eye if wanted is None else wanted
    stamp = "19:00:%02d.%03d" % (int(ms // 1000), int(ms % 1000))
    line = (f"{stamp} Trace: t={ms if t is None else t:.1f} kind={kind} eye_world=({eye[0]:.5f} {eye[1]:.5f} {eye[2]:.5f}) step=0.0000 "
            f"wanted=({wanted[0]:.5f} {wanted[1]:.5f} {wanted[2]:.5f}) correction={carried:.4f} lease=0x0 pose_armed=1 entry_ease=1.00 "
            f"source={'native' if kind == 6 else 'head'} root=({root[0]:.5f} {root[1]:.5f} {root[2]:.5f}) "
            f"axes=(1.000000 0.000000 0.000000 0.000000 1.000000 0.000000 0.000000 0.000000 1.000000) looked={looked} taken={taken} quiet=0")
    return line + (f" tick={1000000 + ms:.3f}" if tick else "")


HELD = "19:00:00.000 Lock: +30 frames held (+0 in another camera, kind now 6), +0 missed (kind 0, helper error 0), largest kick 0.0 degrees"


def path(steps, start=0.0):
    """Places along x from the steps between them."""
    places = [start]
    for step in steps:
        places.append(places[-1] + step)
    return places


class Measure(unittest.TestCase):
    """How a jump is measured (round 10, 2026-11-24): the change of the presented eye's step as a vector, and what
    the body root itself does in that frame or the one before, with the wanted eye, is the body's own."""

    def judge(self, eyes, roots=None, wanted=None, carried=None, **more):
        lines = ["19:00:00.000 State: swap", HELD]
        for i, x in enumerate(eyes):
            lines.append(full(33.0 * i, (x, 1.0, 2.0), None if wanted is None else (wanted[i], 1.0, 2.0),
                              (0.0, 0.0, 0.0) if roots is None else (roots[i], 0.0, 0.0), 0.0 if carried is None else carried[i], **more))
        return matrix.judge(lines)

    def test_a_step_back_is_a_jump_though_its_length_is_the_same(self):
        # 0.04 m forwards and then 0.04 m back: the lengths say nothing changed, the eye's step changed by 0.08 m
        result = self.judge(path([0.04, 0.04, 0.04, -0.04, -0.04]))
        self.assertAlmostEqual(result["largest_jump_m"], 0.08, places=3)
        self.assertLess(result["by_lengths_m"], 0.001)
        self.assertFalse(result["pass"])

    def test_the_bodys_own_lunge_is_not_the_eyes_jump(self):
        # a climb: the root's step goes 0.03, 0.13, 0.29, 0.38, 0.30, 0.15, 0.02 m and the rig eye is the root a frame late
        steps = [0.01, 0.03, 0.13, 0.29, 0.38, 0.30, 0.15, 0.02, 0.0, 0.0]
        roots = path(steps)
        eyes = [1.0] + [1.0 + x for x in roots[:-1]]
        result = self.judge(eyes, roots)
        self.assertLess(result["largest_jump_m"], 0.03)   # the 0.02 m with which it sets off: under the limit, so the eye's
        self.assertTrue(result["pass"])
        self.assertAlmostEqual(result["body_m"], 0.16, places=3)       # still in view: what the body did
        self.assertAlmostEqual(result["by_lengths_m"], 0.16, places=3)  # the figure written down before round 10
        self.assertTrue(all(p["whose"] == "body" for p in result["pops"]) and result["pops"])

    def test_the_same_change_without_the_root_is_a_jump(self):
        steps = [0.01, 0.03, 0.13, 0.29, 0.38, 0.30, 0.15, 0.02, 0.0, 0.0]
        result = self.judge(path(steps, 1.0))
        self.assertAlmostEqual(result["largest_jump_m"], 0.16, places=3)
        self.assertFalse(result["pass"])

    def test_a_root_checked_under_an_eye_that_goes_on_is_a_jump(self):
        # the head camera in a dive: the root stops, the wanted eye (the head) goes on, the presented eye was slowed
        # with the root. The root made the change; the eye asked for did not.
        roots = path([0.2, 0.2, 0.2, 0.05, 0.05, 0.05])
        wanted = path([0.2] * 6, 1.0)
        eyes = [1.0 + x for x in roots]
        result = self.judge(eyes, roots, wanted, [0.0, 0.0, 0.0, 0.0, 0.15, 0.3, 0.45], kind=4)
        self.assertAlmostEqual(result["largest_jump_m"], 0.15, places=3)
        self.assertEqual(result["pops"][0]["whose"], "filter")

    def test_a_small_change_of_the_root_takes_nothing_off(self):
        # the root's own change is under the limit: 0.07 m of the eye is 0.07 m, not 0.04 m
        roots = path([0.05, 0.05, 0.08, 0.08, 0.08])
        eyes = path([0.05, 0.05, 0.05, 0.12, 0.12], 1.0)
        result = self.judge(eyes, roots)
        self.assertAlmostEqual(result["largest_jump_m"], 0.07, places=3)

    def test_only_what_the_root_did_is_taken_off(self):
        # the root lunges by 0.15 m and the eye, a frame late, by 0.20 m: 0.05 m is the eye's
        roots = path([0.0, 0.0, 0.15, 0.15, 0.15, 0.15])
        result = self.judge(path([0.0, 0.0, 0.0, 0.20, 0.20, 0.20], 1.0), roots)
        self.assertAlmostEqual(result["largest_jump_m"], 0.05, places=3)
        # and by 0.30 m: that is not the root's lunge at all, and nothing is taken off
        result = self.judge(path([0.0, 0.0, 0.0, 0.30, 0.30, 0.30], 1.0), roots)
        self.assertAlmostEqual(result["largest_jump_m"], 0.30, places=3)

    def test_every_frame_over_the_limit_is_listed_with_its_time(self):
        result = self.judge(path([0.0, 0.0, 0.1, 0.1, 0.1, 0.0, 0.0]), carried=[0, 0, 0, 0.1, 0.1, 0.1, 0, 0], looked=1)
        self.assertEqual([p["at"] for p in result["pops"]], ["19:00:00.099", "19:00:00.198"])
        first = result["pops"][0]
        self.assertEqual((first["kind"], first["looked"], first["whose"]), (6, 1, "filter"))
        self.assertAlmostEqual(first["carried"], 0.1, places=3)
        self.assertAlmostEqual(first["shown"], 0.1, places=3)

    def test_the_eyes_own_stop_is_listed_as_its_own_and_still_fails(self):
        result = self.judge(path([0.07, 0.07, 0.07, 0.0, 0.0]))
        self.assertEqual([p["whose"] for p in result["pops"]], ["own"])
        self.assertFalse(result["pass"])

    def test_frames_on_either_side_of_a_state_line_are_neighbours(self):
        # the landing of 08:18:55.525 (soak 24): 0.120 m, with a State line between its frames, was never judged
        eyes = path([0.0, 0.0, 0.1, 0.1])
        lines = ["19:00:00.000 State: swap"] + [full(33.0 * i, (x, 1.0, 2.0)) for i, x in enumerate(eyes)]
        for at in (2, 3, 4):
            cut = lines[:at] + ["19:00:00.050 State: hold ended"] + lines[at:]
            self.assertAlmostEqual(matrix.judge(cut)["largest_jump_m"], 0.1, places=3, msg=at)

    def test_frames_before_the_first_state_line_are_judged(self):
        # a soak's stretch of the log begins where the action began, not at a State line
        eyes = path([0.0, 0.0, 0.1, 0.1])
        lines = [full(33.0 * i, (x, 1.0, 2.0)) for i, x in enumerate(eyes)]
        result = matrix.judge(lines)
        self.assertTrue(result["from_trace"])
        self.assertAlmostEqual(result["largest_jump_m"], 0.1, places=3)

    def test_a_pause_in_the_trace_is_not_a_step(self):
        lines = ["19:00:00.000 State: swap"] + [full(ms, (x, 1.0, 2.0)) for ms, x in ((0, 0.0), (33, 0.0), (66, 0.0), (700, 3.0), (733, 3.0), (766, 3.0))]
        result = matrix.judge(lines)
        self.assertLess(result["largest_jump_m"], 0.001)
        self.assertEqual(result["judged"], 2)

    def test_nothing_traced_says_so(self):
        result = matrix.judge([HELD])
        self.assertFalse(result["from_trace"])
        self.assertEqual(result["judged"], 0)
        self.assertEqual(matrix.describe(result), "not traced")


class SeenInTheGame(unittest.TestCase):
    """Two stretches of the Stable soak of 2026-11-24 on 0.6.0-feel.48 (~/.cache/tfp-soak-24.modlog)."""

    def test_the_climb_of_a_vault_is_the_bodys(self):
        # 08:07:04.3 to 05.2: the root rises 1.2 m in eight frames (its step 0.133, 0.288, 0.376, 0.305 m) and the rig
        # eye is the root a frame late. Flagged at 0.135 m by the lengths; the filter added nothing.
        result = matrix.judge((ROOT / "tests/reference/trace-climb.log").read_text().splitlines() + [HELD])
        self.assertTrue(result["pass"])
        self.assertAlmostEqual(result["by_lengths_m"], 0.135, places=3)
        self.assertLess(result["largest_jump_m"], matrix.POP_M)
        self.assertGreater(result["body_m"], 0.13)
        self.assertGreaterEqual(sum(p["whose"] == "body" for p in result["pops"]), 4)

    def test_a_braking_given_to_the_eye_a_frame_late_is_still_the_filters(self):
        # 08:13:20.317: the sprint ends 330 ms after the native view came back; the root's change is set aside in one
        # frame and the rig eye, going on against the root, is taken up in the next: 0.119 m for a wanted 0.053 m
        result = matrix.judge((ROOT / "tests/reference/trace-braking.log").read_text().splitlines() + [HELD])
        self.assertGreater(result["largest_jump_m"], 0.10)
        self.assertIn("filter", [p["whose"] for p in result["pops"]])
        self.assertFalse(result["pass"])


if __name__ == "__main__":
    unittest.main()
