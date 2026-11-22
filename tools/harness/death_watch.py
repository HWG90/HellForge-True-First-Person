#!/usr/bin/env python3
"""Run matrix passes until the character dies, and capture the death.

  death_watch.py OUT_DIR [passes]

A watcher reads the mod's log twice a second; when a "Body:" or "Dead:" line appears (the lock holds a body that
is off foot, thrown or dead) it takes 14 captures. Deaths in the harness are chance (enemies, a grenade that
lands close); two of the first four full matrix runs had one.
"""
import subprocess
import sys
import threading
import time
from pathlib import Path

import drive
import matrix

PHASES = ("idle fire aim-fire sprint dive stand-up swap-secondary pistol-fire swap-primary quick-swaps grenade grenade-held "
          "stim melee jump-vault fire sprint dive-back stand-up").split()


def main(argv):
    out = Path(argv[1]); out.mkdir(parents=True, exist_ok=True)
    passes = int(argv[2]) if len(argv) > 2 else 2
    log = matrix.log_path(); start = log.stat().st_size
    seen = threading.Event(); stop = threading.Event()

    def watch():
        while not stop.is_set():
            with open(log, errors="replace") as handle:
                handle.seek(start); text = handle.read()
            if " Dead: " in text or " Body: " in text:
                seen.set()
                for i in range(14):
                    try:
                        drive.shot(out / f"death-{i:02d}.png")
                    except drive.DriveError:
                        pass
                    time.sleep(0.6)
                return
            time.sleep(0.5)

    watcher = threading.Thread(target=watch); watcher.start()
    for n in range(passes):
        if seen.is_set():
            break
        subprocess.run([sys.executable, "matrix.py", str(out / f"pass{n}")] + PHASES, capture_output=True, text=True)
    stop.set(); watcher.join()
    with open(log, errors="replace") as handle:
        handle.seek(start); lines = handle.read().splitlines()
    for line in lines:
        if " Dead: " in line or " Body: " in line or " Missed: " in line:
            print(line[:12], line[13:200])
    print("death seen" if seen.is_set() else "no death", matrix.judge(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
