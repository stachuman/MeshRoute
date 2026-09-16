#!/usr/bin/env python3
"""Real command TU + uniquely extracted board/OTA owners; hardware primitives are explicit fakes."""
import sys
import argparse,hashlib,importlib.util,json,os,re,shlex,subprocess,tempfile
from pathlib import Path
from controls import CONTROLS

# Local checks: 12 confirmation refusals + 40 erase/order + 16 sleep + 24 prep
# + 10 basic board calls + 9 debug refusals + 2 OTA calls + 1 hang = 114.
# ESP adds 3 toggle/failure checks. Typed support/effects add 36 / 37 / 41.
PIN_ABSENT=150
PIN_NRF=151
PIN_ESP=158
PIN_BASE=114
PIN_BASE_ESP=117
PIN_TRANSCRIPTS=39
PIN_CONTROLS=19
PIN_SOURCE_CONTROLS=4

def source_reader_controls():
    sample='void selected() { /* } */ const char* s="}"; }'
    for candidate in ('',sample+'\n'+sample):
        try:function(candidate,'void selected() {')
        except AssertionError:pass
        else:raise AssertionError('missing/duplicate function was accepted')
    for shadow in ('// void selected() { }\n','const char* x="void selected() { }";\n'):
        assert function(shadow+sample,'void selected() {')==sample
    return 4
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools/probe_console_sink'))
spec=importlib.util.spec_from_file_location('console_source_reader',ROOT/'tools/probe_console_sink/structural.py')
reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
def function(text,signature):
    neutral=reader._neutral(text);assert neutral.count(signature)==1,(signature,neutral.count(signature))
    start=neutral.index(signature);opening=neutral.index('{',start);depth=1;end=opening+1
    while depth:
        if neutral[end]=='{':depth+=1
        elif neutral[end]=='}':depth-=1
        end+=1
    return text[start:end]
