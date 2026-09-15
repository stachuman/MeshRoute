#include "node.h"
char mr_abi_size__meshroute_Node[sizeof(meshroute::Node)];
char mr_abi_align__meshroute_Node[alignof(meshroute::Node)];
char mr_abi_size__meshroute_RemoteSessionState[sizeof(meshroute::RemoteSessionState)];
char mr_abi_align__meshroute_RemoteSessionState[alignof(meshroute::RemoteSessionState)];
char mr_abi_size__meshroute_TranscriptHeader[sizeof(meshroute::TranscriptHeader)];
char mr_abi_align__meshroute_TranscriptHeader[alignof(meshroute::TranscriptHeader)];
char mr_abi_size__meshroute_QaDeferredActionRecord[sizeof(meshroute::QaDeferredActionRecord)];
char mr_abi_align__meshroute_QaDeferredActionRecord[alignof(meshroute::QaDeferredActionRecord)];
static_assert(sizeof(meshroute::QaActionKind) == 1);
static_assert(sizeof(meshroute::QaActionBackend) == 1);
#if MR_FEAT_RADMIN_ACCEPT
char mr_abi_included__accept_action_resident[1];
#else
char mr_abi_included__client_no_action_resident[1];
#endif
