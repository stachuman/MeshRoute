// MeshRoute — tools/probe_custody_usb/probe_main.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §B278 S4 USB WIRING PROBE — it COMPILES AND EXECUTES the REAL `src/firmware_custody_push.h` against the REAL
// custody codec, through the REAL Arduino `Print` seam, and compares the produced line BYTE FOR BYTE.
//
// WHY IT EXISTS. `src/fw_main.cpp` is compiled by neither the native suite (`test_build_src = no`) nor the
// simulator, so until S4 the custody USB arm was three facts and a fixed sentence and "the glue makes no
// decisions" was arguable. S4 gives the line a MODE selection (direct vs translated), a target-KIND selection
// and a second fail-loud arm — three decisions — so the renderer moved into its own header and this probe is
// what executes it. The runner additionally PINS that `fw_main.cpp`'s switch delegates exactly once and keeps
// no parser of its own; that structural pin is NECESSARY but not SUFFICIENT, and the executable cases below are
// the behavioural half.
//
// ⛔ ONE FAKE, REUSED, NEVER FORKED (U1): `tools/probe_console_sink/fakes/Arduino.h`, whose `Print` reproduces
//    the real cores' CALL GRANULARITY (`println(F(x))` is two writes, `print(int)` is its own call). A second
//    fake is how two probes end up measuring two different Arduinos.
// ⛔ THE JSON CROSS-PIN IS EXECUTED, NOT ASSERTED: the probe links the real `lib/console/console_json.cpp` and
//    renders the SAME record through `write_push`, then compares the USB word/value against the JSON one. The
//    two renderers cannot share a symbol (a `lib/` TU may not depend on `src/`), so their agreement is measured
//    rather than argued.
//
// Usage: probe <path/to/src/fw_main.cpp> <path/to/src/firmware_custody_push.h>
//        Both paths are REQUIRED so a negative control can hand in a mutated COPY without the probe ever writing
//        to the repository.

#include <Arduino.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "frame_codec.h"
#include "command.h"
#include "console_json.h"
#include "firmware_custody_push.h"

FakeSerial Serial;   // the fake declares it `extern`; nothing in this probe drives it (the helper takes a Print&)

