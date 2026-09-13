from pathlib import Path
import ast,importlib.util,json,re,subprocess,hashlib
q=Path(__file__).resolve().parent;r=q/'snapshot'
def emit(name,data): (q/name).write_text(json.dumps(data,indent=2)+'\n')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('abi',r/'tools/probe_board_abi.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
h=(r/'lib/core/remote_session.h').read_text()
header=re.search(r'struct TranscriptHeader \{.*?\n\};',h,re.S).group().replace('TranscriptHeader','CandidateTranscriptHeader').replace('\n};','\n    uint32_t activation_ms;\n};')
state=re.search(r'struct RemoteSessionState \{.*?\n\};',h,re.S).group().replace('RemoteSessionState','CandidateRemoteSessionState').replace('TranscriptHeader','CandidateTranscriptHeader').replace('\n};','\n    CandidateDeferredAction actions[2];\n};')
fixture='''// AUTHOR PRICING CANDIDATE ONLY. Not a production plan or storage authorization.
#include "console_line.h"
namespace meshroute {
struct HistoricalDeferredCandidate {
    uint64_t request_id;
    uint32_t activate_at_ms;
    uint8_t action, controller_slot, armed, ack_seen, in_use;
};
struct CandidateDeferredAction {
    uint64_t request_id, admin_epoch, activate_at_ms;
    uint32_t source_hash, activation_ms;
    uint16_t command_len;
    uint8_t controller_slot, authority, action, phase;
    uint8_t reserved[2];
    uint8_t command[console::remote_command_max_bytes + 1];
};
'''+header+'\n'+state+'\n}\n'+m.FIXTURE_SOURCE
entries=tuple(('meshroute::'+s,'MR_FEAT_RADMIN_ACCEPT') for s in ('HistoricalDeferredCandidate','CandidateDeferredAction','TranscriptHeader','CandidateTranscriptHeader','RemoteSessionState','CandidateRemoteSessionState','Node'))
tu=m.generate_tu(entries,fixture_source=fixture);(q/'candidate-layout.cpp').write_text(tu)
measure={}
for target in ('native','gateway','heltec_mobile'):
 data=m.idedata(target);emit('candidate-'+target+'-idedata.json',data)
 obj=q/('candidate-'+target+'.o');cmd=m.compile_command(data,q/'candidate-layout.cpp',obj)
 p=subprocess.run(cmd,cwd=r,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs'/('candidate-'+target+'-compile.log')).write_text(p.stdout)
 assert p.returncode==0,(target,p.stdout)
 sizes=m.read_sizes(m.binutil(data['cxx_path'],'nm'),obj)
 measure[target]={'command':cmd,'sizes':sizes,'object_sha256':sha(obj)}
 print(target,sizes,flush=True)
emit('candidate-measurements.json',measure)
policy=(r/'src/firmware_command_authority.h').read_text()
rows=[{'line':policy[:x.start()].count('\n')+1,'verb':x[1],'semantic':x[2],'authority':x[3],'disruptive':x[4]=='true'} for x in re.finditer(r'\{"([^"]+)", "([^"]+)", CommandClass::(\w+), (true|false)\}',policy)]
assert len(rows)==204
selected=[x for x in rows if x['disruptive']];assert len(selected)==48
emit('disruptive-inventory.json',{'policy_sha256':sha(r/'src/firmware_command_authority.h'),'all_rows':len(rows),'disruptive_rows':len(selected),'rows':selected})
values={};tables={}
for n in ast.parse((r/'tools/probe_ui_model_mutations.py').read_text()).body:
 if isinstance(n,ast.Assign):
  for t in n.targets:
   if isinstance(t,ast.Name):
    try:values[t.id]=ast.literal_eval(n.value)
    except (ValueError,TypeError):
     if t.id=='MUTS_BY_TARGET':tables={ast.literal_eval(k):v.id for k,v in zip(n.value.keys,n.value.values)}
old=json.loads(Path('/tmp/mr-qa-s7b2-gate-0phs0ag4/union/selectors.json').read_text())['union']
timing={t:p for t,p in values['TARGET_SRC'].items() if any(k in p for k in ('remote_activation','mac_wait'))}
chosen=sorted(set(old)|set(timing));counts={t:len(values[tables[t]]) for t in chosen}
for t in chosen:
 text=(r/values['TARGET_SRC'][t]).read_text()
 for name,pattern,replacement in values[tables[t]]:assert text.count(pattern)==1,(t,name)
emit('historical-mutation-floor.json',{'historical_7b2':old,'timing_dependencies':timing,'union':chosen,'counts':counts,'configured_patterns':sum(counts.values()),'patterns_match_once':True,'executed_this_precheck':False})
print('mutation floor',len(chosen),sum(counts.values()),timing,flush=True)
current=q/'corpus';previous=Path('/tmp/mr-qa-s7b2-gate-0phs0ag4/corpus-final')
cmd=['python3','tools/run_corpus.py','--validate',str(previous)];p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs/previous-corpus-validate.log').write_bytes(p.stdout);assert p.returncode==0
cmd=['python3','tools/run_corpus.py','--compare',str(previous),str(current)];p=subprocess.run(cmd,cwd=r,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(q/'logs/corpus-compare.log').write_bytes(p.stdout);print(p.stdout.decode()[-900:]);assert p.returncode==0
manifest=json.loads((current/'manifest.json').read_text());paths=sorted(current.glob('**/*.jsonl'));oldpaths=sorted(previous.glob('**/*.jsonl'));assert len(paths)==len(oldpaths)==36
streams=[]
for p in paths:
 oldp=previous/p.relative_to(current);assert p.read_bytes()==oldp.read_bytes(),p
 streams.append({'path':str(p.relative_to(current)),'bytes':p.stat().st_size,'sha256':sha(p)})
log=(q/'logs/sim-build.log').read_text();compile_lines=[x for x in log.splitlines() if re.match(r'\[\d+/\d+\]',x) and ' -c ' in x]
assert len(compile_lines)==64
emit('corpus-identity.json',{'streams_compared_byte_for_byte':len(streams),'previous':str(previous),'current':str(current),'lus_sha256':manifest['lus_sha256'],'baseline_sha256':manifest['baseline_sha256'],'fresh_compiler_actions':len(compile_lines),'streams':streams})
print('AUDIT COMPLETE',flush=True)
