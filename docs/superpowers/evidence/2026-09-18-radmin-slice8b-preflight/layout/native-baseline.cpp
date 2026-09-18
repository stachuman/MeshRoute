#include "/tmp/mr-s8b-r2-ksqkjdbw/snapshot/lib/core/node.h"
#include <cstddef>
char mr_abi_core[sizeof(meshroute::RemoteClientPendingCore)];
char mr_abi_row[sizeof(meshroute::RemoteClientPending)];
char mr_abi_state[sizeof(meshroute::RemoteClientState)];
char mr_abi_state_align[alignof(meshroute::RemoteClientState)];
char mr_abi_node[sizeof(meshroute::Node)];
char mr_abi_slot_offset[offsetof(meshroute::RemoteClientPendingCore,target_book_slot)+1];