// MeshRoute — src/device_nv.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Persist the device's runtime state to on-chip flash so it survives reboot. FOUR records:
//   - `/mrcfg`   (Blob)     = RADIO/PROTOCOL CONFIG + the short `node_id` (a `cfg set` over the console).
//   - `/mrid`    (IdBlob)   = the 32-byte identity master seed + name (HW-RNG on first boot; `regen`). The
//                             keypair / key_hash32 are DERIVED from the seed at boot (lib/core/identity).
//   - `/mrpeers` (PeerBlob) = the pinned peer-key store (see §2 below).
//   - `/mrfault` (mrfault::FaultLog) = the HW fault history; lib/core/fault_log.h owns its validity rule.
//
// STRUCTURE — three layers, and the ORDER is deliberate (NV1, register B26):
//   1. the record types + the `Slot` table + the `blob_valid_*` predicate, ABOVE the platform `#if`, so they
//      are HOST-TESTABLE (test/test_device_nv.cpp). The predicate used to be hand-copied SIX times *inside*
//      the platform arms, where no test could reach it.
//   2. per-backend `read_slot`/`write_slot` — the only platform-specific code, TWO functions per arm:
//        nRF52 (Adafruit core) -> Adafruit_LittleFS / InternalFS FILES, addressed by `Slot::path`
//        ESP32 (Heltec)        -> Preferences / NVS KEY-VALUE, addressed by `Slot::ns` + `Slot::key`
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
    int8_t   tx_power;         // dBm (repurposed _pad2); SX1262 range -9..22. `cfg set tx_power`
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
};
constexpr uint32_t kMagic   = 0x4D524331u;   // 'MRC1'
constexpr uint16_t kVersion = 23;            // v23: §loc-per-send — the `loc_in_dm` byte is GONE (location became the per-send `send -l` flag; the toggle aired coordinates in the clear, open-bug-register B0). ⚠ REPROVISION-ON-REFLASH: the struct layout changed, so load() rejects a v22 blob and the node comes up UNPROVISIONED on first contact after this flash — the companion must expect that. v22: §team-ch-key team channel keypair (team_ch_pub + team_ch_priv + team_ch_key_present) — REPROVISION-ON-REFLASH, see the fields. v21: §S2 intro_attach toggle (first-contact pubkey attach). v20: remote-mgmt admin auth (admin_pubkey + admin_counter_floor + admin_provisioned). v19: team_local_id (§mobile 6.4 — persist the team-DAD id across reboot). v18: team_id (§mobile 6.1). v17: per-layer BW+CR (l1_bw_hz + l1_cr). v16: anti-spam per-leaf tunables (channel_active_fraction + the two burst floors). v15: channel_ctr persist (reboot id-reuse fix). v14: R6.1 leaf-config (lineage_id + config_epoch + leaf_name). v13: gw_herd_slack. v12: per-layer frequency (l1_freq_mhz). v11: gateway-announce duty knobs. v10: e2e_dm toggle. v9: loc_in_dm toggle. v8: DUAL-LAYER GATEWAY (n_layers + layer0_id + window schedule + the l1_*
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

// ---- Pinned peer-key store (`/mrpeers`) — E2E §2. The QR/`peerkey`-installed VERIFIED keys, reloaded at boot as
// PINNED so a scanned contact survives reboot with no re-scan. On-air (TOFU) keys stay RAM-only. Whole-blob R/W like
// /mrid; a `peerkey` install rewrites it. Dev hardware: a format change just bumps kPeersVersion (no migration).
struct PeerRec  { uint32_t key_hash32; uint8_t ed_pub[32]; };
struct PeerBlob {
    uint32_t magic;       // kPeersMagic
    uint16_t version;     // kPeersVersion
    uint16_t count;       // entries in use (0..kMaxPinnedPeers)
    PeerRec  rec[16];     // == cap_peer_keys; PINNED keys only
};
constexpr uint32_t kPeersMagic     = 0x4D525052u;  // 'MRPR'
constexpr uint16_t kPeersVersion   = 1;
constexpr uint8_t  kMaxPinnedPeers = 16;

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
//   /mrpeers (PeerBlob) / identity or comes up with no pinned peers (kVersion's REPROVISION-ON-REFLASH note).
// The policy is now a NAMED call at the one wrapper instead of a hand-copied comparison, so changing one
// record's policy is one line and cannot leak into another's. `blob_valid_exact` IS the degenerate range —
// one comparison core, two names, no fork (U1).
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

}  // namespace mrnv

// ----- platform slot primitives (header-inline; device_nv.h is included by the one device TU) ---------
// ★ THE ONLY PLATFORM-SPECIFIC CODE IN THIS FILE. Two functions per arm, where there used to be eight
// near-identical load/save wrappers differing only in the slot name and the read/write call.
#if defined(ARDUINO) && (defined(NRF52_SERIES) || defined(ARDUINO_ARCH_NRF52) || defined(NRF52840_XXAA) || defined(BOARD_XIAO_WIO_SX1262))
  #include <Adafruit_LittleFS.h>
  #include <InternalFileSystem.h>
namespace mrnv {
// nRF52: the slots are Adafruit LittleFS FILES, addressed by `Slot::path`.
inline int read_slot(const Slot& s, void* dst, size_t len) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    File f(InternalFS);
    if (!f.open(s.path, FILE_O_READ)) return -1;               // absent slot (first boot) — not an error
    const int n = f.read(dst, static_cast<uint16_t>(len));     // ⚠ < 0 on a corrupt CTZ block (mount_or_repair)
    f.close();
    return n;
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
namespace mrnv {
// ESP32: the slots are Preferences/NVS KEY-VALUE pairs, addressed by `Slot::ns` + `Slot::key`.
inline int read_slot(const Slot& s, void* dst, size_t len) {
    Preferences p;
    if (!p.begin(s.ns, /*readOnly=*/true)) return -1;
    if (!p.isKey(s.key)) { p.end(); return -1; }         // no record yet (first boot) — silent, no NVS error log
    const size_t n = p.getBytes(s.key, dst, len);
    p.end();
    return static_cast<int>(n);
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
// partitions/OTA state are untouched). The inbox records backend is the [BENCH-TODO] stub here (disabled),
// so there is no separate inbox store to wipe.
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
inline int  read_slot (const Slot&, void*, size_t)      { return -1; }     // "no such slot", never a length
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
    // bound to `cfg set` can hammer, and the one nv_persist_join_state re-writes on the leased channel-ctr
    // roll (fw_main.cpp — `lease_due`, roughly every kChannelCtrLeaseMargin sends).
    // ✖ NOT REPLICATED onto save_id / save_peers / save_faults, ON PURPOSE (NV1 scope ruling, C1): /mrid is
    // written on `regen` or `cfg set name`, /mrpeers on a `peerkey` install, /mrfault once per boot — none is
    // slider-driven, and adding a read-before-write to them would be a real behaviour change (an extra flash
    // read per save) smuggled inside a refactor. This is a deliberate asymmetry, NOT missed dedup.
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
inline bool load_peers(PeerBlob& out) {                              // §2: the pinned-key store
    const int n = read_slot(kSlotPeers, &out, sizeof out);
    return blob_valid_exact(out, n, kPeersMagic, kPeersVersion);     // EQUALITY — a mismatch drops the store
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
}  // namespace mrnv
