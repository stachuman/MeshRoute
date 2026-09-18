#!/usr/bin/env python3
"""Independent controller identity/KDF/request vectors: hashlib + libsodium, no production imports."""
import hashlib,json
import nacl.bindings as sodium

def identity(seed):
    secret=bytearray(hashlib.blake2b(seed,digest_size=64).digest()[:32])
    secret[0]&=248;secret[31]&=127;secret[31]|=64
    public=sodium.crypto_scalarmult_ed25519_base_noclamp(bytes(secret))
    return bytes(secret),public
cs,cp=identity(bytes(range(1,33)))
ts,tp=identity(bytes(range(77,109)))
shared=sodium.crypto_scalarmult(cs,sodium.crypto_sign_ed25519_pk_to_curve25519(tp))
base=hashlib.blake2b(b'MeshRoute remote-admin v2 base'+shared+cp+tp,digest_size=64).digest()[:32]
session=hashlib.blake2b(b'MeshRoute remote-admin v2 session'+base+(9).to_bytes(8,'little'),digest_size=64).digest()[:32]
header=bytes([3])+(100).to_bytes(8,'little')
aad=b'\xa0'+header+cp[:4]
nonce=hashlib.blake2b(b'MeshRoute remote-admin v2 nonce'+session+b'\xa0'+header+b'\x00'+cp[:4],digest_size=64).digest()[:24]
body=header+sodium.crypto_aead_xchacha20poly1305_ietf_encrypt(b'status',aad,nonce,session)
print(json.dumps({k:v.hex() for k,v in dict(controller_pub=cp,target_pub=tp,shared=shared,base=base,session=session,body=body).items()},indent=2))
