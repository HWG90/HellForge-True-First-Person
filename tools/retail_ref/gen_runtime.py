#!/usr/bin/env python3
"""Reference vectors for src/tfp_runtime.c from the retail runtime.lua, unmodified, under LuaJIT.

  gen_runtime.py --retail <Goose Gadget Kit> --out tests/vectors     write runtime_vectors.h and runtime_fakes.h
  gen_runtime.py --retail <Goose Gadget Kit> --coverage              line coverage of runtime.lua, nothing written
  gen_runtime.py --host-header src/tfp_runtime_host.h                the host struct, from runtime_spec.py
  gen_runtime.py --retail ... --lines <file>                         also the raw answers (to compare interpreters)

runtime_spec.py lists what runtime.lua reaches outside itself; the host struct, the Lua fakes
(run_runtime.lua) and the C fakes (runtime_fakes.h) all come from it. The sessions are written by
tools/retail_ref/runtime_sessions.py. Each session is replayed once to count the calls each
operation makes into the fakes, then again with one of those calls raising, refusing or answering
nil (the sessions marked for it), so every call site of the covered paths fails once.
"""
import argparse
import hashlib
import math
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import runtime_spec as spec  # noqa: E402
from gen_motion_runtime import square_shim  # noqa: E402
from runtime_spec import KNOBS, TABLES  # noqa: E402

FROZEN = "1d0d453"
SHA256 = "51099af45ca34487d10454b7513e6149261733f98a60c1bdae1fbe307f579aba"
INF = float("inf")
ENTRY_INDEX = {e.name: i + 1 for i, e in enumerate(spec.ENTRY_LIST)}
METHOD_INDEX = {m.host: i + 1 for i, m in enumerate(spec.METHOD_LIST)}


class Ref:
    def __init__(self, n):
        self.n = int(n)

    def __eq__(self, other):
        return isinstance(other, Ref) and other.n == self.n

    def __hash__(self):
        return hash(("R", self.n))

    def __repr__(self):
        return f"R{self.n}"


KNOBS.defaults[KNOBS.index["config_table"]] = Ref(900)
KNOBS.defaults[KNOBS.index["actions_actions"]] = Ref(901)


# ------------------------------------------------------------------ sessions
class Session:
    def __init__(self, name, sweep=False, tail=()):
        self.name, self.ops, self.pending = name, [], []
        self.sweep = sweep          # after the counting run: one copy per call of each marked op, failing there
        self.tail = tail            # ops appended to a failing copy after the failed op

    def set(self, knob, value):
        if isinstance(value, int) and not isinstance(value, bool):
            value = float(value)
        self.pending.append((KNOBS.index[knob], value))
        return self

    def sets(self, **knobs):
        for knob, value in knobs.items():
            self.set(knob.replace("__", "#").replace("_DOT_", "."), value)
        return self

    def group(self, group_, present_=True, **fields):
        base, kind = KNOBS.groups[group_]
        self.pending.append((base, 1.0 if present_ else 0.0))
        for field, value in fields.items():
            assert field == "ref" or field in TABLES[kind], (group_, field)
            self.set(f"{group_}.{field}", value)
        return self

    def op(self, entry, *args, faults=(), cbs=(), mark=False):
        args = [float(a) if isinstance(a, int) and not isinstance(a, bool) else a for a in args]
        cbs = [(METHOD_INDEX[host], n, ENTRY_INDEX[name], [float(a) if isinstance(a, int) and not isinstance(a, bool) else a for a in cargs])
               for host, n, name, cargs in cbs]
        self.ops.append({"sets": self.pending, "entry": ENTRY_INDEX[entry], "args": args, "faults": list(faults), "cbs": cbs,
                         "mark": mark})
        self.pending = []
        return self


def lua_value(value):
    if value is None:
        return "nil"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, Ref):
        return f"R({value.n})"
    if isinstance(value, str):
        return '"' + "".join("\\%03d" % ord(c) for c in value) + '"'
    if value != value:
        return "(0/0)"
    if value in (INF, -INF):
        return "math.huge" if value > 0 else "(-math.huge)"
    if value == 0 and math.copysign(1, value) < 0:
        return "(-1/math.huge)"
    return str(int(value)) if value == int(value) and abs(value) < 1e15 else repr(float(value))


