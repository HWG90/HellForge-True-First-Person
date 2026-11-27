#!/usr/bin/env python3
"""Drive a HellForge game started by the Bingus Development Harness.

The game runs as a native Wayland window, so it only reads input while it really has desktop focus and
only the compositor can capture it. This module therefore:
  - brings the game window to the front through a KWin script (focus),
  - plays key, button and pointer messages into the game window from inside its Wine prefix (Input),
  - captures the active window with Spectacle (shot),
  - runs Lua inside the game through the harness (lua).

While a run is going the keyboard and mouse belong to the game. Every action first checks that the game
still has focus and refuses to type into another window.

Command line:
  drive.py focus
  drive.py shot OUT.png
  drive.py run "wait 0.5; key r; hold w 0.7; click left 0.6; look 0 -40; key u"
  drive.py run "down right; wait 0.35; click middle; wait 2"    full aim (the middle button is the game's aim mode)
  drive.py run "down r; wait 0.8; wheel 1; wait 0.3; up r"      the scope's range: hold R, the wheel up one notch
"""
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HARNESS = Path(os.environ.get("HD2H_DIR", Path.home() / "hellforge-research/tools/BingusDevelopmentHarness"))
WINDOW_CLASS = "helldivers2.exe"


class DriveError(RuntimeError):
    pass


def lua(code, timeout=40):
    """Run Lua in the game; returns the first returned value."""
    env = dict(os.environ, HD2H_TARGET="hellforge")
    out = subprocess.run([sys.executable, "harness.py", "lua", code], cwd=HARNESS, env=env, capture_output=True,
                         text=True, timeout=timeout)
    try:
        reply = json.loads(out.stdout)
    except ValueError as error:
        raise DriveError(f"harness lua gave no JSON: {out.stdout[-300:]!r} {out.stderr[-300:]!r}") from error
    if reply.get("error"):
        raise DriveError(f"lua error: {reply['error']}")
    values = reply.get("values") or [None]
    return values[0]


def has_focus():
    return lua("return stingray.Window.has_focus()") is True


def _kwin(script):
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
        handle.write(script)
    name = f"tfp-drive-{os.getpid()}-{time.monotonic_ns()}"
    try:
        ident = subprocess.run(["qdbus6", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting.loadScript",
                                handle.name, name], capture_output=True, text=True, timeout=10).stdout.strip()
        if not ident.lstrip("-").isdigit() or int(ident) < 0:
            raise DriveError(f"KWin refused the script: {ident!r}")
        subprocess.run(["qdbus6", "org.kde.KWin", f"/Scripting/Script{ident}", "org.kde.kwin.Script.run"],
                       capture_output=True, timeout=10)
        time.sleep(0.3)
        subprocess.run(["qdbus6", "org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting.unloadScript", name],
                       capture_output=True, timeout=10)
    finally:
        os.unlink(handle.name)


def focus(tries=5):
    """Bring the game window to the front; true once the engine itself reports focus."""
    script = ("for (const w of workspace.windowList()) { if (w.resourceClass == '%s') {"
              " w.minimized = false; workspace.activeWindow = w; } }" % WINDOW_CLASS)
    for _ in range(tries):
        _kwin(script)
        time.sleep(0.5)
        if has_focus():
            return True
    return False


