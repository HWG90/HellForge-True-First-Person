"""The scripted world of the head-visibility and model-shadow-source vectors: memory, snapshot
fields, SDK handles and rig meshes as tokens for tools/retail_ref/run_visibility.lua and
tests/visibility_fake.h (which both read the same script text)."""
import hashlib
import subprocess
import sys

FROZEN = "1d0d453"
EXE = 0x140000000
RIG_RESOURCE = bytes([237, 139, 160, 60, 73, 77, 207, 208])
PINS = {0x3f4ca0: "488bd1e9a8f8ffff", 0x51fe80: "40574883ec30440fb689dc000000",
        0x3f4c23: "3b3d970e48027504b101eb363b3d930e48027504b102eb2a",
        0x51feaa: "84d27405450ac8eb0641f6d04522c8488b5908448889dc000000",
        0xa06d0: "48895c24084889742410574883ec20488b351270a101"}
PADS = [("g_undergarment", 0x20F17556, 54), ("g_r_elbowpad", 0xF589D526, 55), ("g_l_elbowpad", 0x69502AD0, 56),
        ("g_l_shoulderpad", 0x44E19D12, 57), ("g_r_shoulderpad", 0xCB0E1712, 58)]
# Function codes of the `B<code>=<mode>` token (0: absent, 1: the function, 2: another function that does the same).
MAIN_WORLD, UNITS, SET, HAS_MESH, MESH, MESH_NAME, MESH_NODE, MESH_MATERIALS, MESH_HAS_MATERIAL = range(1, 10)
PLAYER_SNAPSHOT, PIN_SNAPSHOT, BACKPACK, FOREIGN, FALLBACK, PINNED, FULL_BODY, HIDE_TORSO, SHOW_TORSO, SEATED_HEAD, DESCRIPTOR = range(10, 21)
DEFAULT_VARS = dict(exe=EXE, exe_fail=0, tick=100000, mode=1, enabled=1, foreign=0, fallback=1, pinned=0, full_body=0, hide_torso=0,
                    show_torso=0, seated_head=1, pack_mode=0, pack_ref=0, act_mode=1, on_foot=1, full_fail=0, player_fail=0,
                    pin_fail=0, fail_fn=0, fail_nth=0, fail_mode=0, desc_at=0, insp_mode=0, world_handle=0,
                    s_base=0x7FF600000000, s_record=0x1F0B2200, s_head=3, s_tail=4, s_kind=6, s_request_weapon=77, s_world=0,
                    s_avatar_owner=0, s_avatar_index=0, s_body=0, s_viewmodel=-1, s_weapon=-1, s_weapon_unit=-1, s_wcd=-1,
                    s_safe=1, s_seated=0, s_wt=0, s_hct=0, s_active=0, s_aiming=0, addr_bias=0)


def num(value):
    if isinstance(value, int):
        return str(value)
    if value != value:
        return "nan"
    if value in (float("inf"), float("-inf")):
        return "inf" if value > 0 else "-inf"
    return repr(float(value))


class Unit:
    def __init__(self, index, generation, obj, array, meshes):
        self.index, self.generation, self.obj, self.array, self.meshes = index, generation, obj, array, meshes
        self.ref = generation * 0x400000 + index
        self.handle = self.ref * 4


