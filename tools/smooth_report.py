#!/usr/bin/env python3
"""Turn a First Person Controller session log into two tables: the camera
request states the game went through, and the view jumps seen in each."""
import argparse
import json
import re
import sys
from pathlib import Path

REQUEST = re.compile(r"Request: t=(\d+) (queue=\[[^\]]*\] .*?seat=\d)(?: held=(\d+))?\s*$")
SMOOTH = re.compile(r"Smooth: t=(\d+) ch=([ETFNSG]+) eye=([\d.]+) turn=([\d.]+) fov=([\d.]+) "
                    r"near=([\d.]+) socket=([\d.]+) owned=([01]) rig=([01]) dropped=(\d+)\s*$")
CHANNELS = "ETFNS"  # the value columns of a Smooth line, in order
BEFORE = "(before the first Request line)"


def read(path):
    states, jumps, order = {}, {}, []
    current, since, last, dropped, held = None, 0, 0, 0, 0

    def leave(until):
        if current is None:
            return
        row = states[current]
        row["total_ms"] += until - since
        row["longest_ms"] = max(row["longest_ms"], until - since)

    with open(path, encoding="utf-8", errors="replace") as lines:
        for line in lines:
            request = REQUEST.search(line)
            if request:
                at = int(request[1])
                leave(at)
                current, since, last = request[2], at, max(last, at)
                held += int(request[3] or 0)
                if current not in states:
                    states[current] = {"state": current, "entries": 0, "total_ms": 0, "longest_ms": 0}
                    order.append(current)
                states[current]["entries"] += 1
                continue
            smooth = SMOOTH.search(line)
            if not smooth:
                continue
            last = max(last, int(smooth[1]))
            dropped += int(smooth[10])
            key = current or BEFORE
            row = jumps.setdefault(key, {"state": key, "frames": 0, "count": {}, "max": {}, "released": 0})
            row["frames"] += 1
            row["released"] += smooth[8] == "0"
            for channel in smooth[2]:
                value = float(smooth[3 + CHANNELS.index(channel)]) if channel in CHANNELS else 0.0
                row["count"][channel] = row["count"].get(channel, 0) + 1
                row["max"][channel] = max(row["max"].get(channel, 0.0), value)
    leave(last)
    for row in jumps.values():
        row["count"] = dict(sorted(row["count"].items()))
        row["max"] = dict(sorted(row["max"].items()))
    listed = [jumps[key] for key in [BEFORE, *order] if key in jumps]
    return {"states": [states[key] for key in order], "jumps": listed, "dropped": dropped, "held": held}


def text(data):
    out = ["Request states (entries, total s, longest s)"]
    for row in data["states"]:
        out.append(f"  {row['entries']:4d} {row['total_ms'] / 1000:8.2f} {row['longest_ms'] / 1000:8.2f}  {row['state']}")
    out += ["", "Jumps (frames, shown from the game's camera, then count and largest per channel:"
            " E eye m, T turn deg, F field of view deg, N near clip m, S weapon socket m,"
            " G frames the view could not be sampled)"]
    for row in data["jumps"]:
        parts = " ".join(f"{channel}={row['count'][channel]}x{row['max'][channel]:.3f}" for channel in row["count"])
        out.append(f"  {row['frames']:4d} {row['released']:4d}  {parts}  {row['state']}")
    out += ["", f"dropped={data['dropped']} held={data['held']}"]
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if not args.log.is_file():
        print(f"No such log: {args.log}", file=sys.stderr)
        return 2
    data = read(args.log)
    print(json.dumps(data, indent=2) if args.json else text(data))
    return 0


if __name__ == "__main__":
    sys.exit(main())
