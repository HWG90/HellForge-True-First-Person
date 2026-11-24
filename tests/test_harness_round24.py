"""Round 24 (clambers and pick-ups): the float judge (tools/harness/trace.py held_offsets), what replay.py reads around
the Trace lines (masks, the camera style, the window opened only by a traversal mask), and the replay windows of the
user's log of 2026-11-24 on feel.51 (skipped where that log is not in ~/.cache)."""
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import replay  # noqa: E402
import trace  # noqa: E402

USER_LOG = Path.home() / ".cache/tfp-soak-31-user.modlog"


class Float(unittest.TestCase):
    def rows(self, offsets, dt=20.0, damp=None):
        return [{"tick": 1000 + dt * i, "eye": [0, 0, 1 + o], "wanted": [0, 0, 1 + (damp or 0)], "damp": [0, 0, damp] if damp else None}
                for i, o in enumerate(offsets)]

    def test_an_eye_held_off_its_target_is_a_float(self):
        # 0.6 m off and back over 0.4 s at 0.04 m a frame: no step of POP_M, a float of what it held for 300 ms
        up = [min(0.6, 0.04 * i) for i in range(16)]
        rows = self.rows(up + [0.6] * 16 + [max(0.0, 0.6 - 0.04 * i) for i in range(16)])
        held, row = trace.largest_float(rows)
        self.assertAlmostEqual(held, 0.6, places=6)
        self.assertGreater(held, trace.FLOAT_M)

    def test_an_offset_shorter_than_the_time_is_not(self):
        rows = self.rows([0.0] * 10 + [0.5] * 10 + [0.0] * 10)   # 200 ms
        self.assertEqual(trace.largest_float(rows)[0], 0.0)

    def test_the_target_is_the_wanted_eye_less_the_damping(self):
        # the eye where the damping put it (0.2 m behind the head): 0.2 m off the raw target, not 0
        rows = self.rows([0.2] * 30, damp=0.2)
        self.assertAlmostEqual(trace.largest_float(rows)[0], 0.2, places=6)
        self.assertEqual(trace.raw_target({"wanted": [1, 2, 3], "damp": None}), [1, 2, 3])

    def test_a_pause_starts_the_time_again(self):
        rows = self.rows([0.5] * 10) + [dict(r, tick=r["tick"] + 1000) for r in self.rows([0.5] * 10)]
        self.assertEqual(trace.largest_float(rows)[0], 0.0)


def trace_line(stamp, t, kind, looked=1, source="head", tick=None):
    return (f"{stamp} Trace: t={t:.1f} kind={kind} eye_world=(0 0 1.6) step=0.0000 wanted=(0 0 1.6) correction=0.0000 lease=0x0 pose_armed=1 "
            f"entry_ease=1.00 source={source} root=(0 0 0) axes=(1 0 0 0 1 0 0 0 1) looked={looked} taken=- quiet=0 tick={tick:.3f}")


class Context(unittest.TestCase):
    def test_the_mask_and_the_window(self):
        lines = ["22:00:00.000 Camera style: Stable (...)",
                 "22:00:00.990 State: hold ended: the gun has left the hands",
                 "22:00:01.000 Hold: mask=00000022 active=0",
                 trace_line("22:00:01.000", 10, 4, tick=1000),
                 "22:00:01.500 Hold: mask=00000020 active=0",
                 trace_line("22:00:01.500", 510, 4, tick=1500),
                 trace_line("22:00:01.700", 710, 4, tick=1700),
                 "22:00:02.300 Hold: mask=00000002 active=0",
                 trace_line("22:00:02.300", 1310, 4, tick=2300)]
        rows = replay.read(lines)
        # the stamp less the tick is 72 000 000 ms here (22:00:00.000)
        self.assertEqual([row["mask"] for row in rows], [0x22, 0x20, 0x20, 0x02])
        self.assertEqual([row["stable"] for row in rows], [1, 1, 1, 1])
        # 1.7 s: the State line's window (to 1.59 s) is over; the change 0x22 -> 0x20 at 1.5 s alone kept it open
        self.assertEqual([row["rearm_only"] for row in rows], [0, 0, 1, 1])

    def test_without_the_lines_nothing_is_known(self):
        rows = replay.read([trace_line("22:00:01.000", 10, 4, tick=1000)])
        self.assertEqual((rows[0]["mask"], rows[0]["stable"], rows[0]["rearm_only"]), (-1, -1, 0))


@unittest.skipUnless(USER_LOG.exists(), "the user's log of 2026-11-24 is not in ~/.cache")
class UserLog(unittest.TestCase):
    """The replay windows of the round-24 report: the logged filter (feel.51, cc0a95f) and this tree's."""
    calls = None

    @classmethod
    def setUpClass(cls):
        calls = replay.read(USER_LOG.read_text(errors="replace").splitlines())
        cls.rows = replay.merge(replay.replay(calls, "cc0a95f")[0])

    def window(self, start, end, eye, carried, taken):
        return replay.window_figures([r for r in self.rows if start <= r["at"] <= end], eye, carried, taken)

    def test_the_high_clambers(self):
        for start, end in (("20:27:42.7", "20:27:43.4"), ("20:27:50.4", "20:27:51.1"), ("20:28:13.1", "20:28:14.8")):
            logged = self.window(start, end, "old_eye", "old_carried", "old_taken")
            present = self.window(start, end, "new_eye", "new_carried", "new_taken")
            self.assertGreater(logged["offset"], 0.7, start)    # the float as it was: 0.72 to 0.89 m
            self.assertLessEqual(present["offset"], 0.25 + 1e-3, start)
            self.assertLessEqual(present["stopped"], 0.10, start)
            self.assertLessEqual(present["held"], trace.FLOAT_M, start)


    def test_the_backpacks(self):
        # the drift after the native view came back: what is carried grows again with nothing taken up (0.154 m at
        # 20:32:10 in the log); THE SETTLE holds the eye through the rig's re-seat and eases once
        for start, end in (("20:27:07.0", "20:27:07.7"), ("20:32:10.2", "20:32:10.8"), ("20:34:19.6", "20:34:20.4")):
            present = self.window(start, end, "new_eye", "new_carried", "new_taken")
            self.assertLessEqual(present["regrown"], 0.10, start)
        logged = self.window("20:32:10.2", "20:32:10.8", "old_eye", "old_carried", "old_taken")
        self.assertGreater(logged["regrown"], 0.15)


if __name__ == "__main__":
    unittest.main()
