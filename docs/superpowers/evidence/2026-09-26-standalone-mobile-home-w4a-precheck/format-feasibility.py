"""Mathematical measurement only, NOT a W4a implementation or gate.
For a name-only 14-cell stored display, prove a subsequent strictly narrower
six-cell formatting pass cannot consume the inserted high-byte marker.
"""
from pathlib import Path
import json
raw=Path('artifacts/2026-09-26-standalone-mobile-home-w4a-precheck')
def display(bs, cols):
 clean=bytes(b if 0x20<=b<0x7f else ord('.') for b in bs)
 return clean if len(clean)<=cols else clean[:cols-1]+bytes([0xbb])
cases=0
for b in range(256):
 for n in range(33):
  bs=bytes([b])*n
  assert display(display(bs,14),6)==display(bs,6)
  cases+=1
# Mixed inputs also put the literal raw 0xBB at every possible position.
for n in range(1,33):
 for i in range(n):
  bs=bytearray((ord('A')+j%26 for j in range(n)));bs[i]=0xbb
  assert display(display(bs,14),6)==display(bs,6)
  cases+=1
result={'kind':'hypothetical name-only formatting composition, not shipped code','checks':cases,'errors':0,'budgets':[14,6], 'constraints':['First pass reads the complete counted raw name (up to 32 bytes).','Name-only: no hash fallback in the stored InviteMember name.','Second budget is strictly smaller; never feed an inserted marker to an equal/wider sanitizing pass.','14-cell confirmation renders the stored result directly.','TEAM separately formats its source at six cells before snapshot/invalidation.'], 'examples':{x:display(x.encode(),6).hex() for x in ['Wolfgangetta','Wolfga','Wolfg']}}
(raw/'format-feasibility.json').write_text(json.dumps(result,indent=2)+'\n');print(result['checks'],'checks')
