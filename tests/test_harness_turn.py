"""Round 49: the judge of the direction shown at a change of camera (tools/harness/matrix.py turn_snaps) and the veil's
bound (veil_faults), on made-up rows and on rows cut from the soaks of 2026-11-27 (feel.59 + rounds 40, 43:
tests/reference/turn; soak 60 is Stable, soak 62 Bodycam)."""
import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import matrix  # noqa: E402
import trace  # noqa: E402

REFERENCE = ROOT / "tests/reference/turn"
DIVE_SWAP = "soak60-A-L1990-2070.log"      # the matrix's dive-swap: 29.0 degrees out of the native view, 39.5 back into it
PRONE_BACK = "soak62-C-L1960-2012.log"     # the primary back, prone after a dive: 72.7 degrees
FOOT_SWAP = "soak60-A-L3280-3420.log"      # a swap on foot: 0.05 and 0.02 degrees at its two changes
FLICK = "soak60-A-L6760-6800.log"          # the primary back, prone: the veil 0.248 m, then nothing
MELEE = "soak62-C-L2760-2815.log"          # #13 melee, prone: the veil 0.196, 0.081 m, then the native view


def cut(name):
    return (REFERENCE / name).read_text().splitlines()


def yaw(degrees, pitch=0.0):
    a, p = math.radians(degrees), math.radians(pitch)
    return [-math.sin(a) * math.cos(p), math.cos(a) * math.cos(p), math.sin(p)]


def rows(frames, dt=33.0):
    """(kind, direction or None, veil or None) a frame."""
    return [{"tick": 1000 + dt * i, "at": f"f{i}", "kind": kind, "turn": turn, "veil": veil, "eye": [0, 0, 0]} for i, (kind, turn, veil) in enumerate(frames)]


class Turn(unittest.TestCase):
    def test_the_angle_is_exact_for_rounded_vectors(self):
        v = [-0.5582, 0.8295, 0.0159]   # four places: its length is 0.99995, and acos of its dot with itself read 0.8 degrees
        self.assertLess(matrix._degrees(v, v), 1e-6)
        self.assertAlmostEqual(matrix._degrees(yaw(0), yaw(29)), 29.0, places=6)

    def test_a_turn_at_a_change_of_kind_is_found(self):
        found = matrix.turn_snaps(rows([(6, yaw(0), None), (6, yaw(1), None), (4, yaw(30), None), (4, yaw(31), None), (6, yaw(31), None)]))
        self.assertEqual([(t["before"], t["kind"], round(t["degrees"], 3)) for t in found], [(6, 4, 29.0), (4, 6, 0.0)])

    def test_the_same_kind_a_pause_and_an_unknown_direction_are_not(self):
        self.assertEqual(matrix.turn_snaps(rows([(4, yaw(0), None), (4, yaw(50), None)])), [])              # the mouse within one camera
        self.assertEqual(matrix.turn_snaps(rows([(6, yaw(0), None), (4, yaw(50), None)], dt=200.0)), [])    # not neighbours
        self.assertEqual(matrix.turn_snaps(rows([(6, None, None), (4, yaw(50), None)])), [])                # an orphan row before round 49

    def test_the_dive_swap_of_soak_60_fails_twice(self):
        j = matrix.judge(cut(DIVE_SWAP))
        self.assertEqual([round(t["degrees"], 1) for t in j["turn_snaps"]], [29.0, 39.5])
        self.assertEqual([(t["before"], t["kind"]) for t in j["turn_snaps"]], [(6, 4), (4, 6)])
        self.assertFalse(j["pass"])
        self.assertAlmostEqual(j["turn_deg"], 39.5, places=1)
        self.assertIn("turn 39.5 deg at a change of camera", matrix.describe(j))
        self.assertTrue(any("29.0 degrees" in n and "over 10 without the mouse" in n for n in j["notes"]))

    def test_the_prone_primary_back_of_soak_62_fails(self):
        snaps = matrix.judge(cut(PRONE_BACK))["turn_snaps"]
        self.assertEqual([round(t["degrees"], 1) for t in snaps], [72.7])

    def test_a_swap_on_foot_passes(self):
        lines = cut(FOOT_SWAP)
        j = matrix.judge(lines)
        self.assertEqual(j["turn_snaps"], [])
        self.assertLess(j["turn_deg"], 0.1)
        self.assertEqual(len(matrix.turn_snaps(trace.frames(lines))), 2)   # both changes were measured

    def test_a_phase_that_moved_the_mouse_is_printed_not_failed(self):
        lines = cut(PRONE_BACK)
        with_mouse, without = matrix.judge(lines, mouse=True), matrix.judge(lines)
        self.assertEqual(len(with_mouse["turn_snaps"]), 1)
        self.assertTrue(any("not counted" in n for n in with_mouse["notes"]))
        self.assertFalse(any("72.7" in n and "not counted" in n for n in without["notes"]))


