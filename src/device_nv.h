// MeshRoute — src/device_nv.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Persist the device's runtime state to on-chip flash so it survives reboot. FOUR records:
//   - `/mrcfg`   (Blob)     = RADIO/PROTOCOL CONFIG + the short `node_id` (a `cfg set` over the console).
//   - `/mrid`    (IdBlob)   = the 32-byte identity master seed + name (HW-RNG on first boot; `regen`). The
//                             keypair / key_hash32 are DERIVED from the seed at boot (lib/core/identity).
//   - `/mrpeers` (PeerBlob) = the peer ADDRESS BOOK: key + name + confidence (see §2 below).
//   - `/mrfault` (mrfault::FaultLog) = the HW fault history; lib/core/fault_log.h owns its validity rule.
//
// STRUCTURE — three layers, and the ORDER is deliberate (NV1, register B26):
//   1. the record types + the `Slot` table + the `blob_valid_*` predicate, ABOVE the platform `#if`, so they
//      are HOST-TESTABLE (test/test_device_nv.cpp). The predicate used to be hand-copied SIX times *inside*
//      the platform arms, where no test could reach it.
//   2. per-backend `read_slot`/`write_slot` — the only platform-specific code, TWO functions per arm:
//        nRF52 (Adafruit core) -> Adafruit_LittleFS / InternalFS FILES, addressed by `Slot::path`
//        ESP32 (Heltec)        -> Preferences / NVS KEY-VALUE, addressed by `Slot::ns` + `Slot::key`
//      ⓘ 2026-08-19: each READ arm is now a slim ADAPTER (nRF52: six members incl. the raw-rc `lookup`; ESP32:
//      five) over the hoisted `fs_read_slot`/`nvs_read_slot` sequence in layer 1, so the branch order (and the two
//      `SlotIo` facts) are host-testable too. The adapters hold no branch of their own; `write_slot` is untouched
//      and stays whole in the arm.
//      ⚠ Those are different storage MODELS, not different syntax — which is exactly why the typed wrappers
//      are NOT collapsed into one `load_peers` with the `#if` moved inside it: same duplication, worse
//      locality. The `#if` belongs at the primitive, and nowhere else.
//   3. the eight typed `load*`/`save*` wrappers, ONE copy each, over whichever primitive pair compiled.
//
// REALITY SPLIT: this compiles under both board envs here; the actual flash read/write + the wear is
// BENCH-VERIFIED BY THE USER (I cannot exercise on-chip flash from the host). A failed/empty load just
// falls back to the compile-time defaults, so an unprovisioned or mismatched-version chip still boots.
#pragma once
#include <stdint.h>
#include <string.h>      // memcmp — H3 save() change-detection
#include "fault_log.h"   // mrfault::FaultLog — the /mrfault store is whole-blob R/W, exactly like Blob