def lua_list(values):
    # nil holes: explicit indices
    return "{" + ",".join(f"[{i + 1}]={lua_value(v)}" for i, v in enumerate(values) if v is not None) + "}"


def lua_spec(sessions):
    out = ["return {"]
    out.append("tables={" + ",".join(f"{kind}={{" + ",".join(f'"{f}"' for f in fields) + "}" for kind, fields in TABLES.items()) + "},")
    out.append("knobs={" + ",".join(f'"{n}"' for n in KNOBS.names[1:]) + "},")
    # Knob ids in Lua are the C ids (the C array has an unused slot 0, the Lua array starts at 1 = C id 1).
    out.append("defaults={" + ",".join(f"[{i}]={lua_value(v)}" for i, v in enumerate(KNOBS.defaults) if i and v is not None) + "},")
    rows = []
    lua_names = {"ptr": "_ptr", "read": "_read", "alignment_context": "_alignment_context"}
    for m in spec.METHOD_LIST:
        ins = ",".join("{" + f'"{kind}"' + (f',"{sub}"' if sub else "") + "}" for kind, sub, _ in m.ins)
        outs = []
        for o in KNOBS.method[m.host]:
            outs.append("{" + ",".join(f'"{x}"' if isinstance(x, str) else str(x) for x in o) + "}")
        rows.append(f'{{index={METHOD_INDEX[m.host]},obj="{m.obj}",lua="{lua_names.get(m.method, m.method) if m.obj == "backend" else m.method}",host="{m.host}",'
                    f'static={"true" if m.static else "false"},optional={"true" if m.optional else "false"},'
                    f'has={KNOBS.has.get(m.host, 0)},ins={{{ins}}},outs={{{",".join(outs)}}}'
                    + (f',effect="{m.effect}"' if m.effect else "") + "}")
    out.append("methods={" + ",\n".join(rows) + "},")
    rows = []
    for g in spec.GETTER_LIST:
        d = KNOBS.getter[g.host]
        rows.append(f'{{obj="{g.obj}",field="{g.field}",out="{d[0] if d[0] != "tab" else "tab"}",knob={d[1]}'
                    + (f',kind="{d[2]}"' if d[0] == "tab" else "") + "}")
    out.append("getters={" + ",".join(rows) + "},")
    out.append("entries={" + ",".join(
        f'{{name="{e.name}",nret={e.nret},args={{{",".join(chr(34) + a + chr(34) for a in e.args)}}}}}' for e in spec.ENTRY_LIST) + "},")
    out.append("arg_groups={" + ",".join(f'{k}="{v}"' for k, v in spec.ARG_GROUPS.items()) + "},")
    out.append("objects={" + ",".join(f'"{n}"' for n in spec.OBJECTS) + "},")
    out.append("sessions={")
    for s in sessions:
        out.append("(function() return {")   # a function each: LuaJIT allows 65536 constants per function
        for op in s.ops:
            row = ["s={" + ",".join(f"[{2 * i + 1}]={k},[{2 * i + 2}]={lua_value(v)}" for i, (k, v) in enumerate(op["sets"])) + "}",
                   f"n={2 * len(op['sets'])}", f"e={op['entry']}"]
            if op["args"]:
                row.append("a=" + lua_list(op["args"]))
            if op["faults"]:
                row.append("f={" + ",".join(f"{n},{kind}" for n, kind in op["faults"]) + "}")
            if op["cbs"]:
                row.append("c={" + ",".join(f"{{{m},{n},{e},{lua_list(a)}}}" for m, n, e, a in op["cbs"]) + "}")
            out.append("{" + ",".join(row) + "},")
        out.append("} end)(),")
    out.append("}}")
    text = "\n".join(out) + "\n"
    # method lookup by host name
    return text.replace("return {", "local S = {", 1) + "S.method={}\nfor _,m in ipairs(S.methods) do S.method[m.host]=m end\nreturn S\n"


