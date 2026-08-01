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
// ★ §AB2: `peername` is APPENDED AT THE END. The sim references these enumerators BY NAME (it includes this header,
// unlike PushKind/SendFailReason which it bridges on the raw uint8_t), so an insert would not silently renumber a
// scenario — but appending costs nothing and keeps ONE rule for every enum in this file.
enum class CmdKind : uint8_t { send, send_layer, send_channel, join, resolve, reqpubkey, peerkey, peername };
// E2E §8b: per-message crypt intent. `def` follows the node's `e2e_dm`; `on`/`off` force a single DM CRYPTED/plain
// (the seal gate = want_crypt = (crypt==on)?true : (crypt==off)?false : _cfg.e2e_dm). Console: sendhashx=on, sendhash=off.
enum class CryptIntent : uint8_t { def = 0, on, off };

// The four Lua send_* verbs collapse to ONE Send + flag bits (same wire bits as
// dv_dual_sf.lua:2187-2189). Addressed by short id (now) / key_hash32 (later) —
// never a name (the device has no name map; that is forever a backend concern).
// Plain PODs (no in-class initializers) so the union has a trivial default ctor and
// the header stays C++17-includable by the sim (hal.h discipline). flags = wire DATA_FLAG_* (E2E_ACK_REQ=0x10, LOCATION=0x08,
// DST_HASH=0x02, PRIORITY=0x01). ★ §loc-per-send (2026-07-31): 0x08 is NO LONGER FREE — DATA_FLAG_LOCATION is how the app
// asks "attach my position to THIS message" (console `-l`), replacing the removed `loc_dm` config toggle whose missing crypt
// gate aired coordinates in the clear (register B0). Set it on `send` only; on `send_layer` it is refused (no cross-layer
// builder can carry a position), and `send_channel` has no `-l` yet — ✖ MISSING, TRIGGER: CL2. ⚠ V1 (2026-07-31): the
// reason was *"a channel location is an alternative inner TYPE, not an added field"*; the owner STRUCK that (spec §2.2.1)
// — `send_channel -t -l -e` IS wanted. The blocker is CL2's payload format (T-K2's `[inner_type]` XOR must become a FLAGS
// byte before text+position can coexist). Node::enqueue_data validates it and REFUSES the send if the DM would not be sealed.
struct SendCmd        { uint8_t dst_id; uint32_t dst_hash; uint8_t flags; uint8_t plane; };   // Wave 2: plane 0=AUTO (companion/sim default -> today's cascade), 1=TEAM (`-t`), 2=GLOBAL (plain `send`). Host-side only, NOT on the wire.
struct SendLayerCmd   { uint8_t hops[protocol::gw_env_max_hops]; uint8_t hop_count; uint32_t dst_hash; uint8_t flags; };   // flags honored on the cross-layer DM (E2E_ACK_REQ -> Y acks via the reversed path, Slice 4d/e2e)
// ★ §chan-crypt CL2b: `loc` = the console `-l`, "attach THIS node's position to THIS post". It gets a field of its
// own rather than riding a flags word: `SendCmd::flags` carries DATA_FLAG_LOCATION because a DM's location IS a DATA
// frame flag, but a channel post has no DATA flags at all — its position rides the SEALED INNER's flags byte
// (protocol::channel_inner_flag_location), which is a different wire field with a different width. Borrowing the DATA
// bit here would name a wire field this command never touches. Costs 0 bytes of `Command` (the union is sized by
// SendLayerCmd) and defaults false, so the two hand-built producers (`testch`, the sim wrapper) are inert by
// construction. on_command refuses it whenever the post would NOT actually be sealed (spec §2.2.1, ruling O6).
struct SendChannelCmd { uint8_t channel_id; bool team; bool global; bool loc; };   // §S7 T-B DM-symmetric plane select: team=`-t` (TEAM), global=`-g` (explicit GLOBAL). Plain (neither) => GLOBAL. `-t -g` => BOTH. Static: plain=leaf, `-t` refused. Host-side only, NOT on the wire. ★ §chan-crypt CL1: the `-e` intent does NOT live here — it rides Command::crypt, the one field every verb's per-message crypt intent uses (U1). on_command refuses `-e` without `-t` (a global channel has no key) and `-t -g -e` (the clear global copy would defeat the seal).
struct JoinCmd        { enum Op : uint8_t { discover, claim, deny } op; uint8_t node_id; uint32_t claimant_hash; };
// Diagnostic: locate the node owning key_hash32 (the hash-locate H flood); the answer rides
// PushKind::hash_resolved. hard = skip caches, reach the owner (verify-on-use). NO body — notify-only,
// distinct from a send-by-hash (which carries a DM and rides CmdKind::send with dst_hash set).
struct ResolveCmd     { uint32_t dst_hash; uint8_t dst_id; bool hard; uint8_t plane; };   // §enc: dst_id!=0 (dst_hash==0) = reqpubkey BY team_local_id -> resolve the hash from the team key cache at execution. Wave 2 plane: 0=AUTO/1=TEAM(-t)/2=GLOBAL
// E2E §3 (QR import): install a scanned peer's full Ed25519 pubkey as a PINNED (verified) key. key_hash32 = ed_pub[:4]
// is derived (never trusted from the wire), so only the 32-byte pubkey rides the command.
struct PeerkeyCmd     { uint8_t ed_pub[32]; };
// §AB2 (address-book spec 2026-07-29 §2.3): `peername 0x<hash> "<text>"` — set/overwrite the CACHED NAME of a peer
// already in the key cache, touching neither the key nor the confidence. The name rides Command::body/body_len (the
// same BORROWED-for-the-call convention `send` uses — U2, no second text carrier), so only the hash needs a field.
// The hash is the identity (spec §1.2: ids are addresses), which is why there is no id form: naming an id-only peer
// is explicitly out of scope (§4) — there is nothing stable to attach the name to.
struct PeerNameCmd    { uint32_t key_hash32; };

