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
// §AB1 (2026-07-31) added the second half: the /mrpeers ADDRESS-BOOK RECORD POLICY (mrnv::peer_rec_put) — the
// round-trip of a name + confidence, the v1-blob rejection, `overheard` refused, upgrade-never-downgrade,
// pinned-over-authoritative eviction, the all-pinned refusal, the flash-wear guard, and the PeerPut enum->string
// mapper. That policy is in device_nv.h and NOT in firmware_commands.cpp precisely so these cases can reach it:
// `test_build_src = no` keeps every src/*.cpp out of the native build.
//
// ⚠ WHAT THIS FILE CANNOT SEE: the two real backends (Adafruit LittleFS files / ESP32 Preferences NVS) are
// unreachable from the host — the three board builds compile them and the owner's bench exercises the flash.
// These tests cover the POLICY, not the storage.
#include "doctest.h"
#include "device_nv.h"
#include "fault_log.h"
#include "node.h"        // §AB1: meshroute::Node::PeerKeyConf — the enum whose VALUES /mrpeers stores verbatim

#include <cstddef>       // offsetof — the on-flash PeerRec field order is a pinned contract
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

// ★★ THE AB1 HOOK — AND AB1 TOOK IT, KEEPING EQUALITY. NV1 wrote this case to pin the POLICY rather than the
// constant (version-1 and version+1 both rejected at whatever kPeersVersion happens to be), so that AB1's bump to
// 2 would stay green and BE the v1-blob rejection test the address-book spec's gate asks for, while a switch to
// `blob_valid_range` would have turned it RED and forced the decision to be argued. The decision, recorded at
// load_peers: EQUALITY KEPT — a v1 store is rejected outright, the pinned peers are lost once, and no migration
// arm is written (it would be code that runs once per chip and can then never be exercised again).
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
    {   // ★ THE v1-BLOB REJECTION, stated in the terms a v1 chip actually presents: a v1 record is 8 + 16*36 = 584
        // bytes, not sizeof(PeerBlob). It fails on SIZE ALONE, before the version byte is even consulted — so the
        // rejection holds on both backends (nRF52 File::read returns 584; ESP32 nvs_get_blob returns 584) even if a
        // later slice were to loosen the version policy. The version bump is belt-and-braces on top.
        constexpr int kV1Bytes = 8 + 16 * (4 + 32);
        CHECK(kV1Bytes == 584);
        CHECK(sizeof(PeerBlob) != static_cast<size_t>(kV1Bytes));         // v2 CHANGED the layout — that is the primary rejector
        PeerBlob v1 = stamped<PeerBlob>(kPeersMagic, 1);                  // magic right, version 1, v1 length
        CHECK_FALSE(blob_valid_exact(v1, kV1Bytes, kPeersMagic, kPeersVersion));
        CHECK_FALSE(blob_valid_range(v1, kV1Bytes, kPeersMagic, 1, kPeersVersion));   // even a RANGE policy could not parse it
    }
}

// ---- §AB1: the /mrpeers RECORD POLICY (mrnv::peer_rec_put) ------------------------------------------------------
// ★ WHY THESE LIVE IN device_nv.h AT ALL: `test_build_src = no` keeps every src/*.cpp out of the native build, so a
// selection/eviction policy written in firmware_commands.cpp would be exactly as untestable as the six hand-copied
// validators NV1 hoisted. The policy is pure and header-inline, so these cases reach it. What they CANNOT see is
// the flash itself — the three board builds compile the backends and the owner's bench exercises the wear.
namespace {
// a record's ed_pub must hash to its key_hash32 (Node::peer_key_set re-verifies it), so build both from one seed
struct TestKey { uint32_t hash; uint8_t ed[32]; };
TestKey key_of(uint8_t seed) {
    TestKey k{};
    for (int i = 0; i < 32; ++i) k.ed[i] = static_cast<uint8_t>(seed + i);
    k.hash = static_cast<uint32_t>(k.ed[0]) | (static_cast<uint32_t>(k.ed[1]) << 8)
           | (static_cast<uint32_t>(k.ed[2]) << 16) | (static_cast<uint32_t>(k.ed[3]) << 24);
    return k;
}
// ⚠ BY VALUE, with an all-zero record for "absent", and a separate has_rec(). A pointer-returning finder let a
// POISON PROBE (pinned made evictable) SIGSEGV the whole runner at the first dereference, which aborted 927 later
// cases — the detection was right but unreadable. A total accessor keeps a broken invariant REPORTABLE.
bool has_rec(const PeerBlob& b, uint32_t hash) {
    for (uint16_t i = 0; i < b.count; ++i) if (b.rec[i].key_hash32 == hash) return true;
    return false;
}
PeerRec rec_of(const PeerBlob& b, uint32_t hash) {
    for (uint16_t i = 0; i < b.count; ++i) if (b.rec[i].key_hash32 == hash) return b.rec[i];
    return PeerRec{};
}
PeerPut put(PeerBlob& b, const TestKey& k, uint8_t conf, const char* name = nullptr) {
    return peer_rec_put(b, k.hash, k.ed, conf, name, name ? static_cast<uint8_t>(std::strlen(name)) : 0);
}
}  // namespace

