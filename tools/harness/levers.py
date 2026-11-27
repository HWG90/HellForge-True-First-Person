#!/usr/bin/env python3
"""Test levers for a HellForge harness session: change the loadout and the stratagems of the running mission, spawn
items, support weapons, stratagem beacons and exosuits, and move the player (docs/design/HARNESS.md "Levers").

They work from inside the game: the game's Lua has LuaJIT's ffi, so levers.lua (the kernel) reads and writes the
game's memory and calls the game's own functions, the way the user's other HellForge mods do. Nothing is sent to the
backend and nothing the account owns changes: every lever acts on the running mission's entities.

  levers.py where                         the player's entity, position, facing and what it carries
  levers.py teleport X Y [Z]              to map coordinates (Z: the ground there when left out)
  levers.py ahead METRES                  that far along the way the body faces
  levers.py loadout SLOT NAME [--grip]    primary | secondary | support: a weapon into the hands' slot, by name
  levers.py armor NAME                    (not built: see HARNESS.md)
  levers.py stratagems [NAME NAME ...]    the equipped stratagems; with names, each is added as a new slot
  levers.py spawn item NAME [METRES]      a pick-up on the ground ahead
  levers.py spawn exosuit [NAME]          an exosuit ahead
  levers.py spawn beacon NAME             a stratagem called in at the player
  levers.py vehicles                      the exosuits and vehicles in the world, with their positions
  levers.py attachments WEAPON            the customization items the game knows near a weapon's defaults
  levers.py names PATTERN                 what a name matches (items, entities, stratagems)
  levers.py undo [N]                      put back the newest N memory writes
  levers.py log                           what the kernel wrote and called this session
  levers.py lua 'CODE'                    Lua with the kernel as K (for probes)

Each lever refuses with a reason instead of acting on anything it could not prove: the code of every game function
is compared with its reviewed pattern before each call, moved offsets are read from the game's instructions, and
only private read-write data pages are written (the bytes before are kept for `undo`).
"""
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
HARNESS = Path(os.environ.get("HD2H_DIR", Path.home() / "hellforge-research/tools/BingusDevelopmentHarness"))
SDK = Path(os.environ.get("HF_SDK", Path.home() / ".local/share/hellforge-prefix/pfx/drive_c/users/steamuser/Documents/HellForge/app/sdk"))
SITES_FILE = HERE / "levers_sites.json"
KERNEL_FILE = HERE / "levers.lua"
VERSION = "1"


class LeverError(RuntimeError):
    """A lever refused or failed; the text says why."""


# ------------------------------------------------------------------------------------------------ sites --
def load_sites(path=SITES_FILE):
    return json.loads(Path(path).read_text())["sites"]


def pattern_regex(pattern):
    """A site's pattern (hex pairs, ?? any byte) as a bytes regex."""
    pairs = re.findall(r"..", pattern.replace(" ", ""))
    return re.compile(b"".join(b"." if p == "??" else re.escape(bytes([int(p, 16)])) for p in pairs), re.S)


def site_value(image, site):
    """What a site names in an image of game.dll (a file laid out as in memory): a function's RVA, a global's RVA
    or an offset read from the instruction. Raises LeverError where the pattern is not at its place."""
    rva, pattern = site["rva"], site["pattern"]
    size = len(pattern) // 2
    if not pattern_regex(pattern).fullmatch(image[rva:rva + size]):
        raise LeverError("the code at game+0x%X differs from the pattern" % rva)
    if site["kind"] == "start":
        return rva
    at = site["at"]
    if site["kind"] == "rip":
        return rva + site["size"] + int.from_bytes(image[rva + at:rva + at + 4], "little", signed=True)
    return int.from_bytes(image[rva + at:rva + at + 4], "little") - site.get("minus", 0)


def lua_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def sites_lua(sites):
    rows = []
    for name in sorted(sites):
        s = sites[name]
        fields = ['module=%s' % lua_string(s["module"]), "rva=0x%X" % s["rva"], 'kind=%s' % lua_string(s["kind"]),
                  'pattern=%s' % lua_string(s["pattern"])]
        if s["kind"] != "start":
            fields += ["at=%d" % s["at"], "size=%d" % s["size"], "minus=%d" % s.get("minus", 0)]
        rows.append("  %s={%s}," % (name, ",".join(fields)))
    return "{\n" + "\n".join(rows) + "\n}"