def run(interpreter, retail, sessions, coverage=None):
    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as handle:
        handle.write(lua_spec(sessions))
    command = interpreter + [str(HERE / "run_runtime.lua"), str(retail), handle.name] + ([str(coverage)] if coverage else [])
    with tempfile.TemporaryDirectory() as folder:
        # x^2 (:997, :999) as the exact square in the interpreter too, as LuaJIT's compiled traces
        # compute it: see SQUARE_SHIM in gen_motion_runtime.py and docs/translation/MOTION-RUNTIME-TRANSLATION.md.
        environment = dict(os.environ, LD_PRELOAD=str(square_shim(folder)))
        done = subprocess.run(command, capture_output=True, text=True, env=environment)
    if done.returncode:
        sys.exit(done.stderr + f"\n(script kept: {handle.name})")
    Path(handle.name).unlink()
    lines = done.stdout.splitlines()
    total = sum(len(s.ops) for s in sessions)
    if len(lines) != total:
        sys.exit(f"{len(lines)} answers for {total} operations")
    answers, counts = [], []
    for line in lines:
        body, _, count = line.rpartition("|")
        answers.append(body)
        counts.append(int(count))
    return answers, counts


def with_faults(sessions, counts):
    """One copy of each marked session per call of each marked operation and per way of failing."""
    out, at = [], 0
    for s in sessions:
        here = counts[at:at + len(s.ops)]
        at += len(s.ops)
        if not s.sweep:
            continue
        for j, op in enumerate(s.ops):
            if not op["mark"]:
                continue
            for n in range(1, here[j] + 1):
                for kind in (1, 2, 3):
                    copy = Session(f"{s.name} / op {j + 1} call {n} fault {kind}")
                    copy.ops = [dict(o) for o in s.ops[:j + 1]]
                    copy.ops[j] = dict(op, faults=[(n, kind)])
                    for entry, args in s.tail:
                        copy.op(entry, *args)
                    out.append(copy)
    return out


# ------------------------------------------------------------------ C
def c_double(value):
    if value != value:
        return "NAN"
    if value in (INF, -INF):
        return "INFINITY" if value > 0 else "-INFINITY"
    if value == int(value) and abs(value) < 1e9 and not (value == 0 and math.copysign(1, value) < 0):
        return str(int(value))
    return float(value).hex()


def c_text(text):
    out = []
    for c in text:
        o = ord(c)
        if c == '"' or c == "\\":
            out.append("\\" + c)
        elif o < 32 or o > 126:
            out.append("\\%03o" % o)
        elif c == "?" and out and out[-1] == "?":
            out.append("\\?")      # no trigraphs
        else:
            out.append(c)
    return '"' + "".join(out) + '"'


class Pool:
    def __init__(self):
        self.items, self.index = [], {}

    def add(self, item):
        if item not in self.index:
            self.index[item] = len(self.items)
            self.items.append(item)
        return self.index[item]


def c_value(value, strings):
    if value is None:
        return "{0,0,0}"
    if value is False:
        return "{1,0,0}"
    if value is True:
        return "{2,0,0}"
    if isinstance(value, Ref):
        return "{5,%d,0}" % value.n
    if isinstance(value, str):
        return "{4,%d,%d}" % (len(value), strings.add(value))
    return "{3,%s,0}" % c_double(value)


C_IN = {"v": "TfpRtValue {n}", "n": "double {n}", "b": "bool {n}", "t": "const char *{n}", "tab": "const TfpRtTable *{n}",
        "tabx": "const TfpRtTable *{n}", "ctx": "const TfpRtContext *{n}", "vals": "const TfpRtValue *{n}"}


def c_params(m):
    params = ["TfpRtHost *h"]
    for kind, sub, name in m.ins:
        if kind == "_":
            continue
        params.append(C_IN[kind].format(n=name) + (f" /* TFP_RT_{sub}_N */" if sub else ""))
    for i, (kind, extra) in enumerate(m.outs):
        if kind == "v":
            params.append(f"TfpRtValue *out{i + 1}")
        elif kind == "tab":
            params.append(f"TfpRtTable *out{i + 1} /* {extra} */")
        else:
            params.append(f"TfpRtPolicies *out{i + 1}")
    return ", ".join(params)