// ★ THE CONFIDENCE BYTE IS A CROSS-LAYER CONTRACT: device_nv.h deliberately does NOT include node.h (the record
// layer must stay free of the protocol engine), so the two constants are asserted equal HERE instead of trusted to
// a comment. If PeerKeyConf is ever reordered, this fails the native BUILD, not a run.
static_assert(kPeerConfAuthoritative == static_cast<uint8_t>(meshroute::Node::PeerKeyConf::authoritative),
              "/mrpeers stores PeerKeyConf values verbatim — kPeerConfAuthoritative must track the enum");
static_assert(kPeerConfPinned == static_cast<uint8_t>(meshroute::Node::PeerKeyConf::pinned),
              "/mrpeers stores PeerKeyConf values verbatim — kPeerConfPinned must track the enum");
static_assert(static_cast<uint8_t>(meshroute::Node::PeerKeyConf::overheard) != kPeerConfAuthoritative
           && static_cast<uint8_t>(meshroute::Node::PeerKeyConf::overheard) != kPeerConfPinned,
              "`overheard` must NOT be a persistable confidence — it cannot seal");

TEST_CASE("device_nv/AB1: ROUND-TRIP — a stored record returns the same key, NAME and CONFIDENCE") {
    // The property v1 could not hold: the name and the confidence survive the store. (The flash leg is the boards';
    // this is the record leg, which is where v1 actually lost the data — PeerRec had nowhere to put either field.)
    PeerBlob b{}; peers_blob_init(b);
    CHECK(b.magic == kPeersMagic);
    CHECK(b.version == kPeersVersion);
    CHECK(b.count == 0);

    const TestKey a = key_of(0x10), p = key_of(0x90);
    CHECK(put(b, a, kPeerConfAuthoritative, "Ania") == PeerPut::inserted);
    CHECK(put(b, p, kPeerConfPinned, "QR contact") == PeerPut::inserted);
    CHECK(b.count == 2);

    CHECK(has_rec(b, a.hash));
    const PeerRec ra = rec_of(b, a.hash);
    CHECK(ra.confidence == kPeerConfAuthoritative);            // ★ NOT promoted to pinned — the v1 restore's bug
    CHECK(ra.name_len == 4);
    CHECK(std::memcmp(ra.name, "Ania", 4) == 0);
    CHECK(std::memcmp(ra.ed_pub, a.ed, 32) == 0);
    CHECK(ra.key_hash32 == a.hash);

    CHECK(has_rec(b, p.hash));
    const PeerRec rp = rec_of(b, p.hash);
    CHECK(rp.confidence == kPeerConfPinned);
    CHECK(rp.name_len == 10);
    CHECK(std::memcmp(rp.name, "QR contact", 10) == 0);

    // and both are RESTORABLE, i.e. the boot restore will re-install them at these levels and not skip them
    CHECK(peer_conf_restorable(ra.confidence));
    CHECK(peer_conf_restorable(rp.confidence));
}

TEST_CASE("device_nv/AB1: the record layer REFUSES to persist an `overheard` key or an unrecognised confidence") {
    // C2. `overheard` cannot seal (sealing gates on conf >= authoritative), so storing one would spend a slot and
    // evict something useful; an unrecognised byte must never be guessed into a sealing capability.
    PeerBlob b{}; peers_blob_init(b);
    const TestKey k = key_of(0x20);
    CHECK(put(b, k, static_cast<uint8_t>(meshroute::Node::PeerKeyConf::overheard), "nope") == PeerPut::refused_conf);
    CHECK(put(b, k, 3, "nope") == PeerPut::refused_conf);      // a value above `pinned`
    CHECK(put(b, k, 0xFF, nullptr) == PeerPut::refused_conf);  // erased flash / bit-rot
    CHECK(b.count == 0);                                       // nothing was stored on any refusal
    CHECK_FALSE(peer_conf_restorable(0));
    CHECK_FALSE(peer_conf_restorable(3));
    CHECK_FALSE(peer_conf_restorable(0xFF));
    CHECK(peer_conf_restorable(kPeerConfAuthoritative));
    CHECK(peer_conf_restorable(kPeerConfPinned));
}

TEST_CASE("device_nv/AB1: confidence UPGRADES, never DOWNGRADES — and PINNED is immutable to an on-air set") {
    // Mirrors Node::peer_key_set's hit path exactly (U1). The pinned case is a COMPLETE no-op — not even the name is
    // refreshed — because RAM refuses the refresh too, and a store that refreshed it would show a reboot-only label.
    PeerBlob b{}; peers_blob_init(b);
    const TestKey k = key_of(0x30);
    CHECK(put(b, k, kPeerConfAuthoritative, "on-air") == PeerPut::inserted);
    CHECK(put(b, k, kPeerConfPinned, "scanned") == PeerPut::updated);          // upgrade
    CHECK(has_rec(b, k.hash));
    CHECK(rec_of(b, k.hash).confidence == kPeerConfPinned);
    CHECK(std::memcmp(rec_of(b, k.hash).name, "scanned", 7) == 0);

    CHECK(put(b, k, kPeerConfAuthoritative, "renamed by air") == PeerPut::unchanged);   // ★ no downgrade, no rename
    CHECK(rec_of(b, k.hash).confidence == kPeerConfPinned);
    CHECK(rec_of(b, k.hash).name_len == 7);
    CHECK(std::memcmp(rec_of(b, k.hash).name, "scanned", 7) == 0);
    CHECK(b.count == 1);                                                       // and no second record for the same hash

    CHECK(put(b, k, kPeerConfPinned, "re-pinned") == PeerPut::updated);         // a USER re-pin still lands
    CHECK(std::memcmp(rec_of(b, k.hash).name, "re-pinned", 9) == 0);
}

