from pathlib import Path
import json,re,subprocess,shlex
root=Path.cwd();raw=root/'artifacts/2026-09-26-standalone-mobile-home-w4a-precheck';scratch=Path(json.loads((raw/'scratch.json').read_text())['path']); run=(root/'tools/probe_firmware_ui/run.sh').read_text();src=(root/'src/firmware_ui.cpp').read_text()
m=re.search(r'  ctl "O8 [^\n]+\n\s+\'([^\']+)\'',run);assert m
orig=m[1]; fixed=orig.replace('\\\\\\\\0','\\\\0'); assert fixed!=orig
summary={}
for name,script in [('original',orig),('repaired',fixed)]:
 counts=[]
 for sub in script.splitlines():
  mutated=subprocess.check_output(['sed',sub],input=src,text=True)
  counts.append(sum(a!=b for a,b in zip(src.splitlines(),mutated.splitlines())))
 mutated=subprocess.check_output(['sed',script],input=src,text=True)
 (scratch/('o8-'+name+'.cpp')).write_text(mutated)
 summary[name]={'substitution_changed_lines':counts,'script':script}
m6=re.search(r'  ctl \"O6 [^\n]+\n\s+\'([^\']+)\'',run);assert m6
(scratch/'o6.cpp').write_text(subprocess.check_output(['sed',m6[1]],input=src,text=True))
summary['O6']={'script':m6[1],'changed_lines':sum(a!=b for a,b in zip(src.splitlines(),(scratch/'o6.cpp').read_text().splitlines()))}
(raw/'o8-anchor-measurement.json').write_text(json.dumps(summary,indent=2)+'\n')
# Diagnostic harness reuses the real runner's build functions verbatim; just one arm and two isolated controls.
diagnostic_main=(root/'tools/probe_firmware_ui/probe_main.cpp').read_text()
anchor='                    if (row_now) snprintf(before, sizeof before, \"%s\", row_now);'
assert diagnostic_main.count(anchor)==1
diagnostic_main=diagnostic_main.replace(anchor,anchor+'\n                    printf(\"DIAGNOSTIC P23d actual candidate row=[%s]\\n\", row_now ? row_now : \"<missing>\");')
(scratch/'o8-probe-main.cpp').write_text(diagnostic_main)
prefix=run.split('# ---- THE LIVE RUN.')[0]
prefix=prefix.replace('\"$HERE/probe_main.cpp\"',shlex.quote(str(scratch/'o8-probe-main.cpp')))
prefix=re.sub(r'HERE=.*',lambda m:'HERE='+shlex.quote(str(root/'tools/probe_firmware_ui')),prefix,count=1)
prefix=re.sub(r'ROOT=.*',lambda m:'ROOT='+shlex.quote(str(root)),prefix,count=1)
prefix=prefix.replace('build_support l2 "${DEFS[@]}"','')
script=prefix+'\nARM=v3; ARM_DEFS=LEAF_DEFS\n'
for name,path in [('live',root/'src/firmware_ui.cpp'),('original',scratch/'o8-original.cpp'),('repaired',scratch/'o8-repaired.cpp'),('O6',scratch/'o6.cpp')]:
 log=raw/('o8-'+name+'.log')
 script+=f'build_variant {shlex.quote(str(path))} "$OUT/probe" -Werror || {{ cat "$OUT/build.log"; exit 1; }}\n'
 script+=f'"$OUT/probe" > {shlex.quote(str(log))} 2>&1; code=$?\nprintf "{name} exit=%s\\n" "$code"\n'
 script+= ('[ "$code" -eq 0 ] || exit 1\n' if name=='live' else '[ "$code" -ne 0 ] || exit 1\n')
script+='echo "FOCUSED O8 DIAGNOSTIC COMPLETE; not a replacement full gate"\n'
(scratch/'o8-run.sh').write_text(script)
print(json.dumps(summary,indent=2),flush=True)
with (raw/'o8-run.log').open('w') as f:
 p=subprocess.run(['bash',str(scratch/'o8-run.sh')],stdout=f,stderr=subprocess.STDOUT)
print('diagnostic exit',p.returncode)
raise SystemExit(p.returncode)
