// MeshRoute — test/fake_inbox_storage.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// In-RAM fakes for the durable SegmentedInboxStore's two injected backends (lib/core/segmented_inbox_store.h),
// for native tests. Heap (std::map/std::vector) is fine here — the device backends are the real flash. The
// KEY property the fakes preserve: the records store (FakeSegmentStore) and the meta store (FakeMetaStore) are
// SEPARATE objects, so a records `wipe()` leaves the meta intact — exactly the §10.1 split the durability logic
// depends on. FakeSegmentStore models append-only-within-a-segment + whole-segment erase + the byte framing,
// so the tests exercise the REAL ring/framing/eviction logic, not a divergent fake.
#pragma once
#include "segmented_inbox_store.h"   // meshroute::ISegmentStore / IMetaStore

#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace meshroute {

// A ring of segment "files" by index, each an append-only byte vector. Mirrors the device LittleFS dir of
// `<i>` files: seg_append grows a file, seg_erase removes it, seg_read returns the whole file.
class FakeSegmentStore : public ISegmentStore {
public:
    bool mount(bool* formatted) override { *formatted = _formatted_once; _formatted_once = false; return true; }

    bool seg_size(uint16_t idx, uint32_t* size) const override {
        auto it = _segs.find(idx);
        if (it == _segs.end()) return false;                       // absent -> false (like a missing file)
        *size = static_cast<uint32_t>(it->second.size());
        return true;
    }
    // ★★ §B135 MID-FRAME FAULT INJECTION — the reason the [[B133]] torn-write hole survived its own test suite.
    // `RamInboxStore::fail_append` fails BEFORE writing anything, so it can never produce a TORN TAIL, and a fault
    // injector that cannot produce the fault is not a fault test. This one fails a chosen seg_append call AFTER
    // letting `partial_bytes` of it land — which is exactly a power cut between the frame header and its body
    // (append() issues two calls per record: #0 = the 6-byte header, #1 = the body).
    bool seg_append(uint16_t idx, const uint8_t* b, uint16_t n) override {
        const bool fail = (_fail_at >= 0 && _appends_seen == _fail_at);
        ++_appends_seen;
        if (fail) {
            const uint16_t k = _partial < n ? _partial : n;
            if (k) { auto& s = _segs[idx]; s.insert(s.end(), b, b + k); }   // don't CREATE a file for a 0-byte tear
            _fail_at = -1;                                         // one-shot: the retry must be able to succeed
            return false;
        }
        auto& s = _segs[idx];                                      // operator[] creates the file on first append
        s.insert(s.end(), b, b + n);
        return true;
    }
    uint32_t seg_read(uint16_t idx, uint8_t* out, uint32_t cap) const override {
        auto it = _segs.find(idx);
        if (it == _segs.end()) return 0;
        const uint32_t n = it->second.size() < cap ? static_cast<uint32_t>(it->second.size()) : cap;
        std::memcpy(out, it->second.data(), n);
        return n;
    }
    // ⛔ FALLIBLE since 2026-08-28 ([[B134]] QG blocker 3). `_erase_fails_at` arms ONE segment index to refuse
    //    erasure — the medium-failure the destructive verbs and the roll must both be able to see.
    bool seg_erase(uint16_t idx) override {
        if (_erase_fails_at >= 0 && idx == static_cast<uint16_t>(_erase_fails_at)) return false;
        _segs.erase(idx);
        return true;                              // "empty afterwards": erasing an absent segment is success
    }
    // Three-valued since [[B134]] QG round 2: `_inspect_fails` models a records store that cannot ANSWER — the
    // fault that must never be laundered into "empty" (see ISegmentStore::any_segments).
    bool any_segments(bool* ok) const override {
        if (ok) *ok = !_inspect_fails;
        if (_inspect_fails) return false;      // the value is meaningless; `*ok` is what the caller must read
        for (const auto& kv : _segs) if (!kv.second.empty()) return true;
        return false;
    }