TEST_CASE("device_nv/AB1: peer_rec_merge REFUSES a downgrade on its own — the rule peer_rec_put's rule-1 shadows") {
    // ★ THIS CASE EXISTS BECAUSE A POISON PROBE FOUND A HOLE. Forcing peer_rec_merge's `upgrade` to true was
    // INVISIBLE through peer_rec_put: its rule-1 early return intercepts the one reachable downgrade (stored pinned +
    // incoming authoritative), and with only two persistable levels everything else IS an upgrade. So the
    // no-downgrade rule was untested even though the store depends on it for any direct caller or a third level.
    // Drive the merge DIRECTLY, which is the only way to reach the conjunct.
    const TestKey scanned = key_of(0x80), other = key_of(0xA0);
    PeerRec pinned_rec = peer_rec_merge(PeerRec{}, scanned.hash, scanned.ed, kPeerConfPinned, "scanned", 7);
    CHECK(pinned_rec.confidence == kPeerConfPinned);
    // an `authoritative` merge onto a `pinned` record must keep BOTH the level and the key
    const PeerRec after = peer_rec_merge(pinned_rec, scanned.hash, other.ed, kPeerConfAuthoritative, "air", 3);
    CHECK(after.confidence == kPeerConfPinned);                       // ★ NOT downgraded to authoritative
    CHECK(std::memcmp(after.ed_pub, scanned.ed, 32) == 0);            // ★ and the verified key was NOT replaced
    CHECK(after.name_len == 3);                                       // the name IS mutable, and only the name moved
    CHECK(std::memcmp(after.name, "air", 3) == 0);
    // the symmetric direction still upgrades: authoritative -> pinned takes the new key AND the new level
    const PeerRec up = peer_rec_merge(peer_rec_merge(PeerRec{}, other.hash, other.ed, kPeerConfAuthoritative, "a", 1),
                                      other.hash, other.ed, kPeerConfPinned, "p", 1);
    CHECK(up.confidence == kPeerConfPinned);
    CHECK(std::memcmp(up.ed_pub, other.ed, 32) == 0);
}

TEST_CASE("device_nv/AB1: the name is MUTABLE and refreshed; an EMPTY name keeps the stored label") {
    PeerBlob b{}; peers_blob_init(b);
    const TestKey k = key_of(0x40);
    CHECK(put(b, k, kPeerConfAuthoritative, "MeshRoute node: 0x40414243") == PeerPut::inserted);
    CHECK(put(b, k, kPeerConfAuthoritative, "Marek") == PeerPut::updated);      // §1.3: MUTABLE name
    CHECK(rec_of(b, k.hash).name_len == 5);
    CHECK(put(b, k, kPeerConfAuthoritative, nullptr) == PeerPut::unchanged);    // no name carried -> KEEP, and do not write
    CHECK(rec_of(b, k.hash).name_len == 5);
    CHECK(std::memcmp(rec_of(b, k.hash).name, "Marek", 5) == 0);
    // over-long names are CLAMPED to the field, never overrun (a 40-char label from a future longer wire field)
    const char long_name[] = "0123456789012345678901234567890123456789";
    CHECK(std::strlen(long_name) == 40);
    CHECK(put(b, k, kPeerConfAuthoritative, long_name) == PeerPut::updated);
    CHECK(rec_of(b, k.hash).name_len == sizeof(PeerRec::name));
    CHECK(rec_of(b, k.hash).name_len == 32);
}

TEST_CASE("device_nv/AB1: `unchanged` IS THE FLASH-WEAR GUARD — a byte-identical re-cache reports no write") {
    // v2 made EVERY on-air key-learn a write candidate, and a TYPE-5 cache-on-pass flood can re-learn the same key
    // repeatedly. Without this the store would take one whole-blob flash write per re-learn. The guard is a
    // whole-RECORD byte compare, which is only valid because peer_rec_merge zeroes the name tail AND _pad.
    PeerBlob b{}; peers_blob_init(b);
    const TestKey k = key_of(0x50);
    CHECK(put(b, k, kPeerConfAuthoritative, "Zosia") == PeerPut::inserted);
    for (int i = 0; i < 5; ++i) CHECK(put(b, k, kPeerConfAuthoritative, "Zosia") == PeerPut::unchanged);
    CHECK(b.count == 1);
    // the padding really is deterministic: two independently-built identical records compare equal byte-for-byte
    const PeerRec x = peer_rec_merge(PeerRec{}, k.hash, k.ed, kPeerConfAuthoritative, "Zosia", 5);
    const PeerRec y = peer_rec_merge(PeerRec{}, k.hash, k.ed, kPeerConfAuthoritative, "Zosia", 5);
    CHECK(std::memcmp(&x, &y, sizeof x) == 0);
    const PeerRec stored = rec_of(b, k.hash);
    CHECK(std::memcmp(&x, &stored, sizeof x) == 0);
}

