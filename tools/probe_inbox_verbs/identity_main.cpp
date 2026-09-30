// MeshRoute — tools/probe_inbox_verbs/identity_main.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★★ W0 (brief §2.7; design §4.3) — THE IDENTITY ARM's DRIVER. It links the REAL, UNEDITED `src/firmware_config.cpp` and
//     `src/firmware_commands.cpp` (the snapshot adapter `mrfw::id_candidate_from_live`, the rename service
//     `mrfw::rename_node`, the three `cfg set` arms and `do_regen`) with the real core/console and this directory's
//     platform/NV fakes, and drives `cfg set name|lat|lon` and `regen` THROUGH THE REAL ROUTER (`mrfw::dispatch`).
//   · TRANSPORT: USB-shaped — the router writes to the real guarded `mrcon`, drained to the fake `Serial` — and, for
//     `regen`, the production `LineSink` over a capture. ⛔ It is NOT the BLE adapter: `fw_main.cpp`'s dedicated BLE
//     `cfg set` branch (B483) is outside this proof and nothing here claims it.
//   · ROLES: `run.sh` builds it twice — `identity_accept` (`[env:heltec_v3]`'s defines) and `identity_client` (+
//     `-DMR_PROFILE_MOBILE`), so CLIENT's `regen` debt refusal is measured on the real binding too.
//   · FIXTURES ARE `probe_main.cpp`'s (U1): it is included with `MR0C_NO_MAIN` (its own `main` left out) and
//     `MR_PROBE_IDENTITY_ARM` (its eight config-handler stubs and the legacy adapter stand-in left out — this build
//     links the real ones, and no proof here is satisfied through the stand-in).
//   · DIAGNOSTICS print lengths and HEX, never raw name bytes (B478 is open).
// ⛔ The platform shim `identity_platform.h` is force-included into the config TU ONLY (run.sh), not into this file.
#define MR0C_NO_MAIN
#define MR_PROBE_IDENTITY_ARM
#include "probe_main.cpp"