namespace mrnv {

struct Blob {                  // packed-ish POD; written/read verbatim. Bump kVersion on any layout change.
    uint32_t magic;            // kMagic — distinguishes a real blob from erased flash
    uint16_t version;          // kVersion — a mismatch => ignore (use defaults)
    uint8_t  claim_epoch;      // node_id DAD: the static tiebreak key, persisted so a reboot keeps its seniority (was _pad hi)
    uint8_t  joined;           // node_id DAD: 1 = node_id was DAD-adopted (defends + yields) vs cfg-pinned (was _pad lo)
    uint16_t channel_ctr;      // v15: the self-keyed channel send-ctr — persisted so a reboot CONTINUES (no id-reuse -> no `already-buffered` dedup-drop)
    double   freq_mhz;
    uint32_t bw_hz;
    uint32_t beacon_ms;
    double   duty;
    uint16_t allowed_sf_bitmap;  // the sf_list (bit=sf); 0 = no data SF -> node refuses to originate data
    uint8_t  routing_sf;
    uint8_t  cr;
    uint8_t  lbt;
    uint8_t  node_id;          // 0 = unprovisioned (no sends until join / `cfg set node_id`)
    int8_t   tx_power;         // requested nominal conducted dBm (repurposed _pad2); board envelope validates it
    uint8_t  is_gateway;       // v6: role/topology config (was live-only; now persisted across reboot)
    uint8_t  gateway_only;     // v6: §7 pure-bridge flag (channel-plane consumer half off too)
    uint8_t  is_mobile;        // v6
    uint8_t  leaf_id;          // v6: leaf membership (floods are leaf-scoped)
    uint8_t  ble_mode;         // v7: BLE companion policy — 0=off (bare-metal, default), 1=on, 2=periodic
    uint8_t  ble_period_min;   // v7: periodic-mode advertising period (minutes); reboot-to-apply
    uint32_t ble_pin;          // v7: the 6-digit BLE pairing passkey (0..999999)
    // v8: DUAL-LAYER GATEWAY. n_layers=1 => single-layer (layer 0 = the legacy fields above: node_id / routing_sf /
    // allowed_sf_bitmap / beacon_ms; layer0_id holds the full 8-bit layer_id). n_layers=2 => gateway, layer 1 in the
    // l1_* block. window fields persist the schedule (0 = re-derive the SF-weighted anti-phase split at on_init).
    uint8_t  n_layers;            // 1 or 2 (0/old blob => treated as 1)
    uint8_t  layer0_id;           // layer 0 FULL 8-bit layer_id (single-layer: == leaf_id)
    uint32_t window_period_ms;    // the shared layer0<->layer1 cycle
    uint32_t l0_window_ms;        // layer 0 presence; 0 = derive
    uint32_t l0_window_offset_ms; // layer 0 phase; 0 = derive (layer 0 = 0)
    uint8_t  l1_node_id;          // layer 1 per-leaf node_id (static; live DAD deferred)
    uint8_t  l1_layer_id;         // layer 1 FULL 8-bit layer_id
    uint8_t  l1_claim_epoch;      // layer 1 per-leaf DAD seniority (forward-compat; DAD deferred)
    uint8_t  l1_joined;           // layer 1 per-leaf DAD adopted (forward-compat; DAD deferred)
    uint8_t  l1_routing_sf;       // layer 1 routing SF (5..12)
    uint16_t l1_allowed_sf_bitmap;// layer 1 data-SF set
    uint32_t l1_beacon_period_ms; // layer 1 beacon cadence
    uint32_t l1_window_ms;        // layer 1 presence; 0 = derive
    uint32_t l1_window_offset_ms; // layer 1 phase; 0 = derive anti-phase
    // ★★ `loc_in_dm` (v9) REMOVED at v23 — §loc-per-send, open-bug-register B0. It persisted an opt-in that attached the
    //    node's coordinates to every originated app DM on a size check ALONE (no crypt gate), so a plaintext DM aired the
    //    position in the clear. Location is now a PER-SEND request (`send … -l`) that is REFUSED unless the DM is sealed,
    //    so there is nothing left to persist — and deliberately no successor field: giving a per-send intent persistent
    //    storage is how the toggle would grow back. The lat/lon themselves still live in /mrid (IdBlob) and are what `-l`
    //    attaches. ⚠ The byte is DROPPED rather than kept as a reserved pad: the struct is versioned as a whole and a
    //    kVersion mismatch REJECTS the blob outright (load() -> defaults), so there is no partial-parse path that a
    //    placeholder would protect. Removing it changes the layout, which is exactly what the version bump announces.
    uint8_t  e2e_dm;              // v10: 1 = originate app DMs ENCRYPTED (E2E §4b). Default off -> plaintext (s18-identical).
    // v11: gateway noise control (duty-cycle protection). A gateway is reactive-only in steady state; these gate its
    // sole unsolicited heartbeat. 0 => use the NodeConfig default (5% / 3 h) at boot (an old/zeroed blob stays sane).
    uint8_t  gw_announce_duty_pct;       // v11: % OF the duty budget below which an unsolicited announce is allowed
    uint32_t gw_announce_min_interval_ms;// v11: min ms between unsolicited steady-state announcements
    double   l1_freq_mhz;                // v12: layer-1 RF carrier (per-layer freq). 0 = inherit freq_mhz (layer 0's). Layer 0 reuses freq_mhz.
    uint8_t  gw_herd_slack;             // v13: §3e herd-spread slack factor (spread = exchange_airtime × this). 0 = use the NodeConfig default (2).
    // v14: R6.1 leaf-config membership. lineage_id 0 = UNMANAGED leaf (a pre-v14/never-`leaf create`d node falls here ->
    // backward-compat peering). config_epoch is the LWW config version. leaf_name is in the config_hash (a change re-fingerprints).
    uint16_t lineage_id;                 // v14: operator-minted leaf lineage (0 = unmanaged); u16 (2026-06-20b right-size)
    uint16_t config_epoch;              // v14: monotonic config version
    uint8_t  leaf_name_len;            // v14: 0..meshroute::protocol::leaf_name_max
    uint8_t  leaf_name[16];            // v14: leaf_name bytes (cap = leaf_name_max = 16)
    // v16: anti-spam v2 per-leaf tunables (promoted to leaf config). 0 => use the NodeConfig default at boot (an old/zeroed blob stays sane).
    float    channel_active_fraction;    // v16: per-origin channel-cap fairness divisor (0 => default 0.125)
    uint32_t channel_min_interval_ms;    // v16: channel burst floor ms (0 => default 10000)
    uint32_t dm_min_interval_ms;         // v16: own-DM burst floor ms (0 => default 3000)
    // v17: per-layer (layer-1) bandwidth + coding-rate. 0 => inherit the global radio_bw_hz/radio_cr — the active_bw_hz()
    // accessor resolves the inherit at READ time (so load just copies 0 through). Layer 0 reuses the global bw_hz/radio_cr.
    uint32_t l1_bw_hz;                    // v17: layer-1 bandwidth (Hz). 0 = inherit.
    uint8_t  l1_cr;                       // v17: layer-1 coding-rate (5..8). 0 = inherit.
    uint32_t team_id;                     // v18: §mobile 6.1 team-id overlay (0 = no team)
    uint8_t  mobile_autoregister;         // v18: §mobile console autonomy toggle (1 = ON default; seeded from the live cfg on reprovision)
    uint8_t  team_local_id;               // v19: §mobile 6.4 the team-DAD'd team-plane id — PERSISTED so a power-cycle (hiker switches off) keeps a STABLE team id (no re-DAD churn). 0 = not team-DAD'd / left the team.
    // v20: remote-management admin auth (spec 2026-07-13). A v19 blob loads with these zero-defaulted = UNPROVISIONED.
    uint8_t  admin_pubkey[32];            // v20: pinned admin Ed25519 pubkey (trust anchor for gated rcmds); all-zero + admin_provisioned=0 = unset
    uint32_t admin_counter_floor;        // v20: highest accepted admin-command counter (replay floor; write-coalesced like channel_ctr)
    uint8_t  admin_provisioned;          // v20: 1 once `password` pinned the pubkey (distinguishes "all-zero pubkey" from "unset")
    uint8_t  intro_attach;               // v21: §S2 first-contact INTRO auto-attach toggle (1 = ON default; seeded from the live cfg on reprovision)
    // v22: §team-ch-key (T-K1, spec 2026-07-26 §2.1) the TEAM CHANNEL keypair — a team's CONTENT key, minted
    // unconditionally by `team new` or adopted from `tkpub=`/`tkpriv=`. UNLIKE the node identity there is NO seed
    // to re-derive this from: these 64 bytes ARE the secret, so /mrcfg is now key material (as /mrid already was).
    // team_ch_priv is stored CANONICAL (RFC-7748-clamped) — see team_channel_key_derive in lib/core/identity.h.
    // present=0 + all-zero = no key. Growing the Blob makes every pre-v22 record fail load()'s `n == sizeof(out)`
    // size check below, so an old chip re-provisions from defaults and CANNOT surface a fabricated key.
    uint8_t  team_ch_pub[32];
    uint8_t  team_ch_priv[32];
    uint8_t  team_ch_key_present;        // v22: 1 once a pair was minted/adopted (distinguishes a real key from all-zero, exactly as admin_provisioned does)
    // v24: §UI-16 K2 — THE ACTIVE BINDING between this node and one `/mrteams` keyring record ([[B240]], P-2b).
    // ★★★ IT IS A SECOND AUTHORITY ON PURPOSE, AND ⛔ NOT A DUPLICATE OF `team_id` ABOVE. `team_id` is MEMBERSHIP,
    //     and membership is PUBLIC — the id rides every team beacon. If the boot restore keyed on it alone, then
    //     leaving a team and later re-joining it (which anyone who heard the beacon can do) would SILENTLY REINSTALL
    //     the retained key — the reactivation the owner's keyring ruling forbids. ⇒ this pair records which team the
    //     key was ACTIVATED for, is CLEARED by `team 0` while the keyring record is RETAINED, and the restore
    //     installs only on an EXACT match of both. Re-arming it takes an explicit operator act (§UI-16 K5).
    uint32_t team_key_team_id;           // v24: the team the active key belongs to (0 = no active binding)
    uint8_t  team_key_active;            // v24: 1 = a /mrteams record for team_key_team_id is ACTIVE
};
constexpr uint32_t kMagic   = 0x4D524331u;   // 'MRC1'
constexpr uint16_t kVersion = 24;            // v24: §UI-16 K2 — the team-key ACTIVE BINDING (team_key_team_id + team_key_active), so a retained /mrteams key is never reactivated by mere knowledge of the public team id. ⚠ REPROVISION-ON-REFLASH: the struct layout changed, so load() rejects a v23 blob and the node comes up UNPROVISIONED on first contact after this flash — the companion must expect that. ⛔ THIS IS AN **NV** VERSION, ⛔ NOT `wire_version`: no frame moves, no scenario re-anchors, and a new NV record (`/mrteams`) is not a wire change either. v23: §loc-per-send — the `loc_in_dm` byte is GONE (location became the per-send `send -l` flag; the toggle aired coordinates in the clear, open-bug-register B0). ⚠ REPROVISION-ON-REFLASH: the struct layout changed, so load() rejects a v22 blob and the node comes up UNPROVISIONED on first contact after this flash — the companion must expect that. v22: §team-ch-key team channel keypair (team_ch_pub + team_ch_priv + team_ch_key_present) — REPROVISION-ON-REFLASH, see the fields. v21: §S2 intro_attach toggle (first-contact pubkey attach). v20: remote-mgmt admin auth (admin_pubkey + admin_counter_floor + admin_provisioned). v19: team_local_id (§mobile 6.4 — persist the team-DAD id across reboot). v18: team_id (§mobile 6.1). v17: per-layer BW+CR (l1_bw_hz + l1_cr). v16: anti-spam per-leaf tunables (channel_active_fraction + the two burst floors). v15: channel_ctr persist (reboot id-reuse fix). v14: R6.1 leaf-config (lineage_id + config_epoch + leaf_name). v13: gw_herd_slack. v12: per-layer frequency (l1_freq_mhz). v11: gateway-announce duty knobs. v10: e2e_dm toggle. v9: loc_in_dm toggle. v8: DUAL-LAYER GATEWAY (n_layers + layer0_id + window schedule + the l1_*
                                             // block). v7: BLE companion policy. v6: role/topology (is_gateway/...). The Blob
                                             // grew, so every pre-v8 blob fails the `n == sizeof(out)` size check in load()
                                             // and is rejected -> the node re-provisions from defaults (BOTH boards — the
                                             // Blob is shared with the ESP32/Preferences backend; no migration, re-run `cfg set`).

// ---- Identity record (`/mrid`) — SEPARATE from the config blob above (spec §1.4). The 32-byte master
// seed is the single source of truth: ed_pub / key_hash32 / x_* are DERIVED at boot via identity_from_seed
// (so a stored pubkey can never disagree with the seed — a deliberate simplification of §1.4's "store
// ed_pub too"). HW-RNG fills the seed on first boot; `regen` mints a new one; `cfg set name` sets name.
struct IdBlob {
    uint32_t magic;            // kIdMagic — real record vs erased flash
    uint16_t version;          // kIdVersion
    uint16_t name_len;         // bytes of `name` in use (0..sizeof(name))
    uint8_t  seed[32];         // master identity secret (the ONLY persisted key material)
    char     name[32];         // human label (app-level, §1.3); not necessarily null-terminated — name_len bounds it
    // Node location (deployment metadata), degrees × 1e7; (0,0) = unset. APPENDED to /mrid (set once via
    // `cfg set lat`/`lon` or the app). The strict size check below means a legacy /mrid (no lat/lon) is
    // rejected on the first boot after reflashing -> the node re-mints a fresh identity. Fine: dev system.
    int32_t  lat_e7;
    int32_t  lon_e7;
};
constexpr uint32_t kIdMagic   = 0x4D524944u; // 'MRID'
constexpr uint16_t kIdVersion = 1;

// ---- Peer ADDRESS BOOK (`/mrpeers`) — E2E §2 + spec 2026-07-29 §2.4 (slice AB1). Whole-blob R/W like /mrid; a
// `peerkey` install or an on-air key-learn rewrites it. Dev hardware: a format change just bumps kPeersVersion
// (no migration). Holds the peers whose identity we can NAME and SEAL to:
//   - the QR/`peerkey`-installed VERIFIED keys (`pinned` — a human checked this one), and
//   - the keys learned ON AIR at `authoritative` (v2 — before it, a reboot cost the ability to send encrypted to
//     every on-air peer, recoverable only by a manual `reqpubkey` each),
// each with its cached NAME (v2), and each reloaded at boot AT THE STORED CONFIDENCE — never promoted. v1 stored
// PINNED keys only, NAMELESS, and re-installed them all as `pinned`; re-installing an on-air key that way would
// assert "a human verified this via QR", which is a lie the seal/UI paths act on.
// ⚠ ONE-TIME LOSS: v2's layout change makes every v1 record fail load()'s size check (584 -> 1160 B, so the
// version byte is belt-and-braces), so the store is EMPTY on the first boot after this flash — the QR ceremony
// must be redone once. Deliberate: the node is not deployed, and a migration path is code that never runs again.
// ✖ NOT persisted, deliberately:
//   - `overheard` keys — they cannot seal (node_hashlocate.cpp gates sealing on conf >= authoritative), so storing
//     one would only spend a slot and evict a useful record;
//   - `peer_confirmed` — a PER-SESSION fact ("we opened a SEALED frame from them", i.e. they hold OUR key). A
//     reboot un-knows it; restoring it from flash would suppress the §S2 INTRO first-contact attach toward a peer
//     who may never have seen our key. It stays RAM-only in Node::PeerKey by design;
//   - the retained peer LOCATION (spec §2.7, owner-ruled RAM-only): a stale position rendered as current is worse
//     than none, and a captured node must not yield every teammate's last known fix.
struct PeerRec {
    uint32_t key_hash32;   // == LE(ed_pub[:4]); THE identity of the address book (ids are addresses, the hash is identity)
    uint8_t  ed_pub[32];   // Ed25519 identity pubkey — IMMUTABLE (Node::peer_key_set re-verifies ed_pub[:4] == hash)
    uint8_t  confidence;   // v2: a Node::PeerKeyConf value; ONLY authoritative(1)/pinned(2) are ever stored (see peer_conf_restorable)
    uint8_t  name_len;     // v2: 0..sizeof(name)
    char     name[32];     // v2: the peer's cached human label — MUTABLE (refreshed on every pubkey message)
    uint8_t  _pad[2];      // ABI tail padding (4+32+1+1+32 = 70, alignof 4 -> 72), NAMED so the on-flash record is
                           // fully zeroable: peer_rec_merge's byte-compare wear-guard needs deterministic padding.
                           // Repurposable without moving sizeof — as Blob's _pad/_pad2 became claim_epoch/tx_power.
};
struct PeerBlob {
    uint32_t magic;       // kPeersMagic
    uint16_t version;     // kPeersVersion
    uint16_t count;       // entries in use (0..kMaxPeerRecs)
    PeerRec  rec[16];     // == cap_peer_keys; v2: pinned AND authoritative records (v1 was pinned-only)
};
constexpr uint32_t kPeersMagic   = 0x4D525052u;  // 'MRPR'
constexpr uint16_t kPeersVersion = 2;            // v2: §AB1 the address book — PeerRec gains `confidence` + `name`/`name_len`, so
                                                 // `authoritative` on-air keys and cached names survive a reboot and nothing is
                                                 // silently promoted to pinned. ⚠ ONE-TIME LOSS of the v1 pinned store, see above.
constexpr uint8_t  kMaxPeerRecs  = 16;           // v2 rename (was kMaxPinnedPeers): ruling (b) made "pinned" false — the store is
                                                 // no longer pinned-only, and a cap named after one of two kinds mis-states the contract.

// A stored `confidence` byte -> may we re-install this record, and at what level? ★ DELIBERATELY NOT "anything
// that is not pinned means authoritative": `authoritative` is exactly the level that lets a DM be SEALED to a peer,
// so widening this would manufacture a sealing capability out of a corrupt flash byte. An unrecognised value is
// SKIPPED by the boot restore and REFUSED by peer_rec_put (C2), never guessed at.
constexpr uint8_t kPeerConfAuthoritative = 1;    // == static_cast<uint8_t>(meshroute::Node::PeerKeyConf::authoritative)
constexpr uint8_t kPeerConfPinned        = 2;    // == static_cast<uint8_t>(meshroute::Node::PeerKeyConf::pinned)
                                                 // ⚠ NOT #included from node.h (this is the device record layer, which must stay
                                                 // free of the protocol engine); test_device_nv.cpp static_asserts the two agree.
inline bool peer_conf_restorable(uint8_t stored) {
    return stored == kPeerConfAuthoritative || stored == kPeerConfPinned;
}
// ★ PER-ABI, NOT native-only. `sizeof` IS the migration policy for this record (load_peers' exact size check), and
// test_device_nv.cpp can only measure the HOST ABI — so pin it here, where it compiles on nRF52/ARM and ESP32/Xtensa
// too. 70 bytes of payload, alignof 4, `_pad[2]` named ⇒ 72; blob = 8-byte header + 16 × 72 = 1160 (v1 was 584).
static_assert(sizeof(PeerRec) == 72,  "device_nv.h: the /mrpeers on-flash record layout moved — bump kPeersVersion");
static_assert(sizeof(PeerBlob) == 8 + 16 * 72, "device_nv.h: the /mrpeers blob layout moved — bump kPeersVersion");

// ---- Join-profile record (`/mrjoin`) — §UI-15 slice 2 (spec 2026-08-18-ui15-provisioning-implementation-plan.md
// §3). FOUR operator-authored "networks I can join" presets, so a field join needs no keyboard.
//
// ★★★ IT IS ITS OWN RECORD, WITH ITS OWN MAGIC AND ITS OWN VERSION, AND THAT IS THE WHOLE POINT (§3.6.3): a corrupt
//     PROFILE STORE must not reset config, identity, team keys or the peer book. Putting the four slots inside
//     `Blob` would have made every profile edit a `/mrcfg` rewrite AND made `/mrjoin` corruption fatal to the join
//     the node is currently running on.
// ★★ …AND IT IS IN THE `"mr"` NAMESPACE ON PURPOSE — see `kSlotJoin` below. `/mrfault` is the ONE record that opts
//    OUT of `factory_erase()`, and it does so by living in its own namespace. Join profiles are USER CONFIGURATION,
//    not fault history, so §3's "factory reset DELETES /mrjoin" is delivered by the namespace choice alone, with no
//    code in `factory_erase()` at all.
// ⛔⛔ AND IT IS DELIBERATELY **NOT** IN `mount_or_repair()`'s nRF52 PROBE LIST (`kFiles[]`, in the nRF52 arm
//    below — the former :400-413 ref had drifted). That function recovers by
//    calling `InternalFS.format()`, whose own comment records *"a reformat wipes /mrid too -> the node re-mints its
//    identity + loses its join"*. ⇒ listing an OPTIONAL preset store there would make ITS corruption destroy identity
//    AND config — the exact inversion of the isolation this record exists for. A failed / short / invalid read is
//    handled LOCALLY, as `JoinRead::invalid`, and the only repair is the operator's explicit `joinprofile reset
//    confirm`. ⓘ The spec records this as a REVERSAL of its own earlier recommendation; it is written here because
//    a header is where an obligation survives.
struct JoinProfile {
    uint8_t  present;      // 0 = EMPTY slot (a zeroed slot is empty by construction — see join_profile_put)
    uint8_t  layer;        // the FULL 1..255 layer id, exactly as `Blob::layer0_id` holds it (leaf = layer & 0x0F,
                           // DERIVED at use by mrfw::join_leaf_of_layer — ⛔ never stored twice)
    uint8_t  routing_sf;   // 5..12
    uint8_t  name_len;     // 0..sizeof(name); the bytes past it are ZERO (join_profile_put zeroes the slot first)
    // ★★ HZ, NOT kHz, AND THE BUILD'S OWN DEFAULT CARRIER IS THE PROOF: 869.4625 MHz is 869462.5 kHz — NOT an
    //    integer — but exactly 869462500 Hz. A `uint32_t freq_khz` would round it to 869462 and CHANGE THE RADIO.
    //    ⛔ No `double` in a new record; `/mrcfg` keeps its own `double freq_mhz` unchanged (a v3 correction).
    uint32_t freq_hz;
    uint32_t bw_hz;
    char     name[12];     // the operator's label; NOT NUL-terminated — name_len bounds it (as IdBlob::name does)
    // ⛔ NO `sf_list` HERE, and it is a decision rather than an omission: team-PHY compatibility excludes it
    //    (lib/core/node.h:302-305, [[B211]]). A profile is the RADIO FLOOR a join needs, nothing more.
};
constexpr uint8_t kJoinProfiles = 4;          // FIXED four slots — ⛔ no dynamic count, so the record has ONE size
struct JoinBlob {
    uint32_t magic;             // kJoinMagic
    uint16_t version;           // kJoinVersion — EQUALITY (see load_join): a mismatch REJECTS the whole record
    uint16_t reserved;          // ★ the header carries the padding, ⛔ never the per-profile struct (§3). Zeroed by
                                //   join_blob_init, so it is part of the byte-identical write-coalescing compare.
    JoinProfile prof[kJoinProfiles];
};
constexpr uint32_t kJoinMagic   = 0x4D524A31u;   // 'MRJ1' — its OWN magic, ⛔ never kMagic ('MRC1')
constexpr uint16_t kJoinVersion = 1;             // v1: the first /mrjoin layout. A bump REJECTS the old record
                                                 // outright (equality policy) -> the node comes up with NO profiles,
                                                 // which is an ordinary state here (⛔ unlike /mrcfg, nothing about
                                                 // the running join depends on this record).
// ★ PER-ABI, NOT native-only — the same reason PeerRec's pair is here: `sizeof` IS the migration policy (load_join's
//   exact size check), and test/ can only measure the HOST ABI, so pin it where it compiles on ARM and Xtensa too.
//   4 × uint8 + 2 × uint32 + 12 chars = 24 with alignof 4 and ⛔ NO tail padding — which is why §3 forbids inventing
//   a per-profile `reserved`: there is nothing for it to fix.
static_assert(sizeof(JoinProfile) == 24, "device_nv.h: the /mrjoin profile layout moved — bump kJoinVersion");
static_assert(alignof(JoinProfile) == 4, "device_nv.h: /mrjoin profile alignment moved — the 24-byte claim is ABI-dependent");
static_assert(sizeof(JoinBlob) == 8 + 4 * 24, "device_nv.h: the /mrjoin record layout moved — bump kJoinVersion");

// ---- Team-key KEYRING record (`/mrteams`) — §UI-16 K1, register [[B240]] ------------------------------
// Spec: docs/superpowers/specs/2026-08-22-ui16-nearby-onboarding-spec.md §4-K1 (owner-ruled 2026-08-22).
//
// ★★★ THE DEFECT IT EXISTS FOR: a team CONTENT key that arrived over the air (the sealed T-K3 grant) was adopted
//     into RAM and NEVER PERSISTED — `team_key_grant_receive` installs live, the push handler prints, and the only
//     writers of `Blob::team_ch_*` are reached from `seed_blob_from_live` on OTHER verbs' save paths. ⇒ a member
//     could read the encrypted channel until the first power-cycle and then silently could not. [[B240]].
// ★★ WHY A RECORD OF ITS OWN AND NOT A `/mrcfg` FIELD (owner ruling, spec §9 R-6): `/mrcfg` holds ONE key, so it
//    cannot hold a key PER TEAM, cannot survive `team 0`, and makes "retained but not active" unrepresentable.
// ⛔ A NEW NV RECORD IS NOT A WIRE CHANGE. No frame moves; nothing re-anchors.
//
// ★ FOUR ENTRIES, matching the four `/mrjoin` join profiles — the same "four networks I can be in" shape.
// ⓘ `reserved[4]` is a NAMED member and ⛔ never implicit tail padding — the [[AB1]]/`PeerRec::_pad` rule: implicit
//   padding is indeterminate after value-initialisation, which would make a whole-record compare unsound, and the
//   write-coalescing policy in `firmware_team_keyring.h` IS a whole-record compare.
// ⚠ SECRET-BEARING. `team_ch_priv` is a standalone secret: ⛔ NO seed derives it (`lib/core/node_role.h:89`), so
//   these bytes and whatever the operator distributed are its only copies. Losing them = re-key the team.
// ★ BOTH HALVES ARE STORED so a restore can DERIVE the public half from the private one and REJECT a record whose
//   stored pub disagrees — `Node::team_channel_key_adopt` (`lib/core/node.h:228`) already performs exactly that
//   derive-and-cross-check, so the keyring CALLS it rather than re-deriving (U1). ⛔ The pub is therefore not
//   redundant: it is the corruption detector.
struct TeamKeyRecord {
    // ⛔ `0` IS NEVER STORED (the write policy refuses it): 0 means "no team" everywhere else in this codebase, so a
    //    zero-keyed record would match every teamless node's binding and hand it a key it was never granted.
    uint32_t team_id;
    uint8_t  team_ch_pub[32];    // the public half — cross-checked against the derived one on restore
    uint8_t  team_ch_priv[32];   // ⚠ THE SECRET
    uint8_t  reserved[4];        // NAMED padding — see the note above; zeroed by the one composition path
};
constexpr uint8_t kTeamKeyRecs = 4;           // FIXED four slots — ⛔ no dynamic count, so the record has ONE size
struct TeamKeyBlob {
    uint32_t magic;              // kTeamKeyMagic
    uint16_t version;            // kTeamKeyVersion — EQUALITY (see team_key_blob_state)
    uint16_t count;              // records in use (0..kTeamKeyRecs); the header carries the padding, as JoinBlob's does
    TeamKeyRecord rec[kTeamKeyRecs];
};
constexpr uint32_t kTeamKeyMagic   = 0x4D524B31u;   // 'MRK1' — its OWN magic, ⛔ never kMagic ('MRC1') and never kJoinMagic
constexpr uint16_t kTeamKeyVersion = 1;             // v1: the first /mrteams layout. A bump REJECTS the old record
                                                    // outright (equality policy) -> the node comes up with NO stored
                                                    // team keys, which leaves it keyless rather than wrongly keyed.
// ★ PER-ABI, NOT native-only — the same reason PeerRec's and JoinProfile's pins are here: `sizeof` IS the migration
//   policy (load_team_keys' exact size check), and test/ can only measure the HOST ABI, so pin it where it compiles
//   on ARM and Xtensa too. 4 + 32 + 32 + 4 = 72 with alignof 4 and ⛔ NO tail padding — which is what `reserved[4]`
//   buys and why it is spelled out. Blob = 8-byte header + 4 × 72 = 296 B (the owner's estimate, now MEASURED).
static_assert(sizeof(TeamKeyRecord) == 72, "device_nv.h: the /mrteams record layout moved — bump kTeamKeyVersion");
static_assert(alignof(TeamKeyRecord) == 4, "device_nv.h: /mrteams record alignment moved — the 72-byte claim is ABI-dependent");
static_assert(sizeof(TeamKeyBlob) == 8 + 4 * 72, "device_nv.h: the /mrteams blob layout moved — bump kTeamKeyVersion");

// ---- UI PRESET CATALOG record (`/mrui`) — §UI-10/11 slice P1 -------------------------------------------
// Spec: docs/superpowers/specs/2026-08-25-ui10-11-preset-catalog-spec.md §3-P1 (owner-approved 2026-08-25), over the
// parent design `2026-07-31-onboard-oled-ui-design.md` §3.2.2 (the catalog table) / §3.2.3 (persistence).
//
// ★★★ WHY A RECORD OF ITS OWN AND ⛔ NEVER A `Blob` FIELD — the design says it in one sentence and it is the whole
//     reason this struct exists: *"Do not grow `mrnv::Blob`: its size/version mismatch deliberately REPROVISIONS the
//     whole node, and editing a phrase must never reset radio, identity, team or key configuration."* ⇒ ★ EDITING A
//     PHRASE CAN NEVER REPROVISION THE NODE, and that property is delivered by the SEPARATION — its own magic, its
//     own version, its own slot — with ⛔ not one line of policy anywhere else.
// ⛔ A NEW NV RECORD IS NOT A WIRE CHANGE. No frame moves; nothing re-anchors (the `/mrteams` note's point).
//
// ★★ SEVENTEEN FIXED SLOTS, and the count is the DESIGN's (§3.2.2): ONE mandatory `emergency` + EIGHT `dm` + EIGHT
//    `channel`. Fixed because a record must have ONE size (the `slot_size_ok` policy); the VISIBLE count is not fixed
//    — zero to eight of each may be enabled, and gaps are valid. Raising a capacity is an explicit format revision.
// ★ THE INDEX ORDER IS THE STABLE SLOT IDENTITY: 0 = emergency, 1..8 = dm1..dm8, 9..16 = channel1..channel8. The
//   design forbids deriving `dmN` from a compose-list ROW index (§B66's cure), so the array index IS the id.
struct UiPresetSlot {
    // ★ EXACTLY 0 OR 1, ⛔ never "non-zero is true" (the owner-ruled canonical bytes): a record whose flag byte reads
    //   `2` would compare unequal to the same catalog written canonically and so rewrite flash forever. The predicate
    //   that REFUSES such a record is `mrfw::preset_slot_canonical` — this header owns the LAYOUT, that one the
    //   CONTENT policy (the `/mrjoin` split: `join_blob_state` here, `validate_profile` there).
    uint8_t enabled;
    uint8_t loc;       // `include_location` — the row's `L` / `-` marker (§3.2.2)
    uint8_t len;       // 0..kUiPresetTextMax; ★ the bytes AT and AFTER `len` are ZERO (canonical)
    // ★★ 18 = 17 CHARACTERS + THE CANONICAL TERMINATOR, and the 17 is OQ-A's owner ruling (2026-08-25): the compose
    //    row ALWAYS shows a selection marker AND a location marker, so BOTH location states consume 2 of the panel's
    //    19 columns. ⛔ The draft's conditional bound (18 when loc=off) was WRONG and is kept visible in the spec.
    // ⓘ ⛔ NO `reserved` MEMBER, and that is a measurement rather than an omission: 3 × uint8 + 18 char = 21 with
    //   alignof 1 and NO implicit padding, so there is nothing for one to fix — the `JoinProfile` ruling verbatim.
    char    text[18];
};
constexpr uint8_t kUiPresets       = 17;   // 1 emergency + 8 dm + 8 channel — the design's §3.2.2 table
constexpr uint8_t kUiPresetTextMax = 17;   // OQ-A: 17 printable ASCII bytes for EVERY preset, both location states
struct UiPresetBlob {
    uint32_t magic;       // kUiPresetMagic
    uint16_t version;     // kUiPresetVersion — EQUALITY (see ui_preset_blob_state)
    uint16_t reserved;    // ★ NAMED header padding, zeroed by ui_preset_blob_init — part of the byte-identical compare
    // ★★★ THE PERSISTED GENERATION (§3.2.3). NON-ZERO by construction: it starts at 1 and SKIPS ZERO on wrap
    //     (`mrfw::preset_generation_next`), so `0` is available as "no generation" and a `SendReq` can never seal one.
    //     Consumers compare it for EQUALITY, never ordering, which is what makes uint32 wrap harmless.
    uint32_t generation;
    UiPresetSlot slot[kUiPresets];
    // ★★★ NAMED TAIL PADDING, AND IT IS LOAD-BEARING ARITHMETIC, ⛔ not symmetry: the header is 12 B and 17 × 21 =
    //     357, so the struct body ends at 369 — which is NOT a multiple of `alignof(UiPresetBlob)` (4, from `magic`).
    //     A compiler therefore inserts THREE bytes of IMPLICIT tail padding, and implicit padding is INDETERMINATE
    //     after `UiPresetBlob{}` — which would make the whole-record `memcmp` the write-coalescing policy IS answer
    //     differently on identical catalogs and rewrite flash for nothing. ⇒ the three bytes are DECLARED, so they
    //     are zeroed by value-initialisation like every other member. (`TeamKeyRecord::reserved[4]`'s rule, arrived
    //     at from the other direction: there the named member REMOVES padding, here it REPLACES it.)
    uint8_t  reserved_tail[3];
};
constexpr uint32_t kUiPresetMagic   = 0x4D525531u;   // 'MRU1' — its OWN magic, ⛔ never kMagic ('MRC1'), never
                                                     // kJoinMagic ('MRJ1'), never kTeamKeyMagic ('MRK1')
constexpr uint16_t kUiPresetVersion = 1;             // v1: the first /mrui layout. A bump REJECTS the old record
                                                     // outright (equality policy) -> the node comes up on the
                                                     // COMPILED DEFAULTS, which is a safe and visible state.
// ★ PER-ABI, NOT native-only — the reason PeerRec's, JoinProfile's and TeamKeyRecord's pins are here: `sizeof` IS the
//   migration policy (load_ui_presets' exact size check), and test/ can only measure the HOST ABI, so pin it where it
//   compiles on ARM and Xtensa too. 3 + 18 = 21 with alignof 1; blob = 12-B header + 17 × 21 + 3 named tail = 372.
static_assert(sizeof(UiPresetSlot) == 21, "device_nv.h: the /mrui slot layout moved — bump kUiPresetVersion");
static_assert(alignof(UiPresetSlot) == 1, "device_nv.h: /mrui slot alignment moved — the 21-byte claim is ABI-dependent");
static_assert(sizeof(UiPresetBlob) == 12 + 17 * 21 + 3, "device_nv.h: the /mrui blob layout moved — bump kUiPresetVersion");
// ★★ AND THE POINT OF THE NAMED TAIL, ASSERTED RATHER THAN ARGUED: the declared members must account for EVERY byte,
//    or an indeterminate hole is back and the coalescing compare is unsound again.
static_assert(sizeof(UiPresetBlob) % alignof(UiPresetBlob) == 0,
              "device_nv.h: /mrui carries IMPLICIT tail padding — reserved_tail no longer closes the record");

// ---- slot table --------------------------------------------------------------------------------------
// The ONE place each record's storage names live. Both live backends address the same four records with
// different models, so a slot carries both spellings and each arm reads the field it needs.
// ⚠ `/mrfault` is in its OWN NVS namespace ON PURPOSE: `factory_erase()` clears "mr" in one shot (config +
// identity + peers) and the HW fault history must SURVIVE that (the nRF52 arm achieves the same by saving
// it back after the format). That asymmetry is DATA here rather than a forked code path.
struct Slot { const char* path; const char* ns; const char* key; };
inline constexpr Slot kSlotCfg   { "/mrcfg",   "mr",      "cfg"   };
inline constexpr Slot kSlotId    { "/mrid",    "mr",      "id"    };
inline constexpr Slot kSlotPeers { "/mrpeers", "mr",      "peers" };
inline constexpr Slot kSlotFault { "/mrfault", "mrfault", "log"   };
// §UI-15 slice 2 — the join-profile presets. ★★ `"mr"` IS THE FACTORY-RESET RULING, EXPRESSED AS DATA: the ESP32
// `factory_erase()` clears the whole `"mr"` namespace in one `clear()`, and the nRF52 arm's `InternalFS.format()`
// takes every file, so a profile store named here is DELETED by a factory reset with ⛔ not one line of new code.
// /mrfault is the deliberate exception ABOVE and this record is emphatically not one: presets are user
// configuration, so the fault-history precedent does NOT apply (spec §3).
inline constexpr Slot kSlotJoin  { "/mrjoin",  "mr",      "join"  };
// §UI-16 K1 — the team-key keyring. ★★ `"mr"` IS THE FACTORY-RESET RULING, EXPRESSED AS DATA, exactly as it is for
// `/mrjoin` one line above: the ESP32 `factory_erase()` clears the whole `"mr"` namespace in one `clear()` and the
// nRF52 arm's `InternalFS.format()` takes every file, so *"factory reset ERASES /mrteams"* is delivered by the
// namespace choice alone, with ⛔ not one line of new code. `/mrfault` is the deliberate exception ABOVE, and a
// store of team SECRETS is emphatically not one — a factory-reset device must not keep the team's content key.
// ⛔⛔ AND IT IS DELIBERATELY **NOT** IN `mount_or_repair()`'s nRF52 PROBE LIST (`kFiles[]`), for the same reason
//    `/mrjoin` is not: that function recovers by `InternalFS.format()`, so listing an optional store there would
//    make ITS corruption destroy identity AND config. A failed/short/invalid read is handled LOCALLY, as
//    `TeamKeyRead::invalid` / `io_failed`.
inline constexpr Slot kSlotTeams { "/mrteams", "mr",      "teams" };
// §UI-10/11 P1 — the UI preset catalog. ★★ `"mr"` IS THE FACTORY-RESET RULING, EXPRESSED AS DATA, exactly as it is
// for `/mrjoin` and `/mrteams` above: the ESP32 `factory_erase()` clears the whole `"mr"` namespace in one `clear()`
// and the nRF52 arm's `InternalFS.format()` takes every file, so the spec's *"factory reset erases `/mrui`"* is
// delivered by the namespace choice alone, with ⛔ not one line of new code. `/mrfault` is the deliberate exception
// ABOVE and configured phrases are emphatically not one — a factory-reset device must come up on the compiled texts.
// ⛔⛔ AND IT IS **A DIFFERENT SLOT FROM `kSlotCfg`, WHICH IS THE WHOLE SEPARATION**: `/mrcfg` is the record whose
//    version mismatch REPROVISIONS the node, so a phrase edit that landed there could reset radio, identity, team and
//    key configuration. The design forbids it in as many words; here is where the forbidding is DATA.
// ⛔ Deliberately NOT in `mount_or_repair()`'s nRF52 probe list (`kFiles[]`), for the reason `/mrjoin` and `/mrteams`
//    are not: that function recovers by `InternalFS.format()`, so listing an OPTIONAL store there would make ITS
//    corruption destroy identity AND config. A failed/short/invalid read is handled LOCALLY, as `UiPresetRead`.
inline constexpr Slot kSlotUi    { "/mrui",    "mr",      "ui"    };

// ---- record validation — ONE definition, DELIBERATELY ABOVE the platform `#if` -----------------------
// This predicate was hand-written SIX times (Blob/IdBlob/PeerBlob × the two backend arms) inside those
// Arduino-only arms, so nothing in test/ could reach it and the version policy was unverifiable off-device.
// Hoisted, it is a host unit (test/test_device_nv.cpp) — and a "reject an old-version record" test becomes
// runnable at all, which is why NV1 came before the peer-address-book slice that needs one.
//
// ★ THE TWO VERSION POLICIES DIFFER, AND BOTH ARE PRESERVED EXACTLY — the six copies hid that they did:
//   /mrcfg   (Blob)     accepts a RANGE, `version >= 2 && <= kVersion`: an older-but-parsable config loads
//                       and is re-stamped in place by nv_load_stamped (src/firmware_config.cpp).
//   /mrid    (IdBlob)   \ EQUALITY only — a mismatch rejects the record outright, so the node re-mints its
//   /mrpeers (PeerBlob) / identity or comes up with an EMPTY address book (kVersion's REPROVISION-ON-REFLASH note).
// The policy is now a NAMED call at the one wrapper instead of a hand-copied comparison, so changing one
// record's policy is one line and cannot leak into another's. `blob_valid_exact` IS the degenerate range —
// one comparison core, two names, no fork (U1).
// ★ §AB1 KEPT /mrpeers ON EQUALITY at the v1 -> v2 bump — deliberately, not by inertia. See load_peers below.
//
// `n` is SIGNED and that is load-bearing: nRF52's `File::read()` returns a NEGATIVE on a corrupt LittleFS
// CTZ block — the very signal `mount_or_repair()` keys its self-heal on — so it must never be laundered
// through an unsigned type where it would compare as a huge length.
inline bool slot_size_ok(int n, size_t want) {
    return n >= 0 && static_cast<size_t>(n) == want;   // short, over-long, absent (0) and error (<0) all fail
}
template <typename BlobT>
inline bool blob_valid_range(const BlobT& b, int n, uint32_t magic, uint16_t v_min, uint16_t v_max) {
    return slot_size_ok(n, sizeof(BlobT)) && b.magic == magic && b.version >= v_min && b.version <= v_max;
}
template <typename BlobT>
inline bool blob_valid_exact(const BlobT& b, int n, uint32_t magic, uint16_t version) {
    return blob_valid_range(b, n, magic, version, version);
}

// ---- what the PRIMITIVE saw, beyond its `int` (§UI-15 slice 2 CORRECTION, 2026-08-19) -----------------
// ★★★ THE DEFECT THIS EXISTS FOR: `read_slot` answered a single `int`, and BOTH live arms folded "the BACKEND would
//     not open" into the SAME `-1` they use for "there is no such record". `join_blob_state` then mapped `-1` to
//     `absent` unconditionally — so a filesystem that would not mount announced **NO PROFILES**, defeating this
//     record's whole honesty requirement one layer beneath the service that implements it.
// ★★ …AND `read_slot` IS SHARED WITH `/mrcfg`, `/mrid`, `/mrpeers`, `/mrfault`, whose load semantics must not move.
//    ⇒ THE SHAPE CHOSEN IS AN **OUT-PARAM**, not a second read path and ⛔ not a new negative sentinel:
//      · the `int` return is UNCHANGED on every path of every arm, so the four bool records — which consult it ONLY
//        through `slot_size_ok` (directly, or via blob_valid_range) — cannot observe a difference. That is asserted,
//        not asserted-by-comment: test_device_nv.cpp drives all four over the new arms.
//      · a second `read_join_slot` per arm would FORK the primitive NV1 spent a slice de-duplicating (U1), and would
//        have to be maintained in parallel forever for a record that reads exactly like the others.
//      · `io` DEFAULTS TO `nullptr` and every extra backend QUESTION is asked only when it is non-null, so the four
//        other records issue the IDENTICAL sequence of LittleFS / NVS calls they issued before.
struct SlotIo {
    // ⛔ NOT "the read failed" and ⛔ not "the record is bad" — this is "the STORE refused to answer, so NOTHING is
    //    known about the record": LittleFS would not mount, its `lfs_stat` lookup answered a METADATA error, a file
    //    the lookup said EXISTS would not open ([[B218]] cases 1/3/4 — allocation failure, `lfs_file_open` failure),
    //    or NVS would not open for a reason other than "this namespace has never been written".
    bool backend_failed = false;
    // ★ THE STORED RECORD IS LONGER THAN THE CALLER'S BUFFER. nRF52 reads only `len` bytes, so a longer file
    //   returns EXACTLY `sizeof(JoinBlob)` and a valid PREFIX would be accepted as the whole record. The length
    //   alone can never show this; only the file's SIZE can.
    bool oversize = false;
};

// ★ THE ABSENT SENTINEL IS THE PRIMITIVE'S CONTRACT, NAMED. Both live `read_slot` arms return EXACTLY -1 for "there
//   is no such slot" (nRF52: the tri-state lookup answered LFS_ERR_NOENT — or, with no `io` asked for, `open()`
//   failed; ESP32: `!has_key()`, or the namespace has never been written), and the
//   host stub returns it unconditionally. ⚠ A nRF52 CORRUPT-CTZ read returns a *different* negative (an LFS_ERR_*,
//   e.g. -84 / -5), which is why this is `== kSlotAbsent` and ⛔ not `< 0`: `< 0` would report a corrupt block as a
//   fresh device, which is the exact dishonesty above.
// ⛔⛔ THE ARMS ALSO RETURN -1 WHEN THE **BACKEND** WOULD NOT OPEN, and no sentinel can fix that without changing
//    what the four other records see — which is precisely why that fact rides `SlotIo::backend_failed` and is
//    consulted BEFORE this comparison. The comment that once said "if a backend ever returned -1 for a read error,
//    that error would read as absent — no host test can rule that out" was describing a LIVE DEFECT, not a residual
//    limit: both arms did exactly that. It is closed above, and `fs_read_slot`/`nvs_read_slot` are host-drivable so
//    the closure is measured rather than argued.
constexpr int kSlotAbsent = -1;

// ---- the two backend read SEQUENCES, hoisted so a host test can drive them ----------------------------
// ★★ WHY THESE ARE TEMPLATES AND NOT INLINE INSIDE THE `#if` ARMS: the arms are unreachable from the native suite,
//    so a property decided there is a property no automated gate can fail. Hoisted, `test_device_nv.cpp` drives the
//    REAL control flow — the real branch ORDER, the real early returns — against a fake FS / fake NVS, instead of
//    a test hand-feeding `join_blob_state` a return value the shipped backend can never produce. (That vacuous
//    shape is exactly what the §UI-15 slice 2 QG HOLD found in the over-length case.)
// ⛔ The adapters below are the only untested residue: one-line forwards plus the nRF52 `lookup` (a locked stat,
//    three straight-line calls), with no branch of their own — every classification is up here where the suite is.

// nRF52 / Adafruit LittleFS — the slots are FILES.
// ★★★ [[B218]] REOPENED 2026-08-19: `File::open() == false` IS NOT "ABSENT" — the vendored `File::_open`
//     (Adafruit_LittleFS_File.cpp:121-153) does an `lfs_stat` FIRST and answers ONE `false` for FOUR facts:
//       1. the stat rc is neither LFS_ERR_OK nor LFS_ERR_NOENT      -> a METADATA error;
//       2. LFS_ERR_NOENT in read mode                                -> the only genuine ABSENT (first boot);
//       3. `rtos_malloc` failed inside `_open_file`                  -> an allocation failure;
//       4. `lfs_file_open` failed on a file that EXISTS              -> an open failure.
//     Only (2) is a fresh device; (1)/(3)/(4) are the store refusing to answer — `backend_failed`'s fact. So the
//     caller that asks (`io != nullptr`) gets a TRI-STATE lookup: the adapter surfaces the RAW `lfs_stat` rc (a
//     locked stat mirroring the library's own `exists()`, which collapses the rc and so cannot be the lookup),
//     and THIS template owns the classification — ⛔ never the adapter, which no automated gate compiles ([[B221]]:
//     a shim signature that erases the distinction is exactly how this defect survived one round).
template <typename FsT>
inline int fs_read_slot(FsT& fs, const char* path, void* dst, size_t len, SlotIo* io) {
    // ★ THE MOUNT RESULT IS NO LONGER DISCARDED. ⓘ The `int` is unchanged by this early return: with the FS
    //   unmounted `open()` cannot succeed either, so this path already answered kSlotAbsent — what is new is that
    //   it now SAYS SO in `io` instead of being indistinguishable from a first boot.
    if (!fs.mount())    { if (io) io->backend_failed = true; return kSlotAbsent; }
    if (io) {
        // ★ ONLY `kAbsentRc` (LFS_ERR_NOENT) means a fresh device. The constants are the ADAPTER's, so the branch
        //   is host-drivable against `FakeFs`, and the branch is HERE, so the adapter stays decision-free.
        const int rc = fs.lookup(path);
        if (rc == FsT::kAbsentRc) return kSlotAbsent;        // (2) no such record — an ordinary first boot
        if (rc != FsT::kFoundRc)  { io->backend_failed = true; return kSlotAbsent; }  // (1) metadata error — ⛔ the open is NOT attempted
        // (3)/(4): the lookup said PRESENT, yet the file would not open — ⛔ never `absent`: four possibly-intact
        // profiles must not read as a fresh device because an allocation or lfs_file_open failed.
        if (!fs.open(path)) { io->backend_failed = true; return kSlotAbsent; }
    } else {
        // ⛔ THE FOUR BOOL RECORDS' PATH, byte-for-byte the sequence they issued before this correction: no lookup,
        //    no size question — mount -> open -> read -> close, same `int` on every path. Collapsing open()'s four
        //    facts is CORRECT for them: absent and unreadable /mrcfg both mean "come up on defaults".
        if (!fs.open(path)) return kSlotAbsent;
    }
    if (io && fs.size() > len) io->oversize = true;          // ★ a longer file would otherwise read as a valid PREFIX
    const int n = fs.read(dst, len);                         // ⚠ < 0 on a corrupt CTZ block (mount_or_repair)
    fs.close();
    return n;
}
// ESP32 / Preferences NVS — the slots are KEYS in a namespace.
template <typename NvsT>
inline int nvs_read_slot(NvsT& nvs, const char* ns, const char* key, void* dst, size_t len, SlotIo* io) {
    // ★★ `Preferences::begin(ns, readOnly=true)` ANSWERS ONE `false` FOR TWO OPPOSITE FACTS: "this namespace has
    //    never been written" — an ORDINARY first boot, since `mr` is created by the first save of ANY record — and
    //    "NVS itself would not open". ⇒ the arm classifies with the ESP-IDF error code; ⛔ calling it a storage
    //    failure unconditionally would make a FRESH device report STORAGE FAILURE, which is the same dishonesty
    //    pointing the other way.
    if (!nvs.open(ns)) { if (io && !nvs.ns_absent(ns)) io->backend_failed = true; return kSlotAbsent; }
    if (!nvs.has_key(key)) { nvs.close(); return kSlotAbsent; }   // no record yet — silent, no NVS error log
    if (io && nvs.blob_len(key) > len) io->oversize = true;       // ★ the same PREFIX hazard, seen from the other arm
    const int n = nvs.get_bytes(key, dst, len);                   // 0 when the stored blob is LONGER than `len`
    nvs.close();
    return n;
}

// ---- /mrjoin: ABSENT and CORRUPT are DIFFERENT ANSWERS (§UI-15 §3, the honesty requirement) -----------
// ★★★ EVERY OTHER RECORD HERE COLLAPSES THE TWO INTO ONE `bool`, AND FOR THEM THAT IS RIGHT: an absent /mrcfg and a
//     corrupt /mrcfg both mean "come up on defaults". ⛔ FOR THE PROFILE STORE IT IS WRONG. A silent fallback would
//     make a corrupted store INDISTINGUISHABLE FROM A FRESH DEVICE — the operator would read `NO PROFILES`, retype
//     four presets, and never learn that the flash ate them. So the state is a THREE-VALUED answer, and the verbs
//     branch on all three (see src/firmware_join_profiles.h's absent/corrupt matrix).
// ⛔ It is `enum class`, not two bools: the states are mutually exclusive and a bool pair would admit a
//    meaningless combination — the binary-test-over-a-ternary-domain defect this arc is pre-registered against.
enum class JoinRead : uint8_t {
    ok,        // a record of the right size, magic and version was read
    absent,    // ★ NO RECORD AT ALL — an ordinary fresh-device state, ⛔ never an error
    invalid,   // ⛔ present but unreadable: short, over-long, wrong magic, wrong version, or a backend read ERROR
    // ★★★ THE FOURTH STATE (2026-08-19 correction), AND IT IS NOT A SYNONYM FOR `invalid`. `invalid` says "I READ
    //     THE STORE AND ITS CONTENTS ARE WRONG" — a fact about the RECORD, repairable by `joinprofile reset
    //     confirm`, which rewrites it. `io_failed` says "I COULD NOT READ THE STORE AT ALL" — a fact about the
    //     DEVICE, about which nothing is known of the record, and ⛔ over which a blind rewrite would destroy four
    //     possibly-intact profiles because a mount failed. The two therefore take DIFFERENT verb behaviour (see
    //     firmware_join_profiles.h) and DIFFERENT operator text; collapsing them is the bug one level up from the
    //     `absent` collapse this record already exists to prevent.
    io_failed,
};
// `io` carries what the `int` cannot (see SlotIo). ⓘ The default is for the PURE cases — the magic/version/length
// matrix, which is about the BYTES and has no backend behind it. The one live caller (`load_join`) always passes
// what the primitive actually reported.
inline JoinRead join_blob_state(const JoinBlob& b, int n, const SlotIo& io = SlotIo{}) {
    // ★★ FIRST, AND AHEAD OF THE `absent` TEST ON PURPOSE: this is the arm that used to be laundered into "NO
    //    PROFILES". A backend that would not open returns kSlotAbsent, so any later ordering would lose it.
    if (io.backend_failed) return JoinRead::io_failed;
    // ★ AN OVER-LENGTH RECORD IS `invalid`, ⛔ never `ok`. `n` alone cannot see it: nRF52 reads `len` bytes out of a
    //   longer file and returns EXACTLY `len`, so a valid PREFIX would pass every check below.
    if (io.oversize) return JoinRead::invalid;
    if (n == kSlotAbsent) return JoinRead::absent;
    // EQUALITY on the version (blob_valid_exact), like /mrid and /mrpeers and ⛔ unlike /mrcfg's range: there is no
    // migration arm for presets, and there must not be — a rejected record costs the operator four retypes, whereas
    // a migration path is code that runs once per chip and can then never be exercised again.
    return blob_valid_exact(b, n, kJoinMagic, kJoinVersion) ? JoinRead::ok : JoinRead::invalid;
}
// Stamp an EMPTY, VALID four-slot record. ONE path (U2) — the magic/version/zeroing triple is never re-typed at a
// write site, exactly as `peers_blob_init` exists so that it is not. ★ `JoinBlob{}` zeroes `reserved` and every
// profile byte, which is what makes the whole-record byte compare a valid "nothing changed".
inline void join_blob_init(JoinBlob& b) {
    b = JoinBlob{};
    b.magic   = kJoinMagic;
    b.version = kJoinVersion;
}

// ---- /mrteams: the SAME FOUR-STATE READ, for a record whose contents are SECRETS (§UI-16 K1) ----------
// ★★★ WHY THE FOUR STATES ARE OWED HERE TOO, and it is not symmetry for its own sake: this store answers
//     "is the team's content key still on this device?". An ABSENT store is an ordinary fresh device and the node
//     comes up keyless; a CORRUPT one means the flash ate an UNRECOVERABLE secret (⛔ no seed re-derives a team
//     content key) and the operator must be told, because the remedy is a re-grant from a teammate; and an
//     `io_failed` store means NOTHING IS KNOWN — over which a blind rewrite would destroy up to four intact keys
//     because a mount failed transiently. Collapsing any pair would make one of those three read as another.
// ⓘ U1, CONSIDERED AND ANSWERED IN PLACE: `JoinRead` (above) carries the same four states, and this enum is a
//   SIBLING rather than a reuse — its four arms are documented in `/mrjoin`'s terms (profiles, `joinprofile reset
//   confirm`, four retypes), none of which is true of a key store, and `join_read_unreadable` / the
//   `ProfileErr` relabel are `/mrjoin` verb vocabulary. ★ A SHARED classifier under a record-neutral name is the
//   right end state and is NOT taken here: it would be a refactor of a shipped record folded into a feature slice
//   (C1). What IS shared, and deliberately so, are the primitives underneath — `SlotIo`, `kSlotAbsent`,
//   `slot_size_ok` and `blob_valid_exact` — so the version/length/backend policy has one implementation.
enum class TeamKeyRead : uint8_t {
    ok,        // a record of the right size, magic and version was read
    absent,    // ★ NO RECORD AT ALL — an ordinary fresh-device state, ⛔ never an error
    invalid,   // ⛔ present but unreadable: short, over-long, wrong magic, wrong version, or a backend read ERROR
    io_failed, // ⛔ the STORE would not answer at all — a fact about the DEVICE, ⛔ not about the record
};
// The branch ORDER mirrors `join_blob_state`'s and for the same measured reasons: a backend that would not open
// returns `kSlotAbsent`, so testing `absent` first would launder a dead store into "no keys stored"; and an
// OVER-LENGTH record is `invalid` and ⛔ never `ok`, because nRF52 reads `len` bytes out of a longer file and a
// valid PREFIX would otherwise pass every check below.
inline TeamKeyRead team_key_blob_state(const TeamKeyBlob& b, int n, const SlotIo& io = SlotIo{}) {
    if (io.backend_failed) return TeamKeyRead::io_failed;
    if (io.oversize)       return TeamKeyRead::invalid;
    if (n == kSlotAbsent)  return TeamKeyRead::absent;
    // EQUALITY on the version, like /mrid, /mrpeers and /mrjoin and ⛔ unlike /mrcfg's range: there is no migration
    // arm for a key store and there must not be one — a rejected record leaves the node KEYLESS, which is safe and
    // visible, whereas a half-understood migration would install bytes under a layout guess.
    return blob_valid_exact(b, n, kTeamKeyMagic, kTeamKeyVersion) ? TeamKeyRead::ok : TeamKeyRead::invalid;
}
// Stamp an EMPTY, VALID keyring. ONE path (U2) — the magic/version/count triple is never re-typed at a write site,
// exactly as `peers_blob_init` and `join_blob_init` exist so that it is not. ★ `TeamKeyBlob{}` zeroes every record
// INCLUDING `reserved`, which is what makes the per-record byte compare a valid "nothing changed".
inline void team_key_blob_init(TeamKeyBlob& b) {
    b = TeamKeyBlob{};
    b.magic   = kTeamKeyMagic;
    b.version = kTeamKeyVersion;
    // ⓘ REDUNDANT BY CONSTRUCTION, AND SAID SO RATHER THAN LEFT TO LOOK LIKE COVERAGE: the value-initialisation
    //   above already zeroed `count`, so ⛔ no mutation can redden this line. It is kept for symmetry with
    //   `peers_blob_init`, which spells the same triple for the same reason.
    b.count   = 0;
}

// ---- /mrui: THE SAME FOUR-STATE READ, for the record a wearer's phrases live in (§UI-10/11 P1) --------
// ★★★ WHY THE FOUR STATES ARE OWED HERE TOO, and the spec RULES each arm's behaviour rather than leaving it to a
//     reader (§3-P1): an ABSENT store is an ordinary first boot and the node runs the COMPILED DEFAULTS with ⛔ NO
//     warning; an INVALID one means the wearer's configured phrases are gone, which he must be TOLD (a counted,
//     visible warning) because the panel is now showing texts he did not choose; and an `io_failed` store means
//     NOTHING IS KNOWN about the record, over which a blind rewrite would destroy a possibly-intact catalog because
//     a mount failed transiently — so every mutation refuses with `store` and ⛔ ZERO writes.
// ⓘ U1, CONSIDERED AND ANSWERED IN PLACE, exactly as `TeamKeyRead` answers it against `JoinRead`: the three enums
//   carry the same four arms and are SIBLINGS rather than one reuse — each documents its arms in ITS OWN record's
//   terms and each feeds a different verb vocabulary (`ProfileErr` / `KeyringErr` / `PresetErr`). ★ A shared
//   classifier under a record-neutral name is the right end state and is ⛔ NOT taken here: it would be a refactor of
//   three shipped records folded into a feature slice (C1). What IS shared, deliberately, are the primitives —
//   `SlotIo`, `kSlotAbsent`, `slot_size_ok`, `blob_valid_exact`.
enum class UiPresetRead : uint8_t {
    ok,        // a record of the right size, magic and version was read
    absent,    // ★ NO RECORD AT ALL — an ordinary first boot, ⛔ never an error and ⛔ never warned about
    invalid,   // ⛔ present but unreadable: short, over-long, wrong magic, wrong version, or a backend read ERROR
    io_failed, // ⛔ the STORE would not answer at all — a fact about the DEVICE, ⛔ not about the record
};
// The branch ORDER mirrors `join_blob_state`'s and `team_key_blob_state`'s, for their measured reasons: a backend
// that would not open returns `kSlotAbsent`, so testing `absent` first would launder a dead store into "no catalog
// configured"; and an OVER-LENGTH record is `invalid` and ⛔ never `ok`, because nRF52 reads `len` bytes out of a
// longer file and a valid PREFIX would otherwise pass every check below.
// ⛔ IT JUDGES THE **STORAGE**, ⛔ NOT THE CATALOG. Whether the seventeen slots obey the owner-ruled canonical-byte
//    rules is `mrfw::presets_canonical`'s question, one layer up, where the CONTENT policy lives (the `/mrjoin`
//    split: the magic/version/length matrix here, `validate_profile` in the service header).
inline UiPresetRead ui_preset_blob_state(const UiPresetBlob& b, int n, const SlotIo& io = SlotIo{}) {
    if (io.backend_failed) return UiPresetRead::io_failed;
    if (io.oversize)       return UiPresetRead::invalid;
    if (n == kSlotAbsent)  return UiPresetRead::absent;
    // EQUALITY on the version, like /mrid, /mrpeers, /mrjoin and /mrteams and ⛔ unlike /mrcfg's range: there is no
    // migration arm for a phrase catalog and there must not be one — a rejected record costs the operator a retype
    // of what he configured and is VISIBLE, whereas a migration path is code that runs once per chip and can then
    // never be exercised again.
    return blob_valid_exact(b, n, kUiPresetMagic, kUiPresetVersion) ? UiPresetRead::ok : UiPresetRead::invalid;
}
// Stamp an EMPTY, VALID catalog record — magic, version and the FIRST generation. ONE path (U2), exactly as
// `peers_blob_init` / `join_blob_init` / `team_key_blob_init` exist so the triple is never re-typed at a write site.
// ★ `UiPresetBlob{}` zeroes `reserved`, `reserved_tail` and every slot byte, which is what makes the whole-record
//   byte compare a valid "nothing changed".
// ⛔ IT LEAVES THE SEVENTEEN SLOTS **EMPTY**, ⛔ NOT DEFAULTED: the COMPILED DEFAULTS are a UI policy (the §3.2.2
//    table) and live in `firmware_ui_presets.h`. A storage header that knew the wearer's phrases would be the second
//    authority over them.
inline void ui_preset_blob_init(UiPresetBlob& b) {
    b = UiPresetBlob{};
    b.magic      = kUiPresetMagic;
    b.version    = kUiPresetVersion;
    // ★★ 1, ⛔ NEVER 0 — and this line is the only place the FIRST generation is spelled. `0` is reserved for "no
    //    generation" (see UiPresetBlob::generation), so a record stamped with it would be rejected as non-canonical
    //    by the very predicate that protects a `SendReq` from sealing one.
    b.generation = 1;
}

// ---- /mrpeers RECORD POLICY — pure, and ABOVE the platform `#if` for the SAME reason as blob_valid_* ----------
// §AB1. This is the SELECTION + EVICTION policy of the address book, and it is deliberately NOT in
// firmware_commands.cpp: `test_build_src = no` keeps every `src/*.cpp` out of the native build, so a policy living
// there would be as untestable as the six hand-copied validators NV1 hoisted — and this one has a
// security-relevant invariant (a pinned key must never be silently lost or invented).
//
// ★ IT MIRRORS Node::peer_key_set (lib/core/node_hashlocate.cpp) AND DOES NOT FORK IT (U1) — same four rules:
//   1. a stored PINNED record is IMMUTABLE to an on-air (non-pinned) set — a complete no-op, NOT EVEN a name
//      refresh, exactly as the RAM path returns early. (If NV refreshed a name RAM had refused, a reboot would
//      show a label the live table never held.)
//   2. UPGRADE, NEVER DOWNGRADE: the key + confidence move only upwards, or on a user re-pin;
//   3. a PINNED record is NEVER the eviction victim — pinned-over-authoritative (spec §2.4);
//   4. every slot pinned + a new hash => REFUSE (C2 fail loud), never drop a human-verified key.
// ✖ THE ONE DELIBERATE DIVERGENCE FROM THE RAM POLICY: RAM evicts the least-recently-SEEN non-pinned entry;
//   PeerRec carries NO timestamp (the spec's record has none), so NV evicts the OLDEST-INSERTED non-pinned record —
//   `rec[]` index order IS insertion order, kept so by the shift-down compaction in peer_rec_put, so "the first
//   non-pinned" IS the oldest. A refresh deliberately does NOT promote a record to the back: reordering the array
//   on every re-cache would move the whole blob's bytes and so defeat the `unchanged` wear-guard below. FIFO with
//   no flash write beats LRU with one write per re-cache — this store lives on flash, the RAM one does not.
enum class PeerPut : uint8_t {
    unchanged,      // the store already says exactly this -> ★ the caller MUST NOT write (the flash-wear guard)
    updated,        // an existing record's key / name / confidence moved
    inserted,       // a free slot took it
    evicted,        // the store was full -> the oldest NON-PINNED record was dropped to make room
    refused_full,   // every slot is PINNED -> nothing dropped, nothing stored (C2)
    refused_conf,   // not a confidence this store persists (`overheard`, or an unrecognised byte) (C2)
    refused_absent, // the LIVE table no longer holds this hash, so there is nothing to mirror (C2 — see peer_store_sync:
                    // a peer_key_cached push is drained AFTER the cache event, and a flood can evict the entry in
                    // between. Distinct from refused_conf on purpose: "gone from RAM" and "a confidence we refuse to
                    // store" are different facts and must not share a label.)
};
// enum -> string, `default`-LESS so -Wswitch fails the build when a seventh outcome is added. This project has
// shipped THREE enum->string defects that the byte-identity gate was structurally blind to; the all-enumerators
// case in test/test_device_nv.cpp is the other half of that lesson.
inline const char* peer_put_name(PeerPut r) {
    switch (r) {
        case PeerPut::unchanged:    return "unchanged";
        case PeerPut::updated:      return "updated";
        case PeerPut::inserted:     return "inserted";
        case PeerPut::evicted:      return "evicted";
        case PeerPut::refused_full: return "refused_full";
        case PeerPut::refused_conf: return "refused_conf";
        case PeerPut::refused_absent: return "refused_absent";
    }
    return "?";     // no `default:` arm — unreachable for a valid enumerator, but the function stays total
}
// Stamp an EMPTY v2 store. The magic/version/count triple used to be re-typed at each write site; one path (U2).
inline void peers_blob_init(PeerBlob& b) {
    b = PeerBlob{};
    b.magic = kPeersMagic;
    b.version = kPeersVersion;
    b.count = 0;
}
// THE one conversion path onto a PeerRec (U2 — never rebuild the carrier field-by-field at a call site). Merging
// onto a ZEROED `prev` is exactly the fresh-insert case, so insert and update cannot drift apart.
inline PeerRec peer_rec_merge(const PeerRec& prev, uint32_t key_hash32, const uint8_t ed_pub[32],
                              uint8_t conf, const char* name, uint8_t name_len) {
    PeerRec r{};                        // zeroed FIRST so the name tail AND _pad are deterministic — which is what
    r.key_hash32 = key_hash32;          // makes peer_rec_put's whole-record byte-compare a valid "nothing changed"
    const bool carries_name = (name && name_len);
    uint8_t nl = carries_name ? name_len : prev.name_len;                     // rule: the name is MUTABLE, refreshed
    if (nl > sizeof r.name) nl = static_cast<uint8_t>(sizeof r.name);         // whenever one is carried; an empty name
    memcpy(r.name, carries_name ? name : prev.name, nl);                      // KEEPS the stored label. Clamp: a
    r.name_len = nl;                                                          // corrupt stored name_len cannot overrun.
    // rule 2 (and a user re-pin). ⚠ MEASURED 2026-07-31 (§AB1 poison probe P2): forcing this to `true` — i.e.
    // allowing a DOWNGRADE — moves NOTHING when peer_rec_put is the caller, because peer_rec_put's rule-1 early
    // return already intercepts the only reachable downgrade (stored pinned + incoming authoritative), and with only
    // two persistable levels every other combination IS an upgrade. So this conjunct is DEFENCE IN DEPTH that rule 1
    // currently shadows. It is KEPT, not deleted, because it is the invariant a DIRECT peer_rec_merge caller or a
    // THIRD confidence level would need — and it now has its own native case driving peer_rec_merge directly, so the
    // probe that found this gap fails if the rule is broken. Do not "simplify" it away on the strength of a green run.
    const bool upgrade = (conf == kPeerConfPinned) || (conf > prev.confidence);
    r.confidence = upgrade ? conf : prev.confidence;
    memcpy(r.ed_pub, upgrade ? ed_pub : prev.ed_pub, sizeof r.ed_pub);
    return r;
}
inline PeerPut peer_rec_put(PeerBlob& b, uint32_t key_hash32, const uint8_t ed_pub[32],
                            uint8_t conf, const char* name, uint8_t name_len) {
    if (!peer_conf_restorable(conf)) return PeerPut::refused_conf;            // C2: `overheard`/garbage is not persisted
    if (b.count > kMaxPeerRecs) b.count = kMaxPeerRecs;                       // a bit-rotted count must never index past rec[]
    for (uint16_t i = 0; i < b.count; ++i) {
        if (b.rec[i].key_hash32 != key_hash32) continue;
        if (b.rec[i].confidence == kPeerConfPinned && conf != kPeerConfPinned) return PeerPut::unchanged;   // rule 1
        const PeerRec want = peer_rec_merge(b.rec[i], key_hash32, ed_pub, conf, name, name_len);
        if (memcmp(&want, &b.rec[i], sizeof want) == 0) return PeerPut::unchanged;   // ★ the flash-wear guard
        b.rec[i] = want;
        return PeerPut::updated;
    }
    const PeerRec fresh = peer_rec_merge(PeerRec{}, key_hash32, ed_pub, conf, name, name_len);
    if (b.count < kMaxPeerRecs) { b.rec[b.count++] = fresh; return PeerPut::inserted; }
    uint16_t victim = kMaxPeerRecs;                                           // rules 3+4: the OLDEST non-pinned, or refuse
    for (uint16_t i = 0; i < kMaxPeerRecs; ++i)
        if (b.rec[i].confidence != kPeerConfPinned) { victim = i; break; }
    if (victim == kMaxPeerRecs) return PeerPut::refused_full;
    for (uint16_t i = victim; i + 1u < kMaxPeerRecs; ++i) b.rec[i] = b.rec[i + 1];   // compact: index order stays INSERTION order
    b.rec[kMaxPeerRecs - 1] = fresh;
    return PeerPut::evicted;
}

}  // namespace mrnv