struct Command {
    CmdKind kind = CmdKind::send;
    union {                              // value-init (Command c{}) zero-inits `send` (the first arm)
        SendCmd        send;
        SendLayerCmd   layer;
        SendChannelCmd channel;
        JoinCmd        join;
        ResolveCmd     resolve;
        PeerkeyCmd     peerkey;
        PeerNameCmd    peername;
    } u;
    const uint8_t* body     = nullptr;   // BORROWED for the call only (mirrors hal.h on_recv)
    uint8_t        body_len = 0;
    CryptIntent    crypt    = CryptIntent::def;   // §8b: per-message crypt override (send/sendhash = def/off, sendhashx = on). ★ §chan-crypt CL1: send_channel uses it too — `-e` => on. `def` is what every non-`-e` caller (companion binary Command, simulator bridge) leaves it at, so accepting `-e` changed no existing path.
    bool           no_intro = false;     // §S4/D1 `-K`: suppress the INTRO first-contact pubkey attach for THIS send (send/send_layer). Message-level ceremony for the app; a no-op on a sealed send (sealed never attaches). Host-side only, NOT on the wire.
};

// ---- synchronous result (the token; matches MeshCore RESP_CODE_*) ----
enum class CmdCode : uint8_t { queued, err_unknown_dst, err_too_large,
                               err_no_gateway, err_priority_capped, err_no_binding, err_unsupported,
                               err_unprovisioned,    // node_id==0: must join or `cfg set node_id` first
                               err_no_data_sf,       // allowed_sf_bitmap==0: configure sf_list before sending data
                               err_ack_ring_full };  // E2E-ack deadline (2026-07-24): the pending-ack ring (cap_pending_e2e_acks) is full -> REFUSE a new -a send loudly rather than evict-oldest (which would re-create the silent-forever class). The app retries once an in-flight -a send is acked or times out.
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
    send_e2e_acked, // the END-TO-END ack for a -a DM we originated arrived (dst = the dest that confirmed, ctr = the acked ctr).
                    //   The true "the dest got it" signal — distinct from send_acked (the link/hop ack). Mirrors a durable inbox receipt.
    hash_resolved, // a `resolve` completed: origin = owner node_id (0 = unresolved/timeout),
                   // dst = authoritative?1:0, body[0..3] = the queried key_hash32 (LE, 4 B)
    peer_key_cached, // E2E §7: a recipient's pubkey was learned (on-air answer / cache-on-pass) -> the app can
                     //   resend an encrypted DM. sender_hash = the cached key_hash32; pinned=false (on-air, TOFU).
    config_adopted,  // R6.2: a CONFIG_ANSWER was adopted (lineage/epoch/sf_list/duty/name changed) -> device persists to NV.
    join_refused,    // R6.3 §7c: a join was refused (wire_version mismatch / leaf full) -> console + companion (telemetry is invisible on metal).
    send_blocked,  // Slice 6a: this node's OWN cap / min-interval blocked an origination pre-TX
                   //   (kind = channel|dm; reason = cap|min_interval; next_ms = ms until allowed). Companion holds + retries.
    channel_sent,  // Slice 6c: outcome of an OWN channel post's origin re-offer. relayed=true (a relay was overheard =
                   //   channel_reoffer_confirm) or relayed=false (the re-offer exhausted with no relay -> reason "no_relay").
    mobile_reg,    // §S2: mobile registration changed. origin=home, dst=local, layer_id=home_layer, ctr=epoch,
                   //   relayed=registered (home_layer/epoch emitted only when registered). registered:false = home lost/dereg.
    team_reg,      // §S2: team-DAD id adopted/re-picked. team_id = _cfg.team_id (hex string), dst=team_local_id.
    join_adopted,  // a static/DAD adopt landed (verb join/create, boot DAD, OR the heal re-adopt): dst=adopted node_id,
                   //   layer_id=leaf_id, ctr=claim_epoch. The app refreshes ready.id (an id change mid-session was silent).
    team_key_received,  // §team-ch-key T-K3: a teammate GRANTED us the team CONTENT keypair over a sealed TYPE-19 DM
                        //   (the node has already ADOPTED it — this is a notification, not a request). team_id = the
                        //   granted team, sender_hash = the granter's key_hash32, origin = the granter's node id,
                        //   body = the optional team NAME the granter typed (empty -> the JSON omits it; NOT persisted).
                        //   ★ APPENDED AT THE END: the numeric value is contract-visible, and the SIM bridges PushKind
                        //   on its underlying uint8_t with a static_assert pinning join_adopted == 13
                        //   (lora-universal-simulator ConsoleNames.cpp + NodeRuntimeWrapper.cpp). Inserting anywhere
                        //   above would silently RENAME an existing push kind for every scenario and the companion.
    team_channel_no_key,  // §chan-crypt CL2a (T-K2 §2.2): a CRYPTED team channel post was delivered to us and we hold
                          //   NO team content key, so the CONTENT was dropped — nothing was inboxed and no
                          //   `channel_recv` will follow for this id. The app's action is to prompt "ask a teammate
                          //   for the key" (`team grantkey` from any keyholder, or the T-K4 QR). team_id = the posting
                          //   team, channel_id / channel_msg_id / origin / layer_id name the post that could not be read.
                          //   ★ RATE-LIMITED to one per protocol::team_channel_no_key_push_min_ms — a member without
                          //   the key cannot read ANY post, so an un-limited push would mirror the channel's whole
                          //   traffic into the app as prompts.
                          //   ⚠ NOT stored-for-later in v1 (T-K2 Open Question Q1): the sealed bytes are not retained
                          //   for a retro-decrypt once a key arrives. The post is still RELAYED normally — a relay is
                          //   content-blind, and an un-keyed member must not break the flood for everyone else.
                          //   ★ APPENDED AT THE END, same contract rule as team_key_received above. Both sim asserts
                          //   pin join_adopted == 13 and are UNAFFECTED (this enumerator is 15).
};
// E2E §5: why a send_failed Push fired, so the app reacts (no_pubkey -> offer Request-key/Scan-QR; the permanent
// reasons -> plain fail). Mirrors the contract `send_failed.reason`. `none` = a non-send_failed push.
enum class SendFailReason : uint8_t { none = 0, no_pubkey, no_identity, too_large, bad_rng, no_route, joining,   // R6.2: joining = un-synced managed leaf
                                      cap, min_interval,   // Slice 6a: send_blocked reasons (per-origin cap / burst floor)
                                      no_cts, no_ack,      // Slice 6b: DM giveup reasons (CTS- / ACK-timeout)
                                      mobile_no_home,      // §mobile: a reply-expecting DM from a mobile with no routable home -> unreachable for the reply (would storm)
                                      gateway_unreachable, // §3-A.5: the gateway-doorstep hold gave up (no gateway window within gateway_send_giveup_ms) — was telemetry-only ("gateway_unreachable_timeout")
                                      e2e_ack_timeout,     // E2E-ack DEADLINE (2026-07-24, shelf item (i)): a -a DM's DATA_TYPE_E2E_ACK never returned within the patience budget (protocol::e2e_ack_deadline_ms / _xl_ms). SEMANTIC: delivery was never CONFIRMED, NOT that it failed — the DM may have arrived and the ack died returning; a LATE ack still fires send_e2e_acked. JSON reason string: "e2e_ack_timeout".
                                      queue_full,          // §defer (2026-07-25): the no-route DEFER queue (cap_deferred_sends) was FULL, so the NEW send was REFUSED
                                                           //   (node_cascade.cpp defer_send — Lua table_cap_hit, NEVER drop-oldest) and the app's future is completed instead of hung.
                                                           //   TRANSIENT: retry. ★ APPENDED AT THE END deliberately — the numeric value is contract-visible and the app may
                                                           //   PERSIST it, so no existing enumerator is ever renumbered. JSON reason string: "queue_full".
                                      reprovisioned,       // §clean-join-carriers (2026-07-27, owner ruling): the operator REPROVISIONED this node (`join` / `create` / `leave`,
                                                           //   or prep-restart) and clear_routing_state -> purge_tx_carriers(reprovision) discarded this DM before it aired.
                                                           //   ★ WHY NOT no_route: `no_route` is TRANSIENT and invites a retry TO THE SAME dst — but a reprovision changes the
                                                           //   NETWORK, so the 8-bit dst now names a DIFFERENT node (or nobody). Retrying it is a mis-address, not a retry.
                                                           //   It is also not TRUE: a route may well have existed; the send was discarded ADMINISTRATIVELY. This is the only
                                                           //   reason whose correct app action is "re-address, then resend" rather than "retry" or "give up".
                                                           //   ★ APPENDED AT THE END, same contract rule as queue_full. JSON reason string: "reprovisioned".
                                      unsealable,          // §team-ch-key T-K3: this message TYPE may travel ONLY sealed, and the transport this send would have taken
                                                           //   cannot carry it sealed-AND-typed — so it was REFUSED rather than downgraded. Today that means a
                                                           //   DATA_TYPE_TEAM_KEY_GRANT, whose body holds the team's PRIVATE content key: a CROSS-LAYER flight (the crypto
                                                           //   core is same-layer-only, so XL could only be cleartext) or a registered mobile's DELEGATED flight (the
                                                           //   MOBILE_SEND wrapper's one enclosed-type slot is already spent on SEALED_RELAY, so the app TYPE would be
                                                           //   silently LOST and raw key bytes would land in the peer's inbox as text). ★ PERMANENT for this route, not
                                                           //   transient: retrying changes nothing. The app's action is to grant from a node on the target's OWN layer, or
                                                           //   over the team plane (`-t`). ★ APPENDED AT THE END, same contract rule as queue_full/reprovisioned — and here
                                                           //   it is doubly load-bearing: `reprovisioned` shipped in a contract the app may already persist.
                                                           //   JSON reason string: "unsealable".
                                                           //   ★ §loc-per-send REUSES this reason for a `-l` send that would NOT be sealed, and for
                                                           //   `send_layer -l` / a `-l` DM whose route turns cross-layer: same semantic exactly — "this
                                                           //   content may travel ONLY sealed and this transport cannot carry it sealed, so it was
                                                           //   REFUSED rather than downgraded." The app's action is `-e` / `e2e_dm` / acquire the key.
                                      no_location };       // ★ §loc-per-send (2026-07-31): a `-l` send asked to attach this node's position and there IS
                                                           //   none — lat_e7/lon_e7 are both 0 (no fix, never provisioned). DISTINCT from `unsealable` on
                                                           //   purpose: conflating them would tell the operator to enable encryption when what they need
                                                           //   is a GPS fix (or `cfg set lat`/`lon`), and chasing the wrong remedy is exactly the failure
                                                           //   this refusal exists to prevent. PERMANENT until the node has a fix; the message was NOT
                                                           //   sent (the position was requested explicitly, so silently omitting it is not an option).
                                                           //   ★ APPENDED AT THE END, same contract rule as queue_full/reprovisioned/unsealable — the
                                                           //   numeric value is contract-visible and the app may persist it, so nothing is renumbered.
                                                           //   JSON reason string: "no_location".
