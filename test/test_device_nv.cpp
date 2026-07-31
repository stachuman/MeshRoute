// MeshRoute — test_device_nv.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN — test_airtime.cpp provides main().
//
// FIRST native coverage of the NV record layer (src/device_nv.h). Before NV1 the size+magic+version
// predicate was hand-written SIX times *inside* `#if defined(ARDUINO)` (Blob/IdBlob/PeerBlob × the nRF52 and
// ESP32 arms), so no host test could reach it and nothing pinned WHICH version policy each record takes.
// It is now `slot_size_ok` / `blob_valid_range` / `blob_valid_exact` above that guard, and this file is its
// detector:
//   - the RANGED /mrcfg policy vs the EXACT /mrid + /mrpeers policy, at their boundaries — the asymmetry the
//     six copies hid;
//   - short / over-long / NEGATIVE reads all rejected (a negative is nRF52 LittleFS's corrupt-block signal,
//     which mount_or_repair() keys its self-heal on — it must never be laundered into a valid length);
//   - the slot table, so a record cannot silently change its storage name under either backend;
//   - the no-backend arm: read_slot/write_slot report "no NV", so every load fails and every save reports
//     failure — the property that lets a device-less build link and boot on compile-time defaults.
//
// ⚠ WHAT THIS FILE CANNOT SEE: the two real backends (Adafruit LittleFS files / ESP32 Preferences NVS) are
// unreachable from the host — the three board builds compile them and the owner's bench exercises the flash.
// These tests cover the POLICY, not the storage.
#include "doctest.h"
#include "device_nv.h"
#include "fault_log.h"

#include <cstring>

using namespace mrnv;

namespace {
// A record at a chosen magic/version, otherwise zeroed. The loaders read only magic+version, so the payload
// is irrelevant to the predicate — which is the point: validation must not depend on the body.
template <typename BlobT>
BlobT stamped(uint32_t magic, uint16_t version) {
    BlobT b{};
    b.magic = magic;
    b.version = version;
    return b;
}
constexpr int full_read(size_t n) { return static_cast<int>(n); }   // "a complete, full-length read"
}  // namespace

TEST_CASE("device_nv: slot_size_ok accepts EXACTLY the wanted length; a short, long or NEGATIVE read fails") {
    CHECK(slot_size_ok(8, 8));
    CHECK(slot_size_ok(0, 0));                 // a zero-length record read as zero bytes is 'complete'
    CHECK_FALSE(slot_size_ok(7, 8));           // short read (truncated / older smaller record)
    CHECK_FALSE(slot_size_ok(9, 8));           // over-long (a longer record from a newer layout)
    CHECK_FALSE(slot_size_ok(0, 8));           // absent slot on the NVS arm (getBytes -> 0)
    CHECK_FALSE(slot_size_ok(-1, 8));          // open() failed / key absent — the read_slot 'no slot' code
    // ★ THE SIGN IS LOAD-BEARING. nRF52 File::read() returns a NEGATIVE on a corrupt LittleFS CTZ block.
    // If that were widened to size_t it would compare as a huge value, not as an error — and for want == 8
    // the comparison would happen to still fail, which is exactly how such a bug hides. Pin it explicitly.
    CHECK_FALSE(slot_size_ok(-8, 8));
    CHECK_FALSE(slot_size_ok(-1, static_cast<size_t>(-1)));   // the widened-negative trap, stated as a test
}