namespace w0 {

#if MR_FEAT_RADMIN_CLIENT
#  define W0_REGEN_NOTE "> regen note old self ACL grants do not follow the new key; dedicated keys and targets preserved\n"
#else
#  define W0_REGEN_NOTE ""
#endif

constexpr int32_t kLiveLat = 521234567, kLiveLon = 211234567;
constexpr int32_t kDiskLat = 111111111, kDiskLon = -222222222;
static const char kLiveName[] = "Live Name";                      // 9 B — the RUNNING name
static const char kDiskName[] = "Disk Name";                      // 9 B — a DIFFERENT name only the medium holds
static const char kMax32[]    = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"; // exactly 32 B

// Four rotating hex buffers, so one diagnostic can show two ranges.
static const char* hex(const void* p, size_t n) {
    static char bufs[4][2 * 320 + 1]; static int k = 0;
    char* b = bufs[k = (k + 1) & 3]; size_t w = 0;
    for (size_t i = 0; i < n && w + 2 < sizeof bufs[0]; ++i)
        w += size_t(std::snprintf(b + w, sizeof bufs[0] - w, "%02x", static_cast<const unsigned char*>(p)[i]));
    b[w] = '\0';
    return b;
}

// The record a correct writer must leave, built FIELD BY FIELD on a zeroed record — ⛔ never through the helper
// under test, so a conversion defect cannot also be the expectation.
static mrnv::IdBlob rec(const uint8_t (&seed)[32], const char* name, size_t len, int32_t lat, int32_t lon) {
    mrnv::IdBlob r{};
    r.magic = mrnv::kIdMagic; r.version = mrnv::kIdVersion; r.name_len = uint16_t(len);
    std::memcpy(r.seed, seed, 32);
    for (size_t i = 0; i < len; ++i) r.name[i] = name[i];
    r.lat_e7 = lat; r.lon_e7 = lon;
    return r;
}

static uint8_t g_live_seed[32];
static uint8_t g_disk_seed[32];

// ★ THE LIVE STATE a booted device runs on: identity, crypto, name and BOTH position mirrors.
static void install_live(const char* name, size_t len, int32_t lat, int32_t lon) {
    meshroute::identity_from_seed(g_identity, g_live_seed);
    g_node.set_identity(10, g_identity.key_hash32);
    g_node.set_crypto_identity(g_identity.x_secret, g_identity.ed_pub);
    g_node.set_name(name, uint8_t(len));
    g_lat_e7 = lat; g_lon_e7 = lon;
    g_node.mutable_config().lat_e7 = lat; g_node.mutable_config().lon_e7 = lon;
}

// Unrelated stores no identity writer may touch — /mrcfg and the administration/controller records' slots — seeded
// with recognisable bytes. Every row checks the medium outside `/mrid` is byte-identical afterwards.
struct Other { const char* key; unsigned char fill; size_t len; };
static const Other kOthers[] = { {"cfg", 0x11, 240}, {"admid", 0x22, 40}, {"acl", 0x33, 64},
                                 {"mkeys", 0x44, 96}, {"targets", 0x55, 128}, {"peers", 0x66, 200} };

// A fresh HEALTHY medium holding the unrelated stores and (optionally) `/mrid`, written by PRODUCTION's `save_id`.
static void medium(const mrnv::IdBlob* durable) {
    MrProbeNv& nv = mrprobe_nv();
    nv.reset();
    nv.ns_present = true; nv.rw_ok = true;
    unsigned char buf[256];
    for (const Other& o : kOthers) { std::memset(buf, o.fill, o.len); (void)nv.put("mr", o.key, buf, o.len); }
    if (durable) (void)mrnv::save_id(*durable);
    nv.writes = 0; nv.reads = 0;
}
static bool others_intact() {
    unsigned char buf[256];
    for (const Other& o : kOthers) {
        std::memset(buf, o.fill, o.len);
        if (!mrprobe_nv().holds("mr", o.key, buf, o.len)) return false;
    }
    return true;
}
static bool medium_holds_id(const mrnv::IdBlob& want) { return mrprobe_nv().holds("mr", "id", &want, sizeof want); }
static const mrnv::IdBlob* stored_id() {
    MrProbeNvSlot* s = mrprobe_nv().find("mr", "id");
    return (s && s->len == sizeof(mrnv::IdBlob)) ? reinterpret_cast<const mrnv::IdBlob*>(s->data) : nullptr;
}
static bool live_name_is(const char* name, size_t len) {
    char buf[32]; const uint8_t n = g_node.effective_name(buf, 32);
    return n == len && std::memcmp(buf, name, len) == 0;
}
static const char* live_name_hex() {
    static char buf[32]; const uint8_t n = g_node.effective_name(buf, 32);
    return hex(buf, n);
}

// The USB-shaped route: the REAL router onto the REAL guarded console, drained to the fake Serial.
static bool usb(const std::string& line) {
    Serial.reset();
    const bool owned = mrfw::dispatch(line.data(), line.size(), mrcon);
    mrcon.service();
    return owned;
}

// ---------------------------------------------------------------------------------------------------------------
// THE STORAGE × WRITER MATRIX (brief §2.7): the medium's `/mrid` is healthy-but-DIFFERENT from the live state, absent,
// unreadable, short, bad-magic, bad-version or oversize. The durable record (when present) carries a DIFFERENT seed,
// name and position from the running device, so any field a writer took from NV bytes shows up in the saved record.
// ---------------------------------------------------------------------------------------------------------------
enum class Fault { healthy_diff, absent, unreadable, short_read, bad_magic, bad_version, oversize };
static const char* fault_name(Fault f) {
    switch (f) {
        case Fault::healthy_diff: return "healthy_diff";
        case Fault::absent:       return "absent";
        case Fault::unreadable:   return "unreadable";
        case Fault::short_read:   return "short";
        case Fault::bad_magic:    return "bad_magic";
        case Fault::bad_version:  return "bad_version";
        case Fault::oversize:     return "oversize";
    }
    return "?";
}
static void apply_fault(Fault f) {
    const mrnv::IdBlob disk = rec(g_disk_seed, kDiskName, sizeof kDiskName - 1, kDiskLat, kDiskLon);
    medium(f == Fault::absent ? nullptr : &disk);
    MrProbeNvSlot* s = mrprobe_nv().find("mr", "id");
    switch (f) {
        case Fault::healthy_diff:
        case Fault::absent:       break;
        case Fault::unreadable:   mrprobe_nv().ns_present = false; break;   // reads fail; the medium still accepts writes
        case Fault::short_read:   s->len = 50; break;                        // a prefix only: seed + part of the name
        case Fault::bad_magic:    s->data[0] ^= 0x40; break;                 // a full read, rejected on its header
        case Fault::bad_version:  s->data[4] ^= 0x40; break;
        case Fault::oversize:     s->len = sizeof(mrnv::IdBlob) + 1; break;  // Preferences refuses an over-long blob
    }
}

enum class Writer { name, lat, lon, regen };
static const char* writer_name(Writer w) {
    switch (w) { case Writer::name: return "name"; case Writer::lat: return "lat"; case Writer::lon: return "lon";
                 case Writer::regen: return "regen"; }
    return "?";
}

static void matrix_row(Writer w, Fault f) {
    const char* wn = writer_name(w); const char* fn = fault_name(f);
    install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon);
    apply_fault(f);
    const uint32_t hash_before = g_identity.key_hash32;
    mrnv::IdBlob want{};
    char want_out[256] = {};
    bool owned = false;
    const char* got_out = nullptr;
    uint8_t exp_seed[32] = {};
    meshroute::Identity exp{};
    switch (w) {
        case Writer::name:
            want = rec(g_live_seed, "New Name", 8, kLiveLat, kLiveLon);
            std::snprintf(want_out, sizeof want_out, "> cfg ok name (saved to /mrid)\r\n");
            owned = usb("cfg set name New Name"); got_out = Serial.out; break;
        case Writer::lat:
            want = rec(g_live_seed, kLiveName, sizeof kLiveName - 1, 532500000, kLiveLon);
            std::snprintf(want_out, sizeof want_out, "> cfg ok (saved to /mrid)\r\n");
            owned = usb("cfg set lat 53.25"); got_out = Serial.out; break;
        case Writer::lon:
            want = rec(g_live_seed, kLiveName, sizeof kLiveName - 1, kLiveLat, 225000000);
            std::snprintf(want_out, sizeof want_out, "> cfg ok (saved to /mrid)\r\n");
            owned = usb("cfg set lon 22.5"); got_out = Serial.out; break;
        case Writer::regen: {
            expected_next_identity(exp_seed, exp);
            uint8_t s32[32]; std::memcpy(s32, exp_seed, 32);
            want = rec(s32, kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon);
            std::snprintf(want_out, sizeof want_out, "> regen ok  key_hash32= 0x%08lX  name=\"%s\"\r\n" W0_REGEN_NOTE,
                          (unsigned long)exp.key_hash32, kLiveName);
            ble_reset(); Serial.reset();
            owned = route_ble("regen"); mrcon.service(); got_out = g_ble; break;
        }
    }
    const int writes = mrprobe_nv().writes;
    mrprobe_nv().ns_present = true;                       // verify through a readable medium
    const mrnv::IdBlob* got = stored_id();
    CHK(owned && std::strcmp(got_out, want_out) == 0,
        "M.%s.%s.out the exact success line through the real router [%s]", wn, fn, hex(got_out, std::strlen(got_out)));
    CHK(writes == 1, "M.%s.%s.writes exactly ONE /mrid write (writes=%d)", wn, fn, writes);
    CHK(medium_holds_id(want),
        "M.%s.%s.rec the saved record is the LIVE seed/name/position with ONLY the intended field replaced [%s]",
        wn, fn, got ? hex(got, sizeof *got) : "none");
    bool live_ok = false;
    switch (w) {
        case Writer::name:
            live_ok = live_name_is("New Name", 8) && g_lat_e7 == kLiveLat && g_lon_e7 == kLiveLon &&
                      g_node.config().lat_e7 == kLiveLat && g_node.config().lon_e7 == kLiveLon &&
                      g_identity.key_hash32 == hash_before;
            break;
        case Writer::lat:
            live_ok = live_name_is(kLiveName, sizeof kLiveName - 1) && g_lat_e7 == 532500000 &&
                      g_node.config().lat_e7 == 532500000 && g_lon_e7 == kLiveLon && g_node.config().lon_e7 == kLiveLon &&
                      g_identity.key_hash32 == hash_before;
            break;
        case Writer::lon:
            live_ok = live_name_is(kLiveName, sizeof kLiveName - 1) && g_lon_e7 == 225000000 &&
                      g_node.config().lon_e7 == 225000000 && g_lat_e7 == kLiveLat && g_node.config().lat_e7 == kLiveLat &&
                      g_identity.key_hash32 == hash_before;
            break;
        case Writer::regen:
            live_ok = live_name_is(kLiveName, sizeof kLiveName - 1) && g_identity.key_hash32 == exp.key_hash32 &&
                      g_node.key_hash32() == exp.key_hash32 && g_node.crypto_ready() &&
                      g_lat_e7 == kLiveLat && g_lon_e7 == kLiveLon;
            break;
    }
    CHK(live_ok, "M.%s.%s.live the live state is exactly the intended one (name=%s lat=%ld lon=%ld)",
        wn, fn, live_name_hex(), (long)g_lat_e7, (long)g_lon_e7);
    CHK(others_intact(), "M.%s.%s.others every unrelated store (/mrcfg, administration, controller, peers) is untouched",
        wn, fn);
}