// ----- platform slot primitives (header-inline; device_nv.h is included by the one device TU) ---------
// ★ THE ONLY PLATFORM-SPECIFIC CODE IN THIS FILE. Two functions per arm, where there used to be eight
// near-identical load/save wrappers differing only in the slot name and the read/write call.
#if defined(ARDUINO) && (defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262))
  #include <Adafruit_LittleFS.h>
  #include <InternalFileSystem.h>
namespace mrnv {
// nRF52: the slots are Adafruit LittleFS FILES, addressed by `Slot::path`.
// ⛔ NO DECISION LIVES HERE. The members forward the library calls; the branch ORDER and the two `io` facts are in
//    `fs_read_slot` above, where the native suite drives them. `lookup` is [[B218]]'s raw-rc stat: the library's
//    own `exists()` (Adafruit_LittleFS.cpp:137-146) is this exact locked-`lfs_stat` idiom but COLLAPSES the rc to
//    a bool, so it cannot be the lookup — the raw rc is the fact `fs_read_slot` classifies on. `_getFS`/`_lockFS`/
//    `_unlockFS` are public (Adafruit_LittleFS.h:78-80, "internal usage only" but callable; ⛔ no vendored file is
//    edited). `lookup` is only ever called AFTER `mount()` returned true, so the lfs handle is live.
struct InternalFsSlot {
    // ★ THE CLASSIFICATION CONSTANTS ARE DECLARED BY THE ADAPTER (FakeFs mirrors them), so the hoisted branch
    //   compares against the backend's OWN convention instead of a magic 0/-2 baked into the template.
    static constexpr int kFoundRc  = LFS_ERR_OK;      // the stat found the record
    static constexpr int kAbsentRc = LFS_ERR_NOENT;   // ⛔ the ONLY rc that means "fresh device" ([[B218]])
    Adafruit_LittleFS_Namespace::File f{InternalFS};
    int lookup(const char* path) {
        struct lfs_info info;
        InternalFS._lockFS();
        const int rc = lfs_stat(InternalFS._getFS(), path, &info);
        InternalFS._unlockFS();
        return rc;
    }
    bool     mount()                     { return InternalFS.begin(); }
    bool     open(const char* path)      { return f.open(path, Adafruit_LittleFS_Namespace::FILE_O_READ); }
    uint32_t size()                      { return f.size(); }
    int      read(void* dst, size_t len) { return f.read(dst, static_cast<uint16_t>(len)); }
    void     close()                     { f.close(); }
};
inline int read_slot(const Slot& s, void* dst, size_t len, SlotIo* io = nullptr) {
    InternalFsSlot fs;
    return fs_read_slot(fs, s.path, dst, len, io);
}
inline bool write_slot(const Slot& s, const void* src, size_t len) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    InternalFS.remove(s.path);                                 // overwrite (LittleFS append-only otherwise)
    File f(InternalFS);
    if (!f.open(s.path, FILE_O_WRITE)) return false;
    const size_t n = f.write(reinterpret_cast<const uint8_t*>(src), len);
    f.close();
    return n == len;
}
// `factory_reset confirm`: erase EVERY persisted NV slot -> the node boots brand-new (default config, fresh
// identity, no peers, empty inbox). FULL InternalFS.format() (spec 2026-06-28-factory-reset-format.md): the prior
// TARGETED remove()s could NOT recover FS-METADATA corruption (the bench brick: `cfg set` -> nv_save_failed that
// survived a reboot AND factory_reset). A format rebuilds the FS metadata -> recovers it; the net file outcome is the
// SAME as the old remove() path (config + identity + peers + inbox META /mri_dm,/mri_ch all gone) EXCEPT it also clears
// the corruption. /mrfault (HW fault history) is preserved across the format (choice B). The inbox RECORDS live on the
// SEPARATE external QSPI chip (a different FS) -> wiped by the inbox stores' wipe() in the command (their domain).
// VERIFIED SAFE: device_ota does not use InternalFS (the format won't touch OTA/DFU). format()==false => the flash
// cannot be formatted => WORN flash (handle_factory_reset surfaces the WARN = the real dead-node signal).
inline bool load_faults(mrfault::FaultLog& out);                // fwd-decls (defined with the other typed wrappers,
inline bool save_faults(const mrfault::FaultLog& b);            // after this arm): factory_erase preserves /mrfault
inline bool factory_erase() {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    static mrfault::FaultLog fl;                            // STATIC, not stack — keep the console-path frame small (the do_post_ack overflow lesson)
    const bool had = load_faults(fl);                      // preserve /mrfault across the format (best-effort)
    const bool ok  = InternalFS.format();                  // FULL format: clears FS-metadata corruption that remove() can't. false => WORN flash.
    InternalFS.begin();                                    // re-mount the clean FS so the load*() at boot run normally
    if (had) save_faults(fl);                              // restore the fault history onto the clean FS (choice B: preserve /mrfault)
    return ok;
}
// InternalFS self-heal (Part 2, 2026-06-24): mount + REPAIR-ON-CORRUPT. Returns true IFF it had to reformat (the
// caller logs loudly + sets a flag so `faults`/`version`/`status` surface it this boot). With LFS_NO_ASSERT (Part
// 1) a corrupt CTZ block now RETURNS an error instead of assert()-halting, so we can DETECT it — begin() fails, or
// a known file opens but read()s a negative (a corrupt skip-list head) — and RECOVER via InternalFS.format() -> a
// clean FS that boots. Call ONCE at the very top of setup(), before any load*(). ⚠ Cost: a reformat wipes /mrid too
// -> the node re-mints its identity + loses its join -> must be re-provisioned (a corrupt FS makes even /mrid
// suspect; identity-preservation across a corrupt-format is a flagged later refinement). This is ALSO the recovery
// image for an already-bricked node: it boots, sees the corruption, reformats, comes up clean.
inline bool mount_or_repair() {
    using namespace Adafruit_LittleFS_Namespace;
    bool corrupt = !InternalFS.begin();                         // FS-metadata corruption -> begin() fails
    if (!corrupt) {
        // ✖ NOT routed through the Slot table on purpose: this probe list is WIDER than the NV records —
        // /mri_dm and /mri_ch are the inbox CURSOR meta, which have no Slot (and no NVS twin). A half-symbolic,
        // half-literal list would read worse than this one, and the probe must cover every file the FS holds.
        static const char* const kFiles[] = { "/mrcfg", "/mrid", "/mrpeers", "/mri_dm", "/mri_ch", "/mrfault" };
        for (const char* path : kFiles) {
            File f(InternalFS);
            if (f.open(path, FILE_O_READ)) {                    // exists -> probe one read; an absent file (open false) is FINE
                uint8_t b; const int r = f.read(&b, 1);
                f.close();
                if (r < 0) { corrupt = true; break; }           // a read ERROR (LFS_ERR_CORRUPT; not EOF=0) -> corrupt block
            }
        }
    }
    if (corrupt) {
        InternalFS.format();                                    // wipe to a clean FS (all NV slots gone -> defaults + re-mint)
        InternalFS.begin();                                     // re-mount the clean FS so the load*() below run normally
    }
    return corrupt;
}
}  // namespace mrnv
#elif defined(ARDUINO) && (defined(ARDUINO_ARCH_ESP32) || defined(ESP32) || defined(BOARD_HELTEC_V3))
  #include <Preferences.h>
  #include <nvs.h>            // nvs_open's ERROR CODE — the ONLY way to tell "never written" from "would not open"
