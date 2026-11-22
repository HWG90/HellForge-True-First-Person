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


if __name__ == "__main__":
    unittest.main()