// ---------------------------------------------------------------------------------------------------------------
// A rename through the real router on a chosen medium; returns the writes it cost.
// ---------------------------------------------------------------------------------------------------------------
struct Snap {
    uint8_t seed[32]; char name[32]; uint8_t name_len; int32_t lat, lon, clat, clon; uint32_t team; uint32_t hash;
};
static Snap snap() {
    Snap s{};
    std::memcpy(s.seed, g_identity.seed, 32);
    s.name_len = g_node.effective_name(s.name, 32);
    s.lat = g_lat_e7; s.lon = g_lon_e7; s.clat = g_node.config().lat_e7; s.clon = g_node.config().lon_e7;
    s.team = g_node.config().team_id; s.hash = g_identity.key_hash32;
    return s;
}
static bool same(const Snap& a, const Snap& b) {
    return std::memcmp(a.seed, b.seed, 32) == 0 && a.name_len == b.name_len && std::memcmp(a.name, b.name, a.name_len) == 0 &&
           a.lat == b.lat && a.lon == b.lon && a.clat == b.clat && a.clon == b.clon && a.team == b.team && a.hash == b.hash;
}

// B448 through the real router: the name is everything after the key's space, raw to the end of the line.
struct NameCase { const char* id; std::string value; bool accepted; };

}  // namespace w0

