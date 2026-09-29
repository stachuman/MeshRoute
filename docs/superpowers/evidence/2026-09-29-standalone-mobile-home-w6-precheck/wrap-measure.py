"""Independent specification arithmetic and word-wrap reference. Not product implementation or a product test gate."""
from pathlib import Path
import itertools,json,random
E=Path(__file__).resolve().parent
checks=0

def wrap(data):
 out=[];s=0
 while s<len(data):
  if len(data)-s<=19:n=len(data)-s
  elif data[s+19]==32 or data[s+18]==32:n=19
  else:
   p=data[s:s+19].rfind(b' ');n=p+1 if p>=0 else 19
  out.append(data[s:s+n]);s+=n
 return out or [b'']

def check(s):
 global checks
 rows=wrap(s)
 assert b''.join(rows)==s
 assert all(len(x)<=19 for x in rows)
 assert (len(rows)+2)//3<=6
 assert all(len(a)+len(b)>=20 for a,b in zip(rows,rows[1:-1]))
 checks+=4
for n in range(164):
 for s in [b'X'*n,b' '*n,(b' '+b'X'*19)*9,(b'X '*82)[:n]]:check(s[:n])
# Exhaust every break mask over the 20-byte decision boundary; a following non-space suffix exercises the next line.
for bits in range(1<<20):
 s=bytes(32 if bits>>i&1 else 88 for i in range(20))+b'X'*20
 check(s)
rng=random.Random(0x5736)
for i in range(10000):check(bytes(rng.choice([32,65,66,67]) for _ in range(rng.randrange(164))))
witness=(b' '+b'X'*19)*8+b' XX';rows=wrap(witness)
assert len(witness)==163 and len(rows)==17
x=dict(scope=__doc__,property_checks=checks,worst_case_witness=witness.decode(),worst_case_line_lengths=list(map(len,rows)),pages=(len(rows)+2)//3,send_line={'promoted_channel_digits':10,'derived_storage':len('send_channel ')+10+len(' "')+163+len('" -t -l -e')+1,'actual_255_storage':len('send_channel 255 "')+163+len('" -t -l -e')+1},default_len=len('Return to base now'),default_projection_hex=('Return to base now'[:16].encode()+b'\xbb').hex())
(E/'wrap-measure.json').write_text(json.dumps(x,indent=2)+'\n');print(json.dumps(x,indent=2))