// R6.3 §7c: why a join was refused (join_refused push). wire_version -> origin=their_ver, dst=my_ver; leaf_full -> no extra.
// §3-A.1: phy_mismatch = a team member refused a home whose PHY differs from its team-provisioned freq/bw/routing_sf (P2-1 Level 2);
//          sf_list_mismatch = ADVISORY — the mobile adopted the host but its configured sf_list low byte disagrees with the host's offered one.
enum class JoinRefuseReason : uint8_t { wire_version = 0, leaf_full = 1, phy_mismatch = 2, sf_list_mismatch = 3 };
struct Push {
    PushKind kind = PushKind::msg_recv;
    SendFailReason reason = SendFailReason::none;   // send_failed only (else none)
    JoinRefuseReason join_reason = JoinRefuseReason::wire_version;   // join_refused only
    uint8_t  origin = 0;
    uint8_t  dst = 0;
    uint8_t  channel_id = 0;   // channel_recv only
    uint8_t  layer_id = 0;     // msg_recv/channel_recv: the FULL 8-bit receiving layer id (§2/Q13 — disambiguates origin across a gateway's leaves)
    uint8_t  origin_layer = 0; // §GapA (cross-layer mobile): msg_recv — the SENDER's layer (layer_ids[0] of the preserved XL path; 0 = same-layer/non-XL). Lets the recipient build the (layer_path, hash) REPLY address; JSON omit-when-0.
    bool     enc = false;      // §8b: msg_recv -> the DM was delivered SEALED (CRYPTED + opened); channel_recv -> false (cleartext today)
    bool     blocked_channel = false;  // send_blocked: true => "channel", false => "dm"
    bool     relayed = false;          // channel_sent: a relay of our channel post was overheard (true) or the re-offer exhausted (false)
    // ★★ §AB2 (address-book spec 2026-07-29 §0.1/§2.2): peer_key_cached ONLY — the confidence the cached entry actually
    // holds, read back out of the live table by Node::push_peer_key_cached. Before this the JSON emitted a HARDCODED
    // `"pinned":false`, so the app could not tell `overheard` (key present, CANNOT seal) from `authoritative` (can) —
    // and e2e_seal_inner requires >= authoritative. The app offered "send encrypted", the user tried, and got
    // `FAILED (no recipient pubkey)`. This field is what makes the contract's existing "gate on conf >= authoritative"
    // rule usable at all.
    // ⚠ RAW uint8_t, NOT the enum: node.h INCLUDES this header, so `Node::PeerKeyConf` is not visible here. The
    // encoding (0 overheard / 1 authoritative / 2 pinned) is pinned by a static_assert next to the enum in node.h —
    // the same discipline, and the same reason, as device_nv.h's kPeerConf* constants. Default 0 = the SAFE answer
    // ("cannot seal") for every other push kind and for a hash whose row aged out between cache and drain.
    // ★ PLACEMENT: byte 11, the alignment pad that already sat between `relayed` (10) and the 2-aligned `ctr` (12) —
    // measured by offsetof, so sizeof(Push) stays 292 and Node::_push_ring[32] (and sizeof(Node)) do not move.
    uint8_t  peer_conf = 0;
    uint16_t ctr = 0;
    uint32_t next_ms = 0;              // send_blocked: ms until the origination is allowed (0 = the floor already passed but cap/duty blocks)
    uint32_t sender_hash = 0;      // msg_recv: the DM sender's stable key_hash32 (0 = no SOURCE_HASH). The app's
                                   //   DM dedup identity is (sender_hash, ctr) when set, else (origin, ctr).
                                   // ★ §chan-crypt CL2c: channel_recv carries the SAME quantity here — the sealed
                                   //   inner's bit2 source_hash (0 = the post did not name one, or it was plaintext).
                                   //   Deliberately the SAME field, not a parallel one (U1): it is the same identity
                                   //   the same app renders, and `origin` on a team post is only a DAD-assigned
                                   //   team_local_id. ⚠ MEMBERSHIP-authenticated, not sender-authenticated — see
                                   //   node.h PeerLocSrc for the bound.
    uint32_t channel_msg_id = 0;   // channel_recv: the FULL 32-bit channel message id (the app's dedup identity)
    uint32_t team_id = 0;          // §S4: channel_recv team scoping (0 = a plain leaf channel -> omitted); §S2 team_reg carries the team id here
    uint32_t seq = 0;              // msg_recv/channel_recv: the inbox per-store seq (0 = inbox disabled -> omit).
                                   //   The app unifies live + pulled by seq + detects a dropped live push (model B).
    bool     has_location = false; // msg_recv: the sender piggybacked a 6-B location (DATA_FLAG_LOCATION).
                                   // ★ §chan-crypt CL2b sets it for channel_recv too (the sealed inner's bit1), and
                                   //   §chan-crypt CL2c is what put it on the WIRE FORMAT of the push — write_push's
                                   //   channel_recv arm now emits `lat`/`lon`. (The DM arm still does not; that is
                                   //   register B36's half, a rendering fix with no wire change.)
    int32_t  lat_e7 = 0, lon_e7 = 0;  //   deg×1e7 (~11 m), valid iff has_location.
    uint8_t  body[protocol::max_payload_bytes_hard_cap] = {};   // msg_recv / channel_recv text (empty otherwise)
    uint8_t  body_len = 0;
};
// ★★ §AB2 — PER-ABI, NOT native-only, and deliberately so (the same discipline, and the same wording, as
// device_nv.h's `sizeof(PeerRec)` assert): this compiles on nRF52/ARM and ESP32/Xtensa too, so the claim that
// `peer_conf` costs ZERO bytes is verified on every target by the ordinary board build rather than only on the host.
// It matters because `Push` is a Node member ring (`_push_ring[cap_push_ring]`), so a byte added OUTSIDE the existing
// alignment pad would multiply by 32 and move `sizeof(Node)` — the D2 trigger. Both halves are asserted: the OFFSETS are
// the load-bearing claim (peer_conf sits in the pad `uint16_t ctr`'s 2-byte alignment already reserved, which is 2 on
// every ABI this project targets), and the total is the layout tripwire.
static_assert(offsetof(Push, relayed) == 10 && offsetof(Push, peer_conf) == 11 && offsetof(Push, ctr) == 12,
              "command.h: Push::peer_conf left the alignment pad between `relayed` and `ctr` — it now costs real bytes "
              "x cap_push_ring and sizeof(Node) has moved (see node.h's layout tripwire)");
static_assert(sizeof(Push) == 292, "command.h: the Push layout moved — Node::_push_ring and sizeof(Node) shift with it");

}  // namespace meshroute