namespace {

int n_ok = 0, n_fail = 0;
void chk(bool cond, const std::string& what) {
    std::printf(cond ? "  ok   %s\n" : "  FAIL %s\n", what.c_str());
    if (cond) ++n_ok; else ++n_fail;
}

// ---- the output seam: a `Print` that keeps every byte, in order ----------------------------------------------
class Capture : public Print {
public:
    using Print::write;
    std::string out;
    size_t write(uint8_t b) override { out.push_back(static_cast<char>(b)); return 1; }
    size_t write(const uint8_t* p, size_t n) override {
        out.append(reinterpret_cast<const char*>(p), n); return n;
    }
    void reset() { out.clear(); }
};

// ---- the fixture records, built by the PRODUCTION packers (⛔ never a hand-laid byte array) -------------------
constexpr uint16_t kCtrH  = 0x0BEE;   // 3054 — the HOME counter, which `ctr=` must keep meaning
constexpr uint16_t kCtrM  = 0x0777;   // 1911 — ctrM, which `mobile_ctr=` must carry. ★ DELIBERATELY DIFFERENT.
constexpr uint8_t  kReporterDirect = 2;
constexpr uint8_t  kReporterTail   = 5;
constexpr uint32_t kTargetHash     = 0xA1B2C3D4u;

meshroute::CustodyFailureRecord base_record() {
    meshroute::CustodyFailureRecord r{};
    r.notice_flags = meshroute::custody_notice_flags(meshroute::CustodyRootStage::cts,
                                                     /*repair_attempted=*/true,
                                                     /*next_was_one_way=*/false, /*has_dst_hash=*/false);
    r.terminal_reason    = meshroute::CustodyFailureReason::cascade_count;
    r.failed_origin      = 1;
    r.failed_dst         = 9;
    r.failed_ctr         = kCtrH;
    r.failed_type        = 139;      // 0x8B
    r.failed_plane       = meshroute::CustodyFailurePlane::static_same_layer;
    r.reporter_layer     = 2;
    r.previous_hop       = 1;
    r.failed_next_hop    = 9;
    r.requeue_count      = 3;
    r.alternatives_tried = 1;
    r.committed_hops     = 1;
    r.remaining_hops     = 4;
    return r;
}

std::vector<uint8_t> pack_direct() {
    std::vector<uint8_t> out(meshroute::custody_record_v1_len, 0);
    const size_t n = meshroute::pack_custody_failure(base_record(), std::span<uint8_t>(out.data(), out.size()));
    if (n != meshroute::custody_record_v1_len) { std::printf("  FAIL fixture: the direct packer refused\n"); ++n_fail; }
    return out;
}

std::vector<uint8_t> pack_translated(meshroute::CustodyTranslatedTargetKind kind, uint32_t target) {
    meshroute::CustodyTranslatedTail t{};
    t.original_reporter = kReporterTail;
    t.target_kind       = kind;
    t.mobile_ctr        = kCtrM;
    t.target_value      = target;
    std::vector<uint8_t> out(meshroute::custody_record_translated_len, 0);
    const size_t n = meshroute::pack_custody_failure_translated(base_record(), t,
                         std::span<uint8_t>(out.data(), out.size()));
    if (n != meshroute::custody_record_translated_len) {
        std::printf("  FAIL fixture: the translated packer refused\n"); ++n_fail;
    }
    return out;
}

meshroute::Push make_push(const std::vector<uint8_t>& body, uint8_t origin, uint16_t ctr, uint32_t seq) {
    meshroute::Push p{};
    p.kind = meshroute::PushKind::custody_failure;
    p.origin = origin; p.dst = 9; p.ctr = ctr; p.layer_id = 2; p.seq = seq;
    p.body_len = static_cast<uint8_t>(body.size());
    for (size_t i = 0; i < body.size(); ++i) p.body[i] = body[i];
    return p;
}

std::string render(const meshroute::Push& p) {
    Capture cap;
    mrfw::print_custody_failure(cap, p);
    return cap.out;
}

std::string render_json(const meshroute::Push& p) {
    char buf[1700];
    const size_t n = meshroute::console::write_push(buf, sizeof buf, p, nullptr);
    return std::string(buf, n);
}

// The raw token that follows `"key":` in a JSON line, up to the next `,` or `}`. ⛔ Not a JSON parser — it is a
// field EXTRACTOR for the cross-pin, and it keeps the quotes so a string/number difference is visible.
std::string json_field(const std::string& line, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const size_t at = line.find(needle);
    if (at == std::string::npos) return "";
    const size_t from = at + needle.size();
    size_t to = from;
    while (to < line.size() && line[to] != ',' && line[to] != '}') ++to;
    return line.substr(from, to - from);
}

bool has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }
size_t count_of(const std::string& hay, const std::string& needle) {
    size_t n = 0, at = 0;
    while ((at = hay.find(needle, at)) != std::string::npos) { ++n; at += needle.size(); }
    return n;
}

std::string read_file(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return "";
    std::string out;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return out;
}

// The body of `fw_main.cpp`'s custody switch arm: from its `case` label to the `break;` that ends it.
std::string custody_switch_arm(const std::string& src) {
    const std::string label = "case meshroute::PushKind::custody_failure:";
    const size_t at = src.find(label);
    if (at == std::string::npos) return "";
    const size_t end = src.find("break;", at);
    if (end == std::string::npos) return src.substr(at);
    return src.substr(at, end - at);
}

