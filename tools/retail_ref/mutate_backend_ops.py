#!/usr/bin/env python3
"""Mutation check of src/backend_ops.c against the retail vectors.

Makes single changes to a scratch copy of the C file (a comparison turned round, && for ||,
a constant plus one, a text changed, a negation dropped, a statement removed, a result swapped),
runs tests/run-vectors-backend-ops.sh on each, and counts how many the vectors notice.
A change that does not compile is left out. Survivors are listed with their line.

  mutate_backend_ops.py --work <scratch folder> [--jobs N] [--only <regex on the line>] [--list] [--retry <earlier output>]
"""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
SOURCE = ROOT / "src/backend_ops.c"
TYPES = ("uint", "int", "bool", "char", "const", "static", "TfpOps", "double", "size_t", "struct", "return", "if", "for", "while",
         "else", "typedef", "enum", "va_", "Pin", "BodyApi", "unsigned", "void", "break", "continue", "#", "}", "{", "/*", "*")


def strings(line):
    """Spans of the string literals of a line."""
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
        if not stripped or stripped.startswith(("/*", "*", "//", "#include", "#ifdef", "#endif", "#undef")):
            continue
        if "B_PRESENTATION_" in line and stripped.startswith("#define") and "ESTABLISHED" not in line:
            continue  # only the test's own header gives these a value
        if stripped.startswith(("{false, 0,", "{true, 0,")):
            continue
        code = line.split("/*")[0] if "/*" in line and outside(strings(line), line.index("/*")) else line
        tail = line[len(code):]
        spans = strings(code)
        define = stripped.startswith("#define")
        # `uint32_t count = 0, index = 0;`: a local that is always assigned before it is read. Its first value is not behaviour.
        declaration = stripped.startswith(TYPES[:10]) and " = " in stripped and "(" not in stripped.split("=")[0] and "{" not in stripped
        if not define:
            for m in re.finditer(r"==|!=", code):
                if outside(spans, m.start()):
                    add(index, code[:m.start()] + ("!=" if m.group() == "==" else "==") + code[m.end():] + tail, f"{m.group()} turned round")
            for m in re.finditer(r"(?<![<>=!\-]) (<=|>=|<|>) (?![<>=])", code):
                if outside(spans, m.start()):
                    swap = {"<": "<=", "<=": "<", ">": ">=", ">=": ">"}[m.group(1)]
                    add(index, code[:m.start(1)] + swap + code[m.end(1):] + tail, f"{m.group(1)} -> {swap}")
            for m in re.finditer(r"&&|\|\|", code):
                if outside(spans, m.start()):
                    add(index, code[:m.start()] + ("||" if m.group() == "&&" else "&&") + code[m.end():] + tail, f"{m.group()} swapped")
            for m in re.finditer(r"\b(true|false)\b", code):
                if outside(spans, m.start()) and not declaration:
                    add(index, code[:m.start()] + ("false" if m.group() == "true" else "true") + code[m.end():] + tail, f"{m.group()} flipped")
            for m in re.finditer(r"\bTFP_OPS_(TRUE|FALSE|NIL|RAISED)\b", code):
                swap = {"TRUE": "FALSE", "FALSE": "TRUE", "NIL": "FALSE", "RAISED": "FALSE"}[m.group(1)]
                add(index, code[:m.start(1)] + swap + code[m.end(1):] + tail, f"result {m.group(1)} -> {swap}")
            for m in re.finditer(r"\(!(?=[A-Za-z_(])", code):
                if outside(spans, m.start()):
                    add(index, code[:m.start() + 1] + code[m.end():] + tail, "negation dropped")
            for m in re.finditer(r"(?<=[ (])!(?=[A-Za-z_(])", code):
                if outside(spans, m.start()) and code[m.start() - 1] != "(":
                    add(index, code[:m.start()] + code[m.end():] + tail, "negation dropped")
            if stripped.endswith(";") and not stripped.startswith(TYPES) and re.match(r"[A-Za-z_][\w\->.\[\]]*(\(| = |\+\+| \|= )", stripped) \
                    and not stripped.startswith(("none(", "va_")):
                add(index, line[:len(line) - len(line.lstrip())] + ";" + "  /* removed */", "statement removed")
        for m in re.finditer(r"\b0x[0-9a-fA-F]+\b|(?<![\w.\"])\d+(?![\w.\"xX])", code):
            if not outside(spans, m.start()):
                continue
            before, after = code[:m.start()].rstrip(), code[m.end():].lstrip()
            if before.endswith("[") and after.startswith("]"):
                continue  # an array size
            if (declaration or stripped.startswith("memset(")) and m.group() == "0":
                continue
            if define and not re.match(r"#define [BL]_|#define (LOW|HIGH|ORPHAN_POLLS)", stripped):
                continue
            text = m.group()
            value = int(text, 16) + 1 if text.lower().startswith("0x") else int(text) + 1
            new = ("0x%x" % value) if text.lower().startswith("0x") else str(value)
            add(index, code[:m.start()] + new + code[m.end():] + tail, f"{text} -> {new}")
        for a, b in spans:
            body = code[a + 1:b - 1]
            if len(body) < 2 or body.startswith("%"):
                continue
            at = next((i for i, ch in enumerate(body) if ch.isalnum() and (i == 0 or body[i - 1] != "%") and (i == 0 or body[i - 1] != "\\")), None)
            if at is None:
                continue
            replacement = "7" if body[at] != "7" else "3"
            add(index, code[:a + 1 + at] + replacement + code[a + 2 + at:] + tail, "text changed")
    return out