TEST_CASE("device_nv/AB1: EVICTION is PINNED-OVER-AUTHORITATIVE — the oldest non-pinned goes, a pinned key never does") {
    // Spec §2.4's open question, settled: authoritative keys now compete for the same 16 slots, so pinned must win.
    PeerBlob b{}; peers_blob_init(b);
    const TestKey pin0 = key_of(0x01), pin1 = key_of(0x02);
    CHECK(put(b, pin0, kPeerConfPinned, "P0") == PeerPut::inserted);
    CHECK(put(b, pin1, kPeerConfPinned, "P1") == PeerPut::inserted);
    for (uint8_t i = 0; i < 14; ++i) CHECK(put(b, key_of(static_cast<uint8_t>(0x60 + i)), kPeerConfAuthoritative, "A") == PeerPut::inserted);
    CHECK(b.count == kMaxPeerRecs);

    const TestKey newcomer = key_of(0xB0);
    CHECK(put(b, newcomer, kPeerConfAuthoritative, "new") == PeerPut::evicted);
    CHECK(b.count == kMaxPeerRecs);                                    // rolled, not grown
    CHECK_FALSE(has_rec(b, key_of(0x60).hash));                         // the OLDEST-INSERTED non-pinned went
    CHECK(has_rec(b, newcomer.hash));
    CHECK(has_rec(b, pin0.hash));                                       // ★ both pinned keys survived
    CHECK(has_rec(b, pin1.hash));
    CHECK(rec_of(b, pin0.hash).confidence == kPeerConfPinned);
    // and index order is still INSERTION order after the shift-down compaction, so the NEXT victim is the next-oldest
    CHECK(put(b, key_of(0xB4), kPeerConfAuthoritative, "new2") == PeerPut::evicted);
    CHECK_FALSE(has_rec(b, key_of(0x61).hash));
    CHECK(has_rec(b, key_of(0x62).hash));
    CHECK(b.rec[kMaxPeerRecs - 1].key_hash32 == key_of(0xB4).hash);     // the newcomer is appended at the back
}

TEST_CASE("device_nv/AB1: a store FULL OF PINNED keys REFUSES an insert — it never drops a human-verified key") {
    // The exact fail-loud Node::peer_key_set takes (`peer_key_full`), because the alternative is silently losing a
    // key a human checked by QR in favour of one that arrived on air.
    PeerBlob b{}; peers_blob_init(b);
    for (uint8_t i = 0; i < kMaxPeerRecs; ++i)
        CHECK(put(b, key_of(static_cast<uint8_t>(0x01 + i)), kPeerConfPinned, "P") == PeerPut::inserted);
    CHECK(b.count == kMaxPeerRecs);
    CHECK(put(b, key_of(0xC0), kPeerConfAuthoritative, "air") == PeerPut::refused_full);
    CHECK(put(b, key_of(0xC0), kPeerConfPinned, "qr") == PeerPut::refused_full);   // even another PINNED key is refused
    CHECK(b.count == kMaxPeerRecs);
    CHECK_FALSE(has_rec(b, key_of(0xC0).hash));
    for (uint8_t i = 0; i < kMaxPeerRecs; ++i) CHECK(has_rec(b, key_of(static_cast<uint8_t>(0x01 + i)).hash));
    // an UPDATE to one of the 16 still works when full — a refusal is about admitting a NEW hash, not about writing
    CHECK(put(b, key_of(0x01), kPeerConfPinned, "P renamed") == PeerPut::updated);
}

TEST_CASE("device_nv/AB1: a bit-rotted `count` cannot index past rec[]") {
    PeerBlob b{}; peers_blob_init(b);
    b.count = 9999;                                            // same size + magic + version, corrupt count
    CHECK(put(b, key_of(0x70), kPeerConfPinned, "x") != PeerPut::refused_conf);
    CHECK(b.count <= kMaxPeerRecs);
}

