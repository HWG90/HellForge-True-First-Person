#!/usr/bin/env python3
"""Cut reference vectors from the retail native_ads_option.lua (tools/retail_ref/run_native_ads_option.lua).

  gen_native_ads_option.py --retail "<Goose Gadget Kit folder>" --out tests/vectors
"""
import sys

from effects_gen import generate

if __name__ == "__main__":
    sys.exit(generate("native_ads_option", "native_ads_option.lua"))
