import io,json,hashlib,struct,subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
q=Path(__file__).resolve().parent; root=q/'measure/.pio-measure'
def sha(data): return hashlib.sha256(data).hexdigest()
def load(side,board):
 p=root/f'qa-{side}'/board; raw=(p/'firmware.elf').read_bytes(); elf=ELFFile(io.BytesIO(raw));
 sec={s.name:dict(type=s['sh_type'],flags=s['sh_flags'],addr=s['sh_addr'],size=s['sh_size'],data=s.data(),index=i) for i,s in enumerate(elf.iter_sections())}
 sym=[dict(name=s.name,value=s['st_value'],size=s['st_size'],type=s['st_info']['type'],bind=s['st_info']['bind'],index=s['st_shndx']) for s in elf.get_section_by_name('.symtab').iter_symbols()]
 return dict(path=p,raw=raw,sec=sec,sym=sym,manifest=json.loads((p/'manifest.json').read_text()))
all_files=sorted(p for p in root.glob('qa-*/*/*') if p.is_file()); before={str(p):sha(p.read_bytes()) for p in all_files}; result={'artifacts':{},'boards':{}}
for board in ['gateway','heltec_mobile']:
 a,b=load('base',board),load('final',board); assert a['sec'].keys()==b['sec'].keys()
 changes=[]
 for name,s in a['sec'].items():
  t=b['sec'][name]
  if any(s[k]!=t[k] for k in ['type','flags','addr','size','data']): changes.append(dict(name=name,allocated=bool(s['flags']&2),before=s['size'],after=t['size'],addr_before=s['addr'],addr_after=t['addr'],bytes_equal=s['data']==t['data']))
 d={'measurements':{side:x['manifest']['measurements'] for side,x in [('base',a),('final',b)]},'changed_sections':changes}
 for side,x in [('base',a),('final',b)]: result['artifacts'][board+'-'+side]={'elf_sha256':sha(x['raw']), 'manifest_sha256':sha((x['path']/'manifest.json').read_bytes())}
 if board=='heltec_mobile':
  assert not [c for c in changes if c['allocated']]; assert a['sym']==b['sym']; assert not any('remote_body_' in s['name'] or 'remote_nonce' in s['name'] for s in b['sym'])
  payloads=[]; descriptors=[]
  for side,x in [('base',a),('final',b)]:
   raw=(x['path']/'firmware.bin').read_bytes(); assert raw[0]==0xe9 and raw[23]==1
   digest=hashlib.sha256(x['raw']).digest(); hits=[i for i in range(len(raw)) if raw.startswith(digest,i)]; assert len(hits)==1
   cursor=24; xor=0xef; segs=[]
   for _ in range(raw[1]):
    addr,n=struct.unpack_from('<II',raw,cursor); cursor+=8; segs.append((addr,n))
    for byte in raw[cursor:cursor+n]: xor^=byte
    cursor+=n
   checksum_offset=((cursor+16)//16)*16-1
   assert len(raw)==checksum_offset+1+32 and raw[checksum_offset]==xor
   assert hashlib.sha256(raw[:-32]).digest()==raw[-32:]
   descriptors.append(dict(elf_digest_offset=hits[0],checksum_offset=checksum_offset,segments=segs,size=len(raw),sha256=sha(raw))); payloads.append(raw)
  assert len(payloads[0])==len(payloads[1]); assert descriptors[0]['elf_digest_offset']==descriptors[1]['elf_digest_offset']
  start=descriptors[0]['elf_digest_offset']; checksum=descriptors[0]['checksum_offset']; allowed=set(range(start,start+32))|set(range(checksum,len(payloads[0])))
  diffs=[i for i,(x,y) in enumerate(zip(*payloads)) if x!=y]; assert set(diffs)<=allowed
  d['image_attribution']={'base':descriptors[0],'final':descriptors[1],'different_byte_count':len(diffs),'only_elf_digest_checksum_and_trailer':True,'all_allocated_elf_sections_and_symbols_equal':True}
 else:
  assert {c['name'] for c in changes if c['allocated']}=={'.text','.data','.ARM.exidx'}
  # One named text function entry per name; no alias sums masquerading as section size.
  def sized(x): return {s['name']:s['size'] for s in x['sym'] if s['size']>0 and s['index']==x['sec']['.text']['index']}
  sa,sb=sized(a),sized(b); sizes=[dict(name=n,before=sa.get(n,0),after=sb.get(n,0),delta=sb.get(n,0)-sa.get(n,0)) for n in sorted(sa.keys()|sb.keys()) if sa.get(n,0)!=sb.get(n,0)]
  def covered(x):
   spans=sorted((s['value']&~1,(s['value']&~1)+s['size']) for s in x['sym'] if s['size'] and s['index']==x['sec']['.text']['index']); total=0; end=0
   for lo,hi in spans:
    total+=max(0,hi-max(lo,end)); end=max(end,hi)
   return total
  cov=[covered(x) for x in [a,b]]; assert sum(s['delta'] for s in sizes)==cov[1]-cov[0]
  assert all(any(t in s['name'] for t in ['remote_','write_header','admission_status']) for s in sizes)
  d['text_symbol_size_changes']=sizes; d['text_covered_bytes']=cov; d['text_uncovered_bytes']=[x['sec']['.text']['size']-n for x,n in zip([a,b],cov)]
  def symbols_at(x,ptr):
   addr=ptr&~1
   return {(s['name'],addr-(s['value']&~1),ptr&1) for s in x['sym'] if s['size'] and (s['value']&~1)<=addr<(s['value']&~1)+s['size']}
  def cstring(x,ptr):
   for s in x['sec'].values():
    if s['type']=='SHT_PROGBITS' and s['flags']&2 and s['addr']<=ptr<s['addr']+s['size']:
     tail=s['data'][ptr-s['addr']:]; end=tail.find(b'\0'); return tail[:end+1] if 0<=end<2048 else None
  data=[]; da=a['sec']['.data']['data']; db=b['sec']['.data']['data']; assert len(da)==len(db) and len(da)%4==0
  for off in range(0,len(da),4):
   va,vb=struct.unpack_from('<I',da,off)[0],struct.unpack_from('<I',db,off)[0]
   if va==vb: continue
   common=symbols_at(a,va)&symbols_at(b,vb)
   if common: reason={'same_symbol_offset':sorted(common)}
   else:
    aa,bb=cstring(a,va),cstring(b,vb); assert aa is not None and aa==bb,(off,hex(va),hex(vb)); reason={'identical_c_string_hex':aa.hex()}
   data.append(dict(offset=off,before=hex(va),after=hex(vb),**reason))
  d['data_pointer_attribution']=data
  unwind=[]
  for x in [a,b]:
   s=x['sec']['.ARM.exidx']; w,personality=struct.unpack('<II',s['data']); val=w&0x7fffffff; val=val-(1<<31) if val&(1<<30) else val
   unwind.append(dict(target=s['addr']+val,personality=personality))
  assert unwind[0]==unwind[1]; d['unwind_semantics']=unwind
  objdump='/home/staszek/.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-objdump'
  for side,x in [('base',a),('final',b)]:
   out=subprocess.check_output([objdump,'-dC',str(x['path']/'firmware.elf')],text=True); (q/'logs'/f'{side}-gateway-disassembly.log').write_text(out)
  d['disassembly_logs']=['logs/base-gateway-disassembly.log','logs/final-gateway-disassembly.log']
 result['boards'][board]=d
assert before=={str(p):sha(p.read_bytes()) for p in all_files}
result['pristine_files_verified']=len(before); (q/'artifact-audit.json').write_text(json.dumps(result,indent=2)+'\n'); print(json.dumps(result,indent=2))
