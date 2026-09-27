"""Independent isolated replays. Whole tracked/untracked snapshot for stage A; no checkout edits."""
from pathlib import Path
import json,re,subprocess,shlex,shutil,hashlib
root=Path.cwd();raw=root/'artifacts/2026-09-27-standalone-mobile-home-w4a-qa';scratch=Path(json.loads((raw/'scratch.json').read_text())['path'])/'focused';scratch.mkdir(exist_ok=True)
base=root/'artifacts/2026-09-26-standalone-mobile-home-w4a/base-copies';initial=json.loads((raw/'inputs-start.json').read_text())[0];stage=scratch/'stage-a-tree';stage.mkdir(exist_ok=True)
for p,entry in initial['files'].items():
 target=stage/p;target.parent.mkdir(parents=True,exist_ok=True)
 if entry['kind']=='symlink':target.symlink_to(entry['target'])
 elif entry['kind']=='file':shutil.copy2(root/p,target)
for p in json.loads((raw/'source-audit.json').read_text())['verified_base_copies']:shutil.copy2(base/p,stage/p)
for p in ('run.sh','probe_main.cpp'):shutil.copy2(root/'artifacts/2026-09-26-standalone-mobile-home-w4a/stageA-snapshot'/p,stage/'tools/probe_firmware_ui'/p)
result={'snapshot':str(stage),'initial_paths_copied':len(initial['files']),'historical_overlays':'10 hash-verified preflight files, then stage-A runner/main','runs':[]}
for mode,repo in [('stage-a',stage),('final',root)]:
 run=(repo/'tools/probe_firmware_ui/run.sh').read_text();ui=(repo/'src/firmware_ui.cpp').read_text();main=(repo/'tools/probe_firmware_ui/probe_main.cpp').read_text();d=scratch/mode;d.mkdir(exist_ok=True)
 anchor='                    if (row_now) snprintf(before, sizeof before, "%s", row_now);';assert main.count(anchor)==1
 main=main.replace(anchor,anchor+'\n                    printf("QA_ROW_HEX="); if(row_now) for(const unsigned char* q=(const unsigned char*)row_now; *q; ++q) printf("%02x",*q); printf("\\n");')
 (d/'probe_main.cpp').write_text(main)
 prefix=run.split('# ---- THE LIVE RUN.')[0]
 prefix=re.sub(r'ROOT=.*',lambda m:'ROOT='+shlex.quote(str(repo)),prefix,count=1);prefix=re.sub(r'HERE=.*',lambda m:'HERE='+shlex.quote(str(repo/'tools/probe_firmware_ui')),prefix,count=1)
 prefix=prefix.replace('"$HERE/probe_main.cpp"',shlex.quote(str(d/'probe_main.cpp'))).replace('build_support l2 "${DEFS[@]}"','')
 variants=[('live',None)]
 if mode=='stage-a':
  parts=[re.search(r'^\s*'+v+r"='([^']+)'",run,re.M)[1] for v in ('o8a','o8b')]
  variants.append(('O8','\n'.join(parts)))
  old=(base/'tools/probe_firmware_ui/run.sh').read_text();variants.append(('O6',re.search(r'  ctl "O6 [^\n]+\n\s+\'([^\']+)\'',old)[1]))
 else:
  for label in ('B241a','B241b','W4a-S1','W4a-S2','W4a-S3','W4a-S4','W4a-S5','W4a-S6','O8'):
   pos=run.index('  ctl "'+label+' ');before=run[:pos];variables=re.findall(r'^\s*(\w+)=\'([^\']*)\'',before,re.M);var=dict(variables);call=run[pos:].split('\n',2)[1].strip()
   if call.startswith('"$'):sed=var[call.strip('"')[1:]]
   elif call.startswith("'"):sed=call.strip("'")
   else:raise RuntimeError((label,call))
   variants.append((label,sed))
 shell=prefix+'\nARM=v3; ARM_DEFS=LEAF_DEFS\n'
 for name,sed in variants:
  mutated=ui if sed is None else subprocess.check_output(['sed',sed],input=ui,text=True);p=d/(name+'.cpp');p.write_text(mutated)
  if sed is not None:assert mutated!=ui
  log=raw/('focused-'+mode+'-'+name+'.log')
  shell+='build_variant '+shlex.quote(str(p))+' "$OUT/qa-probe" -Werror || { cat "$OUT/build.log"; exit 9; }\n'
  shell+='"$OUT/qa-probe" > '+shlex.quote(str(log))+' 2>&1; run_rc=$?\n'
  shell+='printf "'+name+' exit=%s\\n" "$run_rc"\n'
  shell+='[ "$run_rc" -eq '+('0' if sed is None else '1')+' ] || exit 8\n'
 (d/'run.sh').write_text(shell)
 with (raw/('focused-'+mode+'-build.log')).open('w') as f:rc=subprocess.run(['bash',str(d/'run.sh')],cwd=root,stdout=f,stderr=subprocess.STDOUT).returncode
 print(mode,'exit',rc,flush=True)
 if rc:raise SystemExit(rc)
 for name,_ in variants:
  log=raw/('focused-'+mode+'-'+name+'.log');text=log.read_text(errors='replace');result['runs'].append(dict(mode=mode,name=name,log=str(log),sha256=hashlib.sha256(log.read_bytes()).hexdigest(),failures=[x.strip() for x in text.splitlines() if x.startswith('  FAIL ')],row_hex=re.findall(r'QA_ROW_HEX=(\w+)',text)))
(raw/'focused-controls.json').write_text(json.dumps(result,indent=2)+'\n');print('Focused controls complete; separate from stock gate',flush=True)
