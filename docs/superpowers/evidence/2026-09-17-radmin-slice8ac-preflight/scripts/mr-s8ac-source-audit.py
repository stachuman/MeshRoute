from pathlib import Path
import re,json,hashlib,subprocess
q=Path('/tmp/mr-codex-s8ac-preflight-active').read_text().strip();q=Path(q);r=q/'snapshot';rows=[]
checks={
 'lib/core/remote_codec.h':['remote_body_encode','remote_body_decode','remote_kdf_base','remote_kdf_session','remote_nonce','remote_aad','remote_body_cap','remote_make_request_id','RemoteEntropyFn','RemoteKeys','RemoteSource','RemoteCarrier','RemoteDecoded','result_kind','terminal','result_detail','admission','protocol_error'],
 'lib/core/remote_session.cpp':['derive_base','ed_pub_to_x25519','remote_ecdh_shared','remote_kdf_base'],
 'test/radmin_0e_candidate_types.h':['PendingRequestCore','PendingRequestInline','SessionCacheEntry','ResponseAssemblyHeader','ResponseChunk','RetainedResultHeader','AckDebtEntry','kCandidateSealedRequestBytes','kCandidateSealedAckBytes'],
 'lib/core/node.h':['rx_remote_resp_client','remote_inbound_stage','take_remote_inbound','RemoteInbound','_remote_inbound','radmin_rx_owner','rx_remote_cmd_accept','send_by_hash','do_send','send_remote_cmd','send_remote_response','kRadminExpiryTimerId'],
 'lib/core/node_mac_rx.cpp':['Node::rx_remote_resp_client','remote_inbound_stage'],
 'src/firmware_admin_keyring.h':['MgmtKeyService','IMgmtKeyUse','SecretWipeGuard'],
 'src/firmware_admin_targets.h':['ITargetStore','TargetRow','target_mask_by_label','ITargetUse'],
 'src/firmware_admin_identity.h':['admin_fp_hex'],
 'src/firmware_admin_client_verbs.h':['admin_client_ble_public','admin_client_ble_refuses','IClientRemoteDebt','client_regen_admitted'],
 'src/firmware_commands.cpp':['admin_client_router_arm','DeviceClientRemoteDebt','DeviceMgmtKeyUse','DeviceTargetUse','dump_status','exec_console_line'],
 'src/device_rng.h':['mrrng','sd_enabled','sd_rand_application_vector_get','esp_random'],
 'lib/core/hal.h':['rand_bytes'],
 'src/device_ble.h':['dispatch_current_line','g_out[256]','kProductLineMaxBytes','BANDWIDTH_MAX','tx_line','getMtu'],
 'src/fw_main.cpp':['take_remote_inbound','[rcmd ','LineSink ls(ble_sink)','exec_console_line'],
 'lib/console/console_json.h':['write_event','write_err','write_push'],
 'src/console_sink.h':['dropped_lines','return n;'],
 'tools/probe_inbox_verbs/run.sh':['MR_PROFILE_MOBILE'],
}
for f,needles in checks.items():
 s=(r/f).read_text()
 for x in needles:
  lines=[n+1 for n,line in enumerate(s.splitlines()) if x in line]
  rows.append(dict(file=f,symbol=x,lines=lines,present=bool(lines)))
(q/'source-anchors.json').write_text(json.dumps(rows,indent=2)+'\n')
print('Source anchors',sum(x['present'] for x in rows),'/',len(rows));print('Absent:',[x for x in rows if not x['present']])
