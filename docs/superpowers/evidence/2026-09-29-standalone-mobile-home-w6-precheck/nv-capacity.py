"""Source-sized NVS payload/entry budget, NOT a read of a device's occupied/free pages."""
from pathlib import Path
import json,subprocess,hashlib,math
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent
config=json.loads(subprocess.check_output(['pio','project','config','--json-output'],cwd=R))
envs={}
stack=json.loads((E/'stack-measure.json').read_text())
cmd=next(x['command'] for x in stack['compiles'] if x['target']=='heltec_mobile')
fw=Path(next(x[2:] for x in cmd if x.startswith('-I') and x.endswith('/cores/esp32'))).parents[1]
for section,opts in config:
 if not section.startswith('env:'):continue
 d=dict(opts); flags=' '.join(d.get('build_flags',[]))
 if 'MR_FEAT_OLED=1' not in flags:continue
 board=d['board'];board=board[0] if isinstance(board,list) else board
 p=R/'boards'/(board+'.json')
 if not p.exists():p=Path('/home/staszek/.platformio/platforms/espressif32/boards')/(board+'.json')
 bj=json.loads(p.read_text()); part=fw/'tools/partitions'/bj['build']['arduino']['partitions']
 row=[l for l in part.read_text().splitlines() if l.startswith('nvs,')][0];fields=[x.strip() for x in row.split(',')]
 envs[section[4:]]=dict(board=board,board_definition=str(p),partition=str(part),partition_sha256=hashlib.sha256(part.read_bytes()).hexdigest(),nvs_bytes=int(fields[4],0),profile='client' if 'MR_PROFILE_MOBILE' in flags else 'accept')
sizes=json.loads((E/'layout-measure.json').read_text())['variants']['base']['targets']['heltec_mobile']
common={'cfg':'mrnv::Blob','id':'mrnv::IdBlob','peers':'mrnv::PeerBlob','join':'mrnv::JoinBlob','teams':'mrnv::TeamKeyBlob','ui':'mrnv::UiPresetBlob','fault-log':'mrfault::FaultLog'}
records={k:v['size'] for k,v in sizes.items()};sets={}
for profile,extra in [('accept',{'admid':'mrnv::AdminIdBlob','acl':'mrnv::AclBlob'}),('client',{'mkeys':'mrnv::MgmtKeyBlob','targets':'mrnv::TargetBlob'}),('all_retained',{'admid':'mrnv::AdminIdBlob','acl':'mrnv::AclBlob','mkeys':'mrnv::MgmtKeyBlob','targets':'mrnv::TargetBlob'})]:
 rec={k:records[t] for k,t in (common|extra).items()};rec.update(ibm_dm=28,ibm_ch=28)
 variants={}
 for v in ['v1','v2','v2_replacement_overlap']:
  rr=rec.copy()
  if v!='v1':rr['ui']=2852
  if v=='v2_replacement_overlap':rr['ui_new_in_flight']=2852
  entries={k:2+math.ceil(n/32) for k,n in rr.items()}
  # Two namespace entries mr,mrfault. One of 5 pages reserved for GC; 126 entries/page.
  total=sum(entries.values())+2
  variants[v]=dict(record_bytes=rr,payload_bytes=sum(rr.values()),single_chunk_blob_entries=entries,namespace_entries=2,logical_entries=total,conservative_4_page_capacity_entries=4*126,entries_headroom_4_pages=4*126-total,entry_bytes=total*32)
 sets[profile]=variants
x=dict(scope=__doc__,envs=envs,record_sets=sets,limitations=['No connected-device NVS stats or flash dump read.','An estimate of live records is not occupied entries: erased entries, framework keys, abandoned namespaces and fragmentation are not observed.','Single-chunk geometry is 2+ceil(bytes/32); extra chunks add overhead. All proposed records are below a fresh-page chunk capacity.','Profiles exclude the other endpoint during normal writes, but retained keys from an older profile may remain; all_retained row includes both. Fault record included conservatively.','A fresh 20KB NVS image has five 4096-byte pages; budgeting four (504 entries) leaves a GC page.'])
(E/'nv-capacity.json').write_text(json.dumps(x,indent=2)+'\n')
print('OLED',envs)
for k,v in sets.items():print(k,{q:(z['payload_bytes'],z['logical_entries'],z['entries_headroom_4_pages']) for q,z in v.items()})