namespace mrnv {
// ESP32: the slots are Preferences/NVS KEY-VALUE pairs, addressed by `Slot::ns` + `Slot::key`.
// ⛔ NO DECISION LIVES HERE either — see `nvs_read_slot`. `ns_absent` is the one member with a body, and it is a
//    CLASSIFIER, not a policy: it answers ESP-IDF's question, and the policy that consumes it is hoisted.
struct PreferencesSlot {
    Preferences p;
    bool   open(const char* ns)          { return p.begin(ns, /*readOnly=*/true); }
    bool   has_key(const char* key)      { return p.isKey(key); }
    size_t blob_len(const char* key)     { return p.getBytesLength(key); }
    int    get_bytes(const char* key, void* dst, size_t len) { return static_cast<int>(p.getBytes(key, dst, len)); }
    void   close()                       { p.end(); }
    // ⛔ READONLY, ALWAYS. A read-write probe would CREATE the namespace — a FLASH WRITE inside a read path, and it
    //    would also make the very question ("has this ever been written?") answer itself yes forever after.
    bool ns_absent(const char* ns) {
        nvs_handle_t h = 0;
        const esp_err_t e = nvs_open(ns, NVS_READONLY, &h);
        if (e == ESP_OK) { nvs_close(h); return false; }   // it opened on the retry — the namespace exists
        return e == ESP_ERR_NVS_NOT_FOUND;
    }
    // ★★ THE RAW, GENUINELY THREE-VALUED BLOB LOOKUP ([[B134]] QG round 4). ⛔ `isKey()` CANNOT SERVE THIS AND
    //    THAT IS MEASURED, not suspected: it is `getType()`, which tries TEN typed `nvs_get_*` reads and falls
    //    through to `PT_INVALID` for `ESP_ERR_NVS_NOT_FOUND` **and every other NVS error alike**
    //    (`Preferences.cpp:302-350` in the pinned core) — so "this key was never written" and "this storage is
    //    corrupt" arrive as ONE `false`, and the corrupt case then enters the fresh path.
    //    `nvs_get_blob` reports them separately, and its documented set is the authority (`nvs.h:485-492`):
    //      ESP_OK · ESP_FAIL ("internal error; most likely due to corrupted NVS partition") ·
    //      ESP_ERR_NVS_NOT_FOUND · ESP_ERR_NVS_INVALID_HANDLE · ESP_ERR_NVS_INVALID_NAME · ..._INVALID_LENGTH.
    // ⓘ A `nvs_open` failure is returned AS ITS OWN CODE, deliberately: `NOT_FOUND` from the open means the
    //   namespace has never been written (nvs.h:31 says so explicitly for NVS_READONLY), which is the same real
    //   absence as a missing key — while any other open error is a medium fault and must stay one.
    // ⛔ READONLY, ALWAYS — the same rule as ns_absent above.
    // Returns the raw `esp_err_t` as an int; `*got` is the byte count on ESP_OK, 0 otherwise. The CLASSIFICATION
    // of that code is not made here — see `mrinboxfs::classify_blob_lookup`, which is host-reachable.
    int get_blob_raw(const char* ns, const char* key, void* dst, size_t cap, size_t* got) {
        if (got) *got = 0;
        nvs_handle_t h = 0;
        const esp_err_t e = nvs_open(ns, NVS_READONLY, &h);
        if (e != ESP_OK) return static_cast<int>(e);
        size_t len = cap;
        const esp_err_t r = nvs_get_blob(h, key, dst, &len);
        nvs_close(h);
        if (got && r == ESP_OK) *got = len;
        return static_cast<int>(r);
    }
};
inline int read_slot(const Slot& s, void* dst, size_t len, SlotIo* io = nullptr) {
    PreferencesSlot nvs;
    return nvs_read_slot(nvs, s.ns, s.key, dst, len, io);
}
inline bool write_slot(const Slot& s, const void* src, size_t len) {
    Preferences p;
    if (!p.begin(s.ns, /*readOnly=*/false)) return false;
    const size_t n = p.putBytes(s.key, src, len);
    p.end();
    return n == len;
}
// `factory_reset confirm`: erase ALL persisted NV. Config + identity + peers all live as keys in the single
// "mr" Preferences/NVS namespace -> clear() wipes them in one shot (NOT a full nvs_flash_erase, so other
// partitions/OTA state are untouched).
// ⛔ [[B134]] CORRECTED IN PLACE 2026-08-28: the last sentence used to read *"the inbox records backend is the
//    [BENCH-TODO] stub here (disabled), so there is no separate inbox store to wipe"*. ESP32 now has a DURABLE
//    inbox. Nothing changes in this function and that is the point: the inbox META are two keys (`ibm_dm` /
//    `ibm_ch`, src/device_inbox_fs_esp32.h) in this same "mr" namespace, so the one clear() below already takes
//    them — while the inbox RECORDS live on the SEPARATE `spiffs` LittleFS partition and are taken by the
//    stores' own wipe() in the command (firmware_commands.cpp), exactly as on nRF52.
inline bool factory_erase() {
    Preferences p;
    if (!p.begin("mr", /*readOnly=*/false)) return false;
    const bool ok = p.clear();                         // wipe the whole "mr" namespace (config + id + peers)
    p.end();
    return ok;
}
inline bool mount_or_repair() { return false; }    // NVS has no LittleFS-CTZ corruption mode -> nothing to repair (begin() is per-call above)
}  // namespace mrnv
#else
namespace mrnv {
// NO NV BACKEND — either an unknown Arduino board or the HOST/native build. Every load fails, so the caller
// comes up on compile-time defaults; every save reports failure rather than pretending. ⚠ Before NV1 the host
// build had NO mrnv:: functions at all (the whole backend section sat inside `#if defined(ARDUINO)`); this arm
// now covers it, which is what lets a native test link against the typed wrappers below.
// ⛔ `io` IS LEFT UNTOUCHED, DELIBERATELY: a host build has no store to fail, so "no NV backend" is reported as the
//    absent slot it has always been, ⛔ never as a STORAGE FAILURE the host could not have observed.
inline int  read_slot (const Slot&, void*, size_t, SlotIo* = nullptr) { return -1; }   // "no such slot", never a length
inline bool write_slot(const Slot&, const void*, size_t) { return false; }
inline bool factory_erase() { return true; }          // §2 no-op stub: nothing to erase IS success (must still boot)
inline bool mount_or_repair() { return false; }       // no FS -> never reports a repair
}  // namespace mrnv
#endif