def owners(source,baseline,overrides=None):
    overrides=overrides or {}
    f=overrides.get('src/fw_main.cpp',(source/'src/fw_main.cpp').read_text());o=overrides.get('src/device_ota.cpp',(source/'src/device_ota.cpp').read_text())
    ota=['bool ota_start() {' if baseline else 'bool ota_start(Print& out) {','void ota_stop() {','bool ota_active() {','void set_pre_reboot_hook(']
    if not baseline:ota.insert(1,'bool ota_start() {')
    block='namespace mrota {\nconst int HTTP_GET=0, HTTP_POST=1;\n'+'\n'.join(function(o,s) for s in ota)+'\n}\n'
    sigs=[] if baseline else ['mrfw::ActionSupport mrfw::action_build_support()', 'mrfw::ActionOutcome mrfw::action_reboot_apply(', 'mrfw::ActionOutcome mrfw::action_ota_apply(', 'mrfw::ActionOutcome mrfw::action_crash_apply(', 'mrfw::ActionOutcome mrfw::action_prep_restart_apply(']
    sigs+=['static void do_reboot() {','static void do_ota() {','static void handle_crashtest(const char* args, Print& out) {','static void handle_prep_restart(Print& out) {','void fw_reboot()','void fw_ota()','void fw_crashtest(','void fw_prep_restart(']
    fw='\n'.join(function(f,s) for s in sigs)
    primitive='volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(0xFFFFFFF0u); (void)*p;'
    assert fw.count(primitive)==1 and fw.count('__asm volatile("udf #0");')==1
    fw=fw.replace(primitive,'action_fault();').replace('__asm volatile("udf #0");','action_fault();')
    fw=fw.replace('out.flush();','action_flush(out);').replace('mrcon.flush();','action_flush(mrcon);')
    return block+'''
#undef BOARD_HELTEC_V3
#undef ARDUINO_ARCH_ESP32
#undef ESP32
#undef NRF52_SERIES
#undef NRF52_PLATFORM
#undef MRFAULT_ESP32
#undef MRFAULT_HW
#if ACTION_BACKEND == 1
#define NRF52_SERIES 1
#define NRF52_PLATFORM 1
#define MRFAULT_HW 1
#elif ACTION_BACKEND == 2
#define BOARD_HELTEC_V3 1
#define MRFAULT_ESP32 1
#define MRFAULT_HW 1
#endif
#if !ACTION_POWERSAVE
#define MR_NO_POWERSAVE 1
#endif
#define delay action_delay
#define NVIC_SystemReset action_reset
#define ESP action_esp
#define NRF_POWER (&action_power)
#define abort action_fault
'''+fw+'''
#undef delay
#undef NVIC_SystemReset
#undef ESP
#undef NRF_POWER
#undef abort
void fw_faults_dump(Print&) { routed("faults_dump"); }
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--no-neg',action='store_true');ap.add_argument('--baseline-source',type=Path);ap.add_argument('--backend',type=int,choices=[0,1,2]);ap.add_argument('--out',type=Path);args=ap.parse_args()
    output=args.out or Path(tempfile.mkdtemp(prefix='mr-action-probe-'));output.mkdir(parents=True,exist_ok=True)
    source=args.baseline_source or ROOT;baseline=bool(args.baseline_source)
    assert len(CONTROLS)==PIN_CONTROLS
    source_controls=source_reader_controls();assert source_controls==PIN_SOURCE_CONTROLS
    text=(HERE/'probe.cpp').read_text();assert text.count('// @ACTION_OWNERS@')==1
    generated=text.replace('// @ACTION_OWNERS@',owners(source,baseline))
    runner=(ROOT/'tools/probe_inbox_verbs/run.sh').read_text();boundary='rc=0\nif ! build_support;';assert runner.count(boundary)==1
    prefix=runner[:runner.index(boundary)]
    # Reuse the standing builder and its exact dependencies/defines; no forked build recipe.
    commands=['LDWRAP+=(-Wl,--wrap=_ZN9meshroute4Node19clear_learned_stateEv)', 'build_support || exit 2']
    results=[]
    for backend,power in ([(args.backend,1)] if args.backend is not None else [(0,1),(1,1),(2,1),(2,0)]):
        name=f'b{backend}-p{power}';cpp=output/(name+'.cpp')
        cpp.write_text(f'#define ACTION_BACKEND {backend}\n#define ACTION_POWERSAVE {power}\n'+('#define ACTION_BASELINE 1\n' if baseline else '')+generated)
        cmd=f'build_variant {shlex.quote(str(source/"src/firmware_commands.cpp"))} "$FW_INBOX" "" "$OUT/{name}" {shlex.quote(str(cpp))}'
        commands += [cmd+f' || {{ cat "$OUT/build.log"; exit 2; }}',f'cp "$OUT/build.log" {shlex.quote(str(output/(name+"-compile.log")))}',f'"$OUT/{name}" > {shlex.quote(str(output/(name+".log")))} 2>&1 || {{ cat {shlex.quote(str(output/(name+".log")))}; exit 1; }}',f'cat {shlex.quote(str(output/(name+".log")))}']
        results.append({'name':name,'cpp_sha256':hashlib.sha256(cpp.read_bytes()).hexdigest()})
    controls=[]
    if not args.no_neg and not baseline:
        for label,path,old,new,backend,power in CONTROLS:
            original=(ROOT/path).read_text();assert original.count(old)==1,(label,original.count(old))
            changed=original.replace(old,new);name=label.split()[0];directory=output/name;directory.mkdir(exist_ok=True)
            changed_path=directory/Path(path).name;changed_path.write_text(changed)
            body=text.replace('// @ACTION_OWNERS@',owners(ROOT,False,{path:changed}))
            cpp=directory/'probe.cpp';cpp.write_text(f'#define ACTION_BACKEND {backend}\n#define ACTION_POWERSAVE {power}\n'+body)
            router=changed_path if path=='src/firmware_commands.cpp' else ROOT/'src/firmware_commands.cpp'
            log=directory/'run.log'
            commands += [f'build_variant {shlex.quote(str(router))} "$FW_INBOX" "" "$OUT/{name}" {shlex.quote(str(cpp))} || {{ cat "$OUT/build.log"; exit 2; }}',
                         f'cp "$OUT/build.log" {shlex.quote(str(directory/"compile.log"))}',
                         f'"$OUT/{name}" > {shlex.quote(str(log))} 2>&1; ctl_rc=$?; test "$ctl_rc" -eq 1 || {{ cat {shlex.quote(str(log))}; exit 3; }}',
                         f'grep -q "^  FAIL action " {shlex.quote(str(log))} || exit 4',
                         f'echo "CONTROL RED {name}"']
            controls.append({'label':label,'path':path,'matches':1,'source_sha256':hashlib.sha256(original.encode()).hexdigest(),'mutant_sha256':hashlib.sha256(changed.encode()).hexdigest()})
    watched=[ROOT/p for p in ('src/fw_main.cpp','src/firmware_commands.cpp','src/device_ota.cpp','src/device_ota.h','src/firmware_action_effects.h','tools/probe_inbox_verbs/probe_main.cpp','tools/probe_inbox_verbs/fakes/Preferences.h','tools/probe_deferred_actions/run.py','tools/probe_deferred_actions/run.sh','tools/probe_deferred_actions/probe.cpp','tools/probe_deferred_actions/controls.py')]
    before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
    script=prefix+'\n'+ '\n'.join(commands)+'\n'
    (output/'build.sh').write_text(script)
    p=subprocess.run(['bash','-c',script,str(ROOT/'tools/probe_inbox_verbs/run.sh')],env=dict(os.environ,MR_PROBE_ARM='accept'),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (output/'build.log').write_bytes(p.stdout)
    if p.returncode:print(p.stdout.decode()[-8000:]);raise SystemExit(p.returncode)
    assert before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in watched}
    for row in results:
        s=(output/(row['name']+'.log')).read_text();m=re.search(r'ACTION CHECKS (\d+) FAILED (\d+)',s);assert m and m[2]=='0'
        row['checks']=int(m[1]);row['transcripts']=len(re.findall(r'^TRANSCRIPT ',s,re.M))
        expected=(PIN_BASE_ESP if row['name'].startswith('b2') else PIN_BASE) if baseline else {'b0-p1':PIN_ABSENT,'b1-p1':PIN_NRF,'b2-p1':PIN_ESP,'b2-p0':PIN_ESP}[row['name']]
        assert row['checks']==expected and row['transcripts']==PIN_TRANSCRIPTS,(row,expected)
        print(row['name'],row['checks'],'checks',row['transcripts'],'transcripts PASS')
    (output/'results.json').write_text(json.dumps({'baseline':baseline,'source':str(source),'results':results,'controls':controls,'source_preservation':before,'source_reader_controls':source_controls},indent=2)+'\n')
    if controls: print(str(len(controls))+' controls compile/assertion RED')
    print('ACTION PROBE PASS; output='+str(output))
if __name__=='__main__':main()
