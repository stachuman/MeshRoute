// §B95 PROBE — compiles the REAL src/console_sink.h against a transport model and measures the line-admission
// behaviour that no native test and no simulator can reach (`src/` is outside the native build; the simulator
// compiles lib/core only). Every check prints its own denominator.
//
// The three sinks below are compared on the SAME generator and the SAME transport schedule:
//   • mrcon             — the REAL sink under test (whatever console_sink.h currently says).
//   • LegacyConsole     — the PRE-FIX code, copied verbatim. Its job is to make the defect appear, so a green probe
//                         against a fixed sink means something (a positive control).
//   • StrictAtomicSink  — the coder brief's LITERAL §5.1 shape (stage one line, submit in ONE call or drop it whole).
//                         Its job is to MEASURE what that rule costs on a 128-byte FIFO.
#include <cstdio>
#include <string>
#include <vector>
#include <Arduino.h>
#include "console_sink.h"
#include "firmware_help.h"   // §0a/[[B208]]: the REAL help authority, compiled and RUN here

FakeSerial Serial;

#ifndef PROBE_SINK_MD5
#define PROBE_SINK_MD5 "(not injected)"
#endif
#ifndef PROBE_HELP_MD5
#define PROBE_HELP_MD5 "(not injected)"
#endif
#ifndef PROBE_PROFILE
#define PROBE_PROFILE "(not injected)"
#endif