class World:
    """Everything is emitted as tokens into `self.out`; `take()` hands them to the next operation."""

    def __init__(self, capacity=4096):
        self.out, self.mem, self.cursor, self.next_index = [], {}, 0x1F0A1000000, 1
        self.units_list, self.capacity = [], capacity
        self.vars, self.texts, self.rigs, self.binds, self.bad, self.journal = {}, {}, {}, {}, [], None
        self.masks = {}
        self.all_objects = []
        for name, value in DEFAULT_VARS.items():
            self.var(name, value)
        for name in ("s_pid", "s_identity", "s_cbytes", "s_flags", "s_wdesc", "pack_id"):
            self.text(name, None)
        for at, pin in PINS.items():
            self.poke(EXE + at, bytes.fromhex(pin))
        self.u32(EXE + 0x2875ac0, 0xF2760503); self.u32(EXE + 0x2875ac8, 0x3100C3D2)
        self.registry, self.objects, self.generations = self.alloc(0x100), self.alloc(capacity * 8), self.alloc(capacity)
        self.ptr(EXE + 0x1ab76f8, self.registry); self.ptr(self.registry + 0x88, self.objects)
        self.u32(self.registry + 0x98, capacity); self.ptr(self.registry + 0xA0, self.generations)
        self.vt = self.new_vt()
        self.world_native, self.world_handle = self.alloc(0x100), self.alloc(16)
        self.ptr(self.world_handle, self.world_native)
        self.var("world_handle", self.world_handle); self.var("s_world", self.world_native)
        self.owner = 0x1F200000000
        self.var("s_avatar_owner", self.owner)
        self.avatar_index = 0

    # tokens
    def take(self):
        out, self.out = self.out, []
        return out

    def note(self, *undo):
        if self.journal is not None: self.journal.append(undo)

    def var(self, name, value):
        self.note("var", name, self.vars.get(name)); self.vars[name] = value
        self.out.append(f"V{name}={num(value)}")

    def text(self, name, value):
        self.note("text", name, self.texts.get(name)); self.texts[name] = value
        self.out.append(f"T{name}=" + ("-" if value is None else bytes(value).hex()))

    def bind(self, code, mode):
        self.note("bind", code, self.binds.get(code, 1)); self.binds[code] = mode
        self.out.append(f"B{code}={mode}")

    def unreadable(self, lo, hi):
        self.note("bad", list(self.bad)); self.bad.append((lo, hi))
        self.out.append(f"X{lo},{hi}")

    def readable(self, keep=()):
        self.note("bad", list(self.bad)); self.bad = []
        self.out.append("x")
        for lo, hi in keep: self.unreadable(lo, hi)

    def poke(self, at, data):
        data = bytes(data)
        self.note("poke", at, self.peek(at, len(data)))
        for i, value in enumerate(data):
            self.mem[at + i] = value
        self.out.append(f"P{at}={data.hex()}")

    def record(self):
        """Start remembering how to undo what follows."""
        self.journal = []

    def undo(self):
        """Put back everything changed since record()."""
        journal, self.journal = self.journal, None
        for entry in reversed(journal):
            kind = entry[0]
            if kind == "var": self.var(entry[1], entry[2])
            elif kind == "text": self.text(entry[1], entry[2])
            elif kind == "bind": self.bind(entry[1], entry[2])
            elif kind == "bad": self.readable(entry[1])
            elif kind == "poke": self.poke(entry[1], entry[2])
            elif kind == "list": self.units_list = list(entry[1]); self.list_units()
            elif kind == "rig":
                if entry[2] is None: self.no_rig_mesh(entry[1])
                else: self.rig_mesh(entry[1], *entry[2])

    def u8(self, at, value): self.poke(at, bytes([value & 255]))
    def u32(self, at, value): self.poke(at, int(value).to_bytes(4, "little"))
    def ptr(self, at, value): self.poke(at, int(value).to_bytes(8, "little"))
    def peek(self, at, size): return bytes(self.mem.get(at + i, 0) for i in range(size))
    def peek_ptr(self, at): return int.from_bytes(self.peek(at, 8), "little")

    def alloc(self, size):
        at = self.cursor
        self.cursor = (at + size + 64 + 15) // 16 * 16
        return at

    def new_vt(self, good=True):
        vt = self.alloc(0x200)
        self.ptr(vt + 0xC8, EXE + 0x293600); self.ptr(vt + 0xC8 + 48, EXE + (0x28d3e0 if good else 0x28d3e8))
        return vt

    # units
    def unit(self, masks, parent=0, generation=1, index=None, listed=True, world=None, vt=None, resource=None, nodes=0, tag=0, high=0):
        if index is None:
            index = self.next_index; self.next_index += 1
        obj, array = self.alloc(0x400), self.alloc(max(len(masks), 1) * 16)
        unit = Unit(index, generation, obj, array, [])
        unit.ref += high * 0x40000000; unit.handle = unit.ref * 4      # reference bits above the generation byte
        self.all_objects.append(obj)
        self.ptr(obj, vt or self.vt); self.u32(obj + 8, unit.ref); self.ptr(obj + 0x18, world or self.world_native)
        self.u32(obj + 0xC8, len(masks)); self.ptr(obj + 0xD0, array); self.ptr(obj + 0x1D0, parent)
        if nodes: self.u32(obj + 0x70, nodes)
        if resource: self.poke(obj + 0x358, resource)
        self.u8(self.generations + index, generation); self.ptr(self.objects + index * 8, obj)
        for i, mask in enumerate(masks):
            mesh = self.alloc(0x100)
            unit.meshes.append(mesh); self.ptr(array + i * 16, mesh); self.u8(mesh + 0xDC, mask); self.canon(mesh + 0xDC, mask)
        unit.handle += tag
        if listed: self.set_units(self.units_list + [unit.handle])
        return unit

    def move_world(self, native):
        """Every unit and the SDK world handle now name another native world."""
        for obj in self.all_objects: self.ptr(obj + 0x18, native)
        self.ptr(self.world_handle, native); self.var("s_world", native)

    def list_units(self): self.out.append("L" + ",".join(str(h) for h in self.units_list))

    def set_units(self, handles):
        self.note("list", list(self.units_list)); self.units_list = list(handles); self.list_units()

    def unlist(self, unit): self.set_units([h for h in self.units_list if h != unit.handle])

    def retire(self, unit): self.u8(self.generations + unit.index, (unit.generation + 1) & 255)
    def revive(self, unit): self.u8(self.generations + unit.index, unit.generation)

    def children(self, parent, kids):
        """Link `kids` as the child list of `parent` (first child at +0x1d8, next sibling at +0x1e0)."""
        self.ptr(parent.obj + 0x1D8, kids[0].obj if kids else 0)
        for i, kid in enumerate(kids):
            self.ptr(kid.obj + 0x1D0, parent.obj)
            self.ptr(kid.obj + 0x1E0, kids[i + 1].obj if i + 1 < len(kids) else 0)

    def mask(self, unit, mesh, value): self.u8(unit.meshes[mesh] + 0xDC, value)

    def canon(self, at, value):
        """The mask a mesh has when nothing owns it; reset_masks() puts all of them back."""
        self.masks[at] = value; self.out.append(f"K{at}={value}")

    def set_mask(self, unit, mesh, value):
        self.mask(unit, mesh, value); self.canon(unit.meshes[mesh] + 0xDC, value)

    def reset_masks(self):
        for at, value in self.masks.items(): self.mem[at] = value
        self.out.append("k")

    # the player
    def custom(self): return self.owner + 0x546ab0 + self.avatar_index * 0x1B8

    def slot(self, layer, slot, ref): self.u32(self.custom() + 0xDC + layer * 40 + slot * 4, ref)

    def rig_mesh(self, name, at, hash_, node, materials=1, material=1, unit=0):
        self.note("rig", name, self.rigs.get(name)); self.rigs[name] = (at, hash_, node, materials, material, unit)
        self.out.append(f"M{name},{at},{num(hash_)},{num(node)},{num(materials)},{material},{unit}")

    def no_rig_mesh(self, name):
        self.note("rig", name, self.rigs.get(name)); self.rigs.pop(name, None)
        self.out.append(f"m{name}")

    def rig(self, masks=(255, 255, 255, 255, 255), **kw):
        rig = self.unit(list(masks), resource=RIG_RESOURCE, nodes=63, **kw)
        for i, (name, hash_, node) in enumerate(PADS[:len(rig.meshes)]):
            self.rig_mesh(name, rig.meshes[i], hash_, node)
        return rig

    def player(self, pid=b"player-A", kind=6, safe=True):
        self.text("s_pid", pid); self.text("s_identity", pid + b"/gun")
        self.var("s_kind", kind); self.var("s_safe", int(safe))
        self.text("s_cbytes", bytes(6)); self.text("s_flags", bytes(24))

    def weapon_identity(self, weapon_id, weapon_unit):
        """The 24-byte entity descriptor of the weapon and the component row that repeats its first 20 bytes."""
        at, component = self.alloc(24), self.alloc(24)
        descriptor = bytes(8) + int(weapon_id).to_bytes(4, "little") + bytes(8) + bytes([1, 0, 0, 0])
        self.poke(at, descriptor); self.poke(component, descriptor[:20])
        self.var("desc_at", at); self.var("s_weapon", weapon_id); self.var("s_request_weapon", weapon_id)
        self.var("s_weapon_unit", weapon_unit.ref); self.var("s_wcd", component); self.text("s_wdesc", descriptor[:20])
        return at, component


