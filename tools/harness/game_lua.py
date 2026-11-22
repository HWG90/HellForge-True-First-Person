#!/usr/bin/env python3
"""Run Lua in the game through the Bingus harness: lua(source) returns the error and values as one string.
Run as a script, the argument is appended to FIND, which leaves `body` (the player's body unit), `U`, `s`, `us`.
Guard every transform read: Unit.alive(u) and Unit.num_scene_graph_items(u) > 0 (docs/design/HARNESS.md)."""
import json, subprocess, os, sys
def lua(src):
    env=dict(os.environ,HD2H_TARGET='hellforge')
    r=subprocess.run(['python3','harness.py','lua',src],cwd=os.path.expanduser('~/hellforge-research/tools/BingusDevelopmentHarness'),env=env,capture_output=True,text=True)
    t=r.stdout; i=t.find('{')
    if i<0: return 'ERR '+(r.stdout+r.stderr)[-300:]
    j=json.loads(t[i:]); return (j.get('error') or '')+' '+' || '.join(str(v)[:1500] for v in j.get('values',[]))
FIND="""local s=stingray local U=s.Unit local w=s.Application.main_world() local us=s.World.units(w) local body
for i=1,#us do local u=us[i] if U.alive(u) and U.num_scene_graph_items(u)>0 and tostring(U.debug_name(u))=='#ID[4d1c334d294dfa97]' then body=u break end end
if not body then return 'no body' end
"""
if __name__=='__main__': print(lua(FIND+sys.argv[1]))