int main() {
    using namespace w0;
    printf("== W0 identity arm (REAL firmware_config.cpp + firmware_commands.cpp; router-driven; %s) ==\n",
           MR_FEAT_RADMIN_CLIENT ? "CLIENT" : "ACCEPT");
    for (size_t i = 0; i < 32; ++i) { g_live_seed[i] = uint8_t(0x31 + i); g_disk_seed[i] = uint8_t(0xD0 + i); }
    const mrnv::IdBlob live_rec = rec(g_live_seed, kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon);

    // ---- M: the storage × writer matrix ------------------------------------------------------------------------
    for (Writer w : { Writer::name, Writer::lat, Writer::lon, Writer::regen })
        for (Fault f : { Fault::healthy_diff, Fault::absent, Fault::unreadable, Fault::short_read, Fault::bad_magic,
                         Fault::bad_version, Fault::oversize })
            matrix_row(w, f);

    // ---- N: the rename's coalescing, W0R-1 and repair ----------------------------------------------------------
    {   // N1 — the byte-identical record is already durable: zero writes, the name stays live.
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon); medium(&live_rec);
        const bool own = usb("cfg set name Live Name");
        CHK(own && std::strcmp(Serial.out, "> cfg ok name (saved to /mrid)\r\n") == 0 && mrprobe_nv().writes == 0 &&
            medium_holds_id(live_rec) && live_name_is(kLiveName, sizeof kLiveName - 1) && others_intact(),
            "N1 a byte-identical durable record answers ok with ZERO writes (writes=%d)", mrprobe_nv().writes);
    }
    {   // N2 — W0R-1: already durable, not yet live. The durable record holds `Saved Name` with the live seed and position.
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon);
        const mrnv::IdBlob saved = rec(g_live_seed, "Saved Name", 10, kLiveLat, kLiveLon);
        medium(&saved);
        const bool own = usb("cfg set name Saved Name");
        CHK(own && std::strcmp(Serial.out, "> cfg ok name (saved to /mrid)\r\n") == 0 && mrprobe_nv().writes == 0 &&
            medium_holds_id(saved),
            "N2 W0R-1: the requested record is ALREADY durable — ok with ZERO writes (writes=%d)", mrprobe_nv().writes);
        CHK(live_name_is("Saved Name", 10),
            "N2b W0R-1: ...and the requested name is LIVE afterwards, not the previous one (live=%s)", live_name_hex());
    }
    {   // N3 — live-name equality alone is never a no-op: every absent/bad/partial/different record is REPAIRED.
        struct { const char* id; Fault f; } rep[] = { {"N3a", Fault::absent}, {"N3b", Fault::bad_magic},
                                                      {"N3c", Fault::short_read}, {"N3d", Fault::healthy_diff},
                                                      {"N3e", Fault::unreadable} };
        for (auto& r : rep) {
            install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon); apply_fault(r.f);
            const bool own = usb("cfg set name Live Name");
            const int writes = mrprobe_nv().writes; mrprobe_nv().ns_present = true;
            CHK(own && std::strcmp(Serial.out, "> cfg ok name (saved to /mrid)\r\n") == 0 && writes == 1 &&
                medium_holds_id(live_rec) && live_name_is(kLiveName, sizeof kLiveName - 1) && others_intact(),
                "%s the live name equals the request but the record is %s — it is REPAIRED from live values (writes=%d)",
                r.id, fault_name(r.f), writes);
        }
    }
    {   // N4 — a durable record that differs only in POSITION is repaired too (live versus durable).
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon);
        const mrnv::IdBlob moved = rec(g_live_seed, kLiveName, sizeof kLiveName - 1, kDiskLat, kDiskLon);
        medium(&moved);
        const bool own = usb("cfg set name Live Name");
        CHK(own && mrprobe_nv().writes == 1 && medium_holds_id(live_rec),
            "N4 a durable record with a STALE position is repaired from the live position (writes=%d)", mrprobe_nv().writes);
    }
    {   // N5 — a failed save publishes nothing.
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon); medium(&live_rec);
        mrprobe_nv().fail_write = true;
        const Snap before = snap();
        const bool own = usb("cfg set name Other Name");
        CHK(own && std::strcmp(Serial.out, "> cfg err nv_save_failed\r\n") == 0 && mrprobe_nv().writes == 1,
            "N5 a refused save answers nv_save_failed after exactly one attempt [%s]", hex(Serial.out, Serial.n_out));
        CHK(same(before, snap()) && live_name_is(kLiveName, sizeof kLiveName - 1) && medium_holds_id(live_rec),
            "N5b ...and publishes NOTHING: the live identity and the medium are unchanged (live=%s)", live_name_hex());
        mrprobe_nv().fail_write = false;
    }
    {   // N6 — a LIVE 32-byte name is read COUNTED by the adapter (a coordinate write carries all 32 bytes).
        install_live(kMax32, 32, kLiveLat, kLiveLon); medium(nullptr);
        const bool own = usb("cfg set lat 1.5");
        const mrnv::IdBlob want = rec(g_live_seed, kMax32, 32, 15000000, kLiveLon);
        const mrnv::IdBlob* got = stored_id();
        CHK(own && medium_holds_id(want),
            "N6 a 32-byte LIVE name survives a coordinate write whole — the adapter reads it counted [%s]",
            got ? hex(got->name, got->name_len) : "none");
    }

    // ---- G: B448 through the real router --------------------------------------------------------------------
    std::string s31(31, 'a'), s32(32, 'b'), s33(33, 'c'), s300(300, 'x');
    std::string hi32; for (int i = 0; i < 32; ++i) hi32.push_back(char(0x80 + i * 3));
    const NameCase cases[] = {
        {"G1", "",                                              false},   // `cfg set name` — no value
        {"G3", " Two words  ",                                  true },   // spaces are name bytes, leading and trailing
        {"G4", "\"Two words\"",                                 true },   // literal quotes are name bytes
        {"G5", "   ",                                           true },   // an all-space nonempty name is accepted
        {"G6", s31,                                             true },
        {"G7", s32,                                             true },
        {"G8", s33,                                             false},   // one past the field
        {"G9", std::string(30, 'd') + "\xC5\x82",               true },   // a 2-byte character wholly within 32
        {"G10", std::string(31, 'e') + "\xC5\x82",              false},   // a 2-byte character CROSSING byte 32
        {"G11", std::string(30, 'f') + "\xE2\x82\xAC",          false},   // a 3-byte character crossing byte 32
        {"G12", hi32,                                           true },   // 32 high bytes, stored as bytes
        {"G13", s300,                                           false},   // over 255: narrowing would wrap it
    };
    for (const NameCase& c : cases) {
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon); medium(&live_rec);
        const Snap before = snap();
        const std::string line = c.value.empty() ? std::string("cfg set name") : "cfg set name " + c.value;
        const bool own = usb(line);
        const int writes = mrprobe_nv().writes;
        if (c.accepted) {
            const mrnv::IdBlob want = rec(g_live_seed, c.value.data(), c.value.size(), kLiveLat, kLiveLon);
            CHK(own && std::strcmp(Serial.out, "> cfg ok name (saved to /mrid)\r\n") == 0 && writes == 1 &&
                medium_holds_id(want) && live_name_is(c.value.data(), c.value.size()) && others_intact(),
                "%s a %u-byte name is saved WHOLE and made live (saved=%s)", c.id, unsigned(c.value.size()),
                stored_id() ? hex(stored_id()->name, stored_id()->name_len) : "none");
        } else {
            const char* want_out = c.value.empty() ? "> cfg err bad_args\r\n" : "> cfg err too_long\r\n";
            CHK(own && std::strcmp(Serial.out, want_out) == 0,
                "%s a %u-byte name answers %s [%s]", c.id, unsigned(c.value.size()),
                c.value.empty() ? "bad_args" : "too_long", hex(Serial.out, Serial.n_out));
            CHK(writes == 0 && medium_holds_id(live_rec) && others_intact() && same(before, snap()),
                "%sb ...with ZERO writes and the seed, name, both position mirrors, membership and every NV slot unchanged "
                "(writes=%d live=%s)", c.id, writes, live_name_hex());
        }
    }
    {   // G2 — the trailing-space form is the same empty value.
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon); medium(&live_rec);
        const Snap before = snap();
        const bool own = usb("cfg set name ");
        CHK(own && std::strcmp(Serial.out, "> cfg err bad_args\r\n") == 0 && mrprobe_nv().writes == 0 &&
            same(before, snap()) && medium_holds_id(live_rec),
            "G2 `cfg set name ` (an empty value after the separator) answers bad_args with zero writes");
    }

    // ---- C: B482 — a coordinate is published only after its save succeeds -----------------------------------
    for (const char* line : { "cfg set lat 53.25", "cfg set lon 22.5" }) {
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon); medium(&live_rec);
        mrprobe_nv().fail_write = true;
        const Snap before = snap();
        const bool own = usb(line);
        const bool is_lat = line[10] == 't';
        CHK(own && std::strcmp(Serial.out, "> cfg err nv_save_failed\r\n") == 0 && mrprobe_nv().writes == 1,
            "C.%s.err a refused save prints nv_save_failed after exactly one attempt", is_lat ? "lat" : "lon");
        CHK(same(before, snap()) && medium_holds_id(live_rec),
            "C.%s.live ...and BOTH mirrors keep the old coordinate (g=%ld/%ld cfg=%ld/%ld)", is_lat ? "lat" : "lon",
            (long)g_lat_e7, (long)g_lon_e7, (long)g_node.config().lat_e7, (long)g_node.config().lon_e7);
        mrprobe_nv().fail_write = false;
    }

    // ---- R: `regen` refusals keep everything -------------------------------------------------------------------
    {   // R1 — a refused save: the error on the supplied sink, one attempt, identity and medium unchanged.
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon); medium(&live_rec);
        mrprobe_nv().fail_write = true;
        const Snap before = snap();
        ble_reset(); Serial.reset(); rng_reset();
        const bool own = route_ble("regen"); mrcon.service();
        CHK(own && std::strcmp(g_ble, "> regen err nv_save_failed\r\n") == 0 && mrprobe_nv().writes == 1 &&
            Serial.n_out == 0 && same(before, snap()) && medium_holds_id(live_rec) && others_intact(),
            "R1 a refused regen save keeps the identity, the name, the position and the medium [%s]",
            hex(g_ble, g_ble_n));
        mrprobe_nv().fail_write = false;
    }
#if MR_FEAT_RADMIN_CLIENT
    {   // R2 — CLIENT: an outstanding source-bound request refuses `regen` before any draw or write.
        install_live(kLiveName, sizeof kLiveName - 1, kLiveLat, kLiveLon); medium(&live_rec);
        auto& rc = g_node.remote_client(); rc = {};
        rc.pending[0].core.state = uint8_t(meshroute::RemoteClientPhase::response_wait);
        const Snap before = snap();
        ble_reset(); Serial.reset(); rng_reset();
        const bool own = route_ble("regen"); mrcon.service();
        CHK(own && std::strstr(g_ble, "> regen err remote_busy") == g_ble && mrprobe_nv().writes == 0 &&
            same(before, snap()) && medium_holds_id(live_rec) && others_intact(),
            "R2 CLIENT: remote debt refuses regen — zero writes, identity and every store unchanged [%s]",
            hex(g_ble, g_ble_n));
        rc = {};
    }
#endif

    mrprobe_nv().reset();
    printf("checks: %d   failures: %d\n", g_chk, g_fail);
    printf("%s\n", g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
