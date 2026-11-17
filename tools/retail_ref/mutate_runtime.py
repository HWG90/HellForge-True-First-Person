#!/usr/bin/env python3
"""Mutation check of src/tfp_runtime.c against the retail vectors.

Makes single changes to a scratch copy of the C file (the changes of mutate_motion_runtime.py: a
comparison turned round or made strict, && for ||, + for -, * for /, a constant changed, a true for
a false, a negation dropped, a text changed, a statement removed; and here also one named value for
a neighbour: a value kind, a value test, a table field for the next field of its kind, an object
for another object, a host call for a sibling), runs tests/run-vectors-runtime.sh on each, and
counts how many the vectors notice. A change that does not compile is left out. Survivors are
listed with their line.

  mutate_runtime.py --work <scratch folder> [--jobs N] [--only <regex on the line>] [--list] [--retry <earlier output>]
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import mutate_motion_runtime as base  # noqa: E402
import runtime_spec as spec  # noqa: E402

ROOT = HERE.parent.parent
SOURCE = ROOT / "src/tfp_runtime.c"
RUN = ROOT / "tests/run-vectors-runtime.sh"
base.NAMED = [("TFP_RT_TRUE", "TFP_RT_FALSE"), ("TFP_RT_FALSE", "TFP_RT_NIL"), ("TFP_RT_NIL", "TFP_RT_TRUE"), ("TFP_RT_NUMBER", "TFP_RT_TEXT"),
              ("TFP_RT_TEXT", "TFP_RT_NUMBER"), ("TFP_RT_REF", "TFP_RT_TEXT"), ("is_true(", "truthy("), ("truthy(", "is_true("),
              ("is_false(", "is_nil("), ("is_nil(", "is_false("), ("is_num(", "is_text("), ("is_text(", "is_num("),
              ("lua_min", "lua_max"), ("lua_max", "lua_min"), ("acos(", "asin("), ("cos(", "sin("), ("fabs(", "("), ("floor(", "ceil("),
              ("fmod(", "pow("), ("VNIL", "vb(true)"), ("TG_FOV", "TG_BLUR"), ("TG_BLUR", "TG_CAMERA"), ("TG_CAMERA", "TG_HEAD"),
              ("TG_HEAD", "TG_MOVEMENT"), ("TG_MOVEMENT", "TG_FOV"), ("PC_HEAD", "PC_FIRD"), ("PC_FIRD", "PC_CONTROLLER"),
              ("PC_CONTROLLER", "PC_HEAD"), ("TFP_RT_OK", "TFP_RT_RAISED"), ("TFP_RT_RAISED", "TFP_RT_ABSENT"), ("TFP_RT_ABSENT", "TFP_RT_OK"),
              ("p.a", "p.b"), ("p.b", "p.a"), ("r[0]", "r[1]"), ("r[1]", "r[0]"), ("a->v[0]", "a->v[1]"), ("a->v[1]", "a->v[0]"),
              ("&r[0], &r[1]", "&r[1], &r[0]"), ("rt->edit_mode == 1", "rt->edit_mode == 0"), ("rt->has_context", "true")]
OBJECTS = [name.upper() for name in spec.OBJECTS]
STAGES = spec.STAGES[1:]


def extra(lines):
    """Named neighbours that depend on the spec."""
    out = []
    for index, line in enumerate(lines):
        if line.lstrip().startswith(("/*", "*", "#")):
            continue
        for m in re.finditer(r"\bF\(([^,()]+|\*\w+), (\w+), (\w+)\)", line):
            kind, field = m.group(2), m.group(3).lower()
            fields = [f.lower() for f in spec.TABLES.get(kind, [])]
            if field in fields and len(fields) > 1:
                other = fields[(fields.index(field) + 1) % len(fields)].upper()
                out.append((index, line[:m.start(3)] + other + line[m.end(3):], f"field {m.group(3)} -> {other}"))
        for m in re.finditer(r"\b(HAS|NEED|ATT|ATT1|ATTX)\((\w+)", line):
            name = m.group(2)
            if name in OBJECTS:
                other = OBJECTS[(OBJECTS.index(name) + 1) % len(OBJECTS)]
                out.append((index, line[:m.start(2)] + other + line[m.end(2):], f"object {name} -> {other}"))
        for m in re.finditer(r"\bMADE\((\w+)\)", line):
            name = m.group(1)
            if name in STAGES:
                other = STAGES[(STAGES.index(name) + 1) % len(STAGES)]
                out.append((index, line[:m.start(1)] + other + line[m.end(1):], f"stage {name} -> {other}"))
        for m in re.finditer(r"TFP_RT_(REC|PROOF|EVENT|GATE|TIMING|FIRDOPT|DEFAULTS|ACTX)_(\w+)", line):
            kind, field = m.group(1), m.group(2).lower()
            fields = [f.lower() for f in spec.TABLES[kind]]
            if field in fields:
                other = fields[(fields.index(field) + 1) % len(fields)].upper()
                out.append((index, line[:m.start(2)] + other + line[m.end(2):], f"{kind} {m.group(2)} -> {other}"))
        for m in re.finditer(r"H->(\w+)_(stop|shutdown|update|is_clean|is_enabled|is_active|result|preflight|prepare)\(H", line):
            swap = {"stop": "shutdown", "shutdown": "stop", "update": "stop", "is_clean": None, "is_enabled": "is_clean",
                    "is_active": None, "result": None, "preflight": "stop", "prepare": "stop"}[m.group(2)]
            if swap:
                out.append((index, line[:m.start(2)] + swap + line[m.end(2):], f"call {m.group(1)}_{m.group(2)} -> {swap}"))
    return out


def attempt(job):
    number, index, new, what, work, lines = job
    folder = work / f"m{number}"
    folder.mkdir(parents=True, exist_ok=True)
    source = folder / "tfp_runtime.c"
    changed = list(lines)
    changed[index] = new
    source.write_text("\n".join(changed) + "\n")
    env = dict(os.environ, TFP_RUNTIME_SOURCE=str(source), TFP_RUNTIME_WORK=str(folder), TFP_RUNTIME_SHARED=str(work / "base"),
               TFP_RUNTIME_KEEP_TEST_OBJECT="1")
    try:
        done = subprocess.run(["bash", str(RUN)], env=env, capture_output=True, text=True, errors="replace", timeout=240)
        ran = "Runtime:" in done.stdout or "ERROR" in done.stderr or "runtime error" in done.stderr
        result = "survived" if done.returncode == 0 else ("killed" if ran or "error:" not in done.stderr else "invalid")
        if done.returncode < 0:
            result = "killed"  # crashed
    except subprocess.TimeoutExpired:
        result = "killed"  # never ends
    for item in folder.iterdir():
        item.unlink()
    folder.rmdir()
    return number, index, what, new, result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    parser.add_argument("--only")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--retry", type=Path, help="an earlier output of this tool: only its survivors are tried again")
    args = parser.parse_args()
    lines = SOURCE.read_text().split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    todo = base.mutants(lines) + extra(lines)
    seen, unique = set(), []
    for index, new, what in todo:
        if (index, new) not in seen and new != lines[index]:
            seen.add((index, new))
            unique.append((index, new, what))
    todo = sorted(unique)
    if args.only:
        todo = [m for m in todo if re.search(args.only, lines[m[0]])]
    if args.retry:
        earlier = set()
        for row in args.retry.read_text().split("\n"):
            found = re.match(r"SURVIVED (\d+): (.*?) => ", row)
            if found:
                earlier.add((int(found.group(1)) - 1, found.group(2)))
        todo = [m for m in todo if (m[0], m[2]) in earlier]
    if args.list:
        for index, new, what in todo:
            print(f"{index + 1}: {what}: {new.strip()}")
        print(len(todo), "mutants")
        return 0
    folder = args.work / "base"
    folder.mkdir(parents=True, exist_ok=True)
    if subprocess.run(["bash", str(RUN)], env=dict(os.environ, TFP_RUNTIME_WORK=str(folder)), capture_output=True).returncode:
        sys.exit("the unchanged source does not pass")
    jobs = [(n, index, new, what, args.work, lines) for n, (index, new, what) in enumerate(todo)]
    tally = {"killed": 0, "survived": 0, "invalid": 0}
    survivors = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for number, index, what, new, result in pool.map(attempt, jobs):
            tally[result] += 1
            if result == "survived":
                survivors.append(f"SURVIVED {index + 1}: {what} => {new.strip()}")
    for row in survivors:
        print(row)
    print(f"{len(jobs)} single changes, {tally['invalid']} did not compile, {tally['killed']} killed, {tally['survived']} survived")
    return 0


if __name__ == "__main__":
    sys.exit(main())
