// MeshRoute — test_fault_log.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Native tests for the platform-neutral fault-log core (lib/core/fault_log.{h,cpp}): the FaultLog POD round-trip
// (whole-blob R/W, mrnv style), the ring push + drop-oldest at kFaultRingN, the RESETREAS-bit -> reason decode,
// and the `version`/`faults` text formatters. The WDT / HardFault / retained-scratch are HW-only (bench).
// NB: no DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN (test_airtime.cpp provides main()); -fno-exceptions => CHECK only.
#include "doctest.h"
#include "fault_log.h"

#include <cstring>
#include <string>

using namespace mrfault;

namespace {
FaultRecord rec(uint32_t seq, uint16_t reason, uint8_t had_fault = 0, uint32_t ran_ms = 0,
                uint32_t pc = 0, uint32_t cfsr = 0, uint32_t addr = 0) {
    FaultRecord r{}; r.boot_seq = seq; r.reason_bits = reason; r.had_fault = had_fault;
    r.ran_ms = ran_ms; r.fault_pc = pc; r.cfsr = cfsr; r.fault_addr = addr; return r;
}
std::string rstr(uint16_t bits) { char b[48]; reset_reason_str(bits, b, sizeof b); return std::string(b); }
}  // namespace

TEST_CASE("fault_log: FaultLog is a POD that round-trips a whole-blob memcpy (the /mrfault store contract)") {
    FaultLog f; fault_log_init(f);
    CHECK(fault_log_valid(f));
    fault_log_push(f, rec(1, kResetDog, 0, 5000));
    fault_log_push(f, rec(2, kResetSreq, 1, 12000, 0xABCD, 0x8200, 0xDEAD));
    uint8_t bytes[sizeof(FaultLog)];
    std::memcpy(bytes, &f, sizeof f);                         // "serialize" = the verbatim POD copy (== mrnv::save)
    FaultLog g{};
    std::memcpy(&g, bytes, sizeof g);                         // "deserialize"
    CHECK(std::memcmp(&f, &g, sizeof f) == 0);                // byte-identical
    CHECK(fault_log_valid(g));
    CHECK(g.count == 2); CHECK(g.boot_seq == 2);
    CHECK(fault_log_at(g, 0)->boot_seq == 2);                 // newest first
    CHECK(fault_log_at(g, 0)->had_fault == 1);
    CHECK(fault_log_at(g, 0)->fault_pc == 0xABCD);
    CHECK(fault_log_at(g, 1)->boot_seq == 1);
}

TEST_CASE("fault_log: a bad magic/version is INVALID -> the loader re-inits fresh") {
    FaultLog f; fault_log_init(f);
    f.magic = 0xDEADBEEFu; CHECK_FALSE(fault_log_valid(f));
    fault_log_init(f); f.version = 0xFF; CHECK_FALSE(fault_log_valid(f));
}

TEST_CASE("fault_log: ring push drops the oldest at kFaultRingN; newest-first order; boot_seq high-water") {
    FaultLog f; fault_log_init(f);
    const uint16_t N = kFaultRingN;
    for (uint32_t s = 1; s <= static_cast<uint32_t>(N) + 5; ++s) fault_log_push(f, rec(s, kResetDog));
    CHECK(f.count == N);                                      // capped
    CHECK(f.boot_seq == static_cast<uint32_t>(N) + 5);        // high-water = the newest
    CHECK(fault_log_at(f, 0)->boot_seq == static_cast<uint32_t>(N) + 5);          // newest
    CHECK(fault_log_at(f, static_cast<uint16_t>(N - 1))->boot_seq == 6);          // oldest KEPT (1..5 dropped)
    CHECK(fault_log_at(f, N) == nullptr);                    // past the end
}

TEST_CASE("fault_log: reset_reason_str decodes POR(0) / single / combined / unmapped") {
    CHECK(rstr(0) == "POR");                                  // no bits = power-on / brownout
    CHECK(rstr(kResetDog) == "DOG");
    CHECK(rstr(kResetSreq) == "SREQ");
    CHECK(rstr(kResetPin | kResetDog) == "PIN+DOG");          // table order: PIN before DOG
    CHECK(rstr(kResetLockup) == "LOCKUP");
    CHECK(rstr(kResetOff) == "OFF");
    CHECK(rstr(kResetBrownout) == "BROWNOUT");               // ESP32 reasons
    CHECK(rstr(kResetPanic) == "PANIC");
    CHECK(rstr(1u << 12) == "?");                            // an unmapped bit -> not silently empty
}

TEST_CASE("fault_log: format_fault_record — boot/reason/ran, hardfault frame only when present") {
    char b[160];
    format_fault_record(rec(7, kResetDog, 0, 65000), b, sizeof b);   // 65 s = 1m05s
    std::string s(b);
    CHECK(s.find("boot 7") != std::string::npos);
    CHECK(s.find("DOG") != std::string::npos);
    CHECK(s.find("ran 1m05s") != std::string::npos);
    CHECK(s.find("pc=") == std::string::npos);                // no fault -> no frame

    format_fault_record(rec(9, kResetSreq, 1, 0, 0x12340, 0x8200, 0xBEEF), b, sizeof b);
    std::string s2(b);
    CHECK(s2.find("boot 9") != std::string::npos);
    CHECK(s2.find("pc=0x12340") != std::string::npos);
    CHECK(s2.find("cfsr=0x8200") != std::string::npos);

    format_fault_record(rec(1, 0, 0, 0), b, sizeof b);        // POR + unknown uptime -> em-dash, not "ran 0s"
    CHECK(std::string(b).find("ran 0s") == std::string::npos);
}

TEST_CASE("fault_log: format_fault_summary counts records / hardfaults / watchdog resets") {
    FaultLog f; fault_log_init(f);
    fault_log_push(f, rec(1, kResetDog));                     // watchdog
    fault_log_push(f, rec(2, kResetSreq, 1, 1000, 0x1, 0x2)); // hardfault (sreq from the capture reset)
    fault_log_push(f, rec(3, kResetDog));                     // watchdog
    char b[128]; format_fault_summary(f, b, sizeof b);
    std::string s(b);
    CHECK(s.find("3 records") != std::string::npos);
    CHECK(s.find("1 hardfault") != std::string::npos);
    CHECK(s.find("2 watchdog resets") != std::string::npos);
}

TEST_CASE("fault_log: format_last_reset + format_version_banner") {
    char b[160];
    format_last_reset(nullptr, b, sizeof b);
    CHECK(std::string(b).find("unknown") != std::string::npos);   // no log yet

    FaultRecord r = rec(4, kResetDog, 0, 3661000);                // 1h01m01s
    format_last_reset(&r, b, sizeof b);
    std::string s(b);
    CHECK(s.find("last reset: DOG") != std::string::npos);
    CHECK(s.find("ran 1h01m01s") != std::string::npos);

    FaultRecord rf = rec(5, kResetSreq, 1, 2000, 0xCAFE, 0x8000);
    format_last_reset(&rf, b, sizeof b);
    CHECK(std::string(b).find("HARDFAULT pc=0xcafe") != std::string::npos);

    format_version_banner(b, sizeof b, "Jun 24 2026 10:00:00", "abc123-dirty", "xiao_sx1262");
    std::string v(b);
    CHECK(v.find("MeshRoute fw v0.1") != std::string::npos);
    CHECK(v.find("built Jun 24 2026 10:00:00") != std::string::npos);
    CHECK(v.find("abc123-dirty") != std::string::npos);
    CHECK(v.find("board=xiao_sx1262") != std::string::npos);
}