def shot(path):
    """Capture the active window (the game, once focused) to path."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["spectacle", "-b", "-n", "-a", "-o", str(path)], capture_output=True, timeout=30)
    if not path.exists() or path.stat().st_size < 1000:
        raise DriveError(f"no capture at {path}")
    return path


VK = {"shift": 0x10, "ctrl": 0x11, "lctrl": 0xA2, "alt": 0x12, "space": 0x20, "tab": 0x09, "esc": 0x1B, "f9": 0x78, "f10": 0x79,
      "f11": 0x7A, "pagedown": 0x22, "pageup": 0x21, "up": 0x26, "down": 0x28, "left": 0x25, "right": 0x27, "e": 0x45,
      # "left" and "right" alone are the mouse buttons (BUTTONS wins), so the arrow keys have their own names
      "arrowup": 0x26, "arrowdown": 0x28, "arrowleft": 0x25, "arrowright": 0x27}
BUTTONS = {"left": "L", "right": "R", "middle": "M"}
# The mod's view toggle (a letter: _code gives its VK, U 0x55). U by default since round 36: HellForge 0.5's Armor LUT
# editor opens on F11, the old default. "f11" stays in VK to reach HellForge's own key on purpose.
TOGGLE = "u"
WHEEL_DELTA = 120     # one notch of the wheel (Windows' WHEEL_DELTA)
WHEEL_GAP_S = 0.1     # between two notches: three frames at 30 a second, so the game reads each one
WHEEL_MOST = 10       # notches in one step
PREFIX = Path.home() / ".local/share/hellforge-prefix/pfx"
GAME_EXE = r"C:\users\steamuser\Documents\HellForge\game\bin\helldivers2.exe"


def _code(name):
    if name in BUTTONS:
        return BUTTONS[name]
    if name in VK:
        return str(VK[name])
    if len(name) == 1 and name.isalnum():
        return str(ord(name.upper()))
    raise DriveError(f"unknown key {name!r}")


def events(script):
    """Turn 'wait S; key NAME; hold NAME S; click BUTTON [S]; down NAME; up NAME; keep NAME; look DX DY; wheel N' into
    timed lines.

    wheel N: N notches of the mouse wheel, up (away from the player) for N > 0, down for N < 0; one helper line of
    +-120 (WHEEL_DELTA) per notch, WHEEL_GAP_S apart so each lands in its own game frame (round 30: the game's scope
    range on PC is hold R, the weapon functions menu, and the wheel up; down is the flashlight).

    keep NAME: what this script pressed with `down NAME` is left down when the helper ends (the helper otherwise lets
    go of everything it still holds). A later run's `up NAME` lets go of it; Input.close() does for what is left. Round
    26: full aim is held across runs while the log is read (tools/harness/aim.py)."""
    at, lines = 0.0, []
    def add(text):
        lines.append(f"{int(at * 1000)} {text}")
    for step in filter(None, (part.strip() for part in script.split(";"))):
        word, *rest = step.split()
        if word == "wait":
            at += float(rest[0])
        elif word in ("key", "hold", "click"):
            add(f"d {_code(rest[0])}")
            at += float(rest[1]) if len(rest) > 1 else 0.08
            add(f"u {_code(rest[0])}")
            at += 0.03
        elif word == "down":
            add(f"d {_code(rest[0])}")
            at += 0.03
        elif word == "up":
            add(f"u {_code(rest[0])}")
            at += 0.03
        elif word == "keep":
            add(f"k {_code(rest[0])}")
        elif word == "point":   # point X Y: the pointer to a pixel of the game's client area (menus)
            add(f"p {int(rest[0])} {int(rest[1])}")
            at += 0.1
        elif word == "press":   # press X Y: a click message at a client pixel, for the NEST panel (stays inside the game)
            add(f"c {int(rest[0])} {int(rest[1])}")
            at += 0.25
        elif word == "wheel":
            notches = int(rest[0])
            if not notches or abs(notches) > WHEEL_MOST:
                raise DriveError(f"wheel takes 1 to {WHEEL_MOST} notches, up or down: {step!r}")
            for _ in range(abs(notches)):
                add(f"w {WHEEL_DELTA if notches > 0 else -WHEEL_DELTA}")
                at += WHEEL_GAP_S
        elif word == "look":
            for _ in range(10):
                add(f"m {int(rest[0]) // 10} {int(rest[1]) // 10}")
                at += 0.02
        else:
            raise DriveError(f"unknown step {step!r}")
    return lines, at


def _wine_session():
    """The running game's Wine server and environment, so the helper joins that prefix and never starts another."""
    game = server = None
    for proc in Path("/proc").glob("[0-9]*"):
        try:
            if proc.stat().st_uid != os.getuid():
                continue
            name = (proc / "comm").read_text().strip().lower()
            env = dict(os.fsdecode(item).split("=", 1) for item in (proc / "environ").read_bytes().split(b"\0") if b"=" in item)
            if Path(env.get("WINEPREFIX", "/")).resolve() != PREFIX.resolve():
                continue
            if name == "wineserver":
                server = proc
            elif os.fsdecode((proc / "cmdline").read_bytes().split(b"\0", 1)[0]).casefold() == GAME_EXE.casefold():
                game = (proc, env)
        except (OSError, ValueError):
            continue
    if not game or not server:
        raise DriveError("no running HellForge game with its Wine server was found")
    return game[1], (server / "exe").resolve(strict=True)


