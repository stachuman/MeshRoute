#include <firmware_ui_model.h>
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    if(argc != 2) return 2;
    FILE* f=std::fopen(argv[1],"wb"); if(!f) return 2;
    char name[33]="ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
    unsigned calls=0, zero=0, nul=0, outside=0, far=0;
    for(uint32_t h:{0u,0xA0000011u}) for(unsigned n=0;n<33;++n)
    for(unsigned c=0;c<33;++c) for(unsigned cap=0;cap<49;++cap) {
        unsigned char mem[176]; std::memset(mem,0xA5,sizeof mem);
        mrui::ui_fmt_identity(reinterpret_cast<char*>(mem+64),cap,n?name:nullptr,n,h,c);
        ++calls; bool over=false;
        for(unsigned i=0;i<176;++i) if((i<64 || i>=64+cap) && mem[i]!=0xA5) {
            over=true; if(i>=64+cap && i-(64+cap)+1>far) far=i-(64+cap)+1;
        }
        if(cap==0) zero+=over; else {outside+=over; nul+=std::memchr(mem+64,0,cap)==nullptr;}
        if(std::fwrite(mem+64,48,1,f)!=1) return 2;
    }
    if(std::fclose(f)) return 2;
    std::printf("{\"calls\":%u,\"cap0_touched\":%u,\"positive_without_nul\":%u,\"outside\":%u,\"furthest\":%u}\n",calls,zero,nul,outside,far);
}
