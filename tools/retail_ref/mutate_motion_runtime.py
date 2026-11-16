#!/usr/bin/env python3
"""Mutation check of src/motion_runtime.c against the retail vectors.

Makes single changes to a scratch copy of the C file (a comparison turned round or made strict,
&& for ||, + for -, * for /, a constant changed, a true for a false, one named value for its
neighbour, a negation dropped, a text changed, a statement removed), runs
tests/run-vectors-motion-runtime.sh on each, and counts how many the vectors notice. A change
that does not compile is left out. Survivors are listed with their line.

  mutate_motion_runtime.py --work <scratch folder> [--jobs N] [--only <regex on the line>] [--list]
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SOURCE = ROOT / "src/motion_runtime.c"
RUN = ROOT / "tests/run-vectors-motion-runtime.sh"
DECLARES = ("static", "const", "double", "bool", "char", "int", "uint64_t", "size_t", "Tfp", "typedef", "struct", "enum", "#", "}", "{", "/*", "*")
NAMED = [("TFP_MOTION_TRI_TRUE", "TFP_MOTION_TRI_FALSE"), ("TFP_MOTION_TRI_FALSE", "TFP_MOTION_TRI_NIL"), ("TFP_MOTION_TRI_NIL", "TFP_MOTION_TRI_TRUE"),
         ("TFP_MOTION_GOT_VALUE", "TFP_MOTION_GOT_NIL"), ("TFP_MOTION_GOT_NIL", "TFP_MOTION_GOT_RAISED"), ("TFP_MOTION_GOT_RAISED", "TFP_MOTION_GOT_VALUE"),
         ("TFP_MOTION_RUNTIME_TRUE", "TFP_MOTION_RUNTIME_FALSE"), ("TFP_MOTION_RUNTIME_FALSE", "TFP_MOTION_RUNTIME_TRUE"),
         ("TFP_MOTION_RUNTIME_RAISED", "TFP_MOTION_RUNTIME_TRUE"),
         ("TFP_MOTION_PHASE_NEUTRAL", "TFP_MOTION_PHASE_ACTIVE"), ("TFP_MOTION_PHASE_ACTIVE", "TFP_MOTION_PHASE_NEUTRAL"),
         ("TFP_MOTION_PHASE_IDLE", "TFP_MOTION_PHASE_NEUTRAL"), ("TFP_MOTION_PHASE_UNAVAILABLE", "TFP_MOTION_PHASE_NEUTRAL"),
         ("TFP_MOTION_PHASE_DISABLED", "TFP_MOTION_PHASE_NEUTRAL"), ("TFP_MOTION_PHASE_EDITOR_FROZEN", "TFP_MOTION_PHASE_NEUTRAL"),
         ("TFP_MOTION_PHASE_CAPTURE", "TFP_MOTION_PHASE_ACTIVE"), ("TFP_MOTION_PHASE_NONE", "TFP_MOTION_PHASE_IDLE"),
         ("TFP_ACT_ADS", "TFP_ACT_AIM"), ("TFP_ACT_IDLE", "TFP_ACT_AIM"), ("TFP_ACT_NONE", "TFP_ACT_IDLE"),
         ("TFP_PLACE_YAW", "TFP_PLACE_PITCH"), ("TFP_PLACE_PITCH", "TFP_PLACE_ROLL"), ("TFP_PLACE_ROLL", "TFP_PLACE_YAW"),
         ("TFP_PLACE_ALL", "TFP_PLACE_XYZ"), ("TFP_LOCO_ALL", "TFP_LOCO_DX"),
         ("TFP_MOTION_DX", "TFP_MOTION_DY"), ("TFP_MOTION_DY", "TFP_MOTION_DZ"), ("TFP_MOTION_DZ", "TFP_MOTION_DX"), ("TFP_MOTION_QX", "TFP_MOTION_QY"),
         ("TFP_MOTION_QY", "TFP_MOTION_QZ"), ("TFP_MOTION_QZ", "TFP_MOTION_QW"), ("TFP_MOTION_QW", "TFP_MOTION_QX"),
         ("TFP_MOTION_AXES", "TFP_MOTION_QW"),
         ("TFP_MOTION_FALSE", "TFP_MOTION_TRUE"), ("TFP_MOTION_TRUE", "TFP_MOTION_FALSE"), ("TFP_MOTION_OTHER", "TFP_MOTION_TRUE"),
         ("TFP_MOTION_NIL", "TFP_MOTION_NUMBER"), ("TFP_MOTION_NUMBER", "TFP_MOTION_NIL"), ("TFP_MOTION_NOT_NUMBER", "TFP_MOTION_NUMBER"),
         ("TFP_MOTION_RESET_NONE", "TFP_MOTION_RESET_INITIAL"), ("TFP_ACTION_ADS_TRUE", "TFP_ACTION_ADS_OTHER"),
         ("TFP_ACTION_ADS_OTHER", "TFP_ACTION_ADS_TRUE"), ("TFP_VERIFIED_TRUE", "TFP_VERIFIED_FALSY"), ("TFP_VERIFIED_FALSY", "TFP_VERIFIED_TRUE"),
         ("TFP_MOTION_ADS_TRUE", "TFP_MOTION_ADS_OTHER"), ("TFP_MOTION_ADS_OTHER", "TFP_MOTION_ADS_TRUE"),
         ("lua_min", "lua_max"), ("lua_max", "lua_min"), ("sqrt", "fabs"), ("acos", "asin"), ("exp(", "exp2("), ("cos(", "sin(")]


def strings(line):
    return [(m.start(), m.end()) for m in re.finditer(r'"(?:[^"\\]|\\.)*"', line)]


def outside(spans, at):
    return all(not (a <= at < b) for a, b in spans)


def mutants(lines):
    out = []

    def add(index, new, what):
        if new != lines[index]:
            out.append((index, new, what))

    in_comment = False
    for index, line in enumerate(lines):
        stripped = line.strip()
        if in_comment:
            in_comment = "*/" not in line
            continue
        if stripped.startswith("/*") and "*/" not in stripped:
            in_comment = True
            continue
        if not stripped or stripped.startswith(("/*", "*", "//", "#include", "#define NEUTRAL")):
            continue
        cut = None
        for m in re.finditer(r"/\*", line):
            if outside(strings(line), m.start()):
                cut = m.start()
                break
        code = line if cut is None else line[:cut]
        tail = line[len(code):]
        spans = strings(code)

        def put(m, new, what, group=0, code=code, tail=tail, index=index):
            add(index, code[:m.start(group)] + new + code[m.end(group):] + tail, what)
        for m in re.finditer(r"==|!=", code):
            if outside(spans, m.start()):
                put(m, "!=" if m.group() == "==" else "==", f"{m.group()} turned round")
        for m in re.finditer(r"(?<![<>=!\-]) (<=|>=|<|>) (?![<>=])", code):
            if outside(spans, m.start()):
                swap = {"<": "<=", "<=": "<", ">": ">=", ">=": ">"}[m.group(1)]
                put(m, swap, f"{m.group(1)} -> {swap}", 1)
                other = {"<": ">", "<=": ">=", ">": "<", ">=": "<="}[m.group(1)]
                put(m, other, f"{m.group(1)} -> {other}", 1)
        for m in re.finditer(r"&&|\|\|", code):
            if outside(spans, m.start()):
                put(m, "||" if m.group() == "&&" else "&&", f"{m.group()} swapped")
        for m in re.finditer(r" (\+|-|\*|/) (?!=)", code):
            if outside(spans, m.start()) and not stripped.startswith("#"):
                swap = {"+": "-", "-": "+", "*": "/", "/": "*"}[m.group(1)]
                put(m, swap, f"{m.group(1)} -> {swap}", 1)
        for m in re.finditer(r"\b(true|false)\b", code):
            if outside(spans, m.start()):
                put(m, "false" if m.group() == "true" else "true", f"{m.group()} flipped")
        for name, other in NAMED:
            for m in re.finditer(re.escape(name) + (r"\b" if name[-1].isalnum() else ""), code):
                if outside(spans, m.start()) and (m.start() == 0 or not (code[m.start() - 1].isalnum() or code[m.start() - 1] == "_")):
                    put(m, other, f"{name} -> {other.rstrip('(')}")
        for m in re.finditer(r"\(!(?=[A-Za-z_(])", code):
            if outside(spans, m.start()):
                add(index, code[:m.start() + 1] + code[m.end():] + tail, "negation dropped")
        for m in re.finditer(r"(?<=[ ])!(?=[A-Za-z_(])", code):
            if outside(spans, m.start()):
                add(index, code[:m.start()] + code[m.end():] + tail, "negation dropped")
        # Every assignment and every call that is a statement of its own, removed by itself.
        if not stripped.startswith(("for", "#", "typedef")):
            for m in re.finditer(r"(^\s*|[;{)] |else )([A-Za-z_][\w>.\[\]\-]*(?: [|+\-*/]?= |\()[^;]*;)", code):
                if outside(spans, m.start(2)) and outside(spans, m.end(2) - 1) and not m.group(2).startswith(("return", "if", "sizeof")):
                    put(m, "(void)0;", "statement removed: " + m.group(2)[:40], 2)
        for m in re.finditer(r"(?<![\w.\"])(\d+\.\d+|\.\d+|\d+)(?![\w.\"xX])", code):
            if not outside(spans, m.start()):
                continue
            before, after = code[:m.start()].rstrip(), code[m.end():].lstrip()
            if before.endswith("[") and after.startswith("]") and stripped.startswith(DECLARES):
                continue  # an array size
            text = m.group()
            if "." in text:
                new = repr(float(text) * 1.5 + .001)
            else:
                new = str(int(text) + 1)
            put(m, new, f"{text} -> {new}")
        for a, b in spans:
            body = code[a + 1:b - 1]
            if len(body) < 2:
                continue
            at = next((i for i, ch in enumerate(body) if ch.isalpha() and (i == 0 or body[i - 1] not in "%\\")), None)
            if at is None:
                continue
            add(index, code[:a + 1 + at] + ("7" if body[at] != "7" else "3") + code[a + 2 + at:] + tail, "text changed")
    return out


def attempt(job):
    number, index, new, what, work, lines = job
    folder = work / f"m{number}"
    folder.mkdir(parents=True, exist_ok=True)
    source = folder / "motion_runtime.c"
    changed = list(lines)
    changed[index] = new
    source.write_text("\n".join(changed) + "\n")
    env = dict(os.environ, TFP_MOTION_RUNTIME_SOURCE=str(source), TFP_MOTION_RUNTIME_WORK=str(folder),
               TFP_MOTION_RUNTIME_SHARED=str(work / "base"), TFP_MOTION_RUNTIME_KEEP_TEST_OBJECT="1")
    try:
        done = subprocess.run(["bash", str(RUN)], env=env, capture_output=True, text=True, errors="replace", timeout=180)
        ran = "motion_runtime:" in done.stderr or "ERROR" in done.stderr or "runtime error" in done.stderr
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
    todo = mutants(lines)
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
    base = args.work / "base"
    base.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TFP_MOTION_RUNTIME_WORK=str(base))
    if subprocess.run(["bash", str(RUN)], env=env, capture_output=True).returncode:
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