// ----- the typed records: ONE copy of each wrapper, over whichever primitive pair compiled above -------
// ★ NV1: these were 16 near-identical functions (load/save × 4 records × 2 arms) plus 8 stubs, differing ONLY
// in the slot name and the read/write call — both of which are now behind read_slot/write_slot. The version
// policy and the error handling therefore exist once per record instead of twice.
namespace mrnv {
inline bool load(Blob& out) {
    const int n = read_slot(kSlotCfg, &out, sizeof out);
    return blob_valid_range(out, n, kMagic, /*v_min=*/2, /*v_max=*/kVersion);   // RANGE — see the policy note above
}
inline bool save(const Blob& b) {
    // ★★ H3 CHANGE-DETECTION, AND IT IS DELIBERATELY ASYMMETRIC: only /mrcfg coalesces. A `cfg set` (console
    // OR companion/BLE) to the SAME value must NOT rewrite the whole record — every rewrite is flash wear AND
    // widens the reset-during-write corruption window, and this tree has already been BRICKED by NV corruption
    // once (specs/archive/2026-06-24-internalfs-self-heal.md). /mrcfg is the one record a companion slider
    // bound to `cfg set` can hammer, and the one persist_cfg_if_needed re-writes on the leased channel-ctr
    // roll (fw_main.cpp — `lease_due`, roughly every kChannelCtrLeaseMargin sends). [V1: this comment said
    // `nv_persist_join_state`, a name that no longer exists — the function is persist_cfg_if_needed.]
    // ✖ NOT REPLICATED onto save_id / save_peers / save_faults, ON PURPOSE (NV1 scope ruling, C1): /mrid is
    // written on `regen` or `cfg set name`, /mrfault once per boot — neither is slider-driven, and adding a
    // read-before-write to them would be a real behaviour change (an extra flash read per save) smuggled inside a
    // refactor. This is a deliberate asymmetry, NOT missed dedup.
    // ⚠ /mrpeers IS NOW HAMMERABLE and is guarded DIFFERENTLY: §AB1 made every on-air key-learn a write candidate
    // (an AUTHORITATIVE_H_ANSWER_PUBKEY cache-on-pass flood can re-cache the same key repeatedly), so the dedup lives one level down in
    // mrnv::peer_rec_put, which returns `unchanged` on a byte-identical record and the caller then does not call
    // save_peers at all. That is CHEAPER than this whole-blob memcmp — no second 1160-B buffer, and no extra read.
    // No existing/unreadable record (the load fails) => always write (first provision).
    static Blob cur;   // STATIC not stack — keep the console-path frame small (the do_post_ack overflow lesson)
    if (load(cur) && memcmp(&cur, &b, sizeof b) == 0) return true;   // byte-identical -> no-op success
    return write_slot(kSlotCfg, &b, sizeof b);
}
inline bool load_id(IdBlob& out) {
    const int n = read_slot(kSlotId, &out, sizeof out);
    return blob_valid_exact(out, n, kIdMagic, kIdVersion);           // EQUALITY — a mismatch re-mints the identity
}
inline bool save_id(const IdBlob& b) { return write_slot(kSlotId, &b, sizeof b); }
inline bool load_peers(PeerBlob& out) {                              // §2: the peer address book
    const int n = read_slot(kSlotPeers, &out, sizeof out);
    // ★ EQUALITY, KEPT AT v2 — the AB1 decision, made deliberately (NV1 wired this line so a switch to
    // blob_valid_range would turn test_device_nv.cpp's /mrpeers case RED rather than pass silently). A v1 record is
    // REJECTED OUTRIGHT, not migrated: the node is not deployed, so a one-time loss of the pinned store costs one
    // QR ceremony, whereas a migration arm is code that runs once per chip and then can never be exercised again.
    // The size check would reject a v1 record anyway (584 vs 1160 B), so a range policy could not even parse one.
    return blob_valid_exact(out, n, kPeersMagic, kPeersVersion);
}
inline bool save_peers(const PeerBlob& b) { return write_slot(kSlotPeers, &b, sizeof b); }
// Persistent fault log (`/mrfault`) — whole-blob R/W like Blob, but its magic+version rule belongs to
// mrfault::fault_log_valid (lib/core/fault_log.h), so this REUSES that rather than forking a copy here (U1).
// A kFaultVersion bump therefore rejects an old record -> the caller inits fresh.
inline bool load_faults(mrfault::FaultLog& out) {
    const int n = read_slot(kSlotFault, &out, sizeof out);
    return slot_size_ok(n, sizeof out) && mrfault::fault_log_valid(out);
}
inline bool save_faults(const mrfault::FaultLog& b) { return write_slot(kSlotFault, &b, sizeof b); }
// §UI-15 slice 2 — the join-profile presets. ★ THE ONLY WRAPPER PAIR THAT DOES NOT RETURN A BOOL, for the reason
// `JoinRead` states: its caller must be able to tell a fresh device from a corrupted store.
// ★ THE ONE CALLER THAT ASKS THE PRIMITIVE FOR MORE THAN A LENGTH (SlotIo). The other four records pass no `io` and
//   are therefore byte-for-byte the calls they were before — that asymmetry is the whole point of the out-param
//   shape, and it is what lets a storage failure be told apart from a fresh device HERE without moving `/mrcfg`.
inline JoinRead load_join(JoinBlob& out) {
    SlotIo io;
    const int n = read_slot(kSlotJoin, &out, sizeof out, &io);
    return join_blob_state(out, n, io);   // ⚠ `out` may hold a PARTIAL read on a non-ok answer — every caller re-inits
}
// ⛔ NO read-before-write COALESCING HERE, and that is a deliberate asymmetry with `save()` above rather than missed
//    dedup. Every /mrjoin verb has ALREADY loaded the record (it must, to edit one slot of four), so the byte compare
//    belongs one level up where it is FREE — in `mrfw::JoinProfileService`, which is a pure header the native suite
//    can COUNT the writes of. Repeating /mrcfg's pattern here would add a second flash READ per write and would
//    still be untestable off-device, which is exactly how "seventeen green instruments" happened.
inline bool save_join(const JoinBlob& b) { return write_slot(kSlotJoin, &b, sizeof b); }
// §UI-16 K1 — the team-key keyring. ★ The SECOND wrapper pair that does not return a bool, for the reason
// `TeamKeyRead` states: absent, corrupt and unreadable take three different answers when the thing at stake is an
// UNRECOVERABLE secret. ⓘ It asks the primitive for `SlotIo` exactly as `load_join` does; the four bool records
// still pass no `io` and are therefore byte-for-byte the calls they were.
inline TeamKeyRead load_team_keys(TeamKeyBlob& out) {
    SlotIo io;
    const int n = read_slot(kSlotTeams, &out, sizeof out, &io);
    return team_key_blob_state(out, n, io);   // ⚠ `out` may hold a PARTIAL read on a non-ok answer — the caller re-inits
}
// ⛔ NO read-before-write COALESCING HERE — the same deliberate asymmetry `save_join` states, and for the same
//    reason plus one more: the keyring's caller has ALREADY loaded the record (it must, to place one of four
//    entries), so the compare is FREE one level up in `mrfw::TeamKeyringService`, where the native suite can COUNT
//    the writes; here it would cost a second flash READ **of a secret** and still be untestable off-device.
inline bool save_team_keys(const TeamKeyBlob& b) { return write_slot(kSlotTeams, &b, sizeof b); }
// §UI-10/11 P1 — the UI preset catalog. ★ The THIRD wrapper pair that does not return a bool, for the reason
// `UiPresetRead` states: absent (a first boot), corrupt (the wearer's phrases are gone and he must be told) and
// unreadable (nothing is known, so ⛔ nothing may be written) are three different answers. ⓘ It asks the primitive
// for `SlotIo` exactly as `load_join` and `load_team_keys` do; the four bool records still pass no `io` and are
// therefore byte-for-byte the calls they were.
// ⛔⛔ `kSlotUi`, ⛔ NEVER `kSlotCfg` — the separation this record exists for (see the slot table). A phrase edit
//    that reached `/mrcfg` would reprovision radio, identity, team and keys on a version mismatch.
inline UiPresetRead load_ui_presets(UiPresetBlob& out) {
    SlotIo io;
    const int n = read_slot(kSlotUi, &out, sizeof out, &io);
    return ui_preset_blob_state(out, n, io);   // ⚠ `out` may hold a PARTIAL read on a non-ok answer — the caller re-inits
}
// ⛔ NO read-before-write COALESCING HERE — the same deliberate asymmetry `save_join` and `save_team_keys` state, and
//    for the same reason: the caller has ALREADY loaded the record (it must, to edit one slot of seventeen), so the
//    byte compare is FREE one level up in `mrfw::PresetCatalog`, which is a pure header the native suite can COUNT
//    the writes of. Repeating /mrcfg's pattern here would add a second flash READ per write and would still be
//    untestable off-device — which is exactly how "seventeen green instruments" happened.
inline bool save_ui_presets(const UiPresetBlob& b) { return write_slot(kSlotUi, &b, sizeof b); }
}  // namespace mrnv
