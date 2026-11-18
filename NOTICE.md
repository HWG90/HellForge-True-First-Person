# Notice

- **Reference:** the True First Person mode of Goose Gadget Kit 0.5.41-smooth.66 (HWG90, MIT). This repository is a C translation of it for HellForge's game build 25480438. The retail source is not included; the tests read it from a separate checkout when one is present.
- **Starting point:** an earlier C port of version 0.5.34 of the same mode (HWG90, MIT). Its plumbing is still in `src/` and its build is `bash build.sh --donor`. What was taken from it and how it was checked is in [docs/history/PROVENANCE.md](docs/history/PROVENANCE.md); its own notice is [docs/history/NOTICE-0.5.34.md](docs/history/NOTICE-0.5.34.md).
- **Engine bridge:** `src/engine.c` is adapted from the Goose Photo Mode backport for HellForge. It calls the game's own scripting runtime through its exported C functions.
- **HellForge SDK 16** is a build dependency supplied by the user's HellForge installation; it is not included.
- **Game data:** addresses and code checks for build 25480438 were established from the user's own copies of the game's files. No game binaries or assets are included or distributed.
- HELLDIVERS 2 is a trademark of its owners. This project is not affiliated with or endorsed by them.
