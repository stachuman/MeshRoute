#include <cstdio>
#include <cstddef>
#include "device_nv.h"
#include "node.h"
int main(){ printf("Blob=%zu align=%zu intro=%zu team_ch_pub=%zu team_key_team_id=%zu ract=%zu\n", sizeof(mrnv::Blob), alignof(mrnv::Blob), offsetof(mrnv::Blob,intro_attach), offsetof(mrnv::Blob,team_ch_pub), offsetof(mrnv::Blob,team_key_team_id), offsetof(mrnv::Blob,remote_action_activation_ms)); printf("Node=%zu\n", sizeof(meshroute::Node)); return 0; }
