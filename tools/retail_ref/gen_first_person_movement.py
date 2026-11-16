#!/usr/bin/env python3
"""Cut reference vectors from the retail first_person_movement.lua (tools/retail_ref/run_first_person_movement.lua).

  gen_first_person_movement.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import sys

from effects_gen import generate

if __name__ == "__main__":
    sys.exit(generate("first_person_movement", "first_person_movement.lua"))