TEST_CASE("device_nv/AB1: peer_put_name covers EVERY PeerPut enumerator (the enum->string defect class)") {
    // Three enum->string bugs shipped in this tree because the byte-identity gate cannot see a label. The mapper is
    // `default`-less so the compiler catches a 7th enumerator; this catches a 7th that someone maps to "?".
    const PeerPut all[] = { PeerPut::unchanged, PeerPut::updated, PeerPut::inserted, PeerPut::evicted,
                            PeerPut::refused_full, PeerPut::refused_conf, PeerPut::refused_absent };
    CHECK(sizeof(all) / sizeof(all[0]) == 7);
    for (PeerPut r : all) {
        CHECK(peer_put_name(r) != nullptr);
        CHECK(std::strcmp(peer_put_name(r), "?") != 0);        // every enumerator has a REAL name
    }
    CHECK(std::strcmp(peer_put_name(PeerPut::unchanged), "unchanged") == 0);
    CHECK(std::strcmp(peer_put_name(PeerPut::refused_full), "refused_full") == 0);
    CHECK(std::strcmp(peer_put_name(PeerPut::refused_absent), "refused_absent") == 0);
    CHECK(std::strcmp(peer_put_name(static_cast<PeerPut>(99)), "?") == 0);   // and the total-function fallback holds
    // ★ refused_absent is NOT producible by peer_rec_put — it is peer_store_sync's "the live table lost this hash"
    // verdict (firmware_commands.cpp, outside the native build). Pin that division so nobody "fixes" the policy to
    // return it: the record layer never learns about RAM.
    PeerBlob b{}; peers_blob_init(b);
    const TestKey k = key_of(0x22);
    CHECK(put(b, k, kPeerConfPinned, "x") != PeerPut::refused_absent);
    CHECK(put(b, k, 0, "x") == PeerPut::refused_conf);          // the record layer's own refusal is refused_conf
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
    // ★ §AB1 UPDATED all three /mrpeers tripwires TO THE NEW TRUTH rather than deleting any of them — 36 -> 72,
    //   kPeersVersion 1 -> 2, and the blob identity re-stated over the renamed cap.
    // ⚠ 72, NOT 70: the payload is 4 + 32 + 1 + 1 + 32 = 70 and alignof(PeerRec) is 4, so the record carries 2
    //   bytes of tail padding — declared as the named `_pad[2]` so the on-flash bytes are deterministic (the
    //   wear-guard's whole-record memcmp depends on it) rather than indeterminate. The blob therefore doubles
    //   584 -> 1160 B (1.986x), which is ALSO what rejects a v1 record on size alone.
    CHECK(sizeof(PeerRec) == 72);                             // hash 4 + ed_pub 32 + confidence 1 + name_len 1 + name 32 + _pad 2
    CHECK(offsetof(PeerRec, key_hash32) == 0);                // the on-flash field order, pinned: a reorder is a format change
    CHECK(offsetof(PeerRec, ed_pub) == 4);
    CHECK(offsetof(PeerRec, confidence) == 36);
    CHECK(offsetof(PeerRec, name_len) == 37);
    CHECK(offsetof(PeerRec, name) == 38);
    CHECK(sizeof(PeerRec::name) == 32);                       // == Node::PeerKey::name and peer_key_set's clamp
    CHECK(sizeof(PeerBlob) == 8 + kMaxPeerRecs * sizeof(PeerRec));
    CHECK(sizeof(PeerBlob) == 1160);
    CHECK(kMaxPeerRecs == 16);                                // == protocol::cap_peer_keys (the RAM table's cap)
    CHECK(kPeersVersion == 2);                                // §AB1: v2 = confidence + name persisted; v1 rejected outright
    CHECK(kIdVersion == 1);
    CHECK(kVersion == 24);                                    // §UI-16 K2 added the team-key ACTIVE BINDING at v24 (v23 dropped loc_in_dm)
    CHECK(kMagic == 0x4D524331u);                             // 'MRC1'
    CHECK(kIdMagic == 0x4D524944u);                           // 'MRID'
    CHECK(kPeersMagic == 0x4D525052u);                        // 'MRPR'
    // ★★ §UI-16 K1 — THE `/mrteams` KEYRING's ABI, pinned HERE beside its siblings because `sizeof` IS the migration
    //    policy for it too (`load_team_keys`' exact size check). ⚠ 72 with ⛔ NO tail padding, which is exactly what
    //    the NAMED `reserved[4]` buys: the write policy's whole-record `memcmp` would otherwise compare
    //    indeterminate bytes. 8-byte header + 4 × 72 = 296 B — the owner's estimate, now MEASURED.
    CHECK(sizeof(TeamKeyRecord) == 72);
    CHECK(offsetof(TeamKeyRecord, team_id) == 0);             // the on-flash field order, pinned: a reorder is a format change
    CHECK(offsetof(TeamKeyRecord, team_ch_pub) == 4);
    CHECK(offsetof(TeamKeyRecord, team_ch_priv) == 36);
    CHECK(offsetof(TeamKeyRecord, reserved) == 68);
    CHECK(sizeof(TeamKeyBlob) == 8 + kTeamKeyRecs * sizeof(TeamKeyRecord));
    CHECK(sizeof(TeamKeyBlob) == 296);
    CHECK(offsetof(TeamKeyBlob, rec) == 8);                   // the header carries the padding, as JoinBlob's does
    CHECK(kTeamKeyRecs == 4);                                 // FOUR — matching the four /mrjoin join profiles
    CHECK(kTeamKeyVersion == 1);
    CHECK(kTeamKeyMagic == 0x4D524B31u);                      // 'MRK1' — ⛔ its own, never kMagic and never kJoinMagic
    CHECK(kTeamKeyMagic != kMagic);
    CHECK(kTeamKeyMagic != kJoinMagic);
}

// ============================================================ §UI-15 slice 2 CORRECTION (2026-08-19) — THE READ ARMS
// ★★★ WHY THESE CASES EXIST AT ALL. Until 2026-08-19 the two live `read_slot` arms were unreachable from the host, so
//     the QUESTION "what does this arm answer when the BACKEND will not open?" had no runnable answer — and the actual
//     answer was `-1`, the same code as "there is no such record", which `join_blob_state` mapped to `absent`. A
//     broken filesystem therefore announced **NO PROFILES**. The two sequences are now hoisted
//     (`mrnv::fs_read_slot` / `mrnv::nvs_read_slot`), so these drive the REAL branch order against fakes that fail
//     the way the real backends fail — ⛔ not by hand-feeding a return value the shipped backend can never produce,
//     which is exactly the vacuity the QG HOLD found in the over-length case below.
// ⛔ WHAT THEY STILL CANNOT SEE: the adapters (`InternalFsSlot`, `PreferencesSlot`). Each member forwards the library
//    calls and holds no branch — [[B218]]'s `lookup` is a locked stat surfacing the RAW rc, classified up in the
//    template, never in the shim; the flash itself remains the owner's bench.
namespace {

// The Adafruit-LittleFS shape `fs_read_slot` requires — and NOTHING else, so a member the sequence stops calling
// makes this fake stop compiling rather than silently go unmeasured.
// ★ [[B218]]: it mirrors `InternalFsSlot`'s two classification constants and its raw-rc `lookup`, so the tri-state
//   branch in the HOISTED template is driven against the same convention the live shim answers in (LFS_ERR_OK /
//   LFS_ERR_NOENT / any other negative = a metadata error).
struct FakeFs {
    static constexpr int kFoundRc  = 0;    // == LFS_ERR_OK on the live shim
    static constexpr int kAbsentRc = -2;   // == LFS_ERR_NOENT — ⛔ the ONLY rc that means "fresh device"
    bool     mount_ok   = true;
    bool     open_ok    = true;
    int      lookup_result = kFoundRc;     // the raw lfs_stat rc the adapter surfaces ([[B218]])
    uint32_t file_size  = 0;      // ★ the fact the `int` cannot carry: what is ACTUALLY on the medium
    int      read_result = 0;     // what File::read() returns (< 0 = a corrupt CTZ block)
    const void* payload = nullptr;
    size_t   payload_len = 0;
    int mounts = 0, lookups = 0, opens = 0, sizes = 0, reads = 0, closes = 0;

