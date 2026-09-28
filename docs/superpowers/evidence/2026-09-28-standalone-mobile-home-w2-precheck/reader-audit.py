"""Extract the actual 14 dormant sed controls and characterize them on scratch files."""
import pathlib,re,subprocess,json,hashlib,shlex
R=pathlib.Path('/home/staszek/MeshRoute');E=R/'docs/superpowers/evidence/2026-09-28-standalone-mobile-home-w2-precheck';S=pathlib.Path((E/'scratch-path.txt').read_text().strip())/'reader-audit';S.mkdir(exist_ok=True)
t=(R/'tools/probe_board_ui/run.sh').read_text();lines=t.splitlines(keepends=True)
def call(label):
 a=next(i for i,l in enumerate(lines) if l.startswith('wchk_in ') and ('"'+label+' ') in l); b=a
 while lines[b].rstrip().endswith('\\'):b+=1
 return ''.join(lines[a:b+1])
func=[]
for name in ['fn_body','fn_flat']:
 func.append(next(l for l in lines if l.startswith(name+'()')))
for name in ['w49','w51','oled_guarded','w54']:
 a=next(i for i,l in enumerate(lines) if l.startswith(name+'()'));b=next(i for i in range(a,len(lines)) if lines[i].rstrip()=='}');func.append(''.join(lines[a:b+1]))
variables=''.join(l for l in lines if any(l.startswith(x+'=') for x in ['DISPATCH_SIG','SETUP_SIG','BLE_SIG','UI_ARM','UI_ARM_GUARDED']))
script='#!/bin/bash\nset -u\nROOT='+shlex.quote(str(R))+'\nOUT='+shlex.quote(str(S))+'\nCMD_CPP="$ROOT/src/firmware_commands.cpp"\nFW_MAIN="$ROOT/src/fw_main.cpp"\n'+''.join(func)+variables+'''\nwchk_in() {
 local file=$1 label=$2 pred=$3; shift 3
 local key=${label%% *} n=0 expr orig newer
 "$pred" "$file"; orig=$?
 if [ "$key" = W49 ]; then DISPATCH_SIG='bool dispatch(const char* line, size_t len, Print& out, CommandTransport transport) {'; fi
 "$pred" "$file"; newer=$?
 printf '%s\\tBASE\\t%s\\t%s\\n' "$key" "$orig" "$newer"
 for expr in "$@"; do
  n=$((n+1)); sed "$expr" "$file" > "$OUT/$key-$n.cpp"
  printf '%s' "$expr" > "$OUT/$key-$n.sed"
  cmp -s "$file" "$OUT/$key-$n.cpp"; local same=$?
  "$pred" "$OUT/$key-$n.cpp"; local verdict=$?
  printf '%s\\t%s\\t%s\\t%s\\n' "$key" "$n" "$same" "$verdict"
 done
}
'''+''.join(call(k) for k in ['W49','W51','W54'])
(S/'audit.sh').write_text(script); run=subprocess.run(['bash',str(S/'audit.sh')],text=True,capture_output=True); (E/'dormant-controls.tsv').write_text(run.stdout); (S/'audit.stderr').write_text(run.stderr)
rows=[]
for key in ['W49','W51','W54']:
 source=R/('src/fw_main.cpp' if key=='W51' else 'src/firmware_commands.cpp');orig=source.read_text()
 for p in sorted(S.glob(key+'-*.cpp')):
  mut=p.read_text(); idx=p.stem.split('-')[1]; rows.append({'label':key+'-'+idx,'changed':mut!=orig,'sed':(S/(p.stem+'.sed')).read_text(),'original_ui_arm_count':orig.count('handle_ui(line + 2, len - 2, out); return true;'),'mutant_ui_arm_count':mut.count('handle_ui(line + 2, len - 2, out); return true;'),'original_signature_count':orig.count('static void dump_help(Print& out) {'),'mutant_sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(E/'dormant-controls.json').write_text(json.dumps(rows,indent=2)+'\n');print(run.stdout)
