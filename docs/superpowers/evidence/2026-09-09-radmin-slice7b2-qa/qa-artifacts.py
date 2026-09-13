import io,json,hashlib,struct,subprocess,collections
from pathlib import Path
from elftools.elf.elffile import ELFFile
q=Path(__file__).resolve().parent;root=q/'measure/.pio-measure'
sha=lambda b:hashlib.sha256(b).hexdigest()
def load(side,board):
 p=root/f'qa-{side}'/board;raw=(p/'firmware.elf').read_bytes();elf=ELFFile(io.BytesIO(raw))
 sec={s.name:dict(type=s['sh_type'],flags=s['sh_flags'],addr=s['sh_addr'],size=s['sh_size'],data=s.data(),index=i) for i,s in enumerate(elf.iter_sections())}
 sym=[dict(name=s.name,value=s['st_value'],size=s['st_size'],type=s['st_info']['type'],bind=s['st_info']['bind'],index=s['st_shndx']) for s in elf.get_section_by_name('.symtab').iter_symbols()]
 return dict(path=p,raw=raw,sec=sec,sym=sym,manifest=json.loads((p/'manifest.json').read_text()))
all_files=sorted(p for p in root.glob('qa-*/*/*') if p.is_file());before={str(p):sha(p.read_bytes()) for p in all_files};result={}
for board in ['gateway','heltec_mobile']:
 a,b=load('base',board),load('final',board);assert a['sec'].keys()==b['sec'].keys()
 changes=[]
 for name,s in a['sec'].items():
  t=b['sec'][name]
  if any(s[k]!=t[k] for k in ['type','flags','addr','size','data']):changes.append(dict(name=name,allocated=bool(s['flags']&2),size_before=s['size'],size_after=t['size'],addr_before=s['addr'],addr_after=t['addr'],bytes_equal=s['data']==t['data']))
 d=dict(measurements={side:x['manifest']['measurements'] for side,x in [('base',a),('final',b)]},sections=changes,elf_sha256={side:sha(x['raw']) for side,x in [('base',a),('final',b)]})
 if board=='heltec_mobile':
  assert not [c for c in changes if c['allocated']];assert a['sym']==b['sym']
  payloads=[];desc=[]
  for side,x in [('base',a),('final',b)]:
   raw=(x['path']/'firmware.bin').read_bytes();assert raw[0]==0xe9 and raw[23]==1
   dg=hashlib.sha256(x['raw']).digest();hit=raw.find(dg);assert hit>=0 and raw.find(dg,hit+1)<0
   cur=24;xor=0xef;segments=[]
   for _ in range(raw[1]):
    addr,n=struct.unpack_from('<II',raw,cur);cur+=8;segments.append((addr,n))
    for byte in raw[cur:cur+n]:xor^=byte
    cur+=n
   checksum=((cur+16)//16)*16-1
   assert len(raw)==checksum+1+32 and raw[checksum]==xor and hashlib.sha256(raw[:-32]).digest()==raw[-32:]
   desc.append(dict(elf_digest_offset=hit,checksum_offset=checksum,segments=segments,size=len(raw),sha256=sha(raw)));payloads.append(raw)
  assert len(payloads[0])==len(payloads[1]);assert desc[0]['elf_digest_offset']==desc[1]['elf_digest_offset']
  off=desc[0]['elf_digest_offset'];checksum=desc[0]['checksum_offset'];allowed=set(range(off,off+32))|set(range(checksum,len(payloads[0])))
  diffs=[i for i,(x,y) in enumerate(zip(*payloads)) if x!=y];assert set(diffs)<=allowed
  d['payloads']=desc;d['payload_different_bytes']=len(diffs);d['only_elf_digest_checksum_trailer']=True;d['allocated_sections_and_full_symbols_equal']=True
 else:
  def named(x,section):return {s['name']:s for s in x['sym'] if s['size']>0 and s['index']==x['sec'][section]['index']}
  for section in ['.text','.bss','.data']:
   sa,sb=named(a,section),named(b,section)
   rows=[dict(name=n,before=sa.get(n,{}).get('size',0),after=sb.get(n,{}).get('size',0)) for n in sorted(sa.keys()|sb.keys()) if sa.get(n,{}).get('size',0)!=sb.get(n,{}).get('size',0)]
   for row in rows:row['delta']=row['after']-row['before']
   d[section+'_symbol_size_changes']=rows
  def covered(x):
   spans=sorted((s['value']&~1,(s['value']&~1)+s['size']) for s in x['sym'] if s['size'] and s['index']==x['sec']['.text']['index']);total=0;end=0
   for lo,hi in spans:total+=max(0,hi-max(lo,end));end=max(end,hi)
   return total
  cov=[covered(x) for x in [a,b]];d['text_covered_bytes']=cov;d['text_uncovered_bytes']=[x['sec']['.text']['size']-n for x,n in zip([a,b],cov)]
  d['text_named_size_delta']=sum(s['delta'] for s in d['.text_symbol_size_changes'])
  for side,x in [('base',a),('final',b)]:
   path=q/'logs'/f'{side}-gateway-disassembly.log'
   with path.open('w') as f:subprocess.run(['/home/staszek/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objdump','-dC',str(x['path']/'firmware.elf')],stdout=f,check=True)
 result[board]=d
assert before=={str(p):sha(p.read_bytes()) for p in all_files}
result['pristine_files_verified']=len(before)
(q/'artifact-audit.json').write_text(json.dumps(result,indent=2)+'\n')
for board in ['gateway','heltec_mobile']:
 d=result[board];print(board,'measurements',d['measurements']);print('allocated section changes',[x for x in d['sections'] if x['allocated']])
 if board=='gateway':print('RAM symbol deltas',d['.bss_symbol_size_changes']);print('text coverage',d['text_covered_bytes'],'uncovered',d['text_uncovered_bytes'],'named sum',d['text_named_size_delta'],'changed symbols',len(d['.text_symbol_size_changes']))
 else:print('mobile allocated sections/full symbols identical; metadata-only payload bytes',d['payload_different_bytes'])
print('All',len(before),'original measurement files pristine')