    bool     mount()                { ++mounts; return mount_ok; }
    int      lookup(const char*)    { ++lookups; return lookup_result; }
    bool     open(const char*)      { ++opens;  return open_ok; }
    uint32_t size()                 { ++sizes;  return file_size; }
    void     close()                { ++closes; }
    int      read(void* dst, size_t len) {
        ++reads;
        if (payload) std::memcpy(dst, payload, payload_len < len ? payload_len : len);
        return read_result;
    }
};

// The Preferences/NVS shape `nvs_read_slot` requires.
struct FakeNvs {
    bool   open_ok    = true;
    bool   ns_missing = false;    // ESP_ERR_NVS_NOT_FOUND — "this namespace has never been written"
    bool   key_ok     = true;
    size_t stored_len = 0;
    int    get_result = 0;
    const void* payload = nullptr;
    size_t payload_len = 0;
    int opens = 0, key_checks = 0, len_checks = 0, gets = 0, closes = 0, absent_checks = 0;

    bool   open(const char*)            { ++opens; return open_ok; }
    bool   ns_absent(const char*)       { ++absent_checks; return ns_missing; }
    bool   has_key(const char*)         { ++key_checks; return key_ok; }
    size_t blob_len(const char*)        { ++len_checks; return stored_len; }
    void   close()                      { ++closes; }
    int    get_bytes(const char*, void* dst, size_t len) {
        ++gets;
        if (payload) std::memcpy(dst, payload, payload_len < len ? payload_len : len);
        return get_result;
    }
};

JoinBlob valid_join() { JoinBlob b{}; join_blob_init(b); return b; }

}  // namespace

TEST_CASE("device_nv/nRF52 arm: a MOUNT FAILURE is a STORAGE FAILURE — ⛔ never the fresh-device 'absent'") {
    FakeFs fs; fs.mount_ok = false;
    JoinBlob out{};
    SlotIo io;
    const int n = fs_read_slot(fs, "/mrjoin", &out, sizeof out, &io);

    // ★ THE `int` IS UNCHANGED, and that is the whole shape of the fix: /mrcfg, /mrid, /mrpeers and /mrfault read
    //   the same -1 they always did (an unmounted FS could not have opened the file either), so nothing about them
    //   moves. The NEW fact rides `io`.
    CHECK(n == kSlotAbsent);
    CHECK(fs.opens == 0);                       // ⛔ and it no longer even tries the open
    CHECK(fs.lookups == 0);                     // ⛔ nor the lookup — an unmounted FS cannot answer a stat either
    CHECK(io.backend_failed);
    CHECK_FALSE(io.oversize);
    CHECK(join_blob_state(out, n, io) == JoinRead::io_failed);
    // ⛔⛔ THE CONTROL, AND IT IS THE DEFECT ITSELF: with the backend fact DISCARDED this identical read is a fresh
    //     device. That is what the console printed as `NO PROFILES` before this correction.
    CHECK(join_blob_state(out, n, SlotIo{}) == JoinRead::absent);
}

// ★★★ [[B218]] REOPENED (2026-08-19). This case used to be titled "an OPEN failure is an ORDINARY absent slot", and
//     that claim is FALSE against the vendored backend: `File::_open` (Adafruit_LittleFS_File.cpp:121-153) stats
//     FIRST and answers ONE `false` for FOUR facts — a stat METADATA error, LFS_ERR_NOENT (the only genuine
//     absent), an ALLOCATION failure, and an `lfs_file_open` failure on a file that EXISTS. Only NOENT is a fresh
//     device; the other three are the store refusing to answer, and under the old collapse all three read as
//     `NO PROFILES`. The lookup is now TRI-STATE (the adapter's raw `lfs_stat` rc, classified in the hoisted
//     template), so each fact gets the answer it is.
TEST_CASE("device_nv/nRF52 arm: the lookup is TRI-STATE — ONLY LFS_ERR_NOENT is an absent slot") {
    JoinBlob out{};
    SUBCASE("LFS_ERR_NOENT -> absent (the fresh device still reads NO PROFILES — a first boot is not a fault)") {
        FakeFs fs; fs.lookup_result = FakeFs::kAbsentRc;
        SlotIo io;
        const int n = fs_read_slot(fs, "/mrjoin", &out, sizeof out, &io);
        CHECK(n == kSlotAbsent);
        CHECK(fs.mounts == 1);
        CHECK(fs.lookups == 1);
        CHECK(fs.opens == 0);                       // an absent record costs no open at all
        CHECK_FALSE(io.backend_failed);             // ★ the fresh device must still read NO PROFILES
        CHECK(join_blob_state(out, n, io) == JoinRead::absent);
    }
    SUBCASE("a stat METADATA error -> io_failed, and the open is NEVER attempted (counted)") {
        FakeFs fs; fs.lookup_result = -84;          // LFS_ERR_CORRUPT — neither OK nor NOENT
        SlotIo io;
        const int n = fs_read_slot(fs, "/mrjoin", &out, sizeof out, &io);
        CHECK(n == kSlotAbsent);                    // ⛔ the `int` is UNCHANGED — the four bool records do not move
        CHECK(fs.lookups == 1);
        CHECK(fs.opens == 0);                       // ★ COUNTED: metadata the store cannot stat is not then opened
        CHECK(io.backend_failed);
        CHECK_FALSE(io.oversize);
        CHECK(join_blob_state(out, n, io) == JoinRead::io_failed);
    }
    SUBCASE("PRESENT + open failure -> io_failed — ⛔ never absent (allocation / lfs_file_open failure)") {
        FakeFs fs; fs.open_ok = false;              // the stat says FOUND; File::open then fails ([[B218]] cases 3/4)
        SlotIo io;
        const int n = fs_read_slot(fs, "/mrjoin", &out, sizeof out, &io);
        CHECK(n == kSlotAbsent);
        CHECK(fs.lookups == 1);
        CHECK(fs.opens == 1);
        CHECK(io.backend_failed);
        CHECK(join_blob_state(out, n, io) == JoinRead::io_failed);
        // ⛔⛔ THE CONTROL, AND IT IS THE DEFECT ITSELF: with the backend fact DISCARDED this identical sequence is
        //     a fresh device — exactly what the pre-correction collapse announced as `NO PROFILES`.
        CHECK(join_blob_state(out, n, SlotIo{}) == JoinRead::absent);
    }
}