TEST_CASE("device_nv: /mrcfg (Blob) takes the RANGE policy — v2..kVersion load, v1 and kVersion+1 are rejected") {
    const int full = full_read(sizeof(Blob));
    {   // the current layout
        Blob b = stamped<Blob>(kMagic, kVersion);
        CHECK(blob_valid_range(b, full, kMagic, 2, kVersion));
    }
    {   // ★ the low end of the range is 2, not 1 — an explicit, deliberate floor
        Blob b = stamped<Blob>(kMagic, 2);
        CHECK(blob_valid_range(b, full, kMagic, 2, kVersion));
    }
    {   Blob b = stamped<Blob>(kMagic, 1);
        CHECK_FALSE(blob_valid_range(b, full, kMagic, 2, kVersion));       // below the floor
    }
    {   Blob b = stamped<Blob>(kMagic, 0);
        CHECK_FALSE(blob_valid_range(b, full, kMagic, 2, kVersion));       // erased flash / unstamped blob
    }
    {   Blob b = stamped<Blob>(kMagic, static_cast<uint16_t>(kVersion + 1));
        CHECK_FALSE(blob_valid_range(b, full, kMagic, 2, kVersion));       // a FUTURE layout is refused, not guessed at
    }
    {   Blob b = stamped<Blob>(kMagic ^ 1u, kVersion);
        CHECK_FALSE(blob_valid_range(b, full, kMagic, 2, kVersion));       // wrong magic (not our record)
    }
    {   Blob b = stamped<Blob>(0, 0);
        CHECK_FALSE(blob_valid_range(b, full, kMagic, 2, kVersion));       // erased flash reads as zeroes
    }
    {   // a size mismatch rejects regardless of a perfectly good magic+version — the pre-v8/pre-v22 path
        Blob b = stamped<Blob>(kMagic, kVersion);
        CHECK_FALSE(blob_valid_range(b, full - 1, kMagic, 2, kVersion));
        CHECK_FALSE(blob_valid_range(b, full + 1, kMagic, 2, kVersion));
        CHECK_FALSE(blob_valid_range(b, -1, kMagic, 2, kVersion));
    }
}

TEST_CASE("device_nv: /mrid (IdBlob) takes the EXACT policy — only kIdVersion loads, neither neighbour does") {
    const int full = full_read(sizeof(IdBlob));
    {   IdBlob b = stamped<IdBlob>(kIdMagic, kIdVersion);
        CHECK(blob_valid_exact(b, full, kIdMagic, kIdVersion));
    }
    {   IdBlob b = stamped<IdBlob>(kIdMagic, static_cast<uint16_t>(kIdVersion + 1));
        CHECK_FALSE(blob_valid_exact(b, full, kIdMagic, kIdVersion));
    }
    {   IdBlob b = stamped<IdBlob>(kIdMagic, static_cast<uint16_t>(kIdVersion - 1));
        CHECK_FALSE(blob_valid_exact(b, full, kIdMagic, kIdVersion));      // NOT a range: an older /mrid re-mints
    }
    {   IdBlob b = stamped<IdBlob>(kIdMagic ^ 1u, kIdVersion);
        CHECK_FALSE(blob_valid_exact(b, full, kIdMagic, kIdVersion));
    }
    {   IdBlob b = stamped<IdBlob>(kIdMagic, kIdVersion);
        CHECK_FALSE(blob_valid_exact(b, full - 1, kIdMagic, kIdVersion));  // the legacy no-lat/lon record
        CHECK_FALSE(blob_valid_exact(b, -1, kIdMagic, kIdVersion));
    }
}

