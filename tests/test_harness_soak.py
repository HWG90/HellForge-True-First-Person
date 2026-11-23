"""soak.py: a soak that judged no frame is not a pass."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "harness"))
import soak


class SoakVerdict(unittest.TestCase):
    def test_no_frames_held_is_a_failure(self):
        # 507 actions "ok" on 2026-11-23 with the view never switched on: nothing was looked at.
        self.assertEqual(soak.verdict(frames=0, flagged=0), 2)

    def test_frames_and_nothing_flagged_passes(self):
        self.assertEqual(soak.verdict(frames=18136, flagged=0), 0)

    def test_a_flagged_action_fails(self):
        self.assertEqual(soak.verdict(frames=18136, flagged=3), 1)


if __name__ == "__main__":
    unittest.main()
