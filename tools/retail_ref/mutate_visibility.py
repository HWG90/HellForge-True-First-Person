#!/usr/bin/env python3
"""Mutation check of src/head_visibility.c and src/model_shadow_source.c against their vectors.

  mutate_visibility.py --target head|model --count 240 --seed 1 --work <scratch folder> [--jobs 8]

One change at a time is made in a scratch copy of the C file (a comparison or logic operator swapped, a
constant changed, true/false flipped, + and - swapped, a statement dropped); the copy is built with the
vector test and run. A change the test does not notice is a survivor and is printed with its line. A copy
that does not compile is not counted. The build has the sanitizers of the test script (a change that reads
past a buffer is noticed that way). Nothing in the project tree is touched."""
import argparse
import concurrent.futures
import random
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGETS = {"head": ("head_visibility.c", ["model_shadow_source.c"], "head_visibility_test.c"),
           "model": ("model_shadow_source.c", [], "model_shadow_source_test.c")}
SWAPS = [("==", "!="), ("!=", "=="), ("&&", "||"), ("||", "&&"), (" < ", " <= "), (" <= ", " < "), (" > ", " >= "), (" >= ", " > "),
         (" + ", " - "), (" - ", " + "), ("true", "false"), ("false", "true"), (" += ", " -= ")]
NUMBER = re.compile(r"(?<![\w.])(0x[0-9a-fA-F]+|\d+\.\d+|\.\d+|\d+)(?![\w.])")
STATEMENT = re.compile(r"^(\s+)([A-Za-z_*(][^;{}]*(?:=|\+\+|\()[^;{}]*;)\s*(/\*.*\*/)?\s*$")


def code_spans(line):
    """The parts of a line that are code: not comments, strings or preprocessor text."""
    if line.lstrip().startswith("#"): return []
    spans, i, start = [], 0, 0
    while i < len(line):
        if line.startswith("/*", i):
            spans.append((start, i)); end = line.find("*/", i); i = len(line) if end < 0 else end + 2; start = i
        elif line[i] == '"':
            spans.append((start, i)); i += 1
            while i < len(line) and line[i] != '"': i += 2 if line[i] == "\\" else 1
            i += 1; start = i
        elif line[i] == "'":
            spans.append((start, i)); i += 3 if line[i + 1] != "\\" else 4; start = i
        else: i += 1
    spans.append((start, len(line)))
    return [(a, b) for a, b in spans if b > a]


def candidates(lines):
    out, in_comment = [], False
    for number, line in enumerate(lines):
        if in_comment:
            in_comment = "*/" not in line
            continue
        if line.lstrip().startswith("/*") and "*/" not in line:
            in_comment = True
            continue
        if line.lstrip().startswith(("/*", " *", "*")) and "*/" not in line.split("/*")[0]:
            if "*/" in line and line.rstrip().endswith("*/") and line.lstrip().startswith(("/*", "*")): continue
        for a, b in code_spans(line):
            part = line[a:b]
            for old, new in SWAPS:
                at = part.find(old)
                while at >= 0:
                    if not (old in ("true", "false") and (part[at - 1:at].isalnum() or part[at + len(old):at + len(old) + 1].isalnum())):
                        out.append((number, a + at, len(old), new, f"{old.strip()} -> {new.strip()}"))
                    at = part.find(old, at + 1)
            for match in NUMBER.finditer(part):
                text = match.group(1)
                if text.startswith("0x"): new = hex(int(text, 16) + 1)
                elif "." in text: new = repr(float(text) * 2 + 0.1)
                else: new = str(int(text) + 1)
                out.append((number, a + match.start(1), len(text), new, f"{text} -> {new}"))
        match = STATEMENT.match(line)
        if match and not re.match(r"\s*(return|break|continue|typedef|static|const|double|bool|int|size_t|uint8_t|char|Tfp\w+|[A-Z]\w+ \*?\w+( =|;|,))", line):
            out.append((number, len(match.group(1)), len(match.group(2)), ";", "statement dropped"))
    return out


def trial(args):
    index, target, work, number, column, length, new, what, lines = args
    source, others, test = TARGETS[target]
    folder = work / f"m{index}"
    folder.mkdir(parents=True, exist_ok=True)
    mutated = list(lines)
    mutated[number] = lines[number][:column] + new + lines[number][column + length:]
    (folder / source).write_text("\n".join(mutated) + "\n")
    binary = folder / "test"
    build = subprocess.run(["cc", "-std=c11", "-O1", "-w", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined", "-I", str(ROOT / "src"), "-I", str(ROOT / "tests"), str(folder / source),
                            *[str(ROOT / "src" / other) for other in others], str(ROOT / "tests" / test), "-lm", "-o", str(binary)],
                           capture_output=True, text=True)
    if build.returncode:
        return index, "invalid", number, what
    try:
        run = subprocess.run([str(binary)], capture_output=True, timeout=600, env={"ASAN_OPTIONS": "detect_leaks=0"})
        verdict = "killed" if run.returncode else "survived"
    except subprocess.TimeoutExpired:
        verdict = "killed"
    for path in folder.iterdir(): path.unlink()
    folder.rmdir()
    return index, verdict, number, what


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", choices=sorted(TARGETS), required=True)
    parser.add_argument("--count", type=int, default=240)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    lines = (ROOT / "src" / TARGETS[args.target][0]).read_text().split("\n")
    pool = candidates(lines)
    random.Random(args.seed).shuffle(pool)
    chosen = pool[:args.count]
    jobs = [(i, args.target, args.work, *c, lines) for i, c in enumerate(chosen)]
    tally = {"killed": 0, "survived": 0, "invalid": 0}
    survivors = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as executor:
        for index, verdict, number, what in executor.map(trial, jobs):
            tally[verdict] += 1
            if verdict == "survived": survivors.append((number, what))
    for number, what in sorted(survivors):
        print(f"survived: line {number + 1}: {what}: {lines[number].strip()[:150]}")
    counted = tally["killed"] + tally["survived"]
    print(f"{args.target}: {len(pool)} possible changes, {len(chosen)} tried, {tally['invalid']} did not compile, "
          f"{tally['killed']} of {counted} killed ({100 * tally['killed'] / max(counted, 1):.1f}%), {tally['survived']} survived")
    return 0


if __name__ == "__main__":
    sys.exit(main())
