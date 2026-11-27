"""Round 51: the harness levers (tools/harness/levers.py, levers.lua, levers_sites.json, phases_levers.py). Offline:
the sites table against the saved image of game.dll (build 25480438) where it is on this machine, the pure parts
(patterns, names, the physics body's shape, the kernel's source), and the levers' order of work against a fake game.
Only the game can say that a lever does what it says (docs/design/HARNESS.md "Levers")."""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/harness"))
import levers  # noqa: E402
import phases_levers  # noqa: E402

IMAGE = Path(os.environ.get("TFP_GAME_IMAGE", Path.home() / "Documents/ChatGPT/Linux HD2/native-work/evidence/game-pre1020.dll"))
# what HellForge's SDK headers and the game's own code say these are on build 25480438
KNOWN = {"SPAWN": 0xFD9710, "ENTITY_OF_NETWORK": 0xFD9BA0, "ENTITY_DESCRIPTOR": 0xFD9D40, "PLAYER": 0x3326468,
         "INVENTORY": 0x3326738, "WORLD": 0x346BF98, "MOTION": 0x3326558, "AVATARS": 0x3326D20, "VEHICLES": 0x3326668,
         "WIELDER": 0x3326420, "UNITS": 0x3326508, "LOADOUTS": 0x347CE50, "NET": 0x347CEF0, "PACKAGES": 0x347CEB0,
         "GAME_STATE": 0x3326340, "GAME_MODE": 0x33266A0, "EVENTS": 0x3326D68, "WORLD_RECORDS": 0xF32F18,
         "STATE_VALUE": 0xAC21C, "LOADOUT_COUNT": 0x2D200, "EQUIP": 0x9AE030, "SELECT": 0x9AAA50, "PLAYER_SELECT": 0xA93E90,
         "REMOVE": 0xFDC310, "ZONE_SET": 0x978610, "DEFAULTS": 0x74B1B0, "PACKAGE_OF": 0x4F5680}
HEADER = """
#define HF_ITEM_AR23_LIBERATOR 0xAA035309u               /* AR-23 Liberator */
#define HF_ITEM_MG206_HEAVY_MACHINE_GUN 0x1FC9CA61u      /* MG-206 Heavy Machine Gun */
#define HF_STRATAGEM_EXO45_PATRIOT 27u                   /* EXO-45 Patriot Exosuit - a Pelican drop (delivery kind 3) */
#define HF_STRATAGEM_MG206_HEAVY_MACHINE_GUN 32u         /* MG-206 Heavy Machine Gun - Heavy_Mg, stable id 1fc9ca61 */
#define HF_ENTITY_AR23_LIBERATOR 0x968211C0033DCE64ull   /* AR-23 Liberator */
#define HF_ENTITY_AR23P_LIBERATOR_PENETRATOR 0x43CB1033961A2276ull /* AR-23P Liberator Penetrator */
#define HF_ENTITY_MG206_HEAVY_MACHINE_GUN 0x2152D5147B0AC418ull /* MG-206 Heavy Machine Gun - content/fac_helldivers/equipment/support_weapons/heavy_mg */
#define HF_ENTITY_EXO45_PATRIOT_EXOSUIT 0x79E4B3D2DA5E45E3ull /* EXO-45 Patriot Exosuit */
#define HF_ENTITY_EXO49_EMANCIPATOR_EXOSUIT 0xC2D449ECF7FACAB1ull /* EXO-49 Emancipator Exosuit */
"""


