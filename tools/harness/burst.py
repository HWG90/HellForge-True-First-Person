#!/usr/bin/env python3
"""Film actions as strips of captures, for the things numbers do not show (a helmet in the view, arms in the wrong place).

  burst.py OUT_DIR [action ...]      actions default to all; see ACTIONS

Needs a harness session in a mission with player control; takes the keyboard and mouse. For each action the input
plays in one thread while the window is captured as fast as Spectacle allows (about 3 a second); the frames go to
OUT_DIR/<action>-NN.png and one labelled sheet OUT_DIR/<action>.jpg. The view is switched on first if it is off.
"""
import sys
import threading
import time
from pathlib import Path

import drive
import matrix

# name -> (input script, frames)
ACTIONS = {
    "swap": ("key 2; wait 2.6", 8),
    "swap-back": ("key 1; wait 2.6", 8),
    "grenade": ("key g; wait 3.2", 8),
    "stim": ("key v; wait 3.2", 8),
    "reload": ("click left 0.3; wait 0.2; key r; wait 3.2", 8),
    "ammo-check": ("hold r 0.8; wait 2.4", 8),
    "melee": ("key f; wait 2.0", 6),
    "emote": ("key b; wait 3.5", 8),
    "dive": ("down w; wait 0.3; key alt; wait 0.6; up w; wait 1.6; key c; wait 1.5", 10),
    "prone": ("key z; wait 1.6; key z; wait 1.8", 8),
    "vault": ("hold w 0.4; key space; wait 0.2; hold w 0.6; wait 1.6", 8),
    "aim": ("click right 1.6; wait 0.8", 6),
    "sprint": ("down shift; hold w 2.0; up shift; wait 0.8", 8),
    "look-down": ("look 0 900; wait 1.0; look 0 -900; wait 0.6", 6),
    "look-up": ("look 0 -900; wait 1.0; look 0 900; wait 0.6", 6),
    "crouch-aim": ("key c; wait 1.0; down right; wait 1.4; up right; wait 0.8; key c; wait 0.8", 9),
    "prone-aim": ("key z; wait 1.6; down right; wait 1.4; up right; wait 0.8; key z; wait 1.6", 10),
    "aim-fire": ("down right; wait 0.7; click left 0.6; wait 0.4; up right; wait 1.0", 7),
    "sprint-aim": ("down shift; hold w 1.2; up shift; down right; wait 1.4; up right; wait 1.0", 9),
    "aim-reload": ("click left 0.3; down right; wait 0.5; key r; wait 2.8; up right; wait 1.0", 10),
    "hip-fire": ("click left 0.9; wait 0.8", 5),
}


def sheet(out, name, files):
    from PIL import Image, ImageDraw
    cols = 4
    rows = (len(files) + cols - 1) // cols
    page = Image.new("RGB", (640 * cols, 360 * rows))
    for i, path in enumerate(files):
        image = Image.open(path).convert("RGB").resize((640, 360))
        ImageDraw.Draw(image).text((12, 24), f"{name} {i}", fill=(255, 255, 0))
        page.paste(image, ((i % cols) * 640, (i // cols) * 360))
    page.save(out / f"{name}.jpg", quality=80)


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 2
    out = Path(argv[1]); out.mkdir(parents=True, exist_ok=True)
    names = argv[2:] or list(ACTIONS)
    if not drive.focus():
        print("the game window could not be focused"); return 1
    log = matrix.log_path()
    with drive.Input() as device:
        if "frames held" not in log.read_text(errors="replace")[-4000:]:
            device.run("key f11; wait 2.5")
        for name in names:
            script, count = ACTIONS[name]
            player = threading.Thread(target=device.run, args=(script,)); player.start()
            files = []
            for i in range(count):
                path = out / f"{name}-{i:02d}.png"
                try:
                    drive.shot(path); files.append(path)
                except drive.DriveError as error:
                    print(f"  (no capture: {error})")
            player.join(); time.sleep(1.2)
            sheet(out, name, files)
            print(f"{name}: {len(files)} frames -> {out / (name + '.jpg')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
