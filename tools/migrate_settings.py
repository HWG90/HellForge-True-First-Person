"""Export Gadget Kit or standalone First Person settings to a NEW HellForge INI.

Developer/export tool only; the C mod has no Python or Lua mod dependency.
Input files are never changed. The output must not already exist.
"""
from pathlib import Path
import argparse,json,math,re
ACTIONS=('idle','aim','ads','sprint','reload','crouch','prone','slide','dive')
KEY=re.compile(r'[0-9a-fA-F]{16}\Z')
DEFAULT=(.15,0,-.12,0,0,0)
def pose(value):
 if not isinstance(value,dict):return None
 result=tuple(value.get(k,0 if k in ('yaw','pitch','roll') else None) for k in ('x','y','z','yaw','pitch','roll'))
 if any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) or abs(v)>(.5 if i<3 else 90) for i,v in enumerate(result)):return None
 return result
def resource(key):return isinstance(key,str) and bool(KEY.fullmatch(key)) and int(key,16)!=0
def export(config,mcm,output):
 if output.exists():raise FileExistsError('Choose a new staging output; existing files are never replaced')
 raw=json.loads(config.read_text());fp=raw.get('first_person',raw)
 if not isinstance(fp,dict):raise ValueError('First Person config must be an object')
 values={}
 if mcm:
  for line in mcm.read_text().splitlines():
   key,sep,value=line.partition('=')
   if not sep:continue
   key=key.strip();value=value.strip()
   if key.startswith('first_person_'):key=key[13:]
   if value in ('true','false'):values[key]=value=='true'
   elif re.fullmatch(r'-?\d+(?:\.\d+)?',value):values[key]=float(value)
 settings={'toggle_key':122,'editor_key':119,'native_ads':0,'show_markers':1,'marker_legacy':int(not isinstance(fp.get('show_aim_markers'),bool)),
 'near_cm':15,'near_aim':0,'motion_enabled':int(fp.get('motion_enabled',True) is not False),'motion_strength':fp.get('motion_scale',1),
 'weapon_bob_strength':1,'weapon_bob_speed':1,'weapon_sway_strength':1,'weapon_sway_speed':1,'sprint_strength':1,
 'camera_bob_strength':.25,'camera_bob_speed':1,'camera_sway_strength':.25,'camera_sway_speed':1,'camera_kick':int(fp.get('camera_kick_enabled',True) is not False)}
 if isinstance(fp.get('show_aim_markers'),bool):settings['show_markers']=int(fp['show_aim_markers'])
 direct={'toggle_key':'toggle_key','editor_key':'editor_key','true_native_ads':'native_ads','near_clip_cm':'near_cm','near_clip_aim':'near_aim','motion_enabled':'motion_enabled'}
 for source,target in direct.items():
  if source in values:settings[target]=int(values[source])
 percents={'motion_strength':'motion_strength','weapon_bob_strength':'weapon_bob_strength','weapon_bob_speed':'weapon_bob_speed','weapon_sway_strength':'weapon_sway_strength','weapon_sway_speed':'weapon_sway_speed','sprint_motion_strength':'sprint_strength','camera_bob_strength':'camera_bob_strength','camera_bob_speed':'camera_bob_speed','camera_sway_strength':'camera_sway_strength','camera_sway_speed':'camera_sway_speed'}
 for source,target in percents.items():
  if source in values:settings[target]=float(values[source])/100
 if any(isinstance(v,bool) or not isinstance(v,(int,float)) or not math.isfinite(v) for v in settings.values()):raise ValueError('Invalid numeric First Person setting')
 lines=['; Exported First Person settings; positions metres, angles degrees.','[settings]']+[f'{k}={v:.12g}' for k,v in settings.items()]
 lines+=['','[legacy_markers]']
 markers=fp.get('aim_markers',{})
 marker_keys=sorted({k.lower() for k,v in markers.items() if resource(k) and v is True}) if isinstance(markers,dict) else []
 if len(marker_keys)>256:raise ValueError('Native marker limit is 256 weapon models')
 lines += [f'{k}=1' for k in marker_keys]
 lines+=['','[action_map]'];mapping=fp.get('action_state_mapping',{})
 mapping=mapping.get('4D1C334D294DFA97',{}) if isinstance(mapping,dict) else {}
 mappings=[f'{k}={v}' for k,v in sorted(mapping.items()) if re.fullmatch(r'-?\d+:-?\d+',k) and all(-1<=int(n)<=2047 for n in k.split(':')) and v in ACTIONS] if isinstance(mapping,dict) else []
 if len(mappings)>128:raise ValueError('Native action mapping limit is 128')
 lines += mappings
 legacy=fp.get('weapon_placements',{});legacy=legacy if isinstance(legacy,dict) else {}
 profiles=fp.get('action_placements',{});profiles=profiles if isinstance(profiles,dict) else {}
 legacy={k.lower() if resource(k) else k:v for k,v in legacy.items()}
 profiles={k.lower() if resource(k) else k:v for k,v in profiles.items()}
 all_keys={'__all'}|{k.lower() for k in legacy.keys()|profiles.keys() if resource(k)}
 if len(all_keys)>257:raise ValueError('Native profile limit is 256 weapon models')
 for key in sorted(all_keys,key=lambda k:(k!='__all',k)):
  old=pose(legacy.get(key))
  if key=='__all' and old is None:old=pose(fp.get('placement')) or DEFAULT
  actions=profiles.get(key,{});actions=actions if isinstance(actions,dict) else {}
  rows={a:pose(actions.get(a)) for a in ACTIONS};rows={a:v for a,v in rows.items() if v is not None}
  if old is None and not rows:continue
  lines+=['',f'[placement:{key}]']
  if old is not None:lines.append('legacy='+','.join(f'{n:.12g}' for n in old))
  lines += [a+'='+','.join(f'{n:.12g}' for n in v) for a,v in rows.items()]
 output.parent.mkdir(parents=True,exist_ok=True);output.write_text('\n'.join(lines)+'\n')
 return output
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--config',type=Path,required=True);ap.add_argument('--mcm',type=Path);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();print(export(a.config,a.mcm,a.output))
