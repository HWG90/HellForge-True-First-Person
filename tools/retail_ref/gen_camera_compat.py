#!/usr/bin/env python3
"""Cut reference vectors from the retail camera_compat.lua (tools/retail_ref/run_camera_compat.lua).

  gen_camera_compat.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import sys

from effects_gen import generate

if __name__ == "__main__":
    sys.exit(generate("camera_compat", "camera_compat.lua"))
