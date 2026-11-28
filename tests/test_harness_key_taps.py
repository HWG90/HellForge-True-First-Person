"""Round 50: `key NAME` holds the keys the mod polls for 250 ms (two game updates and more), the game's own keys for
80 ms as before; `tap NAME MS` is the short press itself."""
import sys
import unittest
from pathlib import Path

HARNESS = Path(__file__).resolve().parents[1] / "tools" / "harness"
sys.path.insert(0, str(HARNESS))
import drive


def held_ms(script):
    """{key code: [ms held, ...]} of a script's presses."""
    lines, _ = drive.events(script)
    down, held = {}, {}
    for line in lines:
        at, kind, code = line.split()
        if kind == "d":
            down[code] = int(at)
        elif kind == "u":
            held.setdefault(code, []).append(int(at) - down.pop(code))
    return held


class KeyHold(unittest.TestCase):
    def test_the_mods_polled_keys_are_held_for_two_updates_and_more(self):
        for name in (drive.TOGGLE, "f10", "f11", "f9", "pageup", "U"):
            with self.subTest(name=name):
                self.assertEqual(held_ms(f"key {name}"), {drive._code(name): [250]})
        # two updates even at 10 a second, the slowest the sessions showed
        self.assertGreaterEqual(drive.POLLED_HOLD_S, 2 * 0.1)

    def test_the_games_keys_keep_80_ms(self):
        for name in ("r", "g", "c", "z", "1", "2", "ctrl", "space", "e", "pagedown", "arrowup"):
            with self.subTest(name=name):
                self.assertEqual(held_ms(f"key {name}"), {drive._code(name): [80]})

    def test_an_explicit_time_wins_and_hold_and_click_are_as_before(self):
        self.assertEqual(held_ms("key u 0.5"), {"85": [500]})
        self.assertEqual(held_ms("key r 0.2"), {"82": [200]})
        self.assertEqual(held_ms("hold w 0.7"), {"87": [700]})
        self.assertEqual(held_ms("hold u"), {"85": [80]})
        self.assertEqual(held_ms("click left"), {"L": [80]})
        self.assertEqual(held_ms("click left 0.6"), {"L": [600]})

    def test_the_script_time_counts_the_longer_hold(self):
        lines, total = drive.events("key f10; key r")
        self.assertEqual(lines, ["0 d 121", "250 u 121", "280 d 82", "360 u 82"])
        self.assertAlmostEqual(total, 0.39)


class Tap(unittest.TestCase):
    def test_tap_is_down_for_the_milliseconds_given(self):
        lines, total = drive.events("wait 1; tap u 20; wait 0.5; tap f10 5")
        self.assertEqual(lines, ["1000 d 85", "1020 u 85", "1550 d 121", "1555 u 121"])
        self.assertAlmostEqual(total, 1.585)

    def test_tap_refuses_what_is_not_a_key_and_a_time(self):
        for script in ("tap u", "tap u 0", "tap u 0.02", "tap u 1001", "tap u 20 3", "tap nokey 20", "tap u -5"):
            with self.subTest(script=script):
                with self.assertRaises(drive.DriveError):
                    drive.events(script)

    def test_a_tapped_key_is_not_left_held(self):
        self.assertEqual(drive.held_after("down right; keep right; tap u 20"), {"right"})
        self.assertEqual(drive.held_after("tap right 20", {"right"}), set())


if __name__ == "__main__":
    unittest.main()