def attempt(job):
    number, index, new, what, work, lines = job
    folder = work / f"m{number}"
    folder.mkdir(parents=True, exist_ok=True)
    source = folder / "backend_ops.c"
    changed = list(lines)
    changed[index] = new
    source.write_text("\n".join(changed) + "\n")
    env = dict(os.environ, TFP_BACKEND_OPS_SOURCE=str(source), TFP_BACKEND_OPS_WORK=str(folder), TFP_BACKEND_OPS_KEEP_TEST_OBJECT="1")
    for name in ("backend_ops_test.o", "backend_ops_open_test.o"):
        if not (folder / name).exists():
            os.link(work / "base" / name, folder / name)
    try:
        done = subprocess.run(["bash", str(ROOT / "tests/run-vectors-backend-ops.sh")], env=env, capture_output=True, text=True, errors="replace", timeout=120)
        compiled = "error:" not in done.stderr or "differs from retail" in done.stderr or "must refuse" in done.stderr
        result = "survived" if done.returncode == 0 else ("killed" if compiled else "invalid")
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
            found = re.match(r"SURVIVED (\d+): ([^:]+): ", row)
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
    env = dict(os.environ, TFP_BACKEND_OPS_WORK=str(base))
    if subprocess.run(["bash", str(ROOT / "tests/run-vectors-backend-ops.sh")], env=env, capture_output=True).returncode:
        sys.exit("the unchanged source does not pass")
    jobs = [(n, index, new, what, args.work, lines) for n, (index, new, what) in enumerate(todo)]
    tally = {"killed": 0, "survived": 0, "invalid": 0}
    survivors = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for number, index, what, new, result in pool.map(attempt, jobs):
            tally[result] += 1
            if result == "survived":
                survivors.append((index, what, new))
    for index, what, new in sorted(survivors):
        print(f"SURVIVED {index + 1}: {what}: {new.strip()[:170]}\n      was: {lines[index].strip()[:170]}")
    counted = tally["killed"] + tally["survived"]
    print(f"{len(todo)} changes: {tally['invalid']} do not compile, {tally['killed']} killed, {tally['survived']} survive "
          f"({100.0 * tally['killed'] / max(1, counted):.1f}% of {counted})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