SPECIAL_GETTERS = {"actions_observed", "config_table", "config_field", "config_action_mapped", "ref_field", "global_get"}
GETTER_ARG = {"v": "TfpRtValue", "t": "const char *"}


def getter_decl(g, name):
    args = "".join(f", {GETTER_ARG[a]} a{i + 1}" for i, a in enumerate(g.args))
    if g.out == "v":
        return f"TfpRtValue {name}(TfpRtHost *h{args})"
    return f"void {name}(TfpRtHost *h{args}, TfpRtTable *out)"


def host_header():
    out = ["/* Generated by tools/retail_ref/gen_runtime.py --host-header from tools/retail_ref/runtime_spec.py.",
           " * Change the spec and regenerate; the scripted fakes of the vector test come from the same spec.",
           " *",
           " * Everything retail runtime.lua reaches outside itself: one pointer per distinct collaborator method it",
           " * calls, named <module>_<method>, in the order of the spec. A call answers true when the Lua method",
           " * returned and false when it raised, with the raised text in host->raised. Results come back through",
           " * the out parameters: a TfpRtValue for each Lua return value runtime.lua looks at, a TfpRtTable where",
           " * it reads fields of the answer (present = false: the method answered nil). Pointers marked optional",
           " * may be NULL: runtime.lua tests for those methods and so does the C. The getters at the end are",
           " * fields runtime.lua reads off its collaborators; they cannot raise. Included by tfp_runtime.h. */",
           "#ifndef TFP_RUNTIME_HOST_H", "#define TFP_RUNTIME_HOST_H", ""]
    for kind, fields in TABLES.items():
        names = [f"TFP_RT_{kind}_{f.upper()}" for f in fields] + [f"TFP_RT_{kind}_N"]
        out.append("enum { " + ", ".join(names) + " };")
    out += ["", f"enum {{ TFP_RT_POLICY_MAX = {spec.POLICY_SLOTS * 2} }};",
            "typedef struct TfpRtPolicies { /* recovery:snapshot(): .policies in the order they are walked */",
            "  bool present; const void *ref; int count; const char *name[TFP_RT_POLICY_MAX]; TfpRtTable row[TFP_RT_POLICY_MAX]; /* ROW */",
            "} TfpRtPolicies;", "",
            "typedef struct TfpRtHost TfpRtHost;", "struct TfpRtHost {",
            "  void *user;          /* the binder's own */", "  const char *raised;  /* set by a call that answers false */"]
    last = None
    for m in spec.METHOD_LIST:
        if m.obj != last:
            out.append(f"  /* {m.obj} */")
            last = m.obj
        out.append(f"  bool (*{m.host})({c_params(m)});" + (" /* optional */" if m.optional else ""))
    out.append("  /* Fields (not calls in Lua, not in the trace). */")
    for g in spec.GETTER_LIST:
        out.append("  " + getter_decl(g, f"(*{g.host})") + ";")
    out += ["  /* rawset(_G, 'NativeADSCameraTest', api or nil); global_get answers 0 unset, 1 this runtime's api, 2 another value. */",
            "  void (*global_set)(TfpRtHost *h, bool ours);", "};", "",
            "/* runtime.lua's module-level variables that hold an object (or a function) or nil. */",
            "enum { " + ", ".join(f"TFP_RT_O_{n.upper()}" for n in spec.OBJECTS) + ", TFP_RT_O_N };",
            "/* How far the latest on_enable got; a function runtime.lua hands out exists from its stage on. */",
            "enum { " + ", ".join(f"TFP_RT_STAGE_{n}" for n in spec.STAGES) + " };",
            "/* The functions runtime.lua hands out: X(name, stage, values answered). Arguments, in TfpRtArgs:"]
    for e in spec.ENTRY_LIST:
        if e.args:
            out.append(f" *   {e.name}: {', '.join(e.args)}")
    out += [" * (v: the next of args->v; tab: args->table; ctx: args->context). */", "#define TFP_RT_ENTRY_LIST(X) \\"]
    out += [f"  X({e.name}, {e.stage}, {e.nret}) \\" for e in spec.ENTRY_LIST]
    out += ["", "#endif", ""]
    return "\n".join(out)


