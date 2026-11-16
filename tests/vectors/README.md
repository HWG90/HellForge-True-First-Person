# Reference vectors

Each `*_vectors.h` holds a script of operations and what the retail Lua module answered to it, run unmodified under LuaJIT by `tools/retail_ref/gen.py`. The C translation must answer the same. The header records the retail commit (the project is frozen at 1d0d453, 0.5.41-smooth.66) and the SHA-256 of the retail file.

Regenerate only when the retail reference is deliberately moved:

    python3 tools/retail_ref/gen.py --retail "$HOME/Worktrees/goose-gadgets/tfp-smooth/Goose Gadget Kit" --out tests/vectors

The scripts mix hand-written situations (aim in and out inside one blend, a weapon swap, a respawn, three failures in a row) with a few thousand seeded random operations each.
