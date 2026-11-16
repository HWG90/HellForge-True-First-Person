"""Run asserted C fixtures and DLL load checks in an explicit Wine TEST prefix."""
from pathlib import Path
import argparse,json,os,subprocess
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--prefix',type=Path,required=True)
ap.add_argument('--wine',default='wine')
ap.add_argument('--lua-dll',type=Path)
args=ap.parse_args()
root=Path(__file__).resolve().parents[1]
env=dict(os.environ,WINEDEBUG='-all',WINEDLLOVERRIDES='winedbg.exe=d',WINEPREFIX=str(args.prefix.resolve()))
tests=['camera_policy','pose_policy','fov_policy','near_policy','visibility_policy','blur_policy','models','effects','settings','runtime','native_observer','research_recording','dll_smoke']
cases=[(name,[]) for name in tests]
if args.lua_dll:
 cases += [('engine_capture',[]),('engine_result',[]),('engine_vm_close',[str(args.lua_dll.resolve())]),('engine_sdk_census',[str(args.lua_dll.resolve())])]
 cases += [('engine_admission',[str(args.lua_dll.resolve()),str(mode)]) for mode in range(3)]
results=[]
for name,extra in cases:
 filename=name+('_test.exe' if name!='dll_smoke' else '.exe')
 command=[args.wine,str(root/'work/tests'/filename)]+extra
 if name=='dll_smoke':command.append(str(root/'dist/true-first-person/goose_true_first_person.dll'))
 try:
  run=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
  (root/'work'/f'{name}{extra[-1] if name=="engine_admission" else ""}.log').write_bytes(run.stdout)
  lines=[s for s in run.stdout.decode(errors='replace').splitlines() if s.startswith('C ') or 'Assertion failed' in s]
  print(name,run.returncode,' | '.join(lines)[-700:],flush=True)
  results.append({'test':name,'exit':run.returncode})
 except subprocess.TimeoutExpired as e:
  (root/'work'/f'{name}{extra[-1] if name=="engine_admission" else ""}.log').write_bytes(e.stdout or b'')
  print(name,'timeout',flush=True);results.append({'test':name,'exit':'timeout'})
  break
report={'assertions_enabled':True,'real_game_writes':False,'runtime_language':'C11','tests':results}
(root/'docs/native-policy-tests.json').write_text(json.dumps(report,indent=2)+'\n')
raise SystemExit(0 if len(results)==len(cases) and all(r['exit']==0 for r in results) else 1)