def standard(kind=6, backpack=True, arms=True, tree=True, layers=1, rig_first=False):
    """A whole player: body, head, torso, two shoulders, three kept limbs, two arm pieces, backpack, the
    first-person rig and a weapon with a magazine and an optic that carries a lens."""
    w = World()
    w.player(kind=kind)
    p = type("Parts", (), {})()
    if rig_first: p.rig = w.rig()                           # the rig's reference is then the smaller one
    p.body = w.unit([255, 255]); w.var("s_body", p.body.ref)
    if not rig_first: p.rig = w.rig()
    w.var("s_viewmodel", p.rig.ref)
    p.head = w.unit([255, 2], parent=p.body.obj); w.slot(0, 0, p.head.ref)
    p.torso = w.unit([255], parent=p.body.obj, high=2, generation=201); w.slot(0, 2, p.torso.ref)
    p.shoulders = [w.unit([255], parent=p.body.obj), w.unit([255, 255], parent=p.body.obj)]
    w.slot(0, 8, p.shoulders[0].ref); w.slot(0, 9, p.shoulders[1].ref)
    p.limbs = [w.unit([255], parent=p.body.obj) for _ in range(3)]
    for i, limb in enumerate(p.limbs): w.slot(0, 3 + i, limb.ref)
    p.arms = []
    if arms:
        p.arms = [w.unit([255, 255], parent=p.rig.obj if kind == 6 else p.body.obj) for _ in range(2)]
        w.slot(0, 6, p.arms[0].ref); w.slot(0, 7, p.arms[1].ref)
    p.extra = []
    for layer in range(1, layers):
        hat = w.unit([255], parent=p.body.obj); w.slot(layer, 0, hat.ref); p.extra.append(hat)
        vest = w.unit([255, 254], parent=p.body.obj); w.slot(layer, 2, vest.ref); p.extra.append(vest)
    p.pack = None
    if backpack:
        p.pack = w.unit([255], parent=p.body.obj)
        w.var("pack_mode", 1); w.var("pack_ref", p.pack.ref); w.text("pack_id", b"7:pack")
    p.gun = w.unit([255, 255], parent=p.rig.obj)
    p.tree = []
    if tree:
        p.magazine, p.optic = w.unit([255], parent=p.gun.obj), w.unit([255], parent=p.gun.obj)
        p.lens = w.unit([255], parent=p.optic.obj)
        w.children(p.gun, [p.magazine, p.optic]); w.children(p.optic, [p.lens])
        p.tree = [p.magazine, p.optic, p.lens]
    w.children(p.rig, [p.gun])
    w.ptr(p.rig.obj + 0x1D0, 0)
    p.desc_at, p.component = w.weapon_identity(77, p.gun)
    return w, p


