"""Settings export is offline and never replaces an installed configuration."""
from pathlib import Path
import importlib.util, json, tempfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('tfp_migration',root/'tools/migrate_settings.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory() as td:
 p=Path(td);src=p/'old.json';mcm=p/'mcm.cfg';out=p/'staging.ini'
 key='0123456789ABCDEF'
 position={'x':.1,'y':.2,'z':-.1,'yaw':3,'pitch':4,'roll':5}
 raw={'first_person':{'placement':position,'weapon_placements':{key:position},
      'action_placements':{key:{'idle':dict(position,x=.2),'ads':dict(position,z=0)}},
      'aim_markers':{key:True},'action_state_mapping':{'4D1C334D294DFA97':{'78:3':'sprint'}}}}
 src.write_text(json.dumps(raw));mcm.write_text('first_person_true_native_ads=true\nfirst_person_near_clip_cm=20\nfirst_person_camera_bob_strength=40\nfirst_person_toggle_key=122\n')
 before=(src.read_bytes(),mcm.read_bytes());m.export(src,mcm,out);text=out.read_text()
 assert 'native_ads=1\n' in text and 'near_cm=20\n' in text and 'camera_bob_strength=0.4\n' in text
 assert 'marker_legacy=1\n' in text and key.lower()+'=1\n' in text and '78:3=sprint\n' in text
 assert '[placement:__all]\nlegacy=0.1,0.2,-0.1,3,4,5\n' in text
 assert f'[placement:{key.lower()}]\nlegacy=' in text and 'idle=0.2,0.2,-0.1,3,4,5\n' in text
 assert 'ads=0.1,0.2,0,3,4,5\n' in text and '\naim=' not in text
 assert before==(src.read_bytes(),mcm.read_bytes())
 try:m.export(src,mcm,out)
 except FileExistsError:pass
 else:raise AssertionError('Existing destination overwritten')
 assert out.read_text()==text
 raw['first_person']['show_aim_markers']=False;src.write_text(json.dumps(raw))
 m.export(src,None,p/'master.ini');assert 'marker_legacy=0\n' in (p/'master.ini').read_text() and 'show_markers=0\n' in (p/'master.ini').read_text()
 raw['first_person']['weapon_placements']={f'{n:016x}':position for n in range(1,258)}
 src.write_text(json.dumps(raw))
 try:m.export(src,None,p/'oversize.ini')
 except ValueError:pass
 else:raise AssertionError('Oversized profile set accepted')
 assert not (p/'oversize.ini').exists()
print('Settings migration: legacy fallback, action overrides, stable keys, markers, mappings, MCM values, bounds and non-destructive export passed')
