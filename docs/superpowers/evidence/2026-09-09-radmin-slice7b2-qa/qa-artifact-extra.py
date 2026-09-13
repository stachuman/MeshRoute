from pathlib import Path
import io,json,re,hashlib,struct,collections,subprocess
from elftools.elf.elffile import ELFFile
q=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
out={}
# Two fresh 70-action Ninja graphs, with all core objects compiled in both domains.
objects={};builds={}
for side in ['base','final']:
 root=q/f'sim-{side}'; t=(q/'logs'/f'sim-{side}-build.log').read_text(); lines=[l for l in t.splitlines() if re.match(r'\[\d+/\d+\]',l)]
 assert len(lines)==70 and sum(' -c ' in l for l in lines)==64
 objects[side]={str(p.relative_to(root)):sha(p.read_bytes()) for p in sorted(root.rglob('*.o'))}
 assert len(objects[side])==64
 builds[side]=dict(actions=70,compiler_actions=64,objects=objects[side],lus_sha256=sha((root/'orchestrator/lus').read_bytes()))
 assert sum('remote_session.cpp.o -c ' in l for l in lines)==2
 assert sum('node.cpp.o -c ' in l for l in lines)==2
 assert sum('node_mac_rx.cpp.o -c ' in l for l in lines)==2
assert objects['base'].keys()==objects['final'].keys()
out['sim']=dict(builds=builds,changed_objects=[f for f in objects['base'] if objects['base'][f]!=objects['final'][f]])
elfs=[]
for side in ['base','final']:
 p=q/f'measure/.pio-measure/qa-{side}/gateway/firmware.elf';raw=p.read_bytes();e=ELFFile(io.BytesIO(raw));symbols=list(e.get_section_by_name('.symtab').iter_symbols());elfs.append((e,symbols))
def resolve(e,syms,value):
 # Preserve all aliases and offsets, including interior object/string addresses.
 hits=[]
 for s in syms:
  start=s['st_value'];n=s['st_size']
  if not isinstance(s['st_shndx'],int):continue
  if not n:
   if s.name and value==start:hits.append((s.name,0))
   continue
  if s['st_info']['type']=='STT_FUNC':
   if value==start:hits.append((s.name,0))
  elif start<=value<start+n:hits.append((s.name,value-start))
 return sorted(hits)
arrays=[e.get_section_by_name('.data').data() for e,_ in elfs];rows=[]
for i in range(0,len(arrays[0]),4):
 a,b=[struct.unpack_from('<I',v,i)[0] for v in arrays]
 if a==b:continue
 resolved=[resolve(e,s,v) for (e,s),v in zip(elfs,[a,b])]
 eq=bool(set(resolved[0])&set(resolved[1])); row=dict(offset=i,before=hex(a),after=hex(b),symbols=resolved,same_target=eq)
 if not eq:
  # Unnamed literal/string target: compare nul-terminated bytes in allocated PROGBITS.
  data=[]
  for (e,_),v in zip(elfs,[a,b]):
   candidates=[s for s in e.iter_sections() if s['sh_flags']&2 and s['sh_type']=='SHT_PROGBITS' and s['sh_addr']<=v<s['sh_addr']+s['sh_size']]
   assert len(candidates)==1,(i,hex(v));s=candidates[0];tail=s.data()[v-s['sh_addr']:];data.append(tail[:tail.index(b'\0')+1] if b'\0' in tail else tail)
  row['literal_equal']=data[0]==data[1];row['literal_hex']=[v.hex() for v in data];assert row['literal_equal'],row
 rows.append(row)
out['gateway_data_relocations']=rows
ex=[]
for e,_ in elfs:
 s=e.get_section_by_name('.ARM.exidx');a,b=struct.unpack('<II',s.data());prel=a&0x7fffffff;prel-=0x80000000 if prel&0x40000000 else 0;ex.append(dict(target=s['sh_addr']+prel,unwind=b))
assert ex[0]==ex[1];out['gateway_exidx_same_absolute_target']=ex
# Text intervals are measured, not summed across aliases. Retain uncovered string/padding intervals.
intervals={}
for side,(e,syms) in zip(['base','final'],elfs):
 s=e.get_section_by_name('.text');idx=e.get_section_index('.text');spans=sorted((x['st_value']&~1,(x['st_value']&~1)+x['st_size']) for x in syms if x['st_shndx']==idx and x['st_size']);cur=s['sh_addr'];gaps=[]
 for lo,hi in spans:
  if lo>cur:gaps.append((cur,lo))
  cur=max(cur,hi)
 if cur<s['sh_addr']+s['sh_size']:gaps.append((cur,s['sh_addr']+s['sh_size']))
 strings=[];zeros=0;other=0
 for lo,hi in gaps:
  data=s.data()[lo-s['sh_addr']:hi-s['sh_addr']];strings.extend(m.group().decode() for m in re.finditer(rb'[\x20-\x7e]{4,}',data));zeros+=data.count(0);other+=len(data)-data.count(0)
 intervals[side]=dict(uncovered_bytes=sum(b-a for a,b in gaps),zero_bytes=zeros,nonzero_bytes=other,printable_strings=strings,gaps=[dict(address=hex(a),size=b-a) for a,b in gaps])
a,b=[collections.Counter(intervals[side]['printable_strings']) for side in ['base','final']]
out['gateway_uncovered_text']=intervals;out['gateway_added_uncovered_strings']=dict(b-a);out['gateway_removed_uncovered_strings']=dict(a-b)
(q/'artifact-extra.json').write_text(json.dumps(out,indent=2)+'\n')
print('sim 70 actions / 64 compilations each; changed objects',len(out['sim']['changed_objects']),out['sim']['changed_objects'])
print('sim hashes',{s:x['lus_sha256'] for s,x in builds.items()})
print('data relocated words',len(rows),'all same target or literal; exidx',ex)
print('uncovered text',[(s,intervals[s]['uncovered_bytes'],intervals[s]['zero_bytes']) for s in intervals])
print('added strings',out['gateway_added_uncovered_strings']);print('removed strings',out['gateway_removed_uncovered_strings'])
