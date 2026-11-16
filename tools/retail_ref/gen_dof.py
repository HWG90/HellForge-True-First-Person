#!/usr/bin/env python3
"""Cut reference vectors from the retail dof.lua (tools/retail_ref/run_dof.lua).

  gen_dof.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import sys

from effects_gen import generate

if __name__ == "__main__":
    sys.exit(generate("dof", "dof.lua"))
