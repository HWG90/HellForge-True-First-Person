#!/usr/bin/env python3
"""Cut reference vectors from the retail vignette.lua (tools/retail_ref/run_vignette.lua).

  gen_vignette.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import sys

from effects_gen import generate

if __name__ == "__main__":
    sys.exit(generate("vignette", "vignette.lua"))