class Script:
    """Operations of one run; each takes the world tokens written since the last one."""

    def __init__(self, world, name, module, **options):
        self.w, self.name, self.module, self.options, self.ops = world, name, module, options, []
        self.triggers = []

    def at(self, event, build):
        """Run `build()` now, and make its tokens arrive when event number `event` of the next operation does."""
        before = self.w.take()
        build()
        self.triggers.append((event, self.w.take()))
        self.w.out = before

    def op(self, call):
        tokens = self.w.take()
        for event, late in self.triggers:
            tokens += [f"@{event}"] + late
        self.triggers = []
        self.ops.append(" ".join(tokens + ["!" + call]))

    def header(self):
        return f"#run module={self.module} " + " ".join(f"{k}={v}" for k, v in self.options.items())


RETAIL = RUNNER = None


def configure(retail, runner):
    global RETAIL, RUNNER
    RETAIL, RUNNER = retail, runner


def events(script, call):
    """How many calls the operation makes on the world as it stands (it is not added to the script)."""
    w = script.w
    pending = w.take()
    probe = Script(w, "count", script.module, **script.options)
    probe.ops = script.ops + [" ".join(pending + ["!" + call])]
    line = run_reference(RETAIL, [probe], RUNNER)[-1]
    w.out = pending
    return int(line.split(" ev=")[1].split(",")[0])


def trace(script, call):
    """Every call the operation makes on the world as it stands, in order (it is not added to the script):
    memory reads as `r(address,size)`, the others as the result line lists them. Event N is trace[N-1]."""
    import tempfile
    from pathlib import Path
    w = script.w
    pending = w.take()
    ops = script.ops + [" ".join(pending + ["!" + call])]
    w.out = pending
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as handle:
        handle.write(script.header() + "\n" + "".join(op + "\n" for op in ops))
    done = subprocess.run(["luajit", str(RUNNER), str(RETAIL), handle.name, "--dump", str(len(ops) + 1)], capture_output=True, text=True)
    Path(handle.name).unlink()
    if done.returncode:
        sys.exit(done.stderr[-3000:])
    return done.stderr.split("\n")[:-1]


