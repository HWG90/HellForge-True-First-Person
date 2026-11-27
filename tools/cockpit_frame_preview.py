#!/usr/bin/env python3
"""Round 45: paint the cockpit frame offline, from the draw calls src/cockpit_frame.h makes (tools/cockpit_frame_preview.c),
with the theme's colours from HD2-UI-Style's tokens.json, over a background picture.

    python3 tools/cockpit_frame_preview.py --background ~/.cache/tfp-round37/user-mech.webp --out ~/.cache/tfp-round45/preview.png

Also writes <out>-zones.png: the same with the game's HUD places the frame keeps clear outlined. Only a preview: the
game's own font, its HDR output and the host's anti-aliasing are not reproduced."""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
# OvThemeColor order -> the token each role is in the house look (overlay-host src/theme.cpp kHelldivers)
ROLES = ["panel", "panel_alt", "field", "field_hover", "line", "text", "text_dim", "yellow", "select", "yellow", "danger",
         "success", "on_yellow", "select_hover", "field_active", "line_strong", "text_off", "yellow_hover", "yellow_press",
         "hud_plate", "hud_shadow"]
SUPER = 2  # painted at twice the size, then reduced: a stand-in for the host's anti-aliasing


def theme(tokens_path):
    colours = json.loads(Path(tokens_path).read_text())["colors"]
    out = []
    for name in ROLES:
        c = colours[name]
        r, g, b = (int(c["hex"][i:i + 2], 16) for i in (1, 3, 5))
        out.append("%08x" % ((c["alpha"] << 24) | (b << 16) | (g << 8) | r))  # ABGR, as the host hands them out
    return out


def rgba(abgr):
    v = int(abgr, 16)
    return (v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255)


def font(size):
    for path in ("/usr/share/fonts/TTF/DejaVuSansCondensed-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf"):
        if os.path.exists(path):
            return ImageFont.truetype(path, max(1, int(size)))
    return ImageFont.load_default()


def paint(base, calls, zones):
    canvas = base.convert("RGBA").resize((base.width * SUPER, base.height * SUPER), Image.LANCZOS)
    s = SUPER

    def layer(box, draw_fn):
        x0, y0, x1, y1 = (int(v) for v in (box[0] - 4, box[1] - 4, box[2] + 5, box[3] + 5))
        x0, y0 = max(0, x0), max(0, y0)
        x1, y1 = min(canvas.width, x1), min(canvas.height, y1)
        if x1 <= x0 or y1 <= y0:
            return
        piece = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
        draw_fn(ImageDraw.Draw(piece), x0, y0)
        canvas.alpha_composite(piece, (x0, y0))

    for parts in calls:
        kind, v = parts[0], parts[1:]
        if kind == "fill":
            x0, y0, x1, y1, r = (float(a) * s for a in v[:5]); c = rgba(v[5])
            layer((x0, y0, x1, y1), lambda d, ox, oy: d.rounded_rectangle((x0 - ox, y0 - oy, x1 - ox - 1, y1 - oy - 1), radius=r, fill=c))
        elif kind == "rect":
            x0, y0, x1, y1, r, t = (float(a) * s for a in v[:6]); c = rgba(v[6])
            layer((x0, y0, x1, y1), lambda d, ox, oy: d.rounded_rectangle((x0 - ox, y0 - oy, x1 - ox - 1, y1 - oy - 1), radius=r, outline=c, width=max(1, round(t))))
        elif kind == "line":
            x0, y0, x1, y1, t = (float(a) * s for a in v[:5]); c = rgba(v[5])
            layer((min(x0, x1) - t, min(y0, y1) - t, max(x0, x1) + t, max(y0, y1) + t),
                  lambda d, ox, oy: d.line((x0 - ox, y0 - oy, x1 - ox, y1 - oy), fill=c, width=max(1, round(t))))
        elif kind == "circle":
            x, y, r, t = (float(a) * s for a in v[:4]); c = rgba(v[4])
            layer((x - r, y - r, x + r, y + r), lambda d, ox, oy: d.ellipse((x - r - ox, y - r - oy, x + r - ox, y + r - oy), outline=c, width=max(1, round(t))))
        elif kind == "disc":
            x, y, r = (float(a) * s for a in v[:3]); c = rgba(v[3])
            layer((x - r, y - r, x + r, y + r), lambda d, ox, oy: d.ellipse((x - r - ox, y - r - oy, x + r - ox, y + r - oy), fill=c))
        elif kind == "tri":
            p = [float(a) * s for a in v[:6]]; c = rgba(v[6])
            layer((min(p[0::2]), min(p[1::2]), max(p[0::2]), max(p[1::2])),
                  lambda d, ox, oy: d.polygon([(p[0] - ox, p[1] - oy), (p[2] - ox, p[3] - oy), (p[4] - ox, p[5] - oy)], fill=c))
        elif kind == "text":
            x, y, size = (float(a) * s for a in v[:3]); c = rgba(v[3]); text = " ".join(v[4:])
            f = font(size)
            layer((x, y, x + size * len(text), y + size * 1.4), lambda d, ox, oy: d.text((x - ox, y - oy), text, fill=c, font=f))
    framed = canvas.resize((base.width, base.height), Image.LANCZOS)
    zoned = framed.copy()
    d = ImageDraw.Draw(zoned)
    for x0, y0, x1, y1, name in zones:
        d.rectangle((x0, y0, x1 - 1, y1 - 1), outline=(255, 64, 64, 255), width=3)
        d.text((x0 + 8, y0 + 6 if y0 < base.height / 2 else y1 - 40), "HUD: " + name, fill=(255, 64, 64, 255), font=font(26))
    return framed.convert("RGB"), zoned.convert("RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--background", default=os.path.expanduser("~/.cache/tfp-round37/user-mech.webp"))
    ap.add_argument("--out", default=os.path.expanduser("~/.cache/tfp-round45/preview.png"))
    ap.add_argument("--width", type=int, default=2560)
    ap.add_argument("--height", type=int, default=1440)
    ap.add_argument("--opacity", type=float, default=0.9)
    ap.add_argument("--tokens", default=os.path.expanduser("~/Projects/HD2-UI-Style/tokens.json"))
    a = ap.parse_args()
    work = ROOT / "work" / "tools"
    work.mkdir(parents=True, exist_ok=True)
    exe = work / "cockpit_frame_preview"
    subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"), str(ROOT / "tools" / "cockpit_frame_preview.c"),
                    "-o", str(exe)], check=True)
    out = subprocess.run([str(exe), str(a.width), str(a.height), str(a.opacity)] + theme(a.tokens), check=True, capture_output=True, text=True).stdout
    calls, zones, describe = [], [], ""
    for line in out.splitlines():
        parts = line.split(" ")
        if parts[0] == "zone":
            zones.append((*(float(p) for p in parts[1:5]), " ".join(parts[5:])))
        elif parts[0] == "describe":
            describe = line[len("describe "):]
        else:
            calls.append(parts)
    if os.path.exists(a.background):
        base = Image.open(a.background).convert("RGB").resize((a.width, a.height), Image.LANCZOS)
    else:
        base = Image.new("RGB", (a.width, a.height), (96, 104, 112))
    framed, zoned = paint(base, calls, zones)
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    framed.save(a.out)
    zones_out = str(Path(a.out).with_name(Path(a.out).stem + "-zones.png"))
    zoned.save(zones_out)
    print("%d draw calls; %s" % (len(calls), describe))
    print("wrote %s and %s" % (a.out, zones_out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