def veiled(moves, kinds=None, dt=33.0):
    kinds = kinds or [4] * len(moves)
    return rows([(k, yaw(0), [0.0, m, 0.0] if m else None) for k, m in zip(kinds, moves)], dt)


class VeilBound(unittest.TestCase):
    def test_an_opening_and_a_let_out_within_the_bound_pass(self):
        opened, row, faults = matrix.veil_faults(veiled([0, 0.25, 0.24, 0.20, 0.15, 0.10, 0.05, 0.01, 0]))
        self.assertAlmostEqual(opened, 0.25)
        self.assertEqual(row["at"], "f1")
        self.assertEqual(faults, [])

    def test_an_opening_over_the_cap_fails(self):
        _, _, faults = matrix.veil_faults(veiled([0, 0.43, 0.40, 0.36]))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in faults], [("opening", 0.43)])

    def test_the_let_out_of_a_large_move_is_not_a_flick(self):
        # the LAS-5 Scythe's 0.32 m let out rest to rest over 300 ms at 30 frames a second: 0.053 m in its steepest frame
        moves = [0, 0.32] + [0.32 * (1 - (x * x * (3 - 2 * x))) for x in (i * 33.3 / 300 for i in range(1, 10))] + [0]
        self.assertGreater(max(abs(a - b) for a, b in zip(moves[1:], moves[2:])), matrix.VEIL_STEP_M)
        self.assertEqual(matrix.veil_faults(veiled(moves, dt=33.3))[2], [])
        # the same move taken back in three frames is
        self.assertEqual(len(matrix.veil_faults(veiled([0, 0.32, 0.22, 0.11, 0], dt=33.3))[2]), 3)

    def test_a_veil_that_falls_to_nothing_in_one_frame_fails(self):
        _, _, faults = matrix.veil_faults(veiled([0, 0.248, 0, 0]))
        self.assertEqual([(f["what"], round(f["m"], 3), f["at"]) for f in faults], [("step", 0.248, "f2")])

    def test_the_native_view_cutting_a_move_fails(self):
        _, _, faults = matrix.veil_faults(veiled([0, 0.196, 0.15, 0, 0], kinds=[6, 4, 4, 6, 6]))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in faults], [("cut", 0.15)])

    def test_a_let_in_that_grows_too_fast_fails(self):
        _, _, faults = matrix.veil_faults(veiled([0, 0.05, 0.15, 0.15]))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in faults], [("step", 0.10)])

    def test_the_first_veiled_frame_after_a_pause_is_an_opening(self):
        opened, _, faults = matrix.veil_faults(veiled([0.2, 0.2], dt=200.0))
        self.assertAlmostEqual(opened, 0.2)
        self.assertEqual(faults, [])

    def test_the_flick_of_soak_60_fails(self):
        j = matrix.judge(cut(FLICK))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in j["veil_faults"]], [("step", 0.248)])
        self.assertAlmostEqual(j["veil_open_m"], 0.248, places=3)
        self.assertFalse(j["pass"])
        self.assertIn("veil opened 0.248 m", matrix.describe(j))

    def test_the_melee_of_soak_62_fails_on_the_step_and_on_the_cut(self):
        j = matrix.judge(cut(MELEE))
        self.assertEqual([(f["what"], round(f["m"], 3)) for f in j["veil_faults"]], [("step", 0.115), ("cut", 0.081)])
        self.assertFalse(j["pass"])

    def test_a_swap_on_foot_stays_inside_the_bound(self):
        j = matrix.judge(cut(FOOT_SWAP))
        self.assertEqual(j["veil_faults"], [])
        self.assertAlmostEqual(j["veil_open_m"], 0.191, places=3)


if __name__ == "__main__":
    unittest.main()