def kernel_source(sites=None):
    sites = load_sites() if sites is None else sites
    body, table = KERNEL_FILE.read_text(), sites_lua(sites)
    stamp = "%s:%08x" % (VERSION, zlib.crc32((table + body).encode()))    # a changed kernel or table goes in anew
    return "local VERSION=%s local SITES=%s\n%s" % (lua_string(stamp), table, body)


# ------------------------------------------------------------------------------------------------- game --
def run_lua(source, timeout=40):
    """Runs Lua in the game through the Bingus harness; returns the list of returned values. A refusal or a Lua
    error raises LeverError with the game's text."""
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as handle:
        handle.write(source)
    try:
        out = subprocess.run([sys.executable, "harness.py", "lua", "--file", handle.name, "--timeout", str(timeout)],
                             cwd=HARNESS, env=dict(os.environ, HD2H_TARGET="hellforge"), capture_output=True,
                             text=True, timeout=timeout + 20)
    finally:
        os.unlink(handle.name)
    start = out.stdout.find("{")
    if start < 0:
        raise LeverError("the harness gave no reply: %r %r" % (out.stdout[-300:], out.stderr[-300:]))
    reply = json.loads(out.stdout[start:])
    if reply.get("error") or reply.get("ok") is False:
        text = str(reply.get("error"))
        text = re.sub(r"^.*?refused: ", "refused: ", text.split("\nstack traceback")[0], flags=re.S)
        raise LeverError(text)
    return reply.get("values") or []


class Game:
    """The running game with the kernel in it. `run` may be replaced for tests (a fake game)."""

    def __init__(self, run=run_lua):
        self.run = run
        self.installed = False

    def install(self):
        if not self.installed:
            self.run(kernel_source())
            self.installed = True

    def lua(self, body, timeout=40):
        """Lua with the kernel as K; returns the first value."""
        self.install()
        values = self.run("local K=rawget(_G,'TFP_LEVERS') if not K then error('refused: the kernel is gone') end\n" + body, timeout)
        return values[0] if values else None

    def log(self):
        return self.lua("return K.log") or []



# ------------------------------------------------------------------------------------------------ names --
CONTENT_LINE = re.compile(r"#define HF_(ITEM|ENTITY|STRATAGEM)_\w+ (0x[0-9A-Fa-f]+|\d+)u?(?:ll)? +/\* (.*?) \*/")


def parse_content(text):
    """HellForge's hellforge_content.h -> {'item': {name: loadout id}, 'entity': {name: 16 hex digits},
    'stratagem': {name: type}}. A name is the comment up to its first ' - '."""
    out = {"item": {}, "entity": {}, "stratagem": {}}
    for kind, value, comment in CONTENT_LINE.findall(text):
        name = comment.split(" - ")[0].strip()
        number = int(value, 0)
        out[kind.lower()].setdefault(name, "%016x" % number if kind == "ENTITY" else number)
    return out


def content(sdk=None):
    return parse_content(((sdk or SDK) / "hellforge_content.h").read_text(errors="replace"))


def pick(table, wanted, what="name"):
    """The entry a few words name: the exact name, else the shortest name holding every word. Returns (name, value);
    raises LeverError listing what else matched when nothing or nothing clear does."""
    words = wanted.lower().split()
    if not words:
        raise LeverError("give a %s" % what)
    for name in table:
        if name.lower() == wanted.lower():
            return name, table[name]
    hits = sorted((n for n in table if all(w in n.lower() for w in words)), key=lambda n: (len(n), n))
    if not hits:
        raise LeverError('no %s is called "%s"' % (what, wanted))
    if len(hits) > 1 and len(hits[0]) == len(hits[1]):
        raise LeverError('"%s" names several: %s' % (wanted, ", ".join(hits[:8])))
    return hits[0], table[hits[0]]


SLOTS = {"primary": 1, "secondary": 2, "sidearm": 2, "support": 3}
EXOSUIT = "EXO-45 Patriot Exosuit"


# ----------------------------------------------------------------------------------------- physics body --
def game_pid():
    for proc in Path("/proc").glob("[0-9]*"):
        try:
            if proc.stat().st_uid == os.getuid() and (proc / "cmdline").read_bytes().split(b"\0", 1)[0].lower().endswith(b"game\\bin\\helldivers2.exe"):
                return int(proc.name)
        except OSError:
            continue
    raise LeverError("no running game")


