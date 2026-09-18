// Source-validation of the brief's claim that send_ready requests E2E ACK only for execute.
#include "firmware_remote_client.h"
#include "frame_codec.h"
#include <cstdio>
#include <cstring>
using namespace meshroute;
struct C:IRadminCarrier {
 unsigned calls=0;bool requested=false;uint8_t opcode=0;
 bool tx_queue_full()const override{return false;}
 RemoteClientSend submit_request(const RemoteClientRoute&,RemoteSource,const RemoteCarrier&,std::span<const uint8_t> b,bool a)override{++calls;requested=a;RemoteLayout layout{};if(remote_layout(DATA_TYPE_REMOTE_CMD,b[0],layout)==RemoteStatus::ok)opcode=layout.opcode;return RemoteClientSend::queued;}
 RemoteClientSend submit_ack(const RemoteClientRoute&,RemoteSource,const RemoteCarrier&,std::span<const uint8_t>)override{return RemoteClientSend::queued;}
};
bool entropy(void* p,uint8_t* out,size_t n){uint64_t x=++*static_cast<uint64_t*>(p);for(size_t i=0;i<n;++i)out[i]=static_cast<uint8_t>(x>>(i*8));return true;}
int main(){unsigned checks=0,failed=0;auto ck=[&](bool ok,const char* text){++checks;if(!ok){++failed;printf("FAIL %s\n",text);}};Identity self{},target{};uint8_t seed[32];for(unsigned i=0;i<32;++i)seed[i]=i+1;identity_from_seed(self,seed);for(unsigned i=0;i<32;++i)seed[i]=i+77;identity_from_seed(target,seed);
for(const char* text:{"remote t -e rollover","remote t -e -a rollover","remote t -e rollover confirm","remote t -e -a rollover confirm"}){
 mrfw::RemoteLocalCommand local{};ck(mrfw::remote_local_parse(text,strlen(text),local),"real parser accepts");const bool want=strstr(text,"-a")!=nullptr;ck(local.ack==want,"parsed ack flag");const auto op=strstr(text,"confirm")?RemoteCmdOpcode::force_rollover:RemoteCmdOpcode::safe_rollover;ck(local.opcode==op,"rollover opcode");
 RemoteClientState s{};auto& e=s.sessions[0];e.valid=2;e.admin_epoch=9;e.acl_slot=3;e.credential_slot=kRemoteClientSelf;memcpy(e.controller_pub,self.ed_pub,32);memcpy(e.target_admin_pub,target.ed_pub,32);ck(remote_client_base(e.base_key,self,target.ed_pub)==RemoteStatus::ok,"base key");ck(remote_kdf_session(e.session_key,e.base_key,e.admin_epoch)==RemoteStatus::ok,"session key");
 RemoteClientRequest req{};req.identity=&self;req.target_pub={target.ed_pub,32};req.source_hash=self.key_hash32;req.route.target_hash=target.key_hash32;req.opcode=local.opcode;req.e2e_ack=local.ack;req.correlation_free=8;req.carrier=1;C c;uint64_t id=100;auto result=remote_client_start(s,req,1,entropy,&id,c);ck(result.error==RemoteClientError::none,"real controller admits");ck(c.calls==1,"one carrier call");ck(c.opcode==static_cast<uint8_t>(op),"actual sealed control opcode");ck(c.requested==want,"existing control leaks supplied ack flag");printf("%s -> submit_request(e2e_ack=%s)\n",text,c.requested?"true":"false");}
printf("control ACK characterization: %u checks / %u failed\n",checks,failed);return failed?1:0;}
