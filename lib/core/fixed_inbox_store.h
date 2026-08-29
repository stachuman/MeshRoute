// MeshRoute — lib/core/fixed_inbox_store.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// A heap-free, fixed-capacity InboxStore — a VOLATILE RAM ring of `Slots` fixed-size record slots with
// drop-oldest eviction at capacity.
// ⛔ [[B134]] CORRECTED IN PLACE 2026-08-28: this used to call itself *"the INTERIM on-device inbox … until the
//    durable QSPI/LittleFS records backend (src/device_inbox_store.h, Phase 2) is bench-wired"*. Both durable
//    backends are now wired — nRF52/QSPI (`src/device_inbox_store.h`) and ESP32/LittleFS
//    (`src/device_inbox_fs_esp32.h` over `segmented_inbox_store.h`) — so NO board environment in this tree
//    selects this store any more. It remains the arm-3 fallback for a board with NEITHER backend, and it is
//    still exercised natively (test/test_fixed_inbox_store.cpp). The test RamInboxStore (test/) uses
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

    // ★★ [[B134]] QG ROUND 2 — A REAL WIPE, because inheriting the base's successful no-op was the same lie in
    //    miniature. `prep-restart` HALTS the node but does NOT reboot it, so between the verb and the operator's
    //    power cycle this store stayed fully readable while the console had just reported the inbox cleared —
    //    `pull_inbox` would still have streamed every record. The contract is "empty afterwards"; make it true.
    // ⓘ `_epoch` IS DELIBERATELY NOT TOUCHED: it is set once per boot by the backend and identifies THIS runtime's
    //    history to the companion. Re-rolling it here would announce a wipe the reboot is about to make moot.
    // ⓘ AND THERE IS NO next_seq TO PRESERVE, which is the RAM analogue of the segmented store's "never reset
    //    next_seq" rule rather than a divergence from it: `persisted_next_seq()` is 0 here by design (volatile —
    //    no backstop), so there is no high-water on any medium to keep. Sequence non-reuse still holds for the
    //    same reason it holds there: the counter that hands out seqs lives in `Inbox` (`_dm_next`/`_chan_next`),
    //    which `wipe()` does not touch and only `on_init` resets — so the next record after a wipe continues
    //    upward and cannot collide with anything the companion has already filed.
    bool wipe() override { _head = 0; _count = 0; _read_cursor = 0; return true; }

private:
    struct Slot { uint32_t seq = 0; uint16_t len = 0; uint8_t bytes[inbox_record_max_bytes] = {}; };
    Slot     _slot[Slots];
    uint16_t _head = 0;
    uint16_t _count = 0;
    uint32_t _read_cursor = 0;
    uint32_t _epoch = 1;
};

}  // namespace meshroute