def body_candidates(data, base, position, slack=0.02):
    """Addresses in one stretch of memory (bytes at `base`, 16-byte aligned) that could be the physics body carrying
    the Helldiver at `position` (x, y, z of its feet): a centre within `slack` in x and y and 0.3 to 1.8 m above the
    feet, with the speed limits 200 and 100 after it (+0x4C, +0x5C)."""
    import struct
    x, y, z = position
    rows = len(data) // 16
    hits = []
    try:
        import numpy
        cells = numpy.frombuffer(data, dtype="<f4", count=rows * 4).reshape(rows, 4)
        with numpy.errstate(invalid="ignore"):
            near = (numpy.abs(cells[:, 0] - x) < slack) & (numpy.abs(cells[:, 1] - y) < slack) & (cells[:, 2] - z >= 0.3) & (cells[:, 2] - z <= 1.8)
        hits = [int(i) * 16 for i in numpy.nonzero(near)[0]]
    except ImportError:
        for at in range(0, rows * 16, 16):
            bx, by, bz = struct.unpack_from("<fff", data, at)
            if abs(bx - x) < slack and abs(by - y) < slack and 0.3 <= bz - z <= 1.8:
                hits.append(at)
    out = []
    for at in hits:
        if at + 0x60 <= len(data) and struct.unpack_from("<f", data, at + 0x4C) + struct.unpack_from("<f", data, at + 0x5C) == (200.0, 100.0):
            out.append(base + at)
    return out


def find_bodies(raw_hex, pid=None):
    """Reads the game's memory from Linux (read only, as peek.py does) for the Helldiver's physics body."""
    import struct
    position = struct.unpack("<fff", bytes.fromhex(raw_hex))
    pid = pid or game_pid()
    found, chunk = [], 1 << 26
    with open("/proc/%d/mem" % pid, "rb", 0) as memory:
        for line in Path("/proc/%d/maps" % pid).read_text().splitlines():
            parts = line.split(None, 5)
            start, end = (int(x, 16) for x in parts[0].split("-"))
            if "rw" not in parts[1] or (len(parts) > 5 and parts[5].startswith("[v")):
                continue
            at = start
            while at < end:
                size = min(chunk, end - at)
                try:
                    memory.seek(at)
                    data = memory.read(size + 0x60)
                except (OSError, ValueError, OverflowError):
                    data = b""
                found += body_candidates(data, at - at % 16, position) if at % 16 == 0 else []
                at += size
    return sorted(set(found))


# ----------------------------------------------------------------------------------------------- levers --
def lua_list(values):
    return "{" + ",".join(lua_string(v) if isinstance(v, str) else repr(v) for v in values) + "}"


