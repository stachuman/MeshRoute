// MeshRoute — test_fault_log.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the platform-neutral fault-log core (lib/core/fault_log.{h,cpp}): the FaultLog POD round-trip
// (whole-blob R/W, mrnv style), the ring push + drop-oldest at kFaultRingN, the v2 scratch-derived CAUSE classifier,
// the RESETREAS-bit hint decode, and the `version`/`faults` text formatters. The WDT / HardFault / retained-scratch
// are HW-only (bench). NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); CHECK only.
#include "doctest.h"
#include "fault_log.h"

#include <cstring>
#include <string>

using namespace mrfault;

namespace {
FaultRecord rec(uint32_t seq, uint8_t cause, uint8_t had_fault = 0, uint32_t ran_ms = 0,
                uint32_t pc = 0, uint32_t cfsr = 0, uint32_t addr = 0, uint16_t reason = 0, uint32_t lr = 0) {
    FaultRecord r{}; r.boot_seq = seq; r.cause = cause; r.had_fault = had_fault;
    r.ran_ms = ran_ms; r.fault_pc = pc; r.cfsr = cfsr; r.fault_addr = addr; r.reason_bits = reason; r.fault_lr = lr; return r;
}
std::string rstr(uint16_t bits) { char b[48]; reset_reason_str(bits, b, sizeof b); return std::string(b); }
}  // namespace

TEST_CASE("fault_log: FaultLog is a POD that round-trips a whole-blob memcpy (the /mrfault store contract)") {
    FaultLog f; fault_log_init(f);
    CHECK(fault_log_valid(f));
    fault_log_push(f, rec(1, kCauseWatchdog, 0, 5000));
    fault_log_push(f, rec(2, kCauseHardfault, 1, 12000, 0xABCD, 0x8200, 0xDEAD, 0, /*lr*/0x5678));
    uint8_t bytes[sizeof(FaultLog)];
    std::memcpy(bytes, &f, sizeof f);                         // "serialize" = the verbatim POD copy (== mrnv::save)
    FaultLog g{};
    std::memcpy(&g, bytes, sizeof g);                         // "deserialize"
    CHECK(std::memcmp(&f, &g, sizeof f) == 0);                // byte-identical
    CHECK(fault_log_valid(g));
    CHECK(g.count == 2); CHECK(g.boot_seq == 2);
    CHECK(fault_log_at(g, 0)->boot_seq == 2);                 // newest first
    CHECK(fault_log_at(g, 0)->cause == kCauseHardfault);
    CHECK(fault_log_at(g, 0)->had_fault == 1);
    CHECK(fault_log_at(g, 0)->fault_pc == 0xABCD);
    CHECK(fault_log_at(g, 0)->fault_lr == 0x5678);           // v3: the stacked LR round-trips
    CHECK(fault_log_at(g, 1)->boot_seq == 1);
}

TEST_CASE("fault_log: a bad magic/version is INVALID -> the loader re-inits fresh (older record rejected)") {
    FaultLog f; fault_log_init(f);
    f.magic = 0xDEADBEEFu; CHECK_FALSE(fault_log_valid(f));
    fault_log_init(f); f.version = 2; CHECK_FALSE(fault_log_valid(f));   // a v2 record (pre-LR) is rejected -> one-time wipe
    CHECK(kFaultVersion == 3);
}

TEST_CASE("fault_log: ring push drops the oldest at kFaultRingN; newest-first order; boot_seq high-water") {
    FaultLog f; fault_log_init(f);
    const uint16_t N = kFaultRingN;
    for (uint32_t s = 1; s <= static_cast<uint32_t>(N) + 5; ++s) fault_log_push(f, rec(s, kCauseWatchdog));
    CHECK(f.count == N);                                      // capped
    CHECK(f.boot_seq == static_cast<uint32_t>(N) + 5);        // high-water = the newest
    CHECK(fault_log_at(f, 0)->boot_seq == static_cast<uint32_t>(N) + 5);          // newest
    CHECK(fault_log_at(f, static_cast<uint16_t>(N - 1))->boot_seq == 6);          // oldest KEPT (1..5 dropped)
    CHECK(fault_log_at(f, N) == nullptr);                    // past the end
}

