from pathlib import Path
import hashlib,json,shutil,subprocess,stat,os,re
r=Path('/home/staszek/MeshRoute');q=Path('/tmp/mr-s8ac-r4-b1edsao2');a=r/'docs/superpowers/evidence/2026-09-18-radmin-slice8ac-r4-implementation'
def copy(src,dst):
 dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
for name in ['state.json','board-source-parity.json','fence-audit.json','base-corpus-results.json','corpus-byte-comparison.json','reference-corruption-control.json','inputs.json','preparation-inputs.json','gate-inputs.json','preparation.patch','allocation-attribution.json','allocation-attribution-checkpoint.json','final-gate-summary.json','preservation-progress.json','sim-controller.cmake','sim-results.json','boards-results.json','boards-results-first.json','chain-results.json','final-inputs.json','final-chain-results.json','final-sim-results.json','final-boards-results.json']:
 if (q/name).exists():copy(q/name,a/'audit'/name)
for src in (q/'logs').glob('*'):
 if src.is_file():copy(src,a/'logs'/src.name)
for src in (q/'final-logs').glob('*'):
 if src.is_file():copy(src,a/'final-logs'/src.name)
for src in (q/'final-union').glob('*'):
 if src.is_file():copy(src,a/'union'/src.name)
for src in Path('/tmp').glob('mr-s8ac-*.log'):copy(src,a/'development-logs'/src.name)
for name in ['mr-s8ac-r4-prepare.py','mr-s8ac-gate-snapshot.py','mr-s8ac-run.py','mr-s8ac-union.py','mr-s8ac-union-parallel.py','mr-s8ac-archive.py','mr-s8ac-final-prepare.py','mr-s8ac-final-chain.py','mr-s8ac-final-run.py','mr-s8ac-final-union-audit.py','mr-s8ac-final-union.py','mr-s8ac-final-tools.py','mr-s8ac-final-reconcile.py','mr-s8ac-freeze-manifest.py','mr-s8ac-base-corpus.py','mr-s8ac-final-receipt.py']:
 copy(Path('/tmp')/name,a/'scripts'/name)
for label,tree in [('base','snapshot'),('final','board-gate')]:
 for env in ['gateway','heltec_mobile']:
  src=q/tree/'.pio-measure'/('s8ac-base' if label=='base' else 's8ac-final2')/env
  for name in ['manifest.json','sections.txt','symbols.txt','compiler-state.json','build.log']:copy(src/name,a/'boards'/label/env/name)
for name in ['manifest.json']:
 copy(q/'corpus-final2'/name,a/'corpus'/name)
paths=sorted(set(p.decode() for p in subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=r).split(b'\0') if p))
inputs={};different=[]
for rel in paths:
 p=r/rel
 if p.is_file() and (rel.startswith(('lib/','src/','test/','tools/')) or rel=='platformio.ini'):
  h=hashlib.sha256(p.read_bytes()).hexdigest();inputs[rel]=h
  if not (q/'final-gate'/rel).is_file() or hashlib.sha256((q/'final-gate'/rel).read_bytes()).hexdigest()!=h:different.append(rel)
(a/'freeze-code-inputs.json').write_text(json.dumps(inputs,indent=2)+'\n')
(a/'freeze-code-parity.json').write_text(json.dumps({'shared_vs_gate_differences':different},indent=2)+'\n')
assert not different,different
qa=json.loads((q/'preparation-inputs.json').read_text());assert all(hashlib.sha256((r/p).read_bytes()).hexdigest()==v['sha256'] for p,v in qa.items())
sim=Path('/home/staszek/lora-universal-simulator');assert not subprocess.check_output(['git','status','--porcelain=v1'],cwd=sim)
print('ARCHIVED; code inputs',len(inputs),'all match tested gate; QA inputs unchanged; simulator clean')

for label,directory in [('default','mr-action-probe-aakejc1r'),('no-neg','mr-action-probe-sm00nem5')]:
 src=Path('/tmp')/directory
 for p in src.rglob('*'):
  if p.is_file() and (p.suffix in ['.json','.log'] or p.name=='build.sh'):copy(p,a/'deferred-actions'/label/p.relative_to(src))

copy(q/'corpus-base/manifest.json',a/'corpus/base-manifest.json')