// ---- tiny harness ------------------------------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
#define CHK(label, expr) do {                                                              \
    const bool ok_ = (expr);                                                               \
    if (ok_) ++g_pass; else { ++g_fail; printf("  FAIL %-58s  %s\n", (label), #expr); }    \
} while (0)

static void chk_s(const std::string& label, bool ok) {   // CHK with a COMPUTED label (one row per topic)
    if (ok) ++g_pass; else { ++g_fail; printf("  FAIL %s\n", label.c_str()); }
}

// ---- the two comparison sinks ------------------------------------------------------------------------------------
// VERBATIM pre-fix GuardedConsole write pair (git 8228b11 src/console_sink.h:41-49). Do not "improve" it — its whole
// value is being the thing that produced the bench evidence.
class LegacyConsole : public Print {
public:
    size_t write(uint8_t b) override {
        if (!Serial || Serial.availableForWrite() < 1) return 0;
        return Serial.write(b);
    }
    size_t write(const uint8_t* buf, size_t n) override {
        if (!Serial || static_cast<size_t>(Serial.availableForWrite()) < n) return n;
        return Serial.write(buf, n);
    }
};

// The brief's §5.1 shape: one line staged, then ONE guarded whole-chunk write, else the line is dropped and counted.
class StrictAtomicSink : public Print {
public:
    size_t write(uint8_t b) override {
        if (_len < sizeof _buf) _buf[_len++] = static_cast<char>(b); else _over = true;
        if (b == '\n') submit();
        return 1;
    }
    size_t write(const uint8_t* p, size_t n) override { for (size_t i = 0; i < n; ++i) write(p[i]); return n; }
    uint32_t dropped = 0;
private:
    void submit() {
        if (_over) { _len = 0; _over = false; ++dropped; return; }
        if (!Serial || static_cast<size_t>(Serial.availableForWrite()) < _len) { _len = 0; ++dropped; return; }
        Serial.write(reinterpret_cast<const uint8_t*>(_buf), _len);
        _len = 0;
    }
    char   _buf[2048];
    size_t _len = 0;
    bool   _over = false;
};

// A lossless sink — the ORACLE. The expected byte stream is produced by the same generator, so the probe can never
// compare against a hand-typed transcript that has drifted from the formatter.
class PerfectSink : public Print {
public:
    size_t write(uint8_t b) override { s.push_back(static_cast<char>(b)); return 1; }
    size_t write(const uint8_t* p, size_t n) override { s.append(reinterpret_cast<const char*>(p), n); return n; }
    std::string s;
};

// ---- the generator: dump_cfg's CALL GRANULARITY --------------------------------------------------------------------
// A stand-in for src/firmware_commands.cpp dump_cfg (that TU cannot be host-compiled — it needs g_node, NV, JSON and
// the Arduino world). What matters is reproduced exactly: one write per label, one per value, the terminator as its
// own call, and `leaf_name` ONE CHARACTER PER CALL — the granularity that produced the H5-06 capture. Field text and
// order are copied from the real dump_cfg so the row lengths are the real ones.
static void replay_cfg(Print& out) {
    out.print(F("node_id="));           out.println(106);
    out.print(F("  radio : freq="));    out.print(869.0, 4);
    out.print(F(" routing_sf="));       out.print(7);
    out.print(F(" sf_list="));          out.print(6); out.print(','); out.print(7);
    out.print(F(" bw="));               out.print(125.0, 2); out.print(F(" kHz"));
    out.print(F(" cr="));               out.print(5);
    out.print(F(" tx_power="));         out.println(22);
    out.print(F("  proto : duty="));    out.print(1.0, 2); out.print('%');
    out.print(F(" beacon_ms="));        out.print(900000u);
    out.print(F(" hop_cap="));          out.print(16);
    out.print(F(" team_hop_cap="));     out.print(8);
    out.print(F(" lbt="));              out.print(1);
    out.print(F(" nav="));              out.print(1);
    out.print(F(" intra_relay="));      out.print(0);
    out.print(F(" host_mobiles="));     out.print(1);
    out.print(F(" nav_ignore="));       out.println(0);
    out.print(F("  aspam : active_fraction=")); out.print(0.5, 3);
    out.print(F(" ch_min_ms="));        out.print(5000u);
    out.print(F(" dm_min_ms="));        out.println(1000u);
    out.print(F("  layer : "));         out.print(F("layer=")); out.print(5); out.print(F(" "));
    out.print(F("leaf="));              out.print(5);
    out.print(F(" gateway="));          out.print(0);
    out.print(F(" gateway_only="));     out.print(0);
    out.print(F(" mobile="));           out.println(0);
    out.print(F("  member: lineage_id=")); out.print(1);
    out.print(F(" config_epoch="));     out.print(3);
    out.print(F(" leaf_name=\""));
    const char* nm = "Layer5";                                  // the real loop: `out.print(c.leaf_name[i])` per char
    for (const char* p = nm; *p; ++p) out.print(*p);
    out.print(F("\""));                 out.println();
    out.print(F("  ble   : ble_mode=")); out.print(F("off"));
    out.print(F(" ble_period="));       out.print(0);
    out.print(F(" ble_pin="));          out.println(123456u);
    out.print(F("  loc   : e2e_dm="));  out.print(0);
    out.print(F(" team_channel_crypt=")); out.print(0);
    out.print(F(" intro_attach="));     out.print(1);
    out.print(F(" lat="));              out.print(52.2296756, 7);
    out.print(F(" lon="));              out.println(21.0122287, 7);
}

// ---- line helpers -------------------------------------------------------------------------------------------------
static std::vector<std::string> split_lines(const std::string& s) {   // on "\r\n"; a trailing partial is kept as-is
    std::vector<std::string> v; size_t i = 0;
    while (i < s.size()) {
        const size_t j = s.find("\r\n", i);
        if (j == std::string::npos) { v.push_back(s.substr(i)); break; }
        v.push_back(s.substr(i, j - i)); i = j + 2;
    }
    return v;
}
// Every received line must be BYTE-IDENTICAL to one of the expected lines: this is what rejects a fused row, a row
// with a missing label, a stray value fragment and an unterminated row, all at once.
static size_t count_corrupt(const std::string& wire, const std::vector<std::string>& expect) {
    size_t bad = 0;
    for (const auto& got : split_lines(wire)) {
        bool ok = false;
        for (const auto& e : expect) if (got == e) { ok = true; break; }
        if (!ok) ++bad;
    }
    return bad;
}
// ...and they must arrive in the generator's order, without duplication: a SUBSEQUENCE of the expected lines.
static bool is_subsequence(const std::string& wire, const std::vector<std::string>& expect) {
    size_t k = 0;
    for (const auto& got : split_lines(wire)) {
        while (k < expect.size() && expect[k] != got) ++k;
        if (k == expect.size()) return false;
        ++k;
    }
    return true;
}
// ★ THE FORMAL STATEMENT OF THE B95 DEFECT: a received line whose characters appear IN ORDER inside a real line but
// with bytes missing — a row that survived only as a GAPPED subsequence of itself (labels/spaces/terminator dropped,
// later values kept). Derived from the oracle, never from a hand-typed transcript.
static bool proper_subsequence(const std::string& got, const std::string& of) {
    if (got.empty() || got == of || got.size() >= of.size()) return false;
    size_t k = 0;
    for (char c : got) {
        while (k < of.size() && of[k] != c) ++k;
        if (k == of.size()) return false;
        ++k;
    }
    return true;
}
static bool any_gapped_row(const std::string& wire, const std::vector<std::string>& expect) {
    for (const auto& got : split_lines(wire))
        for (const auto& e : expect) if (proper_subsequence(got, e)) return true;
    return false;
}
static size_t count_exact(const std::string& wire, const std::vector<std::string>& expect) {
    size_t n = 0;
    for (const auto& got : split_lines(wire))
        for (const auto& e : expect) if (got == e) { ++n; break; }
    return n;
}
// Run the loop for `passes` passes: service the sink, then let the host read `per_pass` bytes (11 B/ms ~ 115200 baud).
static void run_loop(int passes, size_t per_pass) {
    for (int i = 0; i < passes; ++i) { mrcon.service(); Serial.drain(per_pass); }
}

// ---- §0a/[[B208]] help helpers -------------------------------------------------------------------------------------
// ★ THE OWNER'S NINE TOPICS, IN THE OWNER'S ORDER, WITH THE AVAILABILITY RULE RE-DERIVED FROM THE PRODUCT MACROS.
//   ⛔ Deliberately NOT `MR_HELP_HAS_MOBILE` / `MR_HELP_HAS_REMOTE`: reading firmware_help.h's own gate macros would
//   make every availability row circular — flip the macro and both sides move together. These expressions restate
//   B208's rule ("listed/accepted only when compiled") against the SAME macros the command handlers are gated on:
//   the `mobile …` arm is `MR_N_LAYERS < 2 && MR_FEAT_MOBILE`, `password`/`unlock`/`lock` are `MR_FEAT_REMOTE_MGMT`.
struct HelpTopic { const char* name; bool available; };
static const HelpTopic kHelpTopics[] = {
    { "messaging",    true },
    { "identity",     true },
    { "mobile",       (MR_N_LAYERS < 2) && (MR_FEAT_MOBILE != 0) },
    { "inbox",        true },
    { "diagnostics",  true },
    { "remote",       MR_FEAT_REMOTE_MGMT != 0 },
    { "test",         true },
    { "provisioning", true },
    { "cfg",          true },
};

// Render one console line through the REAL router into a lossless sink. -> did the router OWN the line?
static bool help_render(const char* cmd, std::string& text) {
    PerfectSink p;
    const bool owned = mrfw::help_command(cmd, strlen(cmd), p);
    text = p.s;
    return owned;
}
static bool ends_crlf(const std::string& s) { return s.size() >= 2 && s.compare(s.size() - 2, 2, "\r\n") == 0; }
static std::string first_word(const std::string& line) {
    size_t a = line.find_first_not_of(' ');
    if (a == std::string::npos) return std::string();
    const size_t b = line.find(' ', a);
    return line.substr(a, b == std::string::npos ? std::string::npos : b - a);
}
// The refusal's second line — the one carrying this build's valid topic names.
static std::string valid_topics_line(const std::string& refusal) {
    for (const auto& l : split_lines(refusal)) if (l.find("valid topics") != std::string::npos) return l;
    return std::string();
}
// Whole-word membership: `mobile` must not be answered by the substring inside `mobile_autoregister`.
static bool has_word(const std::string& hay, const std::string& w) {
    for (size_t i = hay.find(w); i != std::string::npos; i = hay.find(w, i + 1)) {
        const bool lok = (i == 0) || hay[i - 1] == ' ';
        const size_t e = i + w.size();
        const bool rok = (e == hay.size()) || hay[e] == ' ';
        if (lok && rok) return true;
    }
    return false;
}

int main() {
    printf("== §B95 console-sink probe ==  sink md5 = %s  stage = %d B\n",
           PROBE_SINK_MD5, (int)MR_CONSOLE_STAGE_BYTES);

    PerfectSink oracle; replay_cfg(oracle);
    const std::vector<std::string> want = split_lines(oracle.s);
    printf("   generator: %d bytes / %d lines, longest %d B (ESP32 UART0 FIFO = 128 B)\n",
           (int)oracle.s.size(), (int)want.size(),
           (int)[&]{ size_t m = 0; for (auto& l : want) m = l.size() > m ? l.size() : m; return m; }());

    // ============================================================================================ P1 / brief test 1
    // POSITIVE CONTROL: the LEGACY sink, on a transport that frees capacity between fragments, reproduces the H5-06
    // defect. If this ever goes green, the probe has stopped being able to see the bug and every other row is void.
    { Serial.reset(128); Serial.auto_drain = 1;
      LegacyConsole legacy; replay_cfg(legacy);
      const size_t bad = count_corrupt(Serial.wire, want);
      CHK("P1a legacy sink CORRUPTS the response (>=1 bad line)", bad >= 1);
      CHK("P1b legacy sink does not deliver it intact",            Serial.wire != oracle.s);
      // The H5-06 signature, stated formally: a row survives only as a GAPPED subsequence of the real row.
      CHK("P1c legacy: a row survives only as a GAPPED subsequence", any_gapped_row(Serial.wire, want));
      printf("   P1 legacy: %d/%d bad lines, %d of %d B delivered\n",
             (int)bad, (int)split_lines(Serial.wire).size(), (int)Serial.wire.size(), (int)oracle.s.size()); }

    // ============================================================================================ P2 / brief test 2
    // THE FIXED SINK, same generator, same 128-B transport: the complete response arrives BYTE-EXACT, in order.
    { Serial.reset(128); Serial.auto_drain = 0;
      replay_cfg(mrcon);
      run_loop(200, 11);
      CHK("P2a fixed sink delivers the response BYTE-EXACT",  Serial.wire == oracle.s);
      CHK("P2b every received line is intact",                count_corrupt(Serial.wire, want) == 0);
      CHK("P2c lines arrive in order, no duplication",        is_subsequence(Serial.wire, want));
      CHK("P2c2 NOT ONE gapped row (the P1c defect is absent)", !any_gapped_row(Serial.wire, want));
      CHK("P2d nothing was dropped",                          mrcon.dropped_lines() == 0);
      CHK("P2e Serial was NEVER handed more than it can take", Serial.overrun_writes == 0);
      CHK("P2f no blocking flush was used",                    Serial.flushes == 0);
      printf("   P2 fixed: %d B in %ld Serial writes, %ld operator-bool calls\n",
             (int)Serial.wire.size(), Serial.writes, Serial.bool_calls); }

    // ============================================================================================ P3 THE MEASUREMENT
    // ★ The brief's LITERAL rule (one write call per line, else drop) on the SAME schedule. A 128-B FIFO cannot admit
    // a 118-B row once anything precedes it, so most of the response becomes permanently undeliverable — the finding
    // that forced the in-order drain. Asserted as an INEQUALITY so this row can never silently become vacuous.
    { Serial.reset(128); Serial.auto_drain = 0;
      StrictAtomicSink strict; replay_cfg(strict);
      const size_t got = count_exact(Serial.wire, want);
      CHK("P3a strict-atomic corrupts nothing (it drops)", count_corrupt(Serial.wire, want) == 0);
      CHK("P3b strict-atomic delivers FEWER lines than the fix", got < want.size());
      CHK("P3c ... and the loss is counted, not silent",   strict.dropped == want.size() - got);
      printf("   P3 strict-atomic: %d of %d lines delivered, %d dropped  <-- the measured cost of \"one write per line\"\n",
             (int)got, (int)want.size(), (int)strict.dropped); }

    // ============================================================================================ P4 / brief test 2b
    // A FULL FIFO YIELDS ZERO BYTES OF THE LINE — never a prefix, never a fragment.
    { Serial.reset(128); Serial.auto_drain = 0;
      Serial.occupied = 128;                                  // the host has stopped reading; the FIFO is full
      replay_cfg(mrcon);
      CHK("P4a a full FIFO admits ZERO bytes",        Serial.wire.empty());
      CHK("P4b ... and Serial.write was not called",  Serial.writes == 0);
      run_loop(200, 11);                                       // the host resumes
      CHK("P4c after capacity returns, the whole response is exact", Serial.wire == oracle.s);
      CHK("P4d ... with nothing dropped",             mrcon.dropped_lines() == 0); }

    // ============================================================================================ P5 / brief test 3
    // A DROPPED / ABSENT NEWLINE CANNOT FUSE TWO RESPONSES. Response A ends without a terminator (the `hl()` CRLF
    // bug, and any formatter that forgets a println); the command boundary must close it.
    { Serial.reset(128); Serial.auto_drain = 0;
      mrcon.print(F("> response A with NO terminator"));       // no println — deliberately
      run_loop(1, 200);                                        // the loop pass = the command boundary
      mrcon.println(F("> response B"));
      run_loop(200, 200);
      CHK("P5a A was terminated before B started",
          Serial.wire.find("> response A with NO terminator\r\n> response B\r\n") == 0);
      CHK("P5b the two never share a physical line",
          Serial.wire.find("terminator> response B") == std::string::npos); }

    // ============================================================================================ P6 / brief test 4
    // ONE DROPPED LINE + RESTORED CAPACITY -> the next line is exact and `!! CONSOLE_DROP lines=1` appears ONCE.
    { Serial.reset(128); Serial.auto_drain = 0;
      std::string huge(MR_CONSOLE_STAGE_BYTES + 64, 'X');      // cannot fit the stage at all
      mrcon.println(huge.c_str());
      CHK("P6a the oversized line put NOTHING on the wire", Serial.wire.find('X') == std::string::npos);
      CHK("P6b ... and was counted",                        mrcon.dropped_lines() == 1);
      mrcon.println(F("> after the drop"));
      run_loop(200, 200);
      CHK("P6c the next line is exact",
          Serial.wire.find("> after the drop\r\n") != std::string::npos);
      const std::string rep = "!! CONSOLE_DROP lines=1\r\n";
      const size_t at = Serial.wire.find(rep);
      CHK("P6d the deferred drop report appeared",           at != std::string::npos);
      CHK("P6e ... exactly once",                           at != std::string::npos &&
                                                            Serial.wire.find(rep, at + 1) == std::string::npos);
      CHK("P6f ... and the counter was cleared, not re-counted", mrcon.dropped_lines() == 0);
      run_loop(50, 200);
      CHK("P6g no second report on later passes",
          Serial.wire.find(rep, at + 1) == std::string::npos); }

    // ============================================================================================ P7 / brief test 5
    // A DISCONNECTED HOST NEVER WAITS and never accumulates: no writes, no flushes, everything counted.
    { Serial.reset(128, /*connected=*/false);
      for (int i = 0; i < 50; ++i) mrcon.println(F("> to nobody"));
      run_loop(10, 0);
      CHK("P7a no host -> zero Serial writes",   Serial.writes == 0);
      CHK("P7b no host -> zero blocking flushes", Serial.flushes == 0);
      CHK("P7c no host -> the 50 lines are counted", mrcon.dropped_lines() == 50);
      // ...and when the host returns, output resumes correctly (the counter reports the loss).
      Serial.connected = true;
      mrcon.println(F("> host is back"));
      run_loop(200, 200);
      CHK("P7d after reconnect the line is exact",
          Serial.wire.find("> host is back\r\n") != std::string::npos);
      CHK("P7e ... and the 50 lost lines are reported once",
          Serial.wire.find("!! CONSOLE_DROP lines=50\r\n") != std::string::npos); }

    // ============================================================================================ P8 / brief test 6
    // STAGING OVERFLOW DROPS THE WHOLE LINE — NO PREFIX LEAKS — and the neighbours are unaffected.
    { Serial.reset(128); Serial.auto_drain = 0;
      mrcon.println(F("> before"));
      std::string huge(MR_CONSOLE_STAGE_BYTES * 2, 'Z');
      mrcon.println(huge.c_str());
      mrcon.println(F("> after"));
      run_loop(400, 200);
      CHK("P8a not one byte of the oversized line leaked", Serial.wire.find('Z') == std::string::npos);
      CHK("P8b the line before it is intact",  Serial.wire.find("> before\r\n") != std::string::npos);
      CHK("P8c the line after it is intact",   Serial.wire.find("> after\r\n")  != std::string::npos);
      CHK("P8d exactly one line was counted",  Serial.wire.find("!! CONSOLE_DROP lines=1\r\n") != std::string::npos);
      CHK("P8e no corrupt line on the wire",
          count_corrupt(Serial.wire, {"> before", "> after", "!! CONSOLE_DROP lines=1", ""}) == 0); }

    // ============================================================================================ P9
    // THE ANTI-WEDGE, MEASURED RATHER THAN ASSERTED: 400 lines into a host that reads NOTHING must cost zero
    // over-capacity writes, zero flushes, and must not stall — plus the pending queue is BOUNDED by the stage.
    { Serial.reset(128); Serial.auto_drain = 0; Serial.occupied = 128;
      for (int i = 0; i < 400; ++i) { mrcon.println(F("> flooding a dead host")); mrcon.service(); }
      CHK("P9a stalled host -> zero over-capacity writes", Serial.overrun_writes == 0);
      CHK("P9b stalled host -> zero flushes",              Serial.flushes == 0);
      CHK("P9c stalled host -> nothing on the wire",       Serial.wire.empty());
      CHK("P9d ... and the loss is counted",               mrcon.dropped_lines() > 300);
      printf("   P9 stalled host: %u lines counted, %ld writes\n",
             (unsigned)mrcon.dropped_lines(), Serial.writes);
      Serial.occupied = 0; run_loop(400, 200); }

    // ============================================================================================ P10
    // service() IS LOAD-BEARING: without the per-pass call, a response longer than the FIFO cannot finish. This is
    // the positive control for the one line added to fw_main's service_console.
    { Serial.reset(128); Serial.auto_drain = 0;
      replay_cfg(mrcon);
      for (int i = 0; i < 200; ++i) Serial.drain(11);          // loop passes WITHOUT service()
      CHK("P10a without service() the response cannot complete", Serial.wire != oracle.s);
      run_loop(200, 11);
      CHK("P10b with service() it completes exactly",             Serial.wire == oracle.s); }

    // ============================================================================================ P11
    // flush() is the ONE deliberately blocking entry point (reset/OTA path). It must actually get the text out, and
    // it must terminate an unterminated tail rather than leaving it to fuse with a post-reboot banner.
    { Serial.reset(128); Serial.auto_drain = 0; Serial.occupied = 128;
      mrcon.println(F("> rebooting"));
      mrcon.print(F("> tail with no newline"));
      mrcon.flush();
      CHK("P11a flush() delivered the queued line", Serial.wire.find("> rebooting\r\n") != std::string::npos);
      CHK("P11b flush() terminated the tail",       Serial.wire.find("> tail with no newline\r\n") != std::string::npos);
      CHK("P11c flush() used the blocking drain",   Serial.flushes > 0); }

    // ============================================================================================ P12
    // The nRF52 transport (256-B CDC FIFO) — the same guarantees on the other measured ceiling.
    { Serial.reset(256); Serial.auto_drain = 0;
      replay_cfg(mrcon);
      run_loop(200, 22);
      CHK("P12a nRF52 256-B FIFO: response byte-exact",     Serial.wire == oracle.s);
      CHK("P12b nRF52: no over-capacity write (write() yields)", Serial.overrun_writes == 0);
      CHK("P12c nRF52: nothing dropped",                    mrcon.dropped_lines() == 0); }

    // ============================================================================================ P13
    // THE HARD CASE: a drop is pending WHILE a response longer than the FIFO is half-drained. The deferred report may
    // not jump the queue — if it is written while a line is mid-flight it lands INSIDE that line, which is the very
    // defect being fixed, dressed up as a diagnostic.
    { Serial.reset(128); Serial.auto_drain = 0;
      std::string huge(MR_CONSOLE_STAGE_BYTES * 2, 'Z');
      mrcon.println(huge.c_str());                          // -> one drop pending
      replay_cfg(mrcon);                                    // -> a long queue, drained over many passes
      // ★ drain > FIFO capacity, DELIBERATELY: it empties the FIFO between passes, which is the only schedule under
      // which a report emitted BEFORE the queue drain could physically fit and therefore land mid-line. With a drain
      // smaller than the FIFO the transport hides the ordering bug (measured — control C5 stayed green at drain 7).
      run_loop(400, 200);
      const std::string rep = "!! CONSOLE_DROP lines=1\r\n";
      const size_t at = Serial.wire.find(rep);
      CHK("P13a no byte of the dropped line leaked",   Serial.wire.find('Z') == std::string::npos);
      CHK("P13b every line on the wire is intact",
          count_corrupt(Serial.wire, [&]{ auto v = want; v.push_back("!! CONSOLE_DROP lines=1"); v.push_back(""); return v; }()) == 0);
      CHK("P13c the report landed on a LINE BOUNDARY",  at != std::string::npos && (at == 0 || Serial.wire[at - 1] == '\n'));
      std::string without = Serial.wire;
      if (at != std::string::npos) without.erase(at, rep.size());
      CHK("P13d ... and the response itself is byte-exact", without == oracle.s); }

    // ============================================================ H — §0a/[[B208]] THE BOUNDED HELP-TOPIC SPLIT ===
    // WHY THESE ROWS EXIST AND WHY THEY LIVE IN *THIS* BINARY. B208's ruling is about BYTES ON A WIRE — a compact
    // index, one whole section per topic, every response inside MR_CONSOLE_STAGE_BYTES with no `CONSOLE_DROP`. The
    // help used to live in `src/firmware_commands.cpp`, a TU nothing can host-compile, so the only available proof
    // was grep. Slice 0a moved the text and the whole `help`/`?`/`help <topic>` recognition into
    // `src/firmware_help.h`, which depends on `Print` + three feature macros and nothing else — so the REAL renderer
    // is compiled here and driven through the REAL `GuardedConsole`, once per real product profile.
    // ⛔ NOTHING BELOW RESTATES HELP TEXT. The expectations are (a) the owner's nine topic names and their
    //    availability rule, re-derived from the PRODUCT macros (NOT from firmware_help.h's own MR_HELP_HAS_*, which
    //    would make the check circular), and (b) the frozen pre-slice content multiset, compared by the Python
    //    wrapper against the CONTENT block this binary prints.
    {
      Serial.reset(4096); Serial.auto_drain = 0; run_loop(400, 4096);   // quiesce the shared sink after P13
      CHK("H0 the shared sink is quiescent before the help rows", mrcon.dropped_lines() == 0);

      printf("\n== §0a help router ==  profile = %s  help md5 = %s\n", PROBE_PROFILE, PROBE_HELP_MD5);

      // ---- H1: the two BARE index spellings ----------------------------------------------------------------
      std::string idx_help, idx_q;
      const bool own_help = help_render("help", idx_help);
      const bool own_q    = help_render("?",    idx_q);
      CHK("H1a bare `help` is owned by the router",                   own_help);
      CHK("H1b bare `?` is owned by the router",                      own_q);
      CHK("H1c bare `help` and bare `?` are BYTE-IDENTICAL",          own_help && own_q && idx_help == idx_q);
      CHK("H1d the index is non-empty and whole-line terminated",     !idx_help.empty() && ends_crlf(idx_help));

      // ---- H2: the index lists exactly this build's topics, once each, in the OWNER's order ----------------
      std::vector<std::string> idx_rows;
      for (const auto& l : split_lines(idx_help))
          if (l.size() > 4 && l.compare(0, 4, "    ") == 0 && l[4] != ' ') idx_rows.push_back(first_word(l));
      std::vector<std::string> want_rows;
      for (const auto& t : kHelpTopics) if (t.available) want_rows.push_back(t.name);
      CHK("H2a the index names exactly the AVAILABLE topics, once each, in owner order", idx_rows == want_rows);
      bool idx_leak = false;
      for (const auto& t : kHelpTopics)
          if (!t.available && idx_help.find(t.name) != std::string::npos) idx_leak = true;
      CHK("H2b no UNAVAILABLE topic name appears anywhere in the index", !idx_leak);

      // ---- H3/H4: every topic is accepted iff it is available; a section opens with its own heading --------
      std::string refusal;
      CHK("H3a an unknown topic is owned and answered", help_render("help zzz", refusal) && !refusal.empty());
      size_t largest = idx_help.size(); std::string largest_name = "(index)";
      std::vector<std::string> content;                    // the union, for the frozen-baseline comparison
      for (const auto& t : kHelpTopics) {
          const std::string cmd = std::string("help ") + t.name;
          std::string text;
          const bool owned = help_render(cmd.c_str(), text);
          chk_s(std::string("H3b `") + cmd + "` is owned by the router", owned);
          if (!t.available) {
              chk_s(std::string("H4a unavailable `") + cmd + "` takes the SAME bounded refusal arm", text == refusal);
              continue;
          }
          const std::vector<std::string> ls = split_lines(text);
          chk_s(std::string("H3c `") + cmd + "` opens with its canonical heading",
                !ls.empty() && !ls[0].empty() && ls[0][0] != ' ');
          chk_s(std::string("H3d `") + cmd + "` is whole-line terminated", ends_crlf(text));
          chk_s(std::string("H3e `") + cmd + "` fits MR_CONSOLE_STAGE_BYTES incl. line endings",
                text.size() < (size_t)MR_CONSOLE_STAGE_BYTES);
          if (text.size() > largest) { largest = text.size(); largest_name = cmd; }
          for (const auto& l : ls) if (!l.empty() && l[0] == ' ') content.push_back(l);
          printf("   topic %-13s %4d B / %2d lines   headroom %4d B\n", t.name, (int)text.size(), (int)ls.size(),
                 (int)((size_t)MR_CONSOLE_STAGE_BYTES - text.size()));
      }
      printf("   index         %4d B / %2d lines   headroom %4d B\n", (int)idx_help.size(),
             (int)split_lines(idx_help).size(), (int)((size_t)MR_CONSOLE_STAGE_BYTES - idx_help.size()));
      printf("   refusal       %4d B / %2d lines   headroom %4d B\n", (int)refusal.size(),
             (int)split_lines(refusal).size(), (int)((size_t)MR_CONSOLE_STAGE_BYTES - refusal.size()));
      if (refusal.size() > largest) { largest = refusal.size(); largest_name = "(refusal)"; }
      printf("   HELP-LARGEST %s %d B (stage %d B)\n", largest_name.c_str(), (int)largest, (int)MR_CONSOLE_STAGE_BYTES);
      CHK("H3f the LARGEST measured response still fits the stage", largest < (size_t)MR_CONSOLE_STAGE_BYTES);

      // ---- H5: unknown / empty / malformed tails all take the one bounded refusal, and never a section -----
      static const char* const kBadTails[] = {
          "help zzz", "help messagingx", "help messaging x", "help ", "help  ", "help MESSAGING",
          "help cfg ", "help  cfg  x", "help mobilex", "help remote2",
      };
      for (const char* bad : kBadTails) {
          std::string text;
          const bool owned = help_render(bad, text);
          chk_s(std::string("H5a `") + bad + "` is OWNED and answered with the one bounded refusal",
                owned && text == refusal);
      }
      // ...while the file's own `peers ` idiom (skip LEADING spaces, then an exact token) still selects a section.
      { std::string a, b2;
        CHK("H5b `help  messaging` (leading spaces skipped) selects the section",
            help_render("help  messaging", a) && help_render("help messaging", b2) && a == b2 && a != refusal); }
      CHK("H5c the refusal carries a usage line", refusal.find("usage:") != std::string::npos);
      // ...naming EXACTLY this build's topics: every available one present, every unavailable one absent.
      { const std::string vt = valid_topics_line(refusal);
        bool names_ok = !vt.empty();
        for (const auto& t : kHelpTopics) {
            const bool present = has_word(vt, t.name);
            if (present != t.available) names_ok = false;
        }
        CHK("H5d the refusal names EXACTLY this build's valid topics", names_ok);
        printf("   refusal valid-topics line: %s\n", vt.c_str()); }

      // ---- H6: nothing else is claimed. A non-help line must fall through untouched. -----------------------
      static const char* const kNotHelp[] = { "helpful", "hel", "?x", "? messaging", "status", "", "h", "HELP" };
      for (const char* nh : kNotHelp) {
          std::string text;
          const bool owned = help_render(nh, text);
          chk_s(std::string("H6a `") + nh + "` is NOT a help line (falls through, emits nothing)",
                !owned && text.empty());
      }

      // ---- H7: the REAL GuardedConsole drains every response whole, with zero CONSOLE_DROP ----------------
      // A 128-B ESP32 UART0 FIFO and a drain wider than it — the schedule under which P13 proved the sink can
      // still lose or fuse a line. `help` used to overflow the stage here; the point of 0a is that it cannot.
      { std::vector<std::string> cmds; cmds.push_back("help"); cmds.push_back("?"); cmds.push_back("help zzz");
        for (const auto& t : kHelpTopics) if (t.available) cmds.push_back(std::string("help ") + t.name);
        for (const auto& c : cmds) {
            std::string oracle_text; help_render(c.c_str(), oracle_text);
            Serial.reset(128); Serial.auto_drain = 0;
            mrfw::help_command(c.c_str(), c.size(), mrcon);
            run_loop(400, 200);
            chk_s(std::string("H7a `") + c + "` reaches the wire BYTE-EXACT through GuardedConsole",
                  Serial.wire == oracle_text);
            chk_s(std::string("H7b `") + c + "` dropped ZERO lines",  mrcon.dropped_lines() == 0);
            chk_s(std::string("H7c `") + c + "` left no fused/unterminated line",
                  ends_crlf(Serial.wire) && count_corrupt(Serial.wire, split_lines(oracle_text)) == 0);
            chk_s(std::string("H7d `") + c + "` emitted no CONSOLE_DROP report",
                  Serial.wire.find("CONSOLE_DROP") == std::string::npos);
        } }

      // ---- H8: the CONTENT block the Python wrapper compares against the frozen pre-slice baseline ---------
      // ⓘ CONTENT only: an indented, non-empty line. A canonical topic HEADING (column 0) and the new blank
      //   separators are LAYOUT and are enumerated in the slice evidence, never smuggled in as inherited help.
      printf("HELP-CONTENT-BEGIN %s %d\n", PROBE_PROFILE, (int)content.size());
      for (const auto& l : content) printf("%s\n", l.c_str());
      printf("HELP-CONTENT-END\n");
      printf("HELP-INDEX-BEGIN %s\n", PROBE_PROFILE);
      printf("%s", idx_help.c_str());
      printf("%s", refusal.c_str());
      printf("HELP-INDEX-END\n");
    }

    printf("§B95 console-sink probe: %d passed / %d failed / %d total\n", g_pass, g_fail, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
