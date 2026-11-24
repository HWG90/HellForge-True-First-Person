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


class SoakWholeLog(unittest.TestCase):
    """The log is cut per action, and three frames that lie across a cut were judged in neither stretch (the soaks of
    2026-11-24: 08:31:09.150 at 0.072 m and 08:32:05.640 at 0.061 m were over the limit and not flagged)."""

    def test_frames_over_the_limit_that_no_action_listed(self):
        whole = [{"at": "08:31:09.150", "tick": 1.0, "whose": "own"}, {"at": "08:31:12.000", "tick": 2.0, "whose": "filter"},
                 {"at": "08:31:15.000", "tick": 3.0, "whose": "body"}]
        self.assertEqual([p["at"] for p in soak.unlisted(whole, {2.0})], ["08:31:09.150"])   # the body's own is not a flag

    def test_two_frames_written_under_one_stamp_are_two_frames(self):
        # frames from before an edge are all written with it: one of them listed by an action does not list the other
        whole = [{"at": "08:31:12.000", "tick": 182331157.344, "whose": "filter"}, {"at": "08:31:12.000", "tick": 182331190.535, "whose": "filter"}]
        self.assertEqual([p["tick"] for p in soak.unlisted(whole, soak.listed([whole[0]]))], [182331190.535])


class SoakCounts(unittest.TestCase):
    def test_the_veils_frames_are_counted_apart(self):
        # Round 19: a frame flagged for the helmet veil's move is the veil's, not the eye's own.
        pops = [{"whose": "own", "beyond": 0.07, "along": None}, {"whose": "veil", "beyond": 0.32, "along": 0.32, "across": 0.0},
                {"whose": "body", "beyond": 0.01, "along": None}]
        # The veil's are printed with their own largest and are not flags: the eye's largest is the eye's.
        self.assertEqual(soak.counted(pops), "1 frames flagged (1 the eye's own, 0 of them along the view; 0 the filter's), "
                                             "largest 0.070 m; 1 the veil's, largest 0.320 m, not counted; 1 the body's own, not counted")

    def test_the_veils_frames_are_not_unlisted_flags(self):
        whole = [{"at": "08:31:09.150", "tick": 1.0, "whose": "veil"}, {"at": "08:31:12.000", "tick": 2.0, "whose": "own"}]
        self.assertEqual([p["tick"] for p in soak.unlisted(whole, set())], [2.0])

    def test_an_eyes_own_frame_under_the_veil_counts_its_own_measure(self):
        # an eye's own jump in a veiled frame: the eye's largest is the measure without the veil ("bare")
        pops = [{"whose": "own", "beyond": 0.40, "bare": 0.20, "along": None}]
        self.assertIn("largest 0.200 m; 0 the veil's, not counted", soak.counted(pops))


if __name__ == "__main__":
    unittest.main()