class Levers:
    """The levers, each returning a dict of what it found and did. `game` runs the Lua; `names` is the parsed
    content header; `bodies` finds the physics body candidates; `sleep` waits (replaced in tests)."""

    def __init__(self, game=None, names=None, bodies=find_bodies, sleep=time.sleep, memory=None):
        self.game = game or Game()
        self._names = names
        self.bodies = bodies
        self.sleep = sleep
        self.memory = memory                      # a file that keeps the body's address between commands
        self.known_bodies = self.recall()

    def recall(self):
        try:
            return [int(a) for a in json.loads(Path(self.memory).read_text())["bodies"]] if self.memory else []
        except (OSError, ValueError, KeyError):
            return []

    def remember(self, candidates):
        if self.memory:
            try:
                Path(self.memory).parent.mkdir(parents=True, exist_ok=True)
                Path(self.memory).write_text(json.dumps({"bodies": candidates}))
            except OSError:
                pass

    @property
    def names(self):
        if self._names is None:
            self._names = content()
        return self._names

    def where(self):
        return self.game.lua("return K.where()")

    def teleport(self, x, y, z=None, settle=0.6):
        """Moves the Helldiver's physics body. Z left out keeps the height (the game drops or lifts the body to the
        ground once it moves). The body's address is remembered for the session; the kernel proves it each time."""
        here = self.where()
        z = here["z"] + 0.05 if z is None else z
        call = "return K.teleport({%s}, %r, %r, %r)"
        done = None
        if self.known_bodies:
            try:
                done = self.game.lua(call % (",".join("%d" % c for c in self.known_bodies), float(x), float(y), float(z)))
            except LeverError as error:
                if "physics bod" not in str(error):
                    raise
        if done is None:
            candidates = self.bodies(here["raw"])
            if not candidates:
                raise LeverError("refused: no physics body found at the Helldiver (on foot, standing)")
            done = self.game.lua(call % (",".join("%d" % c for c in candidates), float(x), float(y), float(z)))
            self.known_bodies = candidates
            self.remember(candidates)
        self.sleep(settle)
        after = self.where()
        done["after"] = [after["x"], after["y"], after["z"]]
        done["moved"] = ((after["x"] - x) ** 2 + (after["y"] - y) ** 2) ** 0.5 < 1.5
        return done

    def ahead(self, metres):
        here = self.where()
        return self.teleport(here["x"] + here["facing_x"] * metres, here["y"] + here["facing_y"] * metres)

    def ready(self, resource, seconds=60):
        """Loads an entity's package and waits for it; returns the package."""
        end = time.monotonic() + seconds
        while True:
            state, package = self.game.lua("local s,p=K.ready(%s) return {s,p}" % lua_string(resource))
            if state == "resident":
                return package
            if time.monotonic() > end:
                raise LeverError("refused: package %s was still loading after %d s" % (package, seconds))
            self.sleep(0.5)

    def entity(self, name):
        if re.fullmatch(r"[0-9a-fA-F]{16}", name):
            return name.lower(), name.lower()
        return pick(self.names["entity"], name, "entity")

    def loadout(self, slot, name, attachments=(), take=True, wait=6.0):
        """A weapon into a hand slot of the running mission: the game's spawn with the weapon's default
        customization (and the named attachments in their slots), the game's equip, the weapon controller's
        selection, then the displaced weapon removed."""
        number = SLOTS.get(slot) or _refuse("a slot is primary, secondary or support")
        full, resource = self.entity(name)
        package = self.ready(resource)
        here = self.where()
        made = self.game.lua(
            "local mods,said=K.customization(%s,%s) local w=K.where() local e=K.spawn(%s,w.x,w.y,w.z+0.4,mods) "
            "local old=K.equip(e,%d) return {entity=e,old=old,attachments=said,mods=mods}"
            % (lua_string(resource), lua_list(list(attachments)), lua_string(resource), number))
        result = {"slot": slot, "name": full, "resource": resource, "package": package, "entity": made["entity"],
                  "displaced": made["old"], "attachments": made.get("attachments") or [], "before": here}
        if take:
            self.sleep(0.2)
            self.game.lua("K.take(%d) return true" % number)
            end = time.monotonic() + wait
            while time.monotonic() < end:
                self.sleep(0.3)
                now = self.where()
                if now["in_hand"] == made["entity"]:
                    break
            result["in_hand"] = now["in_hand"] == made["entity"]
        old = made["old"]
        if old and old not in (0, 0xFFFFFFFF) and old != made["entity"]:
            self.sleep(0.3)
            try:
                result["removed"] = bool(self.game.lua("return K.remove(%d)" % old))
            except LeverError as error:
                result["removed"] = str(error)
        result["after"] = self.where()
        return result

    def stratagems(self, names=None):
        """Without names: the equipped slots. With names: adds each as a slot of its own after the last (a type
        already equipped stays where it is); the mission's own stratagems are never replaced."""
        by_type = {v: k for k, v in self.names["stratagem"].items()}
        if names:
            for wanted in names:
                full, kind = pick(self.names["stratagem"], wanted, "stratagem")
                row = self.game.lua("return K.stratagem_row(%d)" % kind)
                if row.get("package"):
                    self.load(row["package"])
                item = row.get("item") or 0
                for item_name, item_id in self.names["item"].items():       # a support weapon's own package
                    if item and item_id == item and item_name in self.names["entity"]:
                        self.ready(self.names["entity"][item_name])
                self.game.lua("return K.add_stratagem(%d)" % kind)
        rows = self.game.lua("return K.stratagems()")
        for row in rows:
            row["name"] = by_type.get(row["type"], "type %d" % row["type"])
        return rows

    def load(self, package, seconds=60):
        end = time.monotonic() + seconds
        while self.game.lua("return K.load(%s)" % lua_string(package)) != "resident":
            if time.monotonic() > end:
                raise LeverError("refused: package %s was still loading after %d s" % (package, seconds))
            self.sleep(0.5)
        return package

    def spawn_item(self, name, metres=2.0):
        """A pick-up on the ground ahead: the game's spawn with the default customization, its ground zone opened."""
        full, resource = self.entity(name)
        package = self.ready(resource)
        entity = self.game.lua(
            "local w=K.where() local mods=K.customization(%s,{}) "
            "return K.spawn(%s, w.x+w.facing_x*%r, w.y+w.facing_y*%r, w.z+0.4, #mods>0 and mods or nil)"
            % (lua_string(resource), lua_string(resource), float(metres), float(metres)))
        self.sleep(0.5)
        zone = self.game.lua("local z=K.zone(%d,1) if z and z.zone1.handle<0 then z=K.zone(%d,1,true) end return z or false" % (entity, entity))
        self.sleep(0.5)
        zone = self.game.lua("return K.zone(%d,1) or false" % entity) or zone
        return {"name": full, "resource": resource, "package": package, "entity": entity, "pickup": zone}

    def spawn_exosuit(self, name=EXOSUIT, metres=4.0):
        """An exosuit standing ahead, turned to face the Helldiver (the entity itself, no Pelican)."""
        full, resource = self.entity(name)
        if "exosuit" not in full.lower() and not re.fullmatch(r"[0-9a-f]{16}", full):
            raise LeverError('refused: "%s" is not an exosuit' % full)
        package = self.ready(resource)
        entity = self.game.lua("local w=K.where() return K.spawn(%s, w.x+w.facing_x*%r, w.y+w.facing_y*%r, w.z+0.3, nil, "
                               "math.atan2(w.facing_x, -w.facing_y))" % (lua_string(resource), float(metres), float(metres)))
        self.sleep(0.5)
        described = self.game.lua("return K.descriptor(%d) or false" % entity)
        return {"name": full, "resource": resource, "package": package, "entity": entity, "descriptor": described}

    def vehicles(self):
        return self.game.lua("return K.vehicles()") or []

    def attachments(self, name):
        full, resource = self.entity(name)
        self.ready(resource)
        found = self.game.lua("local all,defs=K.attachments(%s) return {all=all,defaults=defs}" % lua_string(resource))
        found["name"] = full
        return found

    def undo(self, count=None):
        done, left = self.game.lua("local d,l=K.undo(%s) return {d,l}" % ("nil" if count is None else int(count)))
        return {"undone": done, "left": left}


