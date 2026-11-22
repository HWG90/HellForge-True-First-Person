#!/usr/bin/env python3
"""Walk the player to a world position by closed loop: read the body's position with Lua in the game (the Bingus
harness's `lua` command), turn, walk, read again.

  walk.py where                     the body's position, and the rare units within 60 m (hashed names, distances)
  walk.py to X Y [STOP_M]           walk until within STOP_M (default 2.5) of (X, Y), 40 steps at most

Needs a harness session in a mission. Unit reads are guarded (alive, scene-graph nodes > 0): an unguarded
Unit.world_position on every unit of the world crashes the game (seen 2026-11-22). Unit.teleport_local_position does
not move the player (the game's own character code owns the position), hence walking.
"""
import json, math, os, subprocess, sys
import drive

BINGUS = os.path.expanduser(os.environ.get("HD2H_DIR", "~/hellforge-research/tools/BingusDevelopmentHarness"))
HEAD = """local s=stingray local U=s.Unit local us=s.World.units(s.Application.main_world()) local body local most=0
for i=1,#us do local u=us[i] if U.alive(u) then local n=U.num_scene_graph_items(u)
  if n>most and n>=100 and n<=110 then body=u most=n end end end
if not body then return 'no body' end local bp=U.world_position(body,1)
"""
# The player's body is taken as the unit with 100 to 110 scene-graph nodes (107 on the armour tested); solo only.


def lua(source):
    run = subprocess.run(["python3", "harness.py", "lua", source], cwd=BINGUS, env=dict(os.environ, HD2H_TARGET="hellforge"),
                         capture_output=True, text=True)
    at = run.stdout.find("{")
    if at < 0:
        raise RuntimeError((run.stdout + run.stderr)[-300:])
    answer = json.loads(run.stdout[at:])
    if answer.get("error"):
        raise RuntimeError(str(answer["error"])[:300])
    return answer.get("values", [])


def where():
    x, y, z = (float(v) for v in lua(HEAD + "return string.format('%.3f %.3f %.3f',bp.x,bp.y,bp.z)")[0].split())
    return x, y, z


def rare(radius=60):
    source = HEAD + f"""local types={{}}
for i=1,#us do local u=us[i] if U.alive(u) and U.num_scene_graph_items(u)>0 then local p=U.world_position(u,1)
  local d=math.sqrt((p.x-bp.x)^2+(p.y-bp.y)^2) local n=tostring(U.debug_name(u)) local t=types[n]
  if not t then t={{c=0,d=1e9}} types[n]=t end t.c=t.c+1 if d<t.d then t.d=d t.x=p.x t.y=p.y t.z=p.z t.n=U.num_scene_graph_items(u) end end end
local out={{}} for n,t in pairs(types) do if t.c<=3 and t.d>4 and t.d<{radius} then out[#out+1]=string.format('%05.1f %s x%d nodes=%d (%.1f %.1f %.1f)',t.d,n,t.c,t.n,t.x,t.y,t.z) end end
table.sort(out) return table.concat(out,'\\n',1,math.min(#out,40))"""
    return lua(source)[0]


def to(x, y, stop=2.5, steps=40):
    d = drive.Input()
    per_pixel = None          # degrees of heading per pixel of look, measured on the way
    last = where()
    d.run("hold w 0.6; wait 0.2")
    for step in range(steps):
        now = where()
        dist = math.hypot(x - now[0], y - now[1])
        if dist <= stop:
            print(f"arrived: {dist:.1f} m from the target after {step} steps")
            return True
        moved = math.hypot(now[0] - last[0], now[1] - last[1])
        if moved < 0.15:      # blocked: sidestep and try again
            d.run("hold a 0.5; hold w 0.5; wait 0.2")
            last = now
            d.run("hold w 0.4; wait 0.2")
            continue
        heading = math.atan2(now[1] - last[1], now[0] - last[0])
        want = math.atan2(y - now[1], x - now[0])
        turn = math.degrees((want - heading + math.pi) % (2 * math.pi) - math.pi)   # positive: to the left
        pixels = 0
        if abs(turn) > 4:
            pixels = int(-turn * (per_pixel or 6.0))
            pixels = max(-1500, min(1500, pixels))
            d.run(f"look {pixels} 0; wait 0.15")
        walk = min(2.0, max(0.4, dist / 5.0))
        before = now
        d.run(f"hold w {walk:.2f}; wait 0.2")
        after = where()
        if pixels and math.hypot(after[0] - before[0], after[1] - before[1]) > 0.3:
            new = math.atan2(after[1] - before[1], after[0] - before[0])
            got = math.degrees((new - heading + math.pi) % (2 * math.pi) - math.pi)
            if abs(got) > 3 and got * -pixels > 0:
                per_pixel = abs(pixels / got)
        print(f"step {step}: {dist:.1f} m to go, turned {turn:.0f} deg ({pixels} px, {per_pixel or 0:.1f} px/deg)")
        last = before
        # the loop's `now` for the next step is read at its top; `last` is where this walk began
    print("not arrived")
    return False


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "where":
        print("body %.2f %.2f %.2f" % where())
        print(rare())
    elif len(sys.argv) >= 4 and sys.argv[1] == "to":
        sys.exit(0 if to(float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]) if len(sys.argv) > 4 else 2.5) else 1)
    else:
        sys.exit(__doc__)
