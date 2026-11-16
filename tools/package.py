"""Package the reviewed C candidate; never installs or changes game files."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo
import argparse, hashlib, json, re

root=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def archive(path,files,prefix):
 with ZipFile(path,'w',compression=ZIP_DEFLATED,compresslevel=9) as z:
  for name,p in sorted(files.items()):
   entry=ZipInfo(prefix+'/'+name,(2026,11,9,0,0,0))
   entry.compress_type=ZIP_DEFLATED;entry.external_attr=0o100644<<16
   z.writestr(entry,p.read_bytes(),compress_type=ZIP_DEFLATED,compresslevel=9)
 with ZipFile(path) as z:
  assert z.testzip() is None
  assert set(z.namelist())=={prefix+'/'+n for n in files}
  for n,p in files.items():assert z.read(prefix+'/'+n)==p.read_bytes()
def package(output):
 manifest=json.loads((root/'mod/hellforge-mod.json').read_text())
 validation=json.loads((root/'docs/VALIDATION.json').read_text())
 match=re.fullmatch(r'([0-9.]+)-hellforge.dev.([0-9]+)',manifest['version']);assert match
 assert validation['version']==manifest['version']
 dll=root/'dist/true-first-person/goose_true_first_person.dll'
 assert validation['files'][dll.name]==sha(dll),'Revalidate the DLL before packaging'
 assert validation['offline_checks_passed'] and not validation['gameplay_accepted']
 output.mkdir(parents=True,exist_ok=True)
 stem=f'True-First-Person-{match[1]}-HellForge-C-dev.{match[2]}-25480438'
 runtime={dll.name:dll,'hellforge-mod.json':root/'mod/hellforge-mod.json'}
 for n in ['README.md','LICENSE','NOTICE.md','docs/VALIDATION.json','docs/FEATURE-COVERAGE.md','docs/LIVE-TEST.md','docs/research/NATIVE-FIRST-PLAN.md','docs/research/HF-SDK-AUDIT.md','docs/research/ENGINE-SDK-INVENTORY.md','docs/research/NATIVE-PROJECTION-DEEP-DIVE.md','docs/research/NATIVE-INPUT-DEEP-DIVE.md','docs/research/NATIVE-RIG-LIFECYCLE-DEEP-DIVE.md','docs/research/C-CALLER-CONTEXT-FEASIBILITY.md','docs/research/NATIVE-VISIBILITY-ABI.md','docs/research/native-visibility-evidence.json']:runtime[n]=root/n
 source={n:root/n for n in ['README.md','LICENSE','NOTICE.md','build.sh','build-probe.sh','mod/hellforge-mod.json','mod/probe-manifest.json']}
 for folder,suffixes in [('src',{'.c','.h'}),('tests',{'.c','.h','.py','.sh'}),('tools',{'.py'}),('docs',{'.md','.json'})]:
  for p in (root/folder).rglob('*'):
   if p.is_file() and p.suffix in suffixes and '__pycache__' not in p.parts and p.name!='probe-installation.json':source[str(p.relative_to(root))]=p
 assert all(p.suffix not in {'.cpp','.lua','.dll','.exe','.pdb','.lib','.o','.obj'} for p in source.values())
 archives={output/(stem+'.zip'):runtime,output/(stem+'-source.zip'):source}
 for p,files in archives.items():archive(p,files,'true-first-person' if files is runtime else 'true-first-person-hellforge')
 receipt={'version':manifest['version'],'target_build':25480438,'language':'C11','candidate':True,'installed':False,'gameplay_accepted':False,'archives':{p.name:{'sha256':sha(p),'bytes':p.stat().st_size,'files':len(files)} for p,files in archives.items()},'runtime_files':{n:sha(p) for n,p in runtime.items()},'source_files':{n:sha(p) for n,p in source.items()}}
 (output/f'True-First-Person-HellForge-C-package-dev.{match[2]}.json').write_text(json.dumps(receipt,indent=2)+'\n')
 for p in archives:print(p)
 print(f'Verified {len(runtime)} runtime files and {len(source)} source files; no installation performed')
if __name__=='__main__':
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();package(a.output.resolve())
