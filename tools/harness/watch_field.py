#!/usr/bin/env python3
"""Print the array the game frees at shutdown (crash helldivers2.exe+0x66a371): {pointer, count} at
[game.dll+0x3326340] + 0xF846C8, with the first bytes behind the pointer and of the 64 KiB block it sits in."""
import struct, sys
sys.path.insert(0, __file__.rsplit("/", 1)[0])
import peek
pid = peek.game_pid()
base, _ = peek.regions(pid, "game.dll")
with open(f"/proc/{pid}/mem", "rb", buffering=0) as mem:
    def read(at, n):
        mem.seek(at)
        return mem.read(n)
    g = struct.unpack("<Q", read(base + 0x3326340, 8))[0]
    row = read(g + 0xF846C8, 0x28)
    ptr, count, ptr2 = struct.unpack_from("<QI4xQ", row)
    print(f"G={g:#x} field={row.hex(' ')}")
    for name, p in (("ptr", ptr), ("ptr2", ptr2)):
        if p:
            try:
                print(f" {name}={p:#x} count={count} data={read(p, 32).hex(' ')}")
                print(f"   block {p & ~0xffff:#x}: {read(p & ~0xffff, 0x50).hex(' ')}")
            except OSError as error:
                print(f" {name}={p:#x} unreadable ({error})")