def _refuse(text):
    raise LeverError("refused: " + text)


NOT_BUILT = {
    "armor": "armor is the customization manager's, set when the Helldiver is made: no in-mission call was found",
    "grenade": "the throwable is a count in the inventory, not an entity in a slot: no call was proven for it",
    "beacon": "ActivateStratagemBeacon takes four arguments nobody has reviewed: equip the stratagem (levers.py "
              "stratagems) and throw it with the keys, or spawn what it delivers (spawn item / spawn exosuit)",
    "enter": "no call that seats the Helldiver was found: walk up and hold E (phases_levers.py does)",
}


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 2
    levers = Levers(memory=Path(tempfile.gettempdir()) / ("tfp-levers-%d.json" % os.getuid()))
    command, rest = argv[1], argv[2:]
    try:
        if command == "lua":
            out = levers.game.lua(rest[0])
        elif command == "log":
            print("\n".join(levers.game.log())); return 0
        elif command == "where":
            out = levers.where()
        elif command == "teleport":
            out = levers.teleport(float(rest[0]), float(rest[1]), float(rest[2]) if len(rest) > 2 else None)
        elif command == "ahead":
            out = levers.ahead(float(rest[0]))
        elif command == "loadout":
            grips = [a.split("=", 1)[1] for a in rest if a.startswith("--with=")]
            if "--grip" in rest: grips.append("Vertical Grip")
            words = [a for a in rest[1:] if not a.startswith("--")]
            if rest[0] in NOT_BUILT: _refuse(NOT_BUILT[rest[0]])
            out = levers.loadout(rest[0], " ".join(words), grips, take="--keep-hand" not in rest)
        elif command in NOT_BUILT:
            _refuse(NOT_BUILT[command])
        elif command == "stratagems":
            out = levers.stratagems(rest or None)
        elif command == "spawn":
            if rest[0] == "item":
                metres = float(rest[-1]) if len(rest) > 2 and re.fullmatch(r"[\d.]+", rest[-1]) else 2.0
                out = levers.spawn_item(" ".join(rest[1:] if metres == 2.0 and not re.fullmatch(r"[\d.]+", rest[-1]) else rest[1:-1]), metres)
            elif rest[0] == "exosuit":
                out = levers.spawn_exosuit(" ".join(rest[1:]) or EXOSUIT)
            elif rest[0] in NOT_BUILT:
                _refuse(NOT_BUILT[rest[0]])
            else:
                print(__doc__); return 2
        elif command == "vehicles":
            out = levers.vehicles()
        elif command == "attachments":
            out = levers.attachments(" ".join(rest))
        elif command == "names":
            names = content(); words = " ".join(rest).lower().split()
            out = {kind: sorted(n for n in table if all(w in n.lower() for w in words)) for kind, table in names.items()}
        elif command == "undo":
            out = levers.undo(int(rest[0]) if rest else None)
        else:
            print(__doc__); return 2
    except LeverError as error:
        print(str(error), file=sys.stderr); return 1
    print(json.dumps(out, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
