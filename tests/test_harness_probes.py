"""The made-up scenes of tools/harness/probes.py: this tree's continuity filter is no worse in any of them than the
numbers recorded for main (tools/harness/probes-main.txt), and the comparison itself says "worse" when it should."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import probes  # noqa: E402


class Probes(unittest.TestCase):
    def test_the_filter_is_no_worse_than_the_record(self):
        bound = probes.parse(probes.RECORD.read_text())
        now, _ = probes.measure()
        self.assertEqual(len(bound), 84)  # 21 scenes at 4 frame times
        worse = [d for d in probes.compare(bound, now) if d[4]]
        self.assertEqual(worse, [])

    def test_a_worse_filter_is_seen(self):
        # 0.6.0-feel.40 believed the middle of a motion dying down: more step and more carried at 33.3 and 50 ms
        now, _ = probes.measure("9a3470b")
        worse = [(d[0], d[1]) for d in probes.compare(probes.parse(probes.RECORD.read_text()), now) if d[4]]
        self.assertEqual(worse, [("dying-down", 33.3), ("dying-down", 50.0)])

    def test_compare(self):
        bound = {("a", 8.0): (0.01, 0.15, 160.0), ("b", 8.0): (0.01, 0.15, 160.0)}
        self.assertEqual(probes.compare(bound, dict(bound)), [])
        self.assertFalse(probes.compare(bound, {("a", 8.0): (0.0104, 0.14, 152.0), ("b", 8.0): (0.01, 0.15, 160.0)})[0][4])
        for worse in ((0.0106, 0.15, 160.0), (0.01, 0.1506, 160.0), (0.01, 0.15, 168.0)):
            self.assertTrue(probes.compare(bound, {("a", 8.0): worse, ("b", 8.0): (0.01, 0.15, 160.0)})[0][4])
        self.assertTrue(probes.compare(bound, {("a", 8.0): (0.01, 0.15, 160.0)})[0][4])  # a scene that is gone


if __name__ == "__main__":
    unittest.main()
