#include "controller-model.h"
#include "node.h"
#include <type_traits>
char mr_abi_off_cfg[__builtin_offsetof(meshroute::Node,_cfg)+1];
char mr_abi_off_channel_counter[__builtin_offsetof(meshroute::Node,_channel_seal_ctr)+1];
#if MR_FEAT_RADMIN_CLIENT
#if S8_PROPOSED
char mr_abi_off_client[__builtin_offsetof(meshroute::Node,_remote_client)+1];
#else
char mr_abi_off_legacy[__builtin_offsetof(meshroute::Node,_remote_inbound)+1];
#endif
#endif
#if MR_FEAT_RADMIN_ACCEPT
char mr_abi_off_accept[__builtin_offsetof(meshroute::Node,_radmin_session)+1];
#endif
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::request_id), decltype(s8model::PendingCore::request_id)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::admin_epoch), decltype(s8model::PendingCore::admin_epoch)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::source_hash), decltype(s8model::PendingCore::source_hash)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::outcome_deadline_ms), decltype(s8model::PendingCore::outcome_deadline_ms)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::next_retry_ms), decltype(s8model::PendingCore::next_retry_ms)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::sealed_len), decltype(s8model::PendingCore::sealed_len)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::target_admin_pub), decltype(s8model::PendingCore::target_admin_pub)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::controller_pub), decltype(s8model::PendingCore::controller_pub)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::acl_slot), decltype(s8model::PendingCore::acl_slot)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::local_transport), decltype(s8model::PendingCore::local_transport)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::carrier), decltype(s8model::PendingCore::carrier)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::retries), decltype(s8model::PendingCore::retries)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::state), decltype(s8model::PendingCore::state)>);
static_assert(std::is_same_v<decltype(radmin0e::PendingRequestCore::flags), decltype(s8model::PendingCore::flags)>);
static_assert(std::is_same_v<decltype(radmin0e::SessionCacheEntry::admin_epoch), decltype(s8model::SessionEntry::admin_epoch)>);
static_assert(std::is_same_v<decltype(radmin0e::SessionCacheEntry::last_used_ms), decltype(s8model::SessionEntry::last_used_ms)>);
static_assert(std::is_same_v<decltype(radmin0e::SessionCacheEntry::target_admin_pub), decltype(s8model::SessionEntry::target_admin_pub)>);
static_assert(std::is_same_v<decltype(radmin0e::SessionCacheEntry::controller_pub), decltype(s8model::SessionEntry::controller_pub)>);
static_assert(std::is_same_v<decltype(radmin0e::SessionCacheEntry::base_key), decltype(s8model::SessionEntry::base_key)>);
static_assert(std::is_same_v<decltype(radmin0e::SessionCacheEntry::session_key), decltype(s8model::SessionEntry::session_key)>);
static_assert(std::is_same_v<decltype(radmin0e::SessionCacheEntry::acl_slot), decltype(s8model::SessionEntry::acl_slot)>);
static_assert(std::is_same_v<decltype(radmin0e::SessionCacheEntry::valid), decltype(s8model::SessionEntry::valid)>);
static_assert(std::is_same_v<decltype(radmin0e::ResponseAssemblyHeader::request_id), decltype(s8model::AssemblyHeader::request_id)>);
static_assert(std::is_same_v<decltype(radmin0e::ResponseAssemblyHeader::started_ms), decltype(s8model::AssemblyHeader::started_ms)>);
static_assert(std::is_same_v<decltype(radmin0e::ResponseAssemblyHeader::bytes_used), decltype(s8model::AssemblyHeader::bytes_used)>);
static_assert(std::is_same_v<decltype(radmin0e::ResponseAssemblyHeader::first_chunk), decltype(s8model::AssemblyHeader::first_chunk)>);
static_assert(std::is_same_v<decltype(radmin0e::ResponseAssemblyHeader::next_seq), decltype(s8model::AssemblyHeader::next_seq)>);
static_assert(std::is_same_v<decltype(radmin0e::ResponseAssemblyHeader::terminal_seen), decltype(s8model::AssemblyHeader::terminal_seen)>);
static_assert(std::is_same_v<decltype(radmin0e::ResponseAssemblyHeader::result_code), decltype(s8model::AssemblyHeader::result_code)>);
static_assert(std::is_same_v<decltype(radmin0e::ResponseAssemblyHeader::in_use), decltype(s8model::AssemblyHeader::in_use)>);
static_assert(std::is_same_v<decltype(radmin0e::RetainedResultHeader::request_id), decltype(s8model::RetainedHeader::request_id)>);
static_assert(std::is_same_v<decltype(radmin0e::RetainedResultHeader::completed_ms), decltype(s8model::RetainedHeader::completed_ms)>);
static_assert(std::is_same_v<decltype(radmin0e::RetainedResultHeader::bytes_total), decltype(s8model::RetainedHeader::bytes_total)>);
static_assert(std::is_same_v<decltype(radmin0e::RetainedResultHeader::first_chunk), decltype(s8model::RetainedHeader::first_chunk)>);
static_assert(std::is_same_v<decltype(radmin0e::RetainedResultHeader::local_transport), decltype(s8model::RetainedHeader::local_transport)>);
static_assert(std::is_same_v<decltype(radmin0e::RetainedResultHeader::delivered), decltype(s8model::RetainedHeader::delivered)>);
static_assert(std::is_same_v<decltype(radmin0e::RetainedResultHeader::reoffer_from_zero), decltype(s8model::RetainedHeader::reoffer_from_zero)>);
static_assert(std::is_same_v<decltype(radmin0e::RetainedResultHeader::in_use), decltype(s8model::RetainedHeader::in_use)>);
static_assert(std::is_same_v<decltype(radmin0e::AckDebtEntry::request_id), decltype(s8model::AckEntry::request_id)>);
static_assert(std::is_same_v<decltype(radmin0e::AckDebtEntry::next_retry_ms), decltype(s8model::AckEntry::next_retry_ms)>);
static_assert(std::is_same_v<decltype(radmin0e::AckDebtEntry::target_admin_pub), decltype(s8model::AckEntry::target_admin_pub)>);
static_assert(std::is_same_v<decltype(radmin0e::AckDebtEntry::sealed_ack), decltype(s8model::AckEntry::sealed_ack)>);
static_assert(std::is_same_v<decltype(radmin0e::AckDebtEntry::acl_slot), decltype(s8model::AckEntry::acl_slot)>);
static_assert(std::is_same_v<decltype(radmin0e::AckDebtEntry::retries), decltype(s8model::AckEntry::retries)>);
static_assert(std::is_same_v<decltype(radmin0e::AckDebtEntry::in_use), decltype(s8model::AckEntry::in_use)>);
static_assert(sizeof(s8model::PendingCore::discovery_id) == 8);
static_assert(sizeof(s8model::PendingCore::route) == 8);
static_assert(sizeof(s8model::PendingCore::credential_slot) == 1);
static_assert(sizeof(s8model::PendingCore::target_book_slot) == 1);
static_assert(sizeof(s8model::SessionEntry::credential_slot) == 1);
static_assert(sizeof(s8model::SessionEntry::target_book_slot) == 1);
static_assert(sizeof(s8model::AssemblyHeader::result_domain) == 1);
static_assert(sizeof(s8model::AssemblyHeader::reserved) == 1);
static_assert(sizeof(s8model::AssemblyHeader::result_detail) == 4);
static_assert(sizeof(s8model::RetainedHeader::result_domain) == 1);
static_assert(sizeof(s8model::RetainedHeader::result_code) == 1);
static_assert(sizeof(s8model::RetainedHeader::result_detail) == 4);
static_assert(sizeof(s8model::AckEntry::source_hash) == 4);
static_assert(sizeof(s8model::AckEntry::route) == 8);
static_assert(sizeof(s8model::AckEntry::credential_slot) == 1);
static_assert(sizeof(s8model::AckEntry::target_book_slot) == 1);
static_assert(sizeof(s8model::AckEntry::carrier) == 1);
static_assert(sizeof(s8model::AckEntry::reserved) == 1);
static_assert(sizeof(s8model::Counters::request_table_pressure) == 2);
static_assert(sizeof(s8model::Counters::assembly_failure) == 2);
static_assert(sizeof(s8model::Counters::unmatched_response) == 2);
static_assert(sizeof(s8model::Counters::auth_failure) == 2);
static_assert(sizeof(s8model::Counters::local_result_pressure) == 2);
static_assert(sizeof(s8model::Counters::radio_enqueue_failure) == 2);