def fakes_header():
    out = ["/* Generated by tools/retail_ref/gen_runtime.py from runtime_spec.py. Do not edit.",
           " * The scripted fakes of tests/tfp_runtime_test.c: each writes its call to the trace and answers what the knobs say,",
           " * exactly as the fakes of tools/retail_ref/run_runtime.lua do. */"]
    arg_code = {"v": "fk_v({n});", "n": "fk_n({n});", "b": "fk_b({n});", "t": "fk_t({n});", "tab": "fk_r({n});", "tabx": "fk_rx({n});",
                "ctx": "fk_c({n});"}
    for m in spec.METHOD_LIST:
        body = [f'fk_begin({METHOD_INDEX[m.host]}, "{m.host}");']
        for kind, sub, name in m.ins:
            if kind == "_":
                continue
            body.append(f"fk_vals({name}, TFP_RT_{sub}_N);" if kind == "vals" else arg_code[kind].format(n=name))
        body.append("fk_end();")
        names = [name for kind, _, name in m.ins if kind != "_"]
        if m.effect == "frame_on":
            body.append(f"K[{KNOBS.index['backend_frame_active']}] = K[{KNOBS.index['frame.during']}];")
        elif m.effect == "frame_off":
            body.append(f"K[{KNOBS.index['backend_frame_active']}] = fk_bool(false);")
        elif m.effect == "revision":
            body.append(f"K[{KNOBS.index['backend_action_placement_revision']}] = fk_number({names[0]});")
        elif m.effect == "ref_set":
            body.append(f"fk_ref_set({names[0]}, {names[1]});")
        body.append("int f = fk_go(h); if (f == 1) return false;")
        for i, o in enumerate(KNOBS.method[m.host]):
            if o[0] == "v":
                body.append(f"fk_out(out{i + 1}, {o[1]}, {i}, f);")
            elif o[0] == "tab":
                body.append(f"fk_tab(out{i + 1}, {o[1]}, TFP_RT_{o[2]}_N, f);")
            else:
                body.append(f"fk_policies(out{i + 1}, {o[1]}, f);")
        if m.effect == "read":
            body.append(f"fk_trim(out1, {names[1]});")
        if m.effect == "clock":
            knob = KNOBS.method[m.host][0][1]
            body.append(f"if (K[{knob}].kind == TFP_RT_NUMBER) K[{knob}].number += K[{KNOBS.index['clock.step']}].number;")
        body.append("return true;")
        out.append(f"static bool fk_{m.host}({c_params(m)}) {{ " + " ".join(body) + " }")
    for g in spec.GETTER_LIST:
        if g.host in SPECIAL_GETTERS:
            continue
        d = KNOBS.getter[g.host]
        if d[0] == "v":
            out.append("static " + getter_decl(g, f"fk_{g.host}") + f" {{ (void)h; return K[{d[1]}]; }}")
        elif d[0] == "lease":
            out.append("static " + getter_decl(g, f"fk_{g.host}") + f" {{ (void)h; fk_lease(out, {d[1]}); }}")
        else:
            out.append("static " + getter_decl(g, f"fk_{g.host}") + f" {{ (void)h; fk_tab(out, {d[1]}, TFP_RT_{d[2]}_N, 0); }}")
    out.append("static void fk_bind(TfpRtHost *h) {")
    for m in spec.METHOD_LIST:
        if m.optional:
            out.append(f"  h->{m.host} = K[{KNOBS.has[m.host]}].number != 0 ? fk_{m.host} : NULL;")
        else:
            out.append(f"  h->{m.host} = fk_{m.host};")
    for g in spec.GETTER_LIST:
        out.append(f"  h->{g.host} = fk_{g.host};")
    out += ["  h->global_set = fk_global_set;", "}"]
    names = {"K_GLOBAL": "global_get", "K_CONFIG": "config_table", "K_OBSERVED": "actions_observed", "K_OBSERVED_NAME": "actions.observed_name",
             "K_MAPPED": "config.mapped_action", "K_MAPPING": "config.action_state_mapping"}
    out.append("enum { " + ", ".join(f"{c} = {KNOBS.index[k]}" for c, k in names.items()) + f", K_LEASE1 = {KNOBS.groups['lease1'][0]}, "
               f"K_LEASE_STEP = {KNOBS.groups['lease2'][0] - KNOBS.groups['lease1'][0]} }};")
    out.append("static const struct { const char *name; int knob; } fk_config[] = {" + ", ".join(
        f'{{"{n[7:]}", {i}}}' for n, i in KNOBS.index.items() if n.startswith("config.") and n != "config.mapped_action") + "};")
    out.append("static const struct { const char *name; int base; int count; } fk_groups[] = {" + ", ".join(
        f'{{"{name}", {base}, TFP_RT_{kind}_N}}' for name, (base, kind) in KNOBS.groups.items() if name in spec.ARG_GROUPS) + "};")
    return "\n".join(out) + "\n"


