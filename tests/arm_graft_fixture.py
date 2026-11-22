#!/usr/bin/env python3
"""Make the arm graft fixture from the retired First Person Controller's own functions.

Reads the 120 paired body / first-person rig poses the retired mod captured in the game on build 25480438
(work/live-d9c4c484/paired-arm-poses-0.3.34.npz in its tree; docs/VALIDATION.md "Body animation on the
first-person model in 0.3.35"), compiles tests/arm_graft_reference.c against the retired sources and
writes the poses with the retired functions' results. The retired tree is only read.

  python3 tests/arm_graft_fixture.py OUT [--every N] [--retired DIR]

--every 4 (30 frames) made tests/reference/arm-graft/paired-poses.bin; --every 1 is all 120 frames.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
STATICS = ("graft_underlying", "graft_local_matrix", "graft_align_action", "graft_set_transform", "graft_mix_local")


def static_function(text, name):
    found = re.search(r"^static [^\n;]*\b%s\(.*?^}\n" % re.escape(name), text, re.S | re.M)
    if not found:
        raise SystemExit(f"{name} is not in the retired src/native_arm_graft.h")
    return found.group(0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out", type=Path)
    parser.add_argument("--every", type=int, default=4)
    parser.add_argument("--retired", type=Path,
                        default=Path.home() / "Projects/HellForge/Retired/true-first-person-controller-body14-20261118")
    args = parser.parse_args()
    source = (args.retired / "src/native_arm_graft.h").read_text()
    statics = "".join(static_function(source, name) for name in STATICS)
    with np.load(args.retired / "work/live-d9c4c484/paired-arm-poses-0.3.34.npz") as capture, \
            tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as directory:
        body, fp = capture["body"][::args.every], capture["fp"][::args.every]
        assert body.shape[1:] == (107, 4, 4) and fp.shape[1:] == (63, 4, 4) and body.dtype == np.float32
        rows = [capture[name].astype("<u4").tobytes() for name in ("body_names", "body_links", "fp_names", "fp_links")]
        for pair in zip(body, fp, strict=True):
            rows.extend(frame.astype("<f4").tobytes() for frame in pair)
        statics_file = Path(directory) / "statics.h"
        statics_file.write_text(statics)
        program = Path(directory) / "reference"
        wrapper = Path(directory) / "reference.c"
        wrapper.write_text("#include <stdlib.h>\n" + (HERE / "arm_graft_reference.c").read_text().replace(
            "ARM_GRAFT_RETIRED_STATICS\n", '#include "statics.h"\n', 1))
        subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-O2", "-ffp-contract=off", "-w", "-I", directory,
                        "-I", args.retired / "src", wrapper, args.retired / "src/native_view.c",
                        args.retired / "src/controller_core.c", "-lm", "-o", program], check=True)
        result = subprocess.run([program, str(len(body))], input=b"".join(rows), stdout=subprocess.PIPE, check=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(result.stdout)
    print(f"{args.out}: {len(body)} frames, {len(result.stdout)} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()
