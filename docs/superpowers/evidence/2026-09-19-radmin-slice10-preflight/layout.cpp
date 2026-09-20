#include "node.h"
#include "device_nv.h"
char mr_abi_node[sizeof(meshroute::Node)];
char mr_abi_blob[sizeof(mrnv::Blob)];
char mr_abi_blob_align[alignof(mrnv::Blob)];
char mr_abi_activation_offset[offsetof(mrnv::Blob, remote_action_activation_ms)];
char mr_abi_intro_offset[offsetof(mrnv::Blob, intro_attach)];
char mr_abi_team_pub_offset[offsetof(mrnv::Blob, team_ch_pub)];
char mr_abi_team_id_offset[offsetof(mrnv::Blob, team_key_team_id)];
