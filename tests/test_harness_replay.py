"""The replay of Trace lines through the continuity filter (tools/harness/replay.py): reading the lines and judging
the steps; never a game, and no compiler."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import replay  # noqa: E402


def line(stamp_ms, t, kind, eye, wanted, carried, root=(0, 0, 0), axes=None, looked=1, taken="-", source="native"):
    stamp = "22:00:%02d.%03d" % (stamp_ms // 1000, stamp_ms % 1000)
    extra = " axes=(%s)" % " ".join("%.4f" % a for a in axes) if axes else ""
    return ("%s Trace: t=%.1f kind=%d eye_world=(%.4f %.4f %.4f) step=0.0000 wanted=(%.4f %.4f %.4f) correction=%.4f lease=0x0 pose_armed=1 "
            "entry_ease=1.00 source=%s root=(%.4f %.4f %.4f)%s looked=%d taken=%s" % (stamp, t, kind, *eye, *wanted, carried, source, *root, extra, looked, taken))


class Frames(unittest.TestCase):
    def test_lines_with_and_without_the_axes(self):
        axes = (1, 0, 0, 0, 1, 0, 0, 0, 1)
        rows = replay.frames(["22:00:00.000 State: one", line(33, 33.0, 6, (1, 2, 3), (1, 2, 3.5), 0.5),
                              line(66, 66.0, 4, (1, 2, 3), (1, 2, 3), 0, axes=axes, source="head", taken="jump+root")])
        self.assertEqual(len(rows), 2)
        self.assertIsNone(rows[0]["axes"])
        self.assertEqual(rows[1]["axes"], list(map(float, axes)))
        self.assertEqual((rows[1]["source"], rows[1]["taken"], rows[0]["carried"]), ("head", "jump+root", 0.5))

    def test_frames_written_late_keep_their_time(self):
        # six frames before the edge are written with it, all at the edge's stamp: their times come from t
        lines = ["22:00:01.000 State: edge"] + [line(1000, -200 + 33.3 * i, 6, (i, 0, 0), (i, 0, 0), 0) for i in range(6)] + [
            line(1034, 0.0, 6, (6, 0, 0), (6, 0, 0), 0)]
        rows = replay.frames(lines)
        gaps = [b["tick"] - a["tick"] for a, b in zip(rows, rows[1:])]
        self.assertEqual(len(rows), 7)
        self.assertTrue(all(30 < gap < 37 for gap in gaps), gaps)

    def test_two_rows_of_one_frame_are_one_frame(self):
        rows = replay.frames(["22:00:00.000 State: one", line(33, 33.0, 6, (1, 0, 0), (1, 0, 0), 0), line(34, 33.7, 4, (1, 0, 0), (1, 0, 0), 0, source="head")])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["kind"], 4)


class Judge(unittest.TestCase):
    def rows(self, eyes, wanted, carried):
        return [{"tick": 33.3 * i, "eye": (e, 0, 0), "wanted": (w, 0, 0), "carried": c, "root": (0, 0, 0), "misfit": 0}
                for i, (e, w, c) in enumerate(zip(eyes, wanted, carried))]

    def test_whose_step_it_is(self):
        # the wanted eye stands and the shown one goes 0.1 m: the filter's
        self.assertEqual([f[0] for f in replay.judge(self.rows((0, 0, .1), (0, 0, 0), (0, 0, .1)))], ["filter"])
        # both go, nothing carried: the game's own
        self.assertEqual([f[0] for f in replay.judge(self.rows((0, 0, .1), (0, 0, .1), (0, 0, 0)))], ["game"])
        # the wanted eye goes 0.3 m, the shown one 0.1 m: reduced
        self.assertEqual([f[0] for f in replay.judge(self.rows((0, 0, .1), (0, 0, .3), (0, 0, .2)))], ["reduced"])
        # under 0.06 m is no step
        self.assertEqual(replay.judge(self.rows((0, 0, .05), (0, 0, 0), (0, 0, .05))), [])

    def test_only_where_the_replay_follows_the_log(self):
        rows = self.rows((0, 0, .1), (0, 0, 0), (0, 0, .1))
        rows[1]["misfit"] = 0.3
        self.assertEqual(replay.judge(rows, known=True), [])
        self.assertEqual(len(replay.judge(rows)), 1)


if __name__ == "__main__":
    unittest.main()