TEST_CASE("fault_log: classify_cause — scratch-derived, PRIORITY order (the v2 keystone)") {
    CHECK(classify_cause(/*magic_valid*/ false, false, false, false) == kCausePowerCycle);   // RAM lost -> power-cycle
    CHECK(classify_cause(true, /*had_fault*/ true,  true,  true)  == kCauseHardfault);        // fault wins over wdt/expected
    CHECK(classify_cause(true, false, /*wdt*/ true,  true)  == kCauseWatchdog);               // wdt wins over expected
    CHECK(classify_cause(true, false, false, /*expected*/ true)  == kCauseReboot);            // deliberate reset
    CHECK(classify_cause(true, false, false, false) == kCauseUnexpected);                     // scratch valid, none set -> pin/brownout/spontaneous
    CHECK(std::string(fault_cause_str(kCausePowerCycle)) == "POWER_CYCLE");
    CHECK(std::string(fault_cause_str(kCauseWatchdog))   == "WATCHDOG");
    CHECK(std::string(fault_cause_str(kCauseUnexpected)) == "UNEXPECTED");
}

TEST_CASE("fault_log: reset_reason_str decodes the RESETREAS HINT (POR / single / combined / unmapped)") {
    CHECK(rstr(0) == "POR");
    CHECK(rstr(kResetDog) == "DOG");
    CHECK(rstr(kResetPin | kResetDog) == "PIN+DOG");          // table order: PIN before DOG
    CHECK(rstr(kResetBrownout) == "BROWNOUT");
    CHECK(rstr(kResetPanic) == "PANIC");
    CHECK(rstr(1u << 12) == "?");                            // an unmapped bit -> not silently empty
}

TEST_CASE("fault_log: format_fault_record — boot/CAUSE/ran, hardfault frame + RESETREAS hint only when present") {
    char b[160];
    format_fault_record(rec(7, kCauseWatchdog, 0, 65000), b, sizeof b);   // 65 s = 1m05s
    std::string s(b);
    CHECK(s.find("boot 7") != std::string::npos);
    CHECK(s.find("WATCHDOG") != std::string::npos);
    CHECK(s.find("ran 1m05s") != std::string::npos);
    CHECK(s.find("pc=") == std::string::npos);                // no fault -> no frame
    CHECK(s.find("lr=") == std::string::npos);                // ... and no LR on a clean record
    CHECK(s.find("hint:") == std::string::npos);              // reason_bits 0 -> no hint

    format_fault_record(rec(9, kCauseHardfault, 1, 0, 0x12340, 0x8200, 0xBEEF, /*reason*/ kResetSreq, /*lr*/0x9abc), b, sizeof b);
    std::string s2(b);
    CHECK(s2.find("HARDFAULT") != std::string::npos);
    CHECK(s2.find("pc=0x12340") != std::string::npos);
    CHECK(s2.find("lr=0x9abc") != std::string::npos);         // v3: the caller addr (addr2line)
    CHECK(s2.find("cfsr=0x8200") != std::string::npos);
    CHECK(s2.find("hint:SREQ") != std::string::npos);         // the secondary RESETREAS hint

    format_fault_record(rec(1, kCausePowerCycle, 0, 0), b, sizeof b);     // RAM lost -> em-dash, not "ran 0s"
    std::string s3(b);
    CHECK(s3.find("POWER_CYCLE") != std::string::npos);
    CHECK(s3.find("ran 0s") == std::string::npos);
}

