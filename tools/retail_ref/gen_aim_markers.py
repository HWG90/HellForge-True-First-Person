#!/usr/bin/env python3
"""Cut reference vectors from the retail aim_markers.lua (tools/retail_ref/run_aim_markers.lua).

  gen_aim_markers.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import sys

from effects_gen import generate

if __name__ == "__main__":
    sys.exit(generate("aim_markers", "aim_markers.lua"))