class Sites(unittest.TestCase):
    def setUp(self):
        self.sites = levers.load_sites()

    def test_table_shape(self):
        for name, site in self.sites.items():
            self.assertEqual(site["module"], "game.dll", name)
            self.assertIn(site["kind"], ("start", "rip", "u32"), name)
            self.assertRegex(site["pattern"], r"^([0-9a-f]{2}|\?\?)+$", name)
            self.assertGreaterEqual(len(site["pattern"]) // 2 - site["pattern"].count("??"), 8, name)    # enough fixed bytes
        self.assertLessEqual(set(KNOWN), set(self.sites))

    def test_pattern_and_value_on_a_made_image(self):
        site = {"rva": 4, "kind": "rip", "pattern": "488b05????????90", "at": 3, "size": 7}
        image = b"\0" * 4 + bytes.fromhex("488b05") + struct.pack("<i", 0x100) + b"\x90" + b"\0" * 8
        self.assertEqual(levers.site_value(image, site), 4 + 7 + 0x100)
        self.assertEqual(levers.site_value(image, dict(site, kind="u32", minus=8)), 0x100 - 8)
        self.assertEqual(levers.site_value(image, {"rva": 4, "kind": "start", "pattern": "488b05"}), 4)
        with self.assertRaises(levers.LeverError):                      # one byte off: refused, never guessed
            levers.site_value(image.replace(b"\x90", b"\x91"), site)

    @unittest.skipUnless(IMAGE.exists(), "the saved image of game.dll is not on this machine")
    def test_sites_in_the_saved_image(self):
        image = IMAGE.read_bytes()
        for name, site in self.sites.items():
            hits = [m.start() for m in levers.pattern_regex(site["pattern"]).finditer(image)]
            self.assertEqual(hits, [site["rva"]], name)                 # found once, where the table says
        for name, value in KNOWN.items():
            self.assertEqual(levers.site_value(image, self.sites[name]), value, name)

    def test_lua_table_and_kernel(self):
        text = levers.sites_lua(self.sites)
        self.assertIn('SPAWN={module="game.dll",rva=0xFD9710,kind="start"', text)
        source = levers.kernel_source()
        self.assertTrue(source.startswith('local VERSION="'))
        self.assertNotEqual(source.split("\n")[0], levers.kernel_source({"X": dict(self.sites["SPAWN"])}).split("\n")[0])   # a changed table is a new kernel
        luajit = shutil.which("luajit")
        if luajit:
            with tempfile.NamedTemporaryFile("w", suffix=".lua") as handle:
                handle.write(source); handle.flush()
                self.assertEqual(subprocess.run([luajit, "-bl", handle.name], capture_output=True).returncode, 0)

    def test_kernel_writes_and_calls_are_guarded(self):
        kernel = levers.KERNEL_FILE.read_text()
        self.assertIn("r.state ~= 0x1000 or r.protection ~= 0x04 or r.type == 0x1000000", kernel)     # data pages only
        self.assertIn("K.journal[#K.journal + 1]", kernel)
        self.assertNotIn("ffi.cast('uint8_t *', address)", kernel)       # no raw dereference of a game address
        casts = [line.strip() for line in kernel.splitlines() if "ffi.cast(ctype" in line]
        self.assertEqual(len(casts), 2)       # a game function from its checked site, a loader export by its name: no other
        self.assertIn("f = { at = at, call = ffi.cast(ctype, at) }", casts[0])
        self.assertIn("K.fn[key] = { call = ffi.cast(ctype, p) }", casts[1])


class Names(unittest.TestCase):
    def setUp(self):
        self.names = levers.parse_content(HEADER)

    def test_parse(self):
        self.assertEqual(self.names["item"]["AR-23 Liberator"], 0xAA035309)
        self.assertEqual(self.names["entity"]["EXO-45 Patriot Exosuit"], "79e4b3d2da5e45e3")
        self.assertEqual(self.names["stratagem"], {"EXO-45 Patriot Exosuit": 27, "MG-206 Heavy Machine Gun": 32})

    def test_pick(self):
        self.assertEqual(levers.pick(self.names["entity"], "ar-23 liberator")[0], "AR-23 Liberator")       # the exact name
        self.assertEqual(levers.pick(self.names["entity"], "liberator")[0], "AR-23 Liberator")             # the shortest
        self.assertEqual(levers.pick(self.names["entity"], "heavy machine")[1], "2152d5147b0ac418")
        with self.assertRaises(levers.LeverError):
            levers.pick(self.names["entity"], "railgun")
        with self.assertRaises(levers.LeverError) as raised:                                                # two as short
            levers.pick({"EXO-45 X": 1, "EXO-49 X": 2}, "exo")
        self.assertIn("several", str(raised.exception))


class Body(unittest.TestCase):
    def record(self, x, y, z, limits=(200.0, 100.0)):
        raw = bytearray(0x80)
        struct.pack_into("<fff", raw, 0, x, y, z); struct.pack_into("<ffff", raw, 0x10, 0, 0, 0, 1)
        struct.pack_into("<f", raw, 0x4C, limits[0]); struct.pack_into("<f", raw, 0x5C, limits[1])
        return bytes(raw)

    def test_candidates(self):
        feet = (8.63, 0.15, 12.08)
        data = self.record(1, 2, 3) + self.record(8.63, 0.15, 13.055) + self.record(8.63, 0.15, 13.0, (3.4e38, 3.4e38)) \
            + self.record(8.63, 0.15, 12.08) + self.record(8.635, 0.151, 13.0)
        self.assertEqual(levers.body_candidates(data, 0x1000, feet), [0x1000 + 0x80, 0x1000 + 0x200])   # the capsules only
        self.assertEqual(levers.body_candidates(data[:0x90], 0x1000, feet), [])                         # cut short: no read past


class FakeGame:
    """Stands in for levers.Game: answers the kernel calls the levers make and keeps their order."""

    def __init__(self, bodies_ok=True):
        self.calls, self.hand, self.entity, self.slots, self.bodies_ok = [], 474, 500, [124, 33], bodies_ok
        self.pos = [1.0, 2.0, 3.0]

    def lua(self, body, timeout=40):
        self.calls.append(body)
        if body.startswith("return K.where()"):
            return {"x": self.pos[0], "y": self.pos[1], "z": self.pos[2], "facing_x": 0.0, "facing_y": 1.0, "raw": "00" * 12,
                    "in_hand": self.hand, "primary": 474, "secondary": 476, "support": 0, "active": 1}
        if "K.ready(" in body:
            return ["resident", "aaaaaaaaaaaaaaaa"]
        if "K.equip(" in body:
            self.entity += 1
            return {"entity": self.entity, "old": 474, "attachments": ["Vertical Grip (slot 1)"], "mods": []}
        if body.startswith("K.take("):
            self.hand = self.entity; return True
        if body.startswith("return K.remove("):
            return True
        if body.startswith("return K.teleport("):
            if not self.bodies_ok and "{4096}" in body:
                raise levers.LeverError("refused: no physics body proven to be the Helldiver's among 1 candidates")
            x, y, z = (float(v) for v in body.split("}, ")[1].rstrip(")").split(", "))
            self.pos = [x, y, z]; return {"from": [1, 2, 3], "to": [x, y, z], "body": "0x2000"}
        if body.startswith("return K.stratagem_row("):
            return {"package": "bbbbbbbbbbbbbbbb", "item": 0x1FC9CA61 if "(32)" in body else 0, "traits": 0}
        if body.startswith("return K.load("):
            return "resident"
        if body.startswith("return K.add_stratagem("):
            self.slots.append(int(body.split("(")[1].rstrip(")"))); return len(self.slots)
        if body.startswith("return K.stratagems()"):
            return [{"slot": i + 1, "type": t, "uses": 0xFFFFFFFF} for i, t in enumerate(self.slots)]
        raise AssertionError("unexpected kernel call: " + body)


class Work(unittest.TestCase):
    def tool(self, game, **more):
        return levers.Levers(game=game, names=levers.parse_content(HEADER), sleep=lambda s: None, **more)

    def test_loadout_order(self):
        game = FakeGame()
        result = self.tool(game).loadout("primary", "liberator", ["Vertical Grip"])
        kinds = [c.split("(")[0].split()[-1] for c in game.calls]
        self.assertLess(game.calls.index(next(c for c in game.calls if "K.ready(" in c)), game.calls.index(next(c for c in game.calls if "K.spawn(" in c)))
        spawn = next(c for c in game.calls if "K.spawn(" in c)
        self.assertIn('K.customization("968211c0033dce64",{"Vertical Grip"})', spawn)         # defaults, the grip in its slot
        self.assertIn("K.equip(e,1)", spawn)
        self.assertLess(game.calls.index(spawn), game.calls.index("K.take(1) return true"))
        self.assertEqual(game.calls[-2], "return K.remove(474)")                              # the displaced one, last
        self.assertTrue(result["in_hand"]); self.assertEqual(result["entity"], 501); self.assertIn("K.where", kinds[-1])
        with self.assertRaises(levers.LeverError):
            self.tool(FakeGame()).loadout("grenade", "liberator")

    def test_teleport_remembers_and_finds_again(self):
        with tempfile.TemporaryDirectory() as folder:
            memory = Path(folder) / "bodies.json"
            scans = []
            tool = self.tool(FakeGame(), bodies=lambda raw: scans.append(raw) or [0x2000], memory=memory)
            self.assertTrue(tool.teleport(5, 6)["moved"])
            self.assertTrue(tool.ahead(3)["moved"])
            self.assertEqual(len(scans), 1)                                                   # the second went by memory
            self.assertEqual(json.loads(memory.read_text()), {"bodies": [0x2000]})
            memory.write_text(json.dumps({"bodies": [4096]}))                                 # a stale address
            stale = self.tool(FakeGame(bodies_ok=False), bodies=lambda raw: scans.append(raw) or [0x2000], memory=memory)
            self.assertTrue(stale.teleport(5, 6)["moved"])
            self.assertEqual(len(scans), 2)
            with self.assertRaises(levers.LeverError):
                self.tool(FakeGame(), bodies=lambda raw: []).teleport(1, 1)

    def test_stratagems_add_and_load_first(self):
        game = FakeGame()
        rows = self.tool(game).stratagems(["patriot", "MG-206"])
        self.assertEqual([r["type"] for r in rows], [124, 33, 27, 32])                        # added after, nothing replaced
        self.assertEqual(rows[2]["name"], "EXO-45 Patriot Exosuit")
        add = game.calls.index("return K.add_stratagem(32)")
        self.assertTrue(any("K.ready(\"2152d5147b0ac418\")" in c for c in game.calls[:add]))  # the weapon's own package first
        self.assertTrue(any(c.startswith("return K.load(") for c in game.calls[:game.calls.index("return K.add_stratagem(27)")]))

    def test_not_built_levers_say_why(self):
        for word in ("armor", "grenade", "beacon", "enter"):
            self.assertGreater(len(levers.NOT_BUILT[word]), 40)
        self.assertEqual(levers.main(["levers.py", "armor", "x"]), 1)
        self.assertEqual(levers.main(["levers.py", "spawn", "beacon", "x"]), 1)


class Phases(unittest.TestCase):
    def test_shapes(self):
        self.assertEqual(set(phases_levers.PHASES), {"foregrip-liberator", "foregrip-adjudicator", "support-weapon",
                                                     "stratagem-throw", "exosuit-seat", "prone-melee"})
        import drive
        for name, (script, settle) in phases_levers.PHASES.items():
            self.assertTrue(drive.events(script), name)                 # every word is one the helper knows
        self.assertTrue(drive.events(phases_levers.CALL_EXOSUIT))
        self.assertEqual(phases_levers.front_of({"x": 1, "y": 2, "z": 3, "facing_x": 0, "facing_y": -1}, 2), (1, 0, 3.1))

    def test_reading(self):
        lines = ["10:00:01.000 Seat: in a vehicle's seat (an exosuit's, an FRV's): first person kept",
                 "10:00:02.000 Seat: +30 frames from the seat (+0 refused, +4 gliding), eye (0.00 0.10 0.60) m",
                 "10:00:02.100 Seat: cockpit: seen through: the suit 0x23e, found by its seat",
                 "10:00:09.000 Seat: left the seat: the diver and the vehicle as the game draws them",
                 "10:00:09.100 Weapon: key=aa name=\"AR-23 Liberator\"", "10:00:09.200 Grip: on the grip"]
        facts = phases_levers.seat_facts(lines)
        self.assertEqual((facts["entered"], facts["left"], facts["seat_frames"], facts["gliding_frames"], facts["seen_through"]), (True, True, 30, 4, True))
        self.assertFalse(phases_levers.seat_facts(lines[4:])["entered"])
        self.assertEqual(phases_levers.kept("foregrip-liberator", lines)["Grip:"]["count"], 1)
        self.assertIn("refused: x", phases_levers.summary({"refused": "refused: x"}))
        self.assertIn("with Vertical Grip", phases_levers.summary({"name": "AR-23 Liberator", "entity": 5, "in_hand": True, "attachments": ["Vertical Grip"]}))

    def test_setup_uses_the_levers(self):
        game = FakeGame()
        tool = levers.Levers(game=game, names=levers.parse_content(HEADER), sleep=lambda s: None)
        report = phases_levers.setup("foregrip-liberator", tool)
        self.assertEqual(report["name"], "AR-23 Liberator")
        self.assertEqual(phases_levers.setup("prone-melee", tool), {})
        self.assertIn("refused", phases_levers.setup("foregrip-adjudicator", tool))          # not in this header: said, not raised


if __name__ == "__main__":
    unittest.main()
