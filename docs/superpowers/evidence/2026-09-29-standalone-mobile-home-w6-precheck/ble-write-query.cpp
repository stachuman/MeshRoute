
#include <cstdint>
#include <cstddef>
#include <string>
#include <cstdio>
struct BLEConnection {uint16_t getMtu(){return 247;}} conn;
struct Fruit {BLEConnection* Connection(uint16_t){return &conn;}} Bluefruit;
struct Uart {int calls=0,fail_after=10000;std::string wire;size_t write(const uint8_t*p,size_t n){++calls;if(calls>fail_after)return 0;wire.append((const char*)p,n);return n;}} g_bleuart;
uint8_t g_conn_count=1;uint16_t g_conn_handle=1;
inline void tx_line(const char* s, size_t n) {
    if (g_conn_count == 0) return;
    // A single g_bleuart.write() emits ONE notification of at most (ATT MTU − 3) bytes; a longer line (cfg
    // ~290 B, a wide status, a long inbox_dm) would lose its tail. Chunk by the negotiated MTU so EACH write
    // is a clean single notification; the app's LineAccumulator reassembles by '\n' regardless of the splits.
    BLEConnection* conn = Bluefruit.Connection(g_conn_handle);
    const uint16_t mtu  = conn ? conn->getMtu() : 23;
    const size_t   chunk = (mtu > 3) ? static_cast<size_t>(mtu - 3) : 20;
    size_t off = 0;
    while (off < n) {
        const size_t len = (n - off < chunk) ? (n - off) : chunk;
        g_bleuart.write(reinterpret_cast<const uint8_t*>(s + off), len);
        off += len;
    }
}
int main(){size_t supplied=0;g_bleuart.fail_after=3;
const size_t sizes[]={243,237,237,237,237,237,237,237,237,242,242,242,242,242,242,242,242,110};
for(size_t n:sizes){std::string line(n,'x');line.back()='\n';supplied+=n;tx_line(line.data(),line.size());}
printf("{\"supplied_bytes\":%zu,\"calls\":%d,\"accepted_bytes\":%zu,\"lost_bytes\":%zu,\"write_failures\":%d}\n",supplied,g_bleuart.calls,g_bleuart.wire.size(),supplied-g_bleuart.wire.size(),g_bleuart.calls-3);
return g_bleuart.calls==18&&g_bleuart.wire.size()==717?0:1;}

