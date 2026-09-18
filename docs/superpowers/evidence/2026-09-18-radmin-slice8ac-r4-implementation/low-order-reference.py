#!/usr/bin/env python3
"""Independent Edwards25519 arithmetic: enumerate the complete canonical eight-torsion subgroup."""
import json
p=2**255-19; d=(-121665*pow(121666,p-2,p))%p; L=2**252+27742317777372353535851937790883648493
O=(0,1)
def add(a,b):
 x,y=a;u,v=b;t=d*x*u*y*v%p
 return ((x*v+y*u)*pow(1+t,p-2,p)%p,(y*v+x*u)*pow(1-t,p-2,p)%p)
def mul(n,a):
 out=O
 while n:
  if n&1:out=add(out,a)
  a=add(a,a);n>>=1
 return out
for y in range(2,100):
 z=(y*y-1)*pow(d*y*y+1,p-2,p)%p;x=pow(z,(p+3)//8,p)
 if x*x%p!=z:x=x*pow(2,(p-1)//4,p)%p
 if x*x%p!=z:continue
 t=mul(L,(x,y))
 if mul(8,t)==O and mul(4,t)!=O:break
else:raise AssertionError('no order-eight generator')
points=[mul(i,t) for i in range(8)];assert len(set(points))==8
encoded=[]
for x,y in points:
 assert (y*y-x*x-1-d*x*x*y*y)%p==0
 b=bytearray(y.to_bytes(32,'little'));b[31]|=(x&1)<<7;encoded.append(b.hex())
print(json.dumps({'method':'independent affine Edwards group arithmetic; exact order eight; all canonical encodings','points':encoded},indent=2))
