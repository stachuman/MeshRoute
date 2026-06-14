// MeshRoute — lib/core/fixed_inbox_store.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// A heap-free, fixed-capacity InboxStore — the INTERIM on-device inbox (volatile RAM) until the durable
// QSPI/LittleFS records backend (src/device_inbox_store.h, Phase 2) is bench-wired. A ring of `Slots`
// fixed-size record slots with drop-oldest eviction at capacity. The test RamInboxStore (test/) uses
// std::deque/std::vector; this one honours the InboxStore "no heap / no exceptions" contract so it runs on
// the device. Platform-neutral (lib/core, no Arduino) so the device build AND the native tests share it.
//
// VOLATILE: history is lost on reboot. set_epoch() takes a per-boot-unique value so the companion sees a NEW
// storage_epoch after every node reboot and re-pulls from 0 (correct — a volatile store has no prior history
// to merge; the app dedups by stable message identity). The durable QSPI store replaces this with a real
// persisted epoch + records that survive reboot.
#pragma once
#ifndef MESHROUTE_NS
#define MESHROUTE_NS meshroute   // Slice 5 faithful two-lib: gateway variant compiles with -DMESHROUTE_NS=meshroute_gw
#endif
#include "inbox.h"   // InboxStore, inbox_record_max_bytes
#include <cstdint>
#include <cstddef>

namespace MESHROUTE_NS {

template <uint16_t Slots>
class FixedInboxStore : public InboxStore {
public:
    FixedInboxStore() = default;

    void set_epoch(uint32_t e) { _epoch = e ? e : 1u; }    // 0 means "no durable epoch" in the contract; keep it non-zero

    bool begin() override { return true; }                 // RAM: nothing to mount

    bool append(uint32_t seq, const uint8_t* rec, uint16_t len) override {
        if (len > inbox_record_max_bytes) return false;    // a single record bigger than a slot (shouldn't happen)
        const uint16_t idx = static_cast<uint16_t>((_head + _count) % Slots);
        if (_count == Slots) _head = static_cast<uint16_t>((_head + 1) % Slots);   // full -> evict the oldest
        else                 ++_count;
        Slot& s = _slot[idx];
        s.seq = seq; s.len = len;
        for (uint16_t i = 0; i < len; ++i) s.bytes[i] = rec[i];
        return true;
    }

    uint16_t read_since(uint32_t since, ReadCb cb, void* ctx) const override {
        uint16_t n = 0;
        for (uint16_t i = 0; i < _count; ++i) {            // the ring is oldest-first starting at _head
            const Slot& s = _slot[(_head + i) % Slots];
            if (s.seq <= since) continue;
            ++n;
            if (!cb(ctx, s.seq, s.bytes, s.len)) break;
        }
        return n;
    }

    uint32_t persisted_next_seq() const override { return 0; }    // volatile: no backstop -> seq restarts at 1 each boot
    bool     set_next_seq(uint32_t) override { return true; }     // no-op (nothing durable to persist)
    uint32_t read_cursor() const override { return _read_cursor; }
    bool     set_read_cursor(uint32_t seq) override { _read_cursor = seq; return true; }
    uint16_t count() const override { return _count; }
    uint32_t storage_epoch() const override { return _epoch; }

private:
    struct Slot { uint32_t seq = 0; uint16_t len = 0; uint8_t bytes[inbox_record_max_bytes] = {}; };
    Slot     _slot[Slots];
    uint16_t _head = 0;
    uint16_t _count = 0;
    uint32_t _read_cursor = 0;
    uint32_t _epoch = 1;
};

}  // namespace meshroute
