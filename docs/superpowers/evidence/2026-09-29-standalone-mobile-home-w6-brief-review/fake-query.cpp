
#include "/home/staszek/MeshRoute/tools/probe_inbox_verbs/fakes/Preferences.h"
#include <cstdio>
#include <initializer_list>
int main(){unsigned char b[2852]{};for(size_t n: {size_t(2304),size_t(2305),size_t(2852)}){auto&nv=mrprobe_nv();nv.reset();nv.ns_present=true;nv.rw_ok=true;Preferences p;bool opened=p.begin("mr",false);size_t wrote=p.putBytes("ui",b,n);printf("requested=%zu opened=%d reported=%zu present=%d stored_len=%zu holds_exact=%d\n",n,opened,wrote,p.isKey("ui"),p.getBytesLength("ui"),nv.holds("mr","ui",b,n));}}
