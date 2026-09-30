// Labelled synthetic nRF-like file: real fs_read_slot and exact-record validator.
// This does not compile the nRF platform adapter or run a hardware filesystem.
#include "device_nv.h"
#include <cstdio>
#include <cstring>
struct FileFacts {
    static constexpr int kAbsentRc=-2, kFoundRc=0;
    unsigned char data[81]{}; int size_queries=0;
    bool mount(){return true;} bool open(const char*){return true;}
    int lookup(const char*){return 0;} unsigned size(){++size_queries;return sizeof data;}
    int read(void* out,size_t cap){size_t n=cap<sizeof data?cap:sizeof data;memcpy(out,data,n);return int(n);}
    void close(){}
};
int main(){
    mrnv::IdBlob in{};in.magic=mrnv::kIdMagic;in.version=mrnv::kIdVersion;
    FileFacts fs;memcpy(fs.data,&in,sizeof in);fs.data[80]=0xA5;
    mrnv::IdBlob out{};int n=mrnv::fs_read_slot(fs,"/mrid",&out,sizeof out,nullptr);
    bool accepted=mrnv::blob_valid_exact(out,n,mrnv::kIdMagic,mrnv::kIdVersion);
    printf("file_bytes=%zu record_bytes=%zu read_bytes=%d size_queries=%d accepted=%d\n",sizeof fs.data,sizeof out,n,fs.size_queries,accepted);
    return !(accepted && n==80 && fs.size_queries==0);
}
