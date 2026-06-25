// MeshRoute — src/sched_send.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Firmware scheduled-send CORE (spec 2026-06-24-firmware-scheduled-send.md): an on-node, RAM-only test workload so
// the oracle arms the node ONCE and lets it run the whole ms-offset schedule autonomously over the radio — killing
// the continuous-USB-CDC overuse that wedges nodes mid-test. THIS header is the PURE, Arduino-free logic (the entry
// ring, the due/late/overdue arithmetic, the tag-body build, the offset-list parse) so it is host-unit-testable
// (test/test_sched_send.cpp includes it directly). fw_main.cpp owns the console glue, the millis() clock, the
// Command build + g_node.on_command() TX (queue/duty-gated), and the Serial I/O. NOT persisted (transient test
// state; persisting would add the very InternalFS writes the self-heal fix is reducing).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace mrsched {

inline constexpr uint16_t kSchedMax = 256;     // entry ring (≈3 KB); add() rejects + the caller reports when full
inline constexpr uint32_t kSlackMs  = 5000;    // drop an entry overdue by more than this it still can't enqueue (don't snowball)
inline constexpr uint32_t kLateMs   = 250;     // fired more than this AFTER at_ms => counts `deferred` (the radio fell behind)

// flags bits (an entry's kind + send options). target = dst id (1..254) | 32-bit key_hash (kHash) | channel id (kChannel).
enum Flag : uint8_t { kAck = 1, kEnc = 2, kChannel = 4, kHash = 8 };

struct Entry { uint32_t at_ms; uint32_t target; uint8_t seq; uint8_t flags; bool fired; };  // fired = CONSUMED (sent OR dropped)

struct Schedule {
    Entry    items[kSchedMax];
    uint16_t n = 0;                            // armed (total appended)
    uint16_t seq = 0;                          // per-arming-session counter (0,1,2,…; the tag #<seq>)
    uint16_t fired = 0, deferred = 0, dropped = 0;
    char     run[16] = {};

    void clear() { n = 0; seq = 0; fired = deferred = dropped = 0; run[0] = '\0'; }
    void set_run(const char* r) { size_t i = 0; for (; r && r[i] && i + 1 < sizeof(run); ++i) run[i] = r[i]; run[i] = '\0'; }
    uint16_t armed() const { return n; }

    // append one entry at absolute at_ms; returns the assigned seq (0..255), or -1 if the ring is full.
    int add(uint32_t at_ms, uint32_t target, uint8_t flags) {
        if (n >= kSchedMax) return -1;
        const uint8_t s = static_cast<uint8_t>(seq & 0xFF);
        items[n++] = Entry{ at_ms, target, s, flags, false };
        ++seq;
        return s;
    }
    // the EARLIEST unfired entry due at `now` (at_ms <= now), or -1. Wraparound-safe (signed diffs).
    int next_due(uint32_t now) const {
        int best = -1; uint32_t best_at = 0;
        for (uint16_t i = 0; i < n; ++i) {
            if (items[i].fired) continue;
            if (static_cast<int32_t>(now - items[i].at_ms) < 0) continue;        // not due yet
            if (best < 0 || static_cast<int32_t>(items[i].at_ms - best_at) < 0) { best = static_cast<int>(i); best_at = items[i].at_ms; }
        }
        return best;
    }
    bool overdue(int i, uint32_t now, uint32_t slack = kSlackMs) const {
        return i >= 0 && static_cast<int32_t>(now - items[static_cast<uint16_t>(i)].at_ms) > static_cast<int32_t>(slack);
    }
    void mark_fired(int i, uint32_t now) {                                       // a successful enqueue
        if (i < 0) return;
        Entry& e = items[static_cast<uint16_t>(i)];
        e.fired = true; ++fired;
        if (static_cast<int32_t>(now - e.at_ms) > static_cast<int32_t>(kLateMs)) ++deferred;   // fired late => the queue/duty held it back
    }
    void mark_dropped(int i) {                                                   // given up (overdue + can't enqueue) OR a permanent send error
        if (i < 0) return;
        items[static_cast<uint16_t>(i)].fired = true; ++dropped;
    }
    bool done() const { for (uint16_t i = 0; i < n; ++i) if (!items[i].fired) return false; return n > 0; }
    // ms until the next unfired entry (0 if already due); -1 if none pending. For teststatus `next=+<ms>`.
    int32_t next_offset_ms(uint32_t now) const {
        int32_t best = 0; bool any = false;
        for (uint16_t i = 0; i < n; ++i) {
            if (items[i].fired) continue;
            int32_t d = static_cast<int32_t>(items[i].at_ms - now);
            if (d < 0) d = 0;
            if (!any || d < best) { best = d; any = true; }
        }
        return any ? best : -1;
    }
};

// Build the body "T<run>S<self>#<seq>@<sendms>" into buf — byte-identical to the harness make_tag(run,self,seq) + the
// "@<sendms>" send-stamp suffix (parse_tag ignores the suffix; the harness's parse_sendms reads it). Returns the length.
inline uint8_t build_body(char* buf, size_t cap, const char* run, uint8_t self, uint8_t seq, uint32_t sendms) {
    int n = snprintf(buf, cap, "T%sS%u#%u@%lu", run ? run : "", (unsigned)self, (unsigned)seq, (unsigned long)sendms);
    if (n < 0) n = 0;
    if (static_cast<size_t>(n) >= cap) n = static_cast<int>(cap) - 1;
    return static_cast<uint8_t>(n);
}

// Parse a comma/space list "1000,2000,5000" -> out[] (ms offsets); returns the count (<= max). Stops at the first
// non-digit/non-separator (so trailing flags after the list are ignored). Skips empty fields.
inline uint16_t parse_offsets(const char* s, uint32_t* out, uint16_t max) {
    uint16_t c = 0;
    while (s && *s && c < max) {
        while (*s == ' ' || *s == ',') ++s;
        if (*s < '0' || *s > '9') break;
        uint32_t v = 0;
        while (*s >= '0' && *s <= '9') { v = v * 10u + static_cast<uint32_t>(*s - '0'); ++s; }
        out[c++] = v;
    }
    return c;
}

// EXACTLY 8 hex chars -> a key_hash32 dst (true); anything else -> false. Matches the `send` console's "hash=8hex".
inline bool parse_hash8(const char* s, uint32_t& out) {
    uint32_t v = 0; int n = 0;
    for (; s && s[n]; ++n) {
        const char c = s[n]; uint32_t d;
        if      (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(10 + c - 'a');
        else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(10 + c - 'A');
        else return false;
        v = (v << 4) | d;
    }
    if (n != 8) return false;
    out = v; return true;
}

// Strict all-digits decimal 0..max (mirrors console_parse's parse_u32_tok — REJECTS a typo'd/partial token rather than
// atol()-truncating it, so a scheduled dst/channel parses EXACTLY as the typed `send` does). Empty/overflow -> false.
inline bool parse_dec(const char* s, uint32_t max, uint32_t& out) {
    if (!s || !*s) return false;
    uint32_t v = 0;
    for (const char* q = s; *q; ++q) {
        if (*q < '0' || *q > '9') return false;
        v = v * 10u + static_cast<uint32_t>(*q - '0');
        if (v > max) return false;
    }
    out = v; return true;
}

}  // namespace mrsched