// ★★ THE AB1 HOOK. `2026-07-29-peer-address-book-design.md` bumps kPeersVersion and must DECIDE whether v2
// accepts a range (load + migrate) or demands equality (reject v1). Today it is EQUALITY, and this case pins
// the POLICY rather than the constant: it asserts version-1 and version+1 are BOTH rejected at whatever
// kPeersVersion happens to be. So a bare bump stays green (a v1 record is rejected — the rejection test AB1's
// gate asks for), while a switch to `blob_valid_range` turns it RED and forces the decision to be explicit.
TEST_CASE("device_nv: /mrpeers (PeerBlob) takes the EXACT policy — an older-version store is REJECTED, not migrated") {
    const int full = full_read(sizeof(PeerBlob));
    {   PeerBlob b = stamped<PeerBlob>(kPeersMagic, kPeersVersion);
        CHECK(blob_valid_exact(b, full, kPeersMagic, kPeersVersion));
    }
    {   PeerBlob b = stamped<PeerBlob>(kPeersMagic, static_cast<uint16_t>(kPeersVersion - 1));
        CHECK_FALSE(blob_valid_exact(b, full, kPeersMagic, kPeersVersion));   // ★ the older-store rejection
    }
    {   PeerBlob b = stamped<PeerBlob>(kPeersMagic, static_cast<uint16_t>(kPeersVersion + 1));
        CHECK_FALSE(blob_valid_exact(b, full, kPeersMagic, kPeersVersion));
    }
    {   PeerBlob b = stamped<PeerBlob>(kPeersMagic ^ 1u, kPeersVersion);
        CHECK_FALSE(blob_valid_exact(b, full, kPeersMagic, kPeersVersion));
    }
    {   // growing PeerRec[] or adding a field changes sizeof -> every stored record is refused. That is the
        // documented "dev hardware: a format change just bumps kPeersVersion (no migration)" contract.
        PeerBlob b = stamped<PeerBlob>(kPeersMagic, kPeersVersion);
        CHECK_FALSE(blob_valid_exact(b, full - static_cast<int>(sizeof(PeerRec)), kPeersMagic, kPeersVersion));
    }
}

TEST_CASE("device_nv: the RANGE-vs-EXACT asymmetry is REAL — the same version offset loads /mrcfg and rejects /mrid+/mrpeers") {
    // One record accepting an older version while the other two reject it is a deliberate design decision,
    // not an accident of six hand-copied lines. Pin the difference so a future 'unification' has to argue.
    Blob    cfg = stamped<Blob>(kMagic, 2);                                   // < kVersion (23 today)
    IdBlob  idb = stamped<IdBlob>(kIdMagic, static_cast<uint16_t>(kIdVersion - 1));
    PeerBlob pb = stamped<PeerBlob>(kPeersMagic, static_cast<uint16_t>(kPeersVersion - 1));
    CHECK(blob_valid_range(cfg, full_read(sizeof cfg), kMagic, 2, kVersion));       // ranged: an old config UPGRADES
    CHECK_FALSE(blob_valid_exact(idb, full_read(sizeof idb), kIdMagic, kIdVersion));       // exact: re-mint
    CHECK_FALSE(blob_valid_exact(pb, full_read(sizeof pb), kPeersMagic, kPeersVersion));   // exact: drop the store
    // and `blob_valid_exact` IS the degenerate range — one comparison core, two named policies (no fork).
    CHECK(blob_valid_range(idb, full_read(sizeof idb), kIdMagic,
                           static_cast<uint16_t>(kIdVersion - 1), kIdVersion));      // a range WOULD accept it
}

TEST_CASE("device_nv: /mrfault keeps its OWN validator (mrfault::fault_log_valid) — device_nv only owns the size check") {
    mrfault::FaultLog f;
    mrfault::fault_log_init(f);
    CHECK(mrfault::fault_log_valid(f));
    CHECK(slot_size_ok(full_read(sizeof f), sizeof f));                    // the composition load_faults() performs
    CHECK((slot_size_ok(full_read(sizeof f), sizeof f) && mrfault::fault_log_valid(f)));
    CHECK_FALSE((slot_size_ok(full_read(sizeof f) - 1, sizeof f) && mrfault::fault_log_valid(f)));  // short read
    mrfault::FaultLog bad{};                                         // erased flash: magic 0
    CHECK_FALSE(mrfault::fault_log_valid(bad));
    CHECK_FALSE((slot_size_ok(full_read(sizeof bad), sizeof bad) && mrfault::fault_log_valid(bad)));
}