def at_each(script, call, wanted, changes, before=(), settle=(), reset=False, limit=None):
    """The operation once per change and per call whose trace text `wanted` accepts: the change arrives with
    exactly that call (or, with `after`, the one behind it) and is undone afterwards."""
    w = script.w
    for op in before: script.op(op)
    events = [i + 1 for i, text in enumerate(trace(script, call)) if wanted(text)]
    if limit: events = events[:limit]
    first = True
    for change in changes:
        for event in events:
            if not first:
                for op in before: script.op(op)
            first = False
            w.record()
            script.at(event, change)
            script.op(call)
            w.undo()
            if reset: w.reset_masks()
            for after in settle: script.op(after)
    return len(events)


def sweep(script, call, changes, stride=1, settle=(), before=(), reset=False):
    """The operation once for every change and every `stride`-th call it makes: the change arrives with that
    call and is undone afterwards. `before` operations run ahead of each one (to reach the same state again),
    `settle` operations after it; `reset` puts every mesh mask back first."""
    w = script.w
    for op in before: script.op(op)
    count = events(script, call)
    first = True
    for number, change in enumerate(changes):
        for event in range(1 + number % stride, count + 2, stride):
            if not first:
                for op in before: script.op(op)
            first = False
            w.record()
            script.at(event, change)
            script.op(call)
            w.undo()
            if reset: w.reset_masks()
            for after in settle:
                script.op(after)


def run_reference(retail, scripts, runner, extra=()):
    """Write the scripts, run them under LuaJIT and return one result line per operation."""
    import tempfile
    from pathlib import Path
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as handle:
        for script in scripts:
            handle.write(script.header() + "\n")
            handle.write("".join(op + "\n" for op in script.ops))
    done = subprocess.run(["luajit", str(runner), str(retail), handle.name, *extra], capture_output=True, text=True)
    Path(handle.name).unlink()
    if done.returncode:
        sys.exit(f"{runner} failed:\n{done.stdout[-2000:]}\n{done.stderr[-4000:]}")
    lines = done.stdout.split("\n")[:-1]
    total = sum(len(script.ops) for script in scripts)
    if len(lines) != total:
        sys.exit(f"{len(lines)} results for {total} operations")
    return lines


def c_string(text):
    out = []
    for i in range(0, max(len(text), 1), 4000):          # string literals are cut so that no compiler limit is met
        out.append('"' + text[i:i + 4000].replace("\\", "\\\\").replace('"', '\\"').replace("??", "?\\?") + '"')
    return " ".join(out)


def write_header(path, title, prefix, retail, files, scripts, lines, commit):
    digests = {name: hashlib.sha256((retail / "src/first_person" / name).read_bytes()).hexdigest() for name in files}
    out = [f"/* Generated by tools/retail_ref/{title}. Do not edit.", f" * Retail commit {commit}:"]
    out += [f" *   src/first_person/{name} sha256 {digest}" for name, digest in digests.items()]
    out += [" * `script`: the operation as tools/retail_ref/run_visibility.lua and tests/visibility_fake.h read it (world",
            " * changes, `@N` changes that arrive with the N-th call, `!call`). `expect`: what the retail file returned, its",
            " * state, the count and checksums of every call it made in order, and the calls that are not memory reads. */",
            f'#define {prefix.upper()}_VECTOR_COMMIT "{commit}"']
    out += [f'#define {prefix.upper()}_VECTOR_SHA256_{i} "{digest}"' for i, digest in enumerate(digests.values())]
    out += ["typedef struct %sRun { const char *name, *header; } %sRun;" % (prefix.title().replace("_", ""), prefix.title().replace("_", "")),
            "typedef struct %sVector { int run; const char *script, *expect; } %sVector;" % (prefix.title().replace("_", ""), prefix.title().replace("_", "")),
            f"static const {prefix.title().replace('_', '')}Run {prefix}_runs[] = {{"]
    out += ["  {%s,%s}," % (c_string(script.name), c_string(script.header())) for script in scripts]
    out += ["};", f"static const {prefix.title().replace('_', '')}Vector {prefix}_vectors[] = {{"]
    at = 0
    for index, script in enumerate(scripts):
        for op in script.ops:
            out.append("  {%d,%s,\n   %s}," % (index, c_string(op), c_string(lines[at])))
            at += 1
    out.append("};")
    path.write_text("\n".join(out) + "\n")


def frozen_commit(retail):
    commit = subprocess.check_output(["git", "-C", str(retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    return commit