def vectors_header(sessions, answers, stamp):
    strings, expects = Pool(), Pool()
    sets, values, faults, cbs, ops = [], [], [], [], []
    at = 0
    placed = {}     # the copies made for the failing calls share their lists with the session they come from
    for s in sessions:
        for j, op in enumerate(s.ops):
            key = (id(op["sets"]), id(op["args"]), id(op["cbs"]))
            if key not in placed:
                set0 = len(sets)
                sets += ["{%d,%s}" % (k, c_value(v, strings)) for k, v in op["sets"]]
                arg0 = len(values)
                values += [c_value(v, strings) for v in op["args"]]
                cb0 = len(cbs)
                for method, n, entry, args in op["cbs"]:
                    cbs.append("{%d,%d,%d,%d,%d}" % (method, n, entry - 1, len(values), len(args)))
                    values += [c_value(v, strings) for v in args]
                placed[key] = (set0, arg0, cb0)
            set0, arg0, cb0 = placed[key]
            fault0 = len(faults)
            faults += ["{%d,%d}" % f for f in op["faults"]]
            ops.append("{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}" % (1 if j == 0 else 0, op["entry"] - 1, set0, len(op["sets"]), arg0,
                                                              len(op["args"]), fault0, len(op["faults"]), cb0, len(op["cbs"]),
                                                              expects.add(answers[at])))
            at += 1
    out = [stamp, "#include <math.h>",
           f"enum {{ RT_KNOBS = {KNOBS.count} }};",
           "typedef struct RtValue { unsigned char kind; double number; unsigned text; } RtValue; /* text: kind 4, number its length */",
           "typedef struct RtSet { unsigned short knob; RtValue value; } RtSet;",
           "typedef struct RtFault { unsigned short call; unsigned char kind; } RtFault;",
           "typedef struct RtCallback { unsigned short method, occurrence, entry; unsigned arg0; unsigned char args; } RtCallback;",
           f"enum {{ RT_METHODS = {len(spec.METHOD_LIST) + 1} }};",
           "typedef struct RtOp { unsigned char first; unsigned short entry; unsigned set0; unsigned short sets; unsigned arg0; "
           "unsigned char args; unsigned fault0; unsigned char faults; unsigned cb0; unsigned char cbs; unsigned expect; } RtOp;",
           "static const char *const rt_entry_names[] = {" + ", ".join(f'"{e.name}"' for e in spec.ENTRY_LIST) + "};",
           "static const unsigned char rt_entry_nret[] = {" + ", ".join(str(e.nret) for e in spec.ENTRY_LIST) + "};",
           "static const char *const rt_entry_args[] = {" + ", ".join('"' + ",".join(e.args) + '"' for e in spec.ENTRY_LIST) + "};",
           "static const RtValue rt_defaults[RT_KNOBS] = {" + ",".join(c_value(v, strings) for v in KNOBS.defaults) + "};"]
    out.append("static const char *const rt_strings[] = {" + ",".join(c_text(t) for t in strings.items) + ",0};")

    def block(name, kind, rows, pad):
        out.append(f"static const {kind} {name}[] = {{")
        for i in range(0, len(rows), 16):
            out.append(",".join(rows[i:i + 16]) + ",")
        out.append(pad + "};")
    block("rt_sets", "RtSet", sets, "{0,{0,0,0}}")
    block("rt_values", "RtValue", values, "{0,0,0}")
    block("rt_faults", "RtFault", faults, "{0,0}")
    block("rt_callbacks", "RtCallback", cbs, "{0,0,0,0,0}")
    out.append("static const char *const rt_expect[] = {")
    out += [c_text(t) + "," for t in expects.items]
    out.append("0};")
    block("rt_ops", "RtOp", ops, "{0,0,0,0,0,0,0,0,0,0,0}")
    out.append(f"enum {{ RT_OPS = {len(ops)} }};")
    return "\n".join(out) + "\n"


