// MeshRoute — test/ram_inbox_store.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// A RAM-backed InboxStore fake for the native tests (the device QSPI/LittleFS store is Phase 2). Append +
// iterate + drop-oldest at the byte cap, mirroring the device store's per-record flash framing ([u16
// total_len] + record) for the byte accounting. Counters live in the object, so re-`on_init`-ing a fresh
// Inbox on the SAME store object simulates a reboot (persisted state survives). Header-only (inline
// methods) so test_inbox.cpp + test_node_r3.cpp can share it. See the persistent-inbox spec §9.
#pragma once
#include "inbox.h"

#include <cstdint>
#include <deque>
#include <vector>

namespace meshroute {

class RamInboxStore : public InboxStore {
public:
    explicit RamInboxStore(uint32_t cap_bytes) : _cap(cap_bytes) {}
    bool begin() override { return true; }   // RAM: nothing to mount
    bool append(uint32_t seq, const uint8_t* rec, uint16_t len) override {
        const uint32_t framed = static_cast<uint32_t>(len) + 2;   // [u16 total_len] + record (flash framing)
        if (framed > _cap) return false;                          // a single record bigger than the whole store
        while (_total + framed > _cap && !_recs.empty()) {        // drop-oldest until it fits
            _total -= static_cast<uint32_t>(_recs.front().bytes.size()) + 2;
            _recs.pop_front();
        }
        _recs.push_back({ std::vector<uint8_t>(rec, rec + len), seq });
        _total += framed;
        return true;
    }
    uint16_t read_since(uint32_t since, ReadCb cb, void* ctx) const override {
        uint16_t n = 0;
        for (const auto& r : _recs) {                             // deque is append (oldest-first) order
            if (r.seq <= since) continue;
            ++n;
            if (!cb(ctx, r.seq, r.bytes.data(), static_cast<uint16_t>(r.bytes.size()))) break;
        }
        return n;
    }
    uint32_t persisted_next_seq() const override { return _persisted_next; }
    bool     set_next_seq(uint32_t next) override {                // fail_set_next models a flash write failure
        if (fail_set_next) { ++failed_set_next_calls; return false; }
        _persisted_next = next; return true;
    }
    bool     fail_set_next = false;          // test knob: make every set_next_seq fail
    uint16_t failed_set_next_calls = 0;
    uint32_t read_cursor() const override { return _read_cursor; }
    bool     set_read_cursor(uint32_t seq) override { _read_cursor = seq; return true; }
    uint16_t count() const override { return static_cast<uint16_t>(_recs.size()); }
private:
    struct Rec { std::vector<uint8_t> bytes; uint32_t seq; };
    std::deque<Rec> _recs;
    uint32_t _cap;
    uint32_t _total = 0;
    uint32_t _persisted_next = 0;
    uint32_t _read_cursor = 0;
};

}  // namespace meshroute