    // --- test knobs ---
    void wipe(bool report_formatted = true) { _segs.clear(); _formatted_once = report_formatted; }  // simulate a records-store format/wipe
    size_t live_segments() const { size_t n = 0; for (const auto& kv : _segs) if (!kv.second.empty()) ++n; return n; }
    size_t seg_bytes(uint16_t idx) const { auto it = _segs.find(idx); return it == _segs.end() ? 0 : it->second.size(); }
    // §B135: arm a one-shot mid-frame failure. `nth` counts seg_append calls from NOW (0 = the very next one, i.e.
    // a record's HEADER; 1 = its BODY). `partial` bytes of the failing call still land on the "flash".
    void fail_mid_frame(int nth, uint16_t partial) { _fail_at = nth; _partial = partial; _appends_seen = 0; }
    bool fault_armed() const { return _fail_at >= 0; }             // false => the injector actually FIRED (never assume it did)
    // §B134b: make ONE segment index refuse erasure (a worn/failing sector). -1 disarms.
    void fail_erase_of(int idx) { _erase_fails_at = idx; }
    // §B134c: make the records store unable to say whether it holds anything.
    void fail_inspection(bool on) { _inspect_fails = on; }

private:
    std::map<uint16_t, std::vector<uint8_t>> _segs;
    bool _formatted_once = false;     // mount() reports this once (a fresh format) then clears it
    int      _fail_at = -1;           // §B135 injector: -1 = disarmed
    uint16_t _partial = 0;
    int      _appends_seen = 0;
    int      _erase_fails_at = -1;    // §B134b erase injector: -1 = disarmed
    bool     _inspect_fails  = false; // §B134c: any_segments() cannot answer
};

// A tiny persistent meta blob. Survives a FakeSegmentStore.wipe() (it's a separate object) — the whole point
// of the meta/records split. wipe() here simulates ALSO losing the meta (a full store wipe).
class FakeMetaStore : public IMetaStore {
public:
    // Three-valued since [[B134]] QG round 3: a store that was never written is ABSENT; one that cannot be read,
    // or comes back the wrong length, is an ERROR — and "error" must never be readable as "fresh".
    MetaLoad load(void* blob, uint16_t len) override {
        if (_fail_loads) return MetaLoad::error;                  // the medium refuses to answer
        if (_blob.empty()) return MetaLoad::absent;               // never saved -> a TRUE fresh
        if (_blob.size() != len) return MetaLoad::error;          // present but the wrong size -> corrupt
        std::memcpy(blob, _blob.data(), len);
        return MetaLoad::loaded;
    }
    // ★★ [[B134]] QG BLOCKER 1's INJECTOR. `_fail_saves` makes the meta store REFUSE to persist — the medium
    //    failure whose result the ring logic used to discard, and through which QG reproduced an acknowledged
    //    record vanishing across a reboot. ⚠ A refused save must leave the PRIOR blob intact: a meta store that
    //    lost its old contents on a failed write would be a different (and easier) fault than the real one.
    bool save(const void* blob, uint16_t len) override {
        const int n = _saves++;
        // §B134f: fail ONE chosen save, counting from the arming call. The append path from an empty store issues
        // TWO (the append_pending marker, then the non_empty finalize), and they have DIFFERENT contracts — the
        // first must REFUSE the append, the second must not — so a battery needs to hit them separately.
        if (_fail_save_at >= 0 && n == _fail_save_at) { _fail_save_at = -1; return false; }
        if (_fail_saves) return false;
        const uint8_t* p = static_cast<const uint8_t*>(blob);
        _blob.assign(p, p + len);
        return true;
    }
    void wipe() { _blob.clear(); }    // simulate losing the meta too (full wipe -> fresh seq + epoch)
    bool saved() const { return !_blob.empty(); }
    // --- §B134b knobs ---
    void fail_saves(bool on) { _fail_saves = on; }
    // §B134d: the meta store cannot be READ (distinct from never having been written).
    void fail_loads(bool on) { _fail_loads = on; }
    void fail_save_at(int nth) { _fail_save_at = nth; _saves = 0; }
    bool save_fault_armed() const { return _fail_save_at >= 0; }   // false => the injector really FIRED
    int  saves() const { return _saves; }

    // --- test knobs for the M2 torn-meta regression: corrupt/read a stored field by byte offset (the Meta
    //     layout is private to SegmentedInboxStore; the layout offsets are documented in its struct). ---
    void     poke_u16(size_t off, uint16_t v) { if (off + 2 <= _blob.size()) { _blob[off] = uint8_t(v); _blob[off + 1] = uint8_t(v >> 8); } }
    void     poke_u8(size_t off, uint8_t v)   { if (off + 1 <= _blob.size()) _blob[off] = v; }
    uint8_t  peek_u8(size_t off) const        { return (off + 1 <= _blob.size()) ? _blob[off] : 0; }
    uint16_t peek_u16(size_t off) const { return (off + 2 <= _blob.size()) ? uint16_t(_blob[off] | (_blob[off + 1] << 8)) : 0; }
    size_t   blob_size() const { return _blob.size(); }

private:
    std::vector<uint8_t> _blob;
    bool                 _fail_saves = false;
    bool                 _fail_loads = false;
    int                  _fail_save_at = -1;
    int                  _saves = 0;
};

}  // namespace meshroute
