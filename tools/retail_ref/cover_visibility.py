#!/usr/bin/env python3
"""Statement coverage of the retail head_visibility.lua and model_shadow_source.lua under the vector scripts.

  cover_visibility.py --retail "<Goose Gadget Kit folder>" --work <scratch folder> <script file>...

The retail files pack many statements on a line. A scratch copy is split to one statement per line (after
`;`, `then`, `else`, `do` and a function's parameter list; before `end`, `else`, `elseif`, `local`, `return`,
`if`, `for`, `while`), every script is replayed on the copies under a line hook, and every line that holds
bytecode but was never reached is listed with the retail line it came from. The copies are only used here:
the vectors come from the unmodified files. Write the script files with the generators' --script option."""
import argparse
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FILES = ["head_visibility.lua", "model_shadow_source.lua"]
TOKEN = re.compile(r"""--[^\n]*|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"|[A-Za-z_][A-Za-z_0-9]*|\n|[ \t]+|\.\.\.|\.\.|[<>=~]=|.""", re.S)
AFTER = {"then", "else", "do", ";"}
BEFORE = {"end", "else", "elseif", "local", "return", "if", "for", "while", "until"}


def split(source):
    out, origin, line, current = [], [], 1, ""
    tokens = TOKEN.findall(source)

    def flush():
        nonlocal current
        if current.strip():
            out.append(current.strip()); origin.append(line)
        current = ""

    i = 0
    while i < len(tokens):
        token = tokens[i]
        if token == "\n":
            flush(); line += 1
        elif token.startswith("--"):
            pass
        elif token == "function":
            current += token
            i += 1
            depth = 0
            while True:                                   # the name and the parameter list stay with the keyword
                token = tokens[i]
                if token == "\n": line += 1; token = " "
                current += token
                if token == "(": depth += 1
                if token == ")":
                    depth -= 1
                    if depth == 0: break
                i += 1
            flush()
        elif token in BEFORE:
            flush(); current += token
            if token in AFTER: flush()
        else:
            current += token
            if token in AFTER: flush()
        i += 1
    flush()
    return out, origin


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--retail", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("scripts", type=Path, nargs="+")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    origins, texts = {}, {}
    for name in FILES:
        lines, origin = split((args.retail / "src/first_person" / name).read_text(encoding="latin-1"))
        offset = 10000 * (FILES.index(name) + 1)                # a line number alone then names the file (see run_visibility.lua)
        (args.work / name).write_text("\n" * offset + "\n".join(lines) + "\n", encoding="latin-1")
        origins["src/first_person/" + name], texts["src/first_person/" + name] = origin, lines
    hit = {}
    for script in args.scripts:
        report = args.work / (script.name + ".cover")
        done = subprocess.run(["luajit", str(HERE / "run_visibility.lua"), str(args.retail), str(script), "--cover", str(args.work), str(report)],
                              capture_output=True, text=True)
        if done.returncode:
            sys.exit(done.stderr[-3000:])
        for row in report.read_text().split("\n")[:-1]:
            name, line, reached = row.split(" ")
            key = (name, int(line))
            hit[key] = hit.get(key, False) or reached == "1"
    for name in sorted(origins):
        # A line that is only `end` holds the implicit return of a function that always leaves through `return`.
        counted = {line: reached for (file, line), reached in hit.items() if file == name and texts[name][line - 1] != "end"}
        missed = sorted(line for line, reached in counted.items() if not reached)
        total = len(counted)
        print(f"{name}: {total - len(missed)} of {total} statements reached")
        for line in missed:
            print(f"  retail line {origins[name][line - 1]}: {texts[name][line - 1][:150]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
