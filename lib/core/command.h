// MeshRoute — lib/core/command.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The app<->firmware command seam. TYPED, bounded, no-heap, transport-agnostic —
// the message path is NOT string-parsed (that is the MeshCore anti-pattern; their
// text CLI is config-only, the message hot path is a typed cmd-code frame). Each
// backend (sim FirmwareNode / device serial-BLE) parses its own wire INTO these
// PODs and calls Node::on_command; lib/core never sees a transport byte.
//
// Mirrors hal.h's discipline: typed PODs in, no std::string/json/heap, bodies
// BORROWED for the call only. See docs/specs/2026-05-30-command-interface-design.md.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include "protocol_constants.h"
#include <cstddef>
#include <cstdint>

namespace MESHROUTE_NS {

// ---- requests (one cmd-code + a bounded typed payload, like a MeshCore frame) ----
enum class CmdKind : uint8_t { send, send_layer, send_channel, join, resolve, reqpubkey, peerkey };
// E2E §8b: per-message crypt intent. `def` follows the node's `e2e_dm`; `on`/`off` force a single DM CRYPTED/plain
// (the seal gate = want_crypt = (crypt==on)?true : (crypt==off)?false : _cfg.e2e_dm). Console: sendhashx=on, sendhash=off.
enum class CryptIntent : uint8_t { def = 0, on, off };

// The four Lua send_* verbs collapse to ONE Send + flag bits (same wire bits as
// dv_dual_sf.lua:2187-2189). Addressed by short id (now) / key_hash32 (later) —
// never a name (the device has no name map; that is forever a backend concern).
// Plain PODs (no in-class initializers) so the union has a trivial default ctor and
// the header stays C++17-includable by the sim (hal.h discipline). flags = wire DATA_FLAG_* (E2E_ACK_REQ=0x10, DST_HASH=0x02, PRIORITY=0x01; 0x08 free).
struct SendCmd        { uint8_t dst_id; uint32_t dst_hash; uint8_t flags; };
struct SendLayerCmd   { uint8_t hops[protocol::gw_env_max_hops]; uint8_t hop_count; uint32_t dst_hash; uint8_t flags; };   // flags honored on the cross-layer DM (E2E_ACK_REQ -> Y acks via the reversed path, Slice 4d/e2e)
struct SendChannelCmd { uint8_t channel_id; };
struct JoinCmd        { enum Op : uint8_t { discover, claim, deny } op; uint8_t node_id; uint32_t claimant_hash; };
// Diagnostic: locate the node owning key_hash32 (the hash-locate H flood); the answer rides
// PushKind::hash_resolved. hard = skip caches, reach the owner (verify-on-use). NO body — notify-only,
// distinct from a send-by-hash (which carries a DM and rides CmdKind::send with dst_hash set).
struct ResolveCmd     { uint32_t dst_hash; bool hard; };
// E2E §3 (QR import): install a scanned peer's full Ed25519 pubkey as a PINNED (verified) key. key_hash32 = ed_pub[:4]
// is derived (never trusted from the wire), so only the 32-byte pubkey rides the command.
struct PeerkeyCmd     { uint8_t ed_pub[32]; };

struct Command {
    CmdKind kind = CmdKind::send;
    union {                              // value-init (Command c{}) zero-inits `send` (the first arm)
        SendCmd        send;
        SendLayerCmd   layer;
        SendChannelCmd channel;
        JoinCmd        join;
        ResolveCmd     resolve;
        PeerkeyCmd     peerkey;
    } u;
    const uint8_t* body     = nullptr;   // BORROWED for the call only (mirrors hal.h on_recv)
    uint8_t        body_len = 0;
    CryptIntent    crypt    = CryptIntent::def;   // §8b: per-message crypt override (send/sendhash = def/off, sendhashx = on)
};

// ---- synchronous result (the token; matches MeshCore RESP_CODE_*) ----
enum class CmdCode : uint8_t { queued, err_unknown_dst, err_too_large,
                               err_no_gateway, err_priority_capped, err_no_binding, err_unsupported,
                               err_unprovisioned,    // node_id==0: must join or `cfg set node_id` first
                               err_no_data_sf };     // allowed_sf_bitmap==0: configure sf_list before sending data
// The synchronous "send handle" — the app records it and correlates async send_acked/send_failed pushes by `ctr`.
// dst_hash / layer_path echo WHAT was sent so the app keeps no command->identity map of its own (and so a small
// hash like 0x10 is NEVER confused with an 8-bit id — it lives in its own 32-bit field):
//   send <id>            -> ctr, dst_hash=0,    layer_path=0
//   sendhash <hash>      -> ctr, dst_hash=hash, layer_path=0
//   send_layer <hash> <l..> -> ctr, dst_hash=hash, layer_path = the hops packed MSB-first (hops[0] high byte;
//                              [2,3] -> (2<<8)|3 = 0x0203; 0 = no layers). Layer ids are >=1 so no leading-zero hop.
struct CmdResult {
    CmdCode  code        = CmdCode::queued;
    uint16_t ctr         = 0;
    uint8_t  queue_depth = 0;
    uint32_t dst_hash    = 0;   // hash/layer-addressed sends: the target key_hash32 (0 = id-addressed)
    uint32_t layer_path  = 0;   // send_layer: the destination layer path packed MSB-first (0 = not a layer send)
};

// ---- async push channel (delivery/ACK/inbound; matches MeshCore PUSH_CODE_*) ----
// Drained by the transport via Node::next_push (CMD_SYNC_NEXT-style). The Node owns
// a bounded ring (cap_push_ring), drop-oldest on overflow (MeshCore offline queue).
enum class PushKind : uint8_t {
    msg_recv,      // a DM was delivered to US (origin/body = the inbound text)
    channel_recv,  // a NEW channel message was received (origin=minter, channel_id, body=text)
    send_acked,    // our send's link ACK returned (ctr = the sent message id)
    send_failed,   // our send gave up (ctr = the sent message id)
    hash_resolved, // a `resolve` completed: origin = owner node_id (0 = unresolved/timeout),
                   // dst = authoritative?1:0, body[0..3] = the queried key_hash32 (LE, 4 B)
    peer_key_cached, // E2E §7: a recipient's pubkey was learned (on-air answer / cache-on-pass) -> the app can
                     //   resend an encrypted DM. sender_hash = the cached key_hash32; pinned=false (on-air, TOFU).
    config_adopted,  // R6.2: a CONFIG_ANSWER was adopted (lineage/epoch/sf_list/duty/name changed) -> device persists to NV.
    join_refused,    // R6.3 §7c: a join was refused (wire_version mismatch / leaf full) -> console + companion (telemetry is invisible on metal).
};
// E2E §5: why a send_failed Push fired, so the app reacts (no_pubkey -> offer Request-key/Scan-QR; the permanent
// reasons -> plain fail). Mirrors the contract `send_failed.reason`. `none` = a non-send_failed push.
enum class SendFailReason : uint8_t { none = 0, no_pubkey, no_identity, too_large, bad_rng, no_route, joining };   // R6.2: joining = un-synced managed leaf
// R6.3 §7c: why a join was refused (join_refused push). wire_version -> origin=their_ver, dst=my_ver; leaf_full -> no extra.
enum class JoinRefuseReason : uint8_t { wire_version = 0, leaf_full = 1 };
struct Push {
    PushKind kind = PushKind::msg_recv;
    SendFailReason reason = SendFailReason::none;   // send_failed only (else none)
    JoinRefuseReason join_reason = JoinRefuseReason::wire_version;   // join_refused only
    uint8_t  origin = 0;
    uint8_t  dst = 0;
    uint8_t  channel_id = 0;   // channel_recv only
    uint8_t  layer_id = 0;     // msg_recv/channel_recv: the FULL 8-bit receiving layer id (§2/Q13 — disambiguates origin across a gateway's leaves)
    bool     enc = false;      // §8b: msg_recv -> the DM was delivered SEALED (CRYPTED + opened); channel_recv -> false (cleartext today)
    uint16_t ctr = 0;
    uint32_t sender_hash = 0;      // msg_recv: the DM sender's stable key_hash32 (0 = no SOURCE_HASH). The app's
                                   //   DM dedup identity is (sender_hash, ctr) when set, else (origin, ctr).
    uint32_t channel_msg_id = 0;   // channel_recv: the FULL 32-bit channel message id (the app's dedup identity)
    uint32_t seq = 0;              // msg_recv/channel_recv: the inbox per-store seq (0 = inbox disabled -> omit).
                                   //   The app unifies live + pulled by seq + detects a dropped live push (model B).
    bool     has_location = false; // msg_recv: the sender piggybacked a 6-B location (DATA_FLAG_LOCATION).
    int32_t  lat_e7 = 0, lon_e7 = 0;  //   deg×1e7 (~11 m), valid iff has_location. (M receive deferred.)
    uint8_t  body[protocol::max_payload_bytes_hard_cap] = {};   // msg_recv / channel_recv text (empty otherwise)
    uint8_t  body_len = 0;
};

}  // namespace meshroute
