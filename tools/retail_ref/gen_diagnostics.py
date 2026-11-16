#!/usr/bin/env python3
"""Cut reference vectors from the retail diagnostics.lua (tools/retail_ref/run_diagnostics.lua).

  gen_diagnostics.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import sys

from effects_gen import generate

if __name__ == "__main__":
    sys.exit(generate("diagnostics", "diagnostics.lua"))
