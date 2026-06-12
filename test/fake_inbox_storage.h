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
    bool seg_append(uint16_t idx, const uint8_t* b, uint16_t n) override {
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
    void seg_erase(uint16_t idx) override { _segs.erase(idx); }
    bool any_segments() const override {
        for (const auto& kv : _segs) if (!kv.second.empty()) return true;
        return false;
    }

    // --- test knobs ---
    void wipe(bool report_formatted = true) { _segs.clear(); _formatted_once = report_formatted; }  // simulate a records-store format/wipe
    size_t live_segments() const { size_t n = 0; for (const auto& kv : _segs) if (!kv.second.empty()) ++n; return n; }

private:
    std::map<uint16_t, std::vector<uint8_t>> _segs;
    bool _formatted_once = false;     // mount() reports this once (a fresh format) then clears it
};

// A tiny persistent meta blob. Survives a FakeSegmentStore.wipe() (it's a separate object) — the whole point
// of the meta/records split. wipe() here simulates ALSO losing the meta (a full store wipe).
class FakeMetaStore : public IMetaStore {
public:
    bool load(void* blob, uint16_t len) override {
        if (_blob.size() != len) return false;                    // never saved / size mismatch -> fresh
        std::memcpy(blob, _blob.data(), len);
        return true;
    }
    bool save(const void* blob, uint16_t len) override {
        const uint8_t* p = static_cast<const uint8_t*>(blob);
        _blob.assign(p, p + len);
        return true;
    }
    void wipe() { _blob.clear(); }    // simulate losing the meta too (full wipe -> fresh seq + epoch)
    bool saved() const { return !_blob.empty(); }

private:
    std::vector<uint8_t> _blob;
};

}  // namespace meshroute
