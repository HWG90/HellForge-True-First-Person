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


if __name__ == "__main__":
    unittest.main()
