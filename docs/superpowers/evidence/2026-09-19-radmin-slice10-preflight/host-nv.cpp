#include "device_nv.h"
#include <cstdio>
int main(){mrnv::Blob b{}; b.magic=mrnv::kMagic;b.version=mrnv::kVersion; bool l=mrnv::load(b),w=mrnv::save(b);std::printf("host load=%d save=%d\n",l,w);return l||w;}