TEST_CASE("device_nv: the slot table names all four records for BOTH storage models") {
    CHECK(std::strcmp(kSlotCfg.path,   "/mrcfg")   == 0);
    CHECK(std::strcmp(kSlotId.path,    "/mrid")    == 0);
    CHECK(std::strcmp(kSlotPeers.path, "/mrpeers") == 0);
    CHECK(std::strcmp(kSlotFault.path, "/mrfault") == 0);
    CHECK(std::strcmp(kSlotCfg.key,    "cfg")   == 0);
    CHECK(std::strcmp(kSlotId.key,     "id")    == 0);
    CHECK(std::strcmp(kSlotPeers.key,  "peers") == 0);
    CHECK(std::strcmp(kSlotFault.key,  "log")   == 0);
    // ★ config/identity/peers share ONE NVS namespace so factory_erase()'s p.clear() wipes all three in a
    // single shot; /mrfault is deliberately in its own, so the HW fault history SURVIVES a factory reset
    // (the nRF52 arm achieves the same by save_faults()-ing it back after the format).
    CHECK(std::strcmp(kSlotCfg.ns,   "mr") == 0);
    CHECK(std::strcmp(kSlotId.ns,    "mr") == 0);
    CHECK(std::strcmp(kSlotPeers.ns, "mr") == 0);
    CHECK(std::strcmp(kSlotFault.ns, "mrfault") == 0);
    CHECK(std::strcmp(kSlotFault.ns, kSlotCfg.ns) != 0);
}

TEST_CASE("device_nv: with no backend compiled every load and save FAILS LOUD; factory_erase is the no-op success") {
    // The host build takes the no-backend arm, so this is the stub contract: nothing persists, nothing
    // pretends to. fw_main treats a false load as 'unprovisioned -> compile-time defaults'.
    uint8_t scratch[8]{};
    CHECK(read_slot(kSlotCfg, scratch, sizeof scratch) < 0);
    CHECK_FALSE(write_slot(kSlotCfg, scratch, sizeof scratch));

    Blob b{}; b.magic = kMagic; b.version = kVersion;
    CHECK_FALSE(load(b));
    CHECK_FALSE(save(b));                       // change-detection can't match, so it falls through to the write
    IdBlob idb{}; idb.magic = kIdMagic; idb.version = kIdVersion;
    CHECK_FALSE(load_id(idb));
    CHECK_FALSE(save_id(idb));
    PeerBlob pb{}; pb.magic = kPeersMagic; pb.version = kPeersVersion;
    CHECK_FALSE(load_peers(pb));
    CHECK_FALSE(save_peers(pb));
    mrfault::FaultLog fl; mrfault::fault_log_init(fl);
    CHECK_FALSE(load_faults(fl));
    CHECK_FALSE(save_faults(fl));
    CHECK(factory_erase());                     // "nothing to erase" is success — a device-less build must boot
    CHECK_FALSE(mount_or_repair());             // no FS => never reports a repair
    // a load that fails must leave the caller's buffer alone to inspect (fw_main re-stamps and re-saves it)
    CHECK(b.magic == kMagic);
    CHECK(b.version == kVersion);
}

TEST_CASE("device_nv: the record sizes the version policy guards are what the stored layout expects") {
    // The size check IS the migration policy for these records, so a silent sizeof change must be visible.
    CHECK(sizeof(PeerRec) == 36);                             // 4-byte hash + 32-byte ed25519 pubkey, no padding
    CHECK(sizeof(PeerBlob) == 8 + kMaxPinnedPeers * sizeof(PeerRec));
    CHECK(kMaxPinnedPeers == 16);
    CHECK(kPeersVersion == 1);                                // ⚠ AB1 bumps this; see the /mrpeers case above
    CHECK(kIdVersion == 1);
    CHECK(kVersion == 23);                                    // §loc-per-send dropped loc_in_dm at v23
    CHECK(kMagic == 0x4D524331u);                             // 'MRC1'
    CHECK(kIdMagic == 0x4D524944u);                           // 'MRID'
    CHECK(kPeersMagic == 0x4D525052u);                        // 'MRPR'
}