const char* kWarning = " — the relay could not complete onward custody; NOT proof the destination missed it "
                       "(an e2e ack may still arrive)";

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("  FAIL usage: probe <src/fw_main.cpp> <src/firmware_custody_push.h>\n");
        return 1;
    }
    const std::string fw_main = read_file(argv[1]);
    const std::string helper  = read_file(argv[2]);
    std::printf("== §B278 S4 custody USB probe (the REAL renderer, the REAL codec, the REAL Print seam) ==\n");

    const std::vector<uint8_t> direct = pack_direct();
    const std::vector<uint8_t> t_node = pack_translated(meshroute::CustodyTranslatedTargetKind::node_id, 9);
    const std::vector<uint8_t> t_hash = pack_translated(meshroute::CustodyTranslatedTargetKind::key_hash,
                                                        kTargetHash);

    // =========================================================================================================
    // U1-U2 — THE PRE-S4 DIRECT GOLDEN LINE, BYTE FOR BYTE.
    // ⛔ Transcribed from the print sequence `src/fw_main.cpp` carried before S4 (the evidence file shows the
    //    normalized diff proving the sequence moved verbatim). If S4 had changed one separator, this fails.
    // =========================================================================================================
    const std::string direct_line = render(make_push(direct, kReporterDirect, kCtrH, /*seq=*/17));
    const std::string want_direct =
        std::string("CUSTODY FAILURE reporter=2 layer=2 origin=1 dst=9 ctr=3054 stage=cts "
                    "reason=cascade_count prev=1 next=9 repair=attempted one_way=0 seq=17")
        + kWarning + "\r\n";
    chk(direct_line == want_direct, "U1  the DIRECT line is byte-identical to the pre-S4 golden");
    if (direct_line != want_direct) std::printf("       got:  [%s]\n       want: [%s]\n",
                                                direct_line.c_str(), want_direct.c_str());
    chk(!has(direct_line, "delegated") && !has(direct_line, "target_") && !has(direct_line, "mobile_ctr"),
        "U2  a DIRECT record grows no translated field");

    // =========================================================================================================
    // U3-U4 — THE TWO TRANSLATED GOLDEN LINES.
    // =========================================================================================================
    const std::string node_line = render(make_push(t_node, kReporterTail, kCtrM, /*seq=*/17));
    const std::string want_node =
        std::string("CUSTODY FAILURE reporter=5 layer=2 origin=1 dst=9 ctr=3054 stage=cts "
                    "reason=cascade_count prev=1 next=9 repair=attempted one_way=0 "
                    "delegated=true target_kind=node_id target_id=9 mobile_ctr=1911 seq=17")
        + kWarning + "\r\n";
    chk(node_line == want_node, "U3  the TRANSLATED node-id line is byte-identical to its golden");
    if (node_line != want_node) std::printf("       got:  [%s]\n       want: [%s]\n",
                                            node_line.c_str(), want_node.c_str());
    const std::string hash_line = render(make_push(t_hash, kReporterTail, kCtrM, /*seq=*/17));
    const std::string want_hash =
        std::string("CUSTODY FAILURE reporter=5 layer=2 origin=1 dst=9 ctr=3054 stage=cts "
                    "reason=cascade_count prev=1 next=9 repair=attempted one_way=0 "
                    "delegated=true target_kind=hash target_hash=a1b2c3d4 mobile_ctr=1911 seq=17")
        + kWarning + "\r\n";
    chk(hash_line == want_hash, "U4  the TRANSLATED hash line is byte-identical to its golden");
    if (hash_line != want_hash) std::printf("       got:  [%s]\n       want: [%s]\n",
                                            hash_line.c_str(), want_hash.c_str());

    // =========================================================================================================
    // U5-U7 — EXACTLY ONE TARGET FIELD, AND NO ALIAS (§8.4: a duplicated value is a second compatibility surface)
    // =========================================================================================================
    chk(has(node_line, " target_id=") && !has(node_line, " target_hash="),
        "U5  the node-id form prints target_id and NOT target_hash");
    chk(has(hash_line, " target_hash=") && !has(hash_line, " target_id="),
        "U6  the hash form prints target_hash and NOT target_id");
    chk(!has(node_line, "via_home") && !has(hash_line, "via_home")
        && !has(node_line, "home_ctr") && !has(hash_line, "home_ctr"),
        "U7  neither line carries a `via_home` / `home_ctr` alias");

    // =========================================================================================================
    // U8-U9 — THE TWO COUNTERS KEEP THEIR MEANINGS (ctrH stays `ctr=`, ctrM is its own field)
    // =========================================================================================================
    chk(has(node_line, " ctr=3054 ") && has(node_line, " mobile_ctr=1911"),
        "U8  `mobile_ctr` is ctrM (1911), not ctrH");
    chk(!has(node_line, " mobile_ctr=3054") && !has(hash_line, " mobile_ctr=3054"),
        "U9  `mobile_ctr` is never the HOME counter");

    // =========================================================================================================
    // U10 — THE FAIL-LOUD ARM: an unparseable record says so and prints nothing else.
    // =========================================================================================================
    {
        std::vector<uint8_t> torn = t_node;
        torn[1] = meshroute::custody_record_v1_len;     // a bit-6 record CLAIMING the direct length
        const std::string got = render(make_push(torn, kReporterTail, kCtrM, /*seq=*/17));
        chk(got == "CUSTODY FAILURE (unparseable record)\r\n",
            "U10 an unparseable record produces the loud line and nothing else");
        if (got != "CUSTODY FAILURE (unparseable record)\r\n") std::printf("       got: [%s]\n", got.c_str());
    }

    // =========================================================================================================
    // U11-U12 — `seq` follows the established push convention (omitted at 0, present when nonzero).
    // =========================================================================================================
    {
        const std::string zero = render(make_push(t_node, kReporterTail, kCtrM, /*seq=*/0));
        chk(!has(zero, " seq="), "U11 `seq` is OMITTED when storage is disabled (seq 0)");
        const std::string nine = render(make_push(t_node, kReporterTail, kCtrM, /*seq=*/9));
        chk(has(nine, " seq=9"), "U12 `seq` is PRESENT when the store assigned one");
    }

    // =========================================================================================================
    // U13-U15 — THE WARNING, AND THE WORDS THAT ARE FORBIDDEN (§14.3: never a NACK, never non-delivery).
    // =========================================================================================================
    chk(has(direct_line, kWarning) && has(node_line, kWarning) && has(hash_line, kWarning),
        "U13 the `NOT proof the destination missed it` warning survives on every rendered report");
    chk(count_of(node_line, kWarning) == 1, "U14 the warning appears exactly once per line");
    {
        const std::string all = direct_line + node_line + hash_line;
        chk(!has(all, "NACK") && !has(all, "nack") && !has(all, "not delivered")
            && !has(all, "delivery failed") && !has(all, "undelivered"),
            "U15 no rendered line claims a NACK or a delivery failure");
    }

    // =========================================================================================================
    // U16-U21 — THE JSON CROSS-PIN, EXECUTED. The USB names and values must be the JSON ones; the two renderers
    //           share no symbol, so this is the only thing that can keep them from drifting.
    // =========================================================================================================
    {
        const std::string jn = render_json(make_push(t_node, kReporterTail, kCtrM, /*seq=*/17));
        const std::string jh = render_json(make_push(t_hash, kReporterTail, kCtrM, /*seq=*/17));
        chk(json_field(jn, "target_kind") == "\"node_id\"" && has(node_line, " target_kind=node_id"),
            "U16 the node-id target word is the SAME on USB and in JSON");
        chk(json_field(jh, "target_kind") == "\"hash\"" && has(hash_line, " target_kind=hash"),
            "U17 the hash target word is the SAME on USB and in JSON");
        chk(json_field(jh, "target_hash") == "\"a1b2c3d4\"" && has(hash_line, " target_hash=a1b2c3d4"),
            "U18 the target hash renders in the SAME canonical 8-digit lower-case form on both surfaces");
        chk(json_field(jn, "target_id") == "9" && has(node_line, " target_id=9"),
            "U19 the node-id target VALUE is the same on both surfaces");
        chk(json_field(jn, "mobile_ctr") == "1911" && has(node_line, " mobile_ctr=1911"),
            "U20 `mobile_ctr` is the same on both surfaces");
        chk(json_field(jn, "delegated") == "true" && has(node_line, " delegated=true"),
            "U21 `delegated` is the same boolean on both surfaces");
        chk(json_field(jn, "ctr") == "3054" && has(node_line, " ctr=3054 "),
            "U22 the existing `ctr` field still means ctrH on both surfaces");
    }

    // =========================================================================================================
    // U23-U26 — THE STRUCTURAL PIN. Necessary, not sufficient: `fw_main.cpp` must DELEGATE and keep nothing.
    // =========================================================================================================
    chk(!fw_main.empty(), "U23 `fw_main.cpp` was read");
    {
        const std::string arm = custody_switch_arm(fw_main);
        chk(!arm.empty() && count_of(arm, "mrfw::print_custody_failure(") == 1
            && count_of(fw_main, "mrfw::print_custody_failure(") == 1,
            "U24 the custody switch arm delegates to the helper EXACTLY ONCE, and nothing else does");
        chk(!has(fw_main, "parse_custody_failure") && !has(fw_main, "parse_custody_translated_tail")
            && !has(fw_main, "custody_stage_of_flags"),
            "U25 `fw_main.cpp` holds no custody parser and no second decode of its own");
        chk(!has(arm, "target_") && !has(arm, "mobile_ctr") && !has(arm, "delegated")
            && !has(arm, "CUSTODY_FLAG_") && !has(arm, "CUSTODY FAILURE"),
            "U26 the switch arm holds no target selection, no counter choice and no wording");
    }
    chk(!helper.empty() && count_of(helper, "void print_custody_failure(") == 1,
        "U27 the helper header defines `print_custody_failure` exactly once");

    std::printf("probe: %d ok, %d failed\n", n_ok, n_fail);
    return n_fail ? 1 : 0;
}
