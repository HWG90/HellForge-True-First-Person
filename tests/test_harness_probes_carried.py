"""The made-up scenes with a correction being carried (tools/harness/probe_filter.c: a jump taken up and a change of
camera while 0.4 and 0.8 m are being carried, and a speed kept across a change of camera): this tree's continuity filter
is no worse in any of them than the numbers recorded with THE LENT SPEED (tools/harness/probes-carried.txt), and the
filter before it is seen to be worse where it was."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import probes  # noqa: E402


class Carried(unittest.TestCase):
    def test_the_filter_is_no_worse_than_the_record(self):
        bound = probes.parse(probes.CARRIED.read_text())
        now, _ = probes.measure()
        self.assertEqual(len(bound), 24)  # 6 scenes at 4 frame times
        self.assertEqual([d for d in probes.compare(bound, now) if d[4]], [])

    def test_the_filter_before_is_seen(self):
        # 0.6.0-feel.42 dropped a speed kept across a change of camera in one frame: more step in six of the eight
        # lent-* cells (at 8 ms a frame the step of 5 m/s is the measure itself); the carried-* cells are the same
        now, _ = probes.measure("f557194")
        differ = probes.compare(probes.parse(probes.CARRIED.read_text()), now)
        self.assertEqual([(d[0], d[1]) for d in differ if d[4]],
                         [("lent-0.4", 16.7), ("lent-0.4", 33.3), ("lent-0.4", 50.0), ("lent-0.8", 16.7), ("lent-0.8", 33.3), ("lent-0.8", 50.0)])
        self.assertEqual([d for d in differ if not d[0].startswith("lent-")], [])

    def test_the_first_record_is_as_it_was(self):
        self.assertEqual(len(probes.parse(probes.RECORD.read_text())), 84)
        self.assertEqual(set(probes.parse(probes.RECORD.read_text())) & set(probes.parse(probes.CARRIED.read_text())), set())


if __name__ == "__main__":
    unittest.main()
