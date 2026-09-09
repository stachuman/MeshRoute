from pathlib import Path
import contextlib,hashlib,io,json,sys,types
from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_decrypt as opened
reference_path=Path(sys.argv[1]).resolve()
source=reference_path.read_text()
assert hashlib.sha256(source.encode()).hexdigest() == "ca485257119e1fb01ede94d5679ec40ac52bad2dc8ee22faf643b1f709227d96"
assert source.count("(op << 4) | 3") == 1
# Independent direct transcription of R-RA-36's permitted code/request pairs.
allowed={(0,0),(1,0),(1,4),(1,5),(2,4),(3,4),(3,5),(4,4),(4,5)}
def legal(w):
 return w[0]>>4==5 and w[0]&15<=9 and w[9]&15==w[0]&15 and (w[10],w[9]>>4) in allowed and (w[11]>0 if w[10]==2 else w[11]==0)
def get(source_text):
 module=types.ModuleType("qa_reference")
 module.__file__=str(reference_path)
 exec(compile(source_text, str(reference_path), "exec"), module.__dict__)
 with contextlib.redirect_stdout(io.StringIO()):
  ref=module.original()
  vectors=module.vectors(ref)
 return ref,vectors
ref,new=get(source)
# Labelled synthetic B384 control in memory; the delivered reference stays unchanged.
_,bad=get(source.replace("(op << 4) | 3", "op | 3"))
assert new['kAdmissionRefValid']==bad['kAdmissionRefValid']
positives=new['kAdmissionRefValid']; negatives=new['kAdmissionRefInvalid']
for i in range(0,len(positives),97):
 row=positives[i:i+97]; w=row[69:]; assert legal(w); assert opened(w[12:],row[36:53],row[12:36],ref.SESSION_KEY)==b''
for i in range(0,len(negatives),28):
 w=negatives[i:i+28]; assert not legal(w)
 pre=b'MeshRoute remote-admin v2 nonce'+ref.SESSION_KEY+b'\xa1'+w[:9]+b'\x00'+ref.SRC_HASH.to_bytes(4,'little')+w[9:12]
 nonce=hashlib.blake2b(pre,digest_size=64).digest()[:24]; aad=b'\xa1'+w[:12]+ref.SRC_HASH.to_bytes(4,'little')
 assert opened(w[12:],aad,nonce,ref.SESSION_KEY)==b''
wrong=bad['kAdmissionRefInvalid']; mislabeled=[w.hex() for i in range(0,len(wrong),28) if legal(w:=wrong[i:i+28])]
assert mislabeled
out={'valid':len(positives)//97,'valid_tag_semantically_invalid':len(negatives)//28,'b384_control_mislabeled_valid':len(mislabeled),'examples':mislabeled,'positive_hash':hashlib.sha256(positives).hexdigest(),'new_negative_hash':hashlib.sha256(negatives).hexdigest()}
print(json.dumps(out,indent=2))
