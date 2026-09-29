"""Labelled synthetic write-failure proof using the verbatim production tx_line body. No BLE hardware claim."""
from pathlib import Path
import re,subprocess,tempfile,json,hashlib
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;D=Path(tempfile.mkdtemp(prefix='w6-ble-write-'))
s=(R/'src/device_ble.h').read_text();start=s.index('inline void tx_line(const char* s, size_t n) {');end=start;depth=0
for i in range(s.index('{',start),len(s)):
 if s[i]=='{':depth+=1
 elif s[i]=='}':
  depth-=1
  if depth==0:end=i+1;break
body=s[start:end]
cpp=r'''
#include <cstdint>
#include <cstddef>
#include <string>
#include <cstdio>
struct BLEConnection {uint16_t getMtu(){return 247;}} conn;
struct Fruit {BLEConnection* Connection(uint16_t){return &conn;}} Bluefruit;
struct Uart {int calls=0,fail_after=10000;std::string wire;size_t write(const uint8_t*p,size_t n){++calls;if(calls>fail_after)return 0;wire.append((const char*)p,n);return n;}} g_bleuart;
uint8_t g_conn_count=1;uint16_t g_conn_handle=1;
@BODY@
int main(){size_t supplied=0;g_bleuart.fail_after=3;
const size_t sizes[]={243,237,237,237,237,237,237,237,237,242,242,242,242,242,242,242,242,110};
for(size_t n:sizes){std::string line(n,'x');line.back()='\n';supplied+=n;tx_line(line.data(),line.size());}
printf("{\"supplied_bytes\":%zu,\"calls\":%d,\"accepted_bytes\":%zu,\"lost_bytes\":%zu,\"write_failures\":%d}\n",supplied,g_bleuart.calls,g_bleuart.wire.size(),supplied-g_bleuart.wire.size(),g_bleuart.calls-3);
return g_bleuart.calls==18&&g_bleuart.wire.size()==717?0:1;}

'''.replace('@BODY@',body)
(D/'query.cpp').write_text(cpp);subprocess.run(['g++','-std=c++20',str(D/'query.cpp'),'-o',str(D/'query')],check=True)
x=json.loads(subprocess.check_output([str(D/'query')]))
(E/'ble-write-query.cpp').write_text(cpp);(E/'ble-write-measure.json').write_text(json.dumps(dict(scope=__doc__,synthetic_fault='Three successful notification writes, then write returns 0 (models timeout/failure, not ordinary queue timing). Body unchanged.',source_sha256=hashlib.sha256(s.encode()).hexdigest(),function_sha256=hashlib.sha256(body.encode()).hexdigest(),result=x),indent=2)+'\n');print(x)