TEST_CASE("device_nv/nRF52 arm: a file LONGER than the record is REJECTED — the read alone cannot see it") {
    const JoinBlob good = valid_join();
    FakeFs fs;
    fs.payload = &good; fs.payload_len = sizeof good;
    fs.file_size   = (uint32_t)sizeof(JoinBlob) + 40u;   // ★ a REAL over-length file…
    fs.read_result = (int)sizeof(JoinBlob);              // …of which the backend returns EXACTLY `len` bytes

    JoinBlob out{};
    SlotIo io;
    const int n = fs_read_slot(fs, "/mrjoin", &out, sizeof out, &io);

    // ★★ THIS IS WHY THE OLD TEST WAS VACUOUS: it asserted `join_blob_state(b, sizeof b + 1)`, a length the arm can
    //    NEVER return. What the arm really returns is a full-length read of a VALID PREFIX.
    CHECK(n == (int)sizeof(JoinBlob));
    CHECK(io.oversize);
    CHECK(join_blob_state(out, n, io) == JoinRead::invalid);
    // ⛔ THE CONTROL: without the file's SIZE this exact read is a perfect record. That is the accepted-oversize bug.
    CHECK(join_blob_state(out, n, SlotIo{}) == JoinRead::ok);
}

TEST_CASE("device_nv/nRF52 arm: an exactly-sized file reads ok; a SHORT one and a corrupt CTZ are invalid") {
    const JoinBlob good = valid_join();
    SUBCASE("exact") {
        FakeFs fs; fs.payload = &good; fs.payload_len = sizeof good;
        fs.file_size = (uint32_t)sizeof(JoinBlob); fs.read_result = (int)sizeof(JoinBlob);
        JoinBlob out{}; SlotIo io;
        const int n = fs_read_slot(fs, "/mrjoin", &out, sizeof out, &io);
        CHECK_FALSE(io.oversize);
        CHECK(fs.closes == 1);                       // ⛔ the handle is released on the success path too
        CHECK(join_blob_state(out, n, io) == JoinRead::ok);
    }
    SUBCASE("short") {
        FakeFs fs; fs.payload = &good; fs.payload_len = sizeof good;
        fs.file_size = 40; fs.read_result = 40;
        JoinBlob out{}; SlotIo io;
        const int n = fs_read_slot(fs, "/mrjoin", &out, sizeof out, &io);
        CHECK_FALSE(io.oversize);                    // ⛔ short is NOT over-long; it is caught by the length
        CHECK(join_blob_state(out, n, io) == JoinRead::invalid);
    }
    SUBCASE("corrupt CTZ block (LFS_ERR_CORRUPT)") {
        FakeFs fs; fs.file_size = (uint32_t)sizeof(JoinBlob); fs.read_result = -84;
        JoinBlob out{}; SlotIo io;
        const int n = fs_read_slot(fs, "/mrjoin", &out, sizeof out, &io);
        CHECK_FALSE(io.backend_failed);              // the FS mounted; it is the BLOCK that is bad
        CHECK(join_blob_state(out, n, io) == JoinRead::invalid);   // ⛔ never absent — -84 is not -1
    }
}