TEST_CASE("fault_log: format_fault_record — a CANARY record shows @w<where> off + the before->after dword") {
    char b[160];
    // canary record packs: fault_pc=before, fault_lr=after, cfsr=where, fault_addr=off (see device_fault.h)
    format_fault_record(rec(20, kCauseCanary, /*had_fault*/0, /*ran*/1000, /*pc=before*/0x20001a40u,
                            /*cfsr=where*/3u, /*addr=off*/8u, /*reason*/0, /*lr=after*/0u), b, sizeof b);
    std::string s(b);
    CHECK(s.find("CANARY") != std::string::npos);
    CHECK(s.find("@w3") != std::string::npos);            // where id 3 (= after_node_tick in fw_main)
    CHECK(s.find("off=8") != std::string::npos);
    CHECK(s.find("0x20001a40") != std::string::npos);     // before
    CHECK(s.find("ran 1s") != std::string::npos);
    CHECK(s.find("pc=") == std::string::npos);            // NOT a hardfault frame
    CHECK(std::string(fault_cause_str(kCauseCanary)) == "CANARY");

    // ADDENDUM: where>=100 = a per-timer-id trip -> "timer id=<where-100>" (here where=157 -> timer 57)
    format_fault_record(rec(21, kCauseCanary, 0, 1000, /*before*/0x7ccb8u, /*cfsr=where*/157u, /*off*/64u, 0, /*after*/0u), b, sizeof b);
    std::string st(b);
    CHECK(st.find("CANARY") != std::string::npos);
    CHECK(st.find("timer id=57") != std::string::npos);
    CHECK(st.find("off=64") != std::string::npos);
    CHECK(st.find("@w") == std::string::npos);            // the timer form, not the @w<where> form

    // ADDENDUM 2: a WATCHPOINT record (the DWT named the store) -> pc/lr (addr2line the pc = the offending line)
    format_fault_record(rec(22, kCauseWatchpoint, 0, 5000, /*pc*/0x12abcu, 0, 0, 0, /*lr*/0x34defu), b, sizeof b);
    std::string sw(b);
    CHECK(sw.find("WATCHPOINT") != std::string::npos);
    CHECK(sw.find("pc=0x12abc") != std::string::npos);
    CHECK(sw.find("lr=0x34def") != std::string::npos);
    CHECK(std::string(fault_cause_str(kCauseWatchpoint)) == "WATCHPOINT");
}

TEST_CASE("fault_log: format_fault_summary counts hardfaults / watchdogs FROM the cause") {
    FaultLog f; fault_log_init(f);
    fault_log_push(f, rec(1, kCauseWatchdog));
    fault_log_push(f, rec(2, kCauseHardfault, 1, 1000, 0x1, 0x2));
    fault_log_push(f, rec(3, kCauseWatchdog));
    char b[128]; format_fault_summary(f, b, sizeof b);
    std::string s(b);
    CHECK(s.find("3 records") != std::string::npos);
    CHECK(s.find("1 hardfault") != std::string::npos);
    CHECK(s.find("2 watchdog") != std::string::npos);
}

TEST_CASE("fault_log: format_last_reset + format_version_banner") {
    char b[160];
    format_last_reset(nullptr, b, sizeof b);
    CHECK(std::string(b).find("unknown") != std::string::npos);   // no log yet

    FaultRecord r = rec(4, kCauseWatchdog, 0, 3661000);           // 1h01m01s
    format_last_reset(&r, b, sizeof b);
    std::string s(b);
    CHECK(s.find("last reset: WATCHDOG") != std::string::npos);
    CHECK(s.find("ran 1h01m01s") != std::string::npos);

    FaultRecord rp = rec(6, kCausePowerCycle, 0, 0);              // power-cycle -> no "ran"
    format_last_reset(&rp, b, sizeof b);
    CHECK(std::string(b).find("last reset: POWER_CYCLE") != std::string::npos);
    CHECK(std::string(b).find("ran ") == std::string::npos);

    FaultRecord rf = rec(5, kCauseHardfault, 1, 2000, 0xCAFE, 0x8000, 0, 0, /*lr*/0xd00d);
    format_last_reset(&rf, b, sizeof b);
    CHECK(std::string(b).find("HARDFAULT pc=0xcafe lr=0xd00d") != std::string::npos);

    format_version_banner(b, sizeof b, "Jun 24 2026 10:00:00", "abc123-dirty", "xiao_sx1262");
    std::string v(b);
    CHECK(v.find("MeshRoute fw v0.1") != std::string::npos);
    CHECK(v.find("built Jun 24 2026 10:00:00") != std::string::npos);
    CHECK(v.find("abc123-dirty") != std::string::npos);
    CHECK(v.find("board=xiao_sx1262") != std::string::npos);
}