def executable_lines(source):
    listing = subprocess.check_output(["luac5.1", "-l", "-l", "-p", str(source)], text=True)
    return sorted({int(m.group(1)) for m in re.finditer(r"^\s+\d+\s+\[(\d+)\]\s+\w+", listing, re.M)})


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--lua", default="luajit", help="interpreter and arguments, e.g. 'luajit -joff'")
    parser.add_argument("--lines", type=Path)
    parser.add_argument("--coverage", action="store_true")
    parser.add_argument("--host-header", type=Path)
    parser.add_argument("--only", help="regex on session names (for debugging; never for the checked-in vectors)")
    args = parser.parse_args()
    if args.host_header:
        args.host_header.write_text(host_header())
        if not args.retail:
            return 0
    commit = subprocess.check_output(["git", "-C", str(args.retail), "rev-parse", "--short", "HEAD"], text=True).strip()
    if commit != FROZEN:
        sys.exit(f"Retail is at {commit}; this project is frozen at {FROZEN}.")
    source = args.retail / "src/first_person/runtime.lua"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    if digest != SHA256:
        sys.exit("runtime.lua is not the frozen file")
    import runtime_sessions
    base = runtime_sessions.build(Session, Ref)
    if args.only:
        base = [s for s in base if re.search(args.only, s.name)]
    _, counts = run(args.lua.split(), args.retail, base)
    sessions = base + with_faults(base, counts)
    if args.coverage:
        with tempfile.NamedTemporaryFile(suffix=".txt") as handle:
            run(args.lua.split(), args.retail, sessions, Path(handle.name))
            hit = {int(line.split()[0]) for line in Path(handle.name).read_text().splitlines()}
        wanted = executable_lines(source)
        missed = [n for n in wanted if n not in hit]
        print(f"runtime.lua: {len(wanted)} executable lines, {len(wanted) - len(missed)} hit; not hit: {missed}")
        return 0
    answers, counts = run(args.lua.split(), args.retail, sessions)
    if args.lines:
        args.lines.write_text("\n".join(f"{s.name} #{j + 1}\t" for s in sessions for j in range(len(s.ops))) and
                              "\n".join(answers) + "\n")
    total = len(answers)
    raised = sum(a.startswith("!") for a in answers)
    summary = (f"{total} operations in {len(sessions)} sessions ({len(base)} written, {len(sessions) - len(base)} with one failing call), "
               f"{sum(counts)} calls into the fakes, {raised} operations raised")
    print("runtime: " + summary)
    if not args.out:
        return 0
    stamp = (f"/* Generated by tools/retail_ref/gen_runtime.py. Do not edit.\n * Retail file: src/first_person/runtime.lua at commit {commit}, "
             f"sha256 {digest}.\n * {summary}.\n * Each answer: what the called function answered | the ordered trace of every call into a "
             f"scripted fake | runtime.lua's own state.\n * See tools/retail_ref/run_runtime.lua for the notation. */\n"
             f'#define RT_VECTOR_COMMIT "{commit}"\n#define RT_VECTOR_SHA256 "{digest}"')
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "runtime_vectors.h").write_text(vectors_header(sessions, answers, stamp))
    (args.out / "runtime_fakes.h").write_text(fakes_header())
    names = "\n".join(f"{i}\t{s.name}" for i, s in enumerate(sessions))
    (HERE.parent.parent / "work").mkdir(exist_ok=True)
    (HERE.parent.parent / "work" / "runtime_sessions.txt").write_text(names + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