TEST_CASE("device_nv/ESP32 arm: 'this namespace was never written' and 'NVS would not open' are DIFFERENT answers") {
    JoinBlob out{};
    SUBCASE("namespace absent (a first boot: `mr` is created by the first save of ANY record)") {
        FakeNvs nv; nv.open_ok = false; nv.ns_missing = true;
        SlotIo io;
        const int n = nvs_read_slot(nv, "mr", "join", &out, sizeof out, &io);
        CHECK(n == kSlotAbsent);
        CHECK_FALSE(io.backend_failed);              // ★ a fresh chip must read NO PROFILES, ⛔ not STORAGE FAILURE
        CHECK(join_blob_state(out, n, io) == JoinRead::absent);
    }
    SUBCASE("NVS itself would not open") {
        FakeNvs nv; nv.open_ok = false; nv.ns_missing = false;
        SlotIo io;
        const int n = nvs_read_slot(nv, "mr", "join", &out, sizeof out, &io);
        CHECK(n == kSlotAbsent);                     // ⛔ the `int` is UNCHANGED — the other four records do not move
        CHECK(io.backend_failed);
        CHECK(join_blob_state(out, n, io) == JoinRead::io_failed);
        CHECK(join_blob_state(out, n, SlotIo{}) == JoinRead::absent);   // ⛔ the control: the bug, exactly
    }
    SUBCASE("the key is simply not there yet") {
        FakeNvs nv; nv.key_ok = false;
        SlotIo io;
        const int n = nvs_read_slot(nv, "mr", "join", &out, sizeof out, &io);
        CHECK(n == kSlotAbsent);
        CHECK(nv.closes == 1);
        CHECK_FALSE(io.backend_failed);
        CHECK(join_blob_state(out, n, io) == JoinRead::absent);
    }
    SUBCASE("a stored blob LONGER than the record") {
        const JoinBlob good = valid_join();
        FakeNvs nv; nv.payload = &good; nv.payload_len = sizeof good;
        nv.stored_len = sizeof(JoinBlob) + 8;
        nv.get_result = 0;                           // Preferences::getBytes answers 0 when len > maxLen
        SlotIo io;
        const int n = nvs_read_slot(nv, "mr", "join", &out, sizeof out, &io);
        CHECK(io.oversize);
        CHECK(join_blob_state(out, n, io) == JoinRead::invalid);
    }
}

// ★★★ PIN 6 — `/mrcfg`, `/mrid`, `/mrpeers`, `/mrfault` ARE UNCHANGED, MEASURED RATHER THAN ARGUED. They pass no
//     `SlotIo`, and with `io == nullptr` neither arm asks the backend a single extra question: no `File::size()`, no
//     `getBytesLength()`, no `nvs_open` classification. The sequence of library calls they issue is therefore the
//     one they issued before this correction, and the `int` they read is the same on every path.
TEST_CASE("device_nv: with NO SlotIo the arms ask the backend NOTHING extra — the four bool records cannot move") {
    const JoinBlob good = valid_join();
    SUBCASE("nRF52: an over-length file costs no size() call and no lookup, and returns the same length as before") {
        FakeFs fs; fs.payload = &good; fs.payload_len = sizeof good;
        fs.file_size = 4096; fs.read_result = (int)sizeof(JoinBlob);
        Blob cfg{};
        const int n = fs_read_slot(fs, "/mrcfg", &cfg, sizeof(JoinBlob), nullptr);
        CHECK(fs.sizes == 0);                        // ⛔ not one extra FS call
        CHECK(fs.lookups == 0);                      // ⛔ [[B218]]: the tri-state stat is /mrjoin's question ALONE
        CHECK(n == (int)sizeof(JoinBlob));
    }
    SUBCASE("nRF52: a mount failure still just answers -1") {
        FakeFs fs; fs.mount_ok = false;
        Blob cfg{};
        CHECK(fs_read_slot(fs, "/mrcfg", &cfg, sizeof cfg, nullptr) == kSlotAbsent);
        CHECK(fs.lookups == 0);
        CHECK_FALSE(blob_valid_range(cfg, kSlotAbsent, kMagic, 2, kVersion));   // /mrcfg: unchanged -> defaults
    }
    SUBCASE("nRF52: an OPEN failure with no io still answers -1 through the OLD collapsed sequence — no lookup") {
        // ★ [[B218]]'s fix must not leak sideways: for the four bool records open()'s four collapsed facts STAY
        //   collapsed (absent and unreadable /mrcfg both mean defaults), and the stat is never issued for them.
        FakeFs fs; fs.open_ok = false;
        Blob cfg{};
        CHECK(fs_read_slot(fs, "/mrcfg", &cfg, sizeof cfg, nullptr) == kSlotAbsent);
        CHECK(fs.mounts == 1);
        CHECK(fs.opens == 1);                        // the identical mount -> open they always issued
        CHECK(fs.lookups == 0);                      // ⛔ the counted discriminator: no lookup call
        CHECK(fs.sizes == 0);
    }
    SUBCASE("ESP32: an open failure costs no nvs_open classification") {
        FakeNvs nv; nv.open_ok = false; nv.ns_missing = false;
        Blob cfg{};
        CHECK(nvs_read_slot(nv, "mr", "cfg", &cfg, sizeof cfg, nullptr) == kSlotAbsent);
        CHECK(nv.absent_checks == 0);                // ⛔ the extra probe is asked ONLY for /mrjoin
    }
    SUBCASE("ESP32: an over-length blob costs no getBytesLength() call") {
        FakeNvs nv; nv.stored_len = 4096; nv.get_result = 0;
        Blob cfg{};
        CHECK(nvs_read_slot(nv, "mr", "cfg", &cfg, sizeof cfg, nullptr) == 0);
        CHECK(nv.len_checks == 0);
    }
    // …and every one of those returns is rejected by the four records' predicates EXACTLY as it was before, because
    // all four consult `n` only through `slot_size_ok` — which never saw a SlotIo and still does not.
    CHECK_FALSE(slot_size_ok(kSlotAbsent, sizeof(Blob)));
    CHECK_FALSE(slot_size_ok(0, sizeof(PeerBlob)));
}