def _helper():
    import hashlib
    source = Path(__file__).resolve().parent / "win_input.c"
    cache = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "tfp-harness-input"
    cache.mkdir(mode=0o700, parents=True, exist_ok=True)
    executable = cache / (hashlib.sha256(source.read_bytes()).hexdigest() + ".exe")
    if not executable.is_file():
        zig = PREFIX / "drive_c/users/steamuser/Documents/HellForge/tools/zig-linux/zig"
        staging = executable.with_suffix(".tmp.exe")
        subprocess.run([zig, "cc", "-target", "x86_64-windows-gnu", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                        source, "-Wl,--subsystem,windows", "-Wl,-e,mainCRTStartup", "-luser32", "-o", staging], check=True)
        staging.replace(executable)
    return executable


def held_after(script, held=()):
    """The names still down after `script` ran, given those down before: a `down` with a `keep` stays down, an `up`
    lets go; everything else the helper lets go of when it ends."""
    held, downs, kept = set(held), set(), set()
    for step in filter(None, (part.strip() for part in script.split(";"))):
        word, *rest = step.split()
        if word == "down":
            downs.add(rest[0])
        elif word == "keep":
            kept.add(rest[0])
        elif word in ("up", "key", "hold", "click"):
            held.discard(rest[0]); downs.discard(rest[0])
    return held | (downs & kept)


class Input:
    """Plays input scripts as window messages posted from inside the game's Wine prefix (win_input.c)."""

    def __init__(self, check_focus=True):
        self.check_focus = check_focus
        self.executable = _helper()
        self.held = set()   # names a script left down with `keep`; close() lets go of them

    def run(self, script):
        lines, total = events(script)
        started = time.monotonic()
        if not lines:
            time.sleep(total)
            return ""
        if self.check_focus and not has_focus() and not focus():
            raise DriveError("the game does not have focus; no input was sent")
        env, server = _wine_session()
        for name in ("WINESERVERSOCKET", "WINELOADERNOEXEC", "WINEPRELOADRESERVE", "LD_PRELOAD", "WINELOADER"):
            env.pop(name, None)
        env.update(WINESERVER="/usr/bin/false", WINEPREFIX=str(PREFIX), PATH="/usr/bin:/bin", WINEDEBUG="-all")
        with tempfile.TemporaryDirectory(prefix="tfp-harness-input-") as scratch:
            (Path(scratch) / "script.txt").write_text("\n".join(lines) + "\n")
            # The server executable is masked, so this client can only join the game's server, never start one.
            command = ["bwrap", "--ro-bind", "/", "/", "--dev", "/dev", "--proc", "/proc", "--ro-bind", "/usr/bin/false",
                       str(server), "--bind", scratch, scratch, "--chdir", scratch, "--", str(server.with_name("wine")),
                       "Z:" + str(self.executable)]
            result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=120)
            report = Path(scratch) / "result.txt"
            text = report.read_text() if report.is_file() else ""
        time.sleep(max(0.0, total - (time.monotonic() - started)))  # a trailing wait is part of the script
        if result.returncode in (0, 4):   # 4: played, some events refused; what was kept may be down
            self.held = held_after(script, self.held)
        if result.returncode:
            raise DriveError(f"input helper failed ({result.returncode}): {text or result.stderr[-300:]}")
        return text

    def hold(self, name, seconds=0.08):
        return self.run(f"hold {name} {seconds}")

    def close(self):
        """Lets go of what a `keep` left down (the aim button of an aim that was interrupted)."""
        if self.held:
            names = sorted(self.held)
            try:
                self.run("; ".join(f"up {name}" for name in names))
            except DriveError as error:
                print(f"drive: could not let go of {', '.join(names)}: {error}; click them in the game", file=sys.stderr)
            self.held = set()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def main(argv):
    if len(argv) >= 2 and argv[1] == "focus":
        print(json.dumps({"focus": focus()}))
    elif len(argv) == 3 and argv[1] == "shot":
        print(json.dumps({"shot": str(shot(argv[2]))}))
    elif len(argv) == 3 and argv[1] == "run":
        with Input() as device:
            device.run(argv[2])
        print(json.dumps({"ran": argv[2]}))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
