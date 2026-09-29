// MeshRoute — tools/probe_inbox_verbs/fakes/Preferences.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §CUSTODY-D host-probe stand-in for an ESP32/arduino-esp32 platform header, so the REAL
// `src/firmware_commands.cpp` + `src/firmware_inbox.cpp` compile on the host under the `[env:heltec_v3]`
// `-D` arm.
// ⓘ Kept in THIS probe's own dir rather than added to the shared `probe_board_ui`/`probe_device_radio` fakes:
//   they are this arm's platform, not a shared Arduino surface (U1 — reuse what is shared, do not widen it).
//
// ★★ §RADMIN-0b / [[B279]] WIDENED THIS FAKE FROM A COMPILE SHIM TO A *STORE*, AND THE WIDENING IS BOUNDED BY ONE
//    RULE: THIS FILE EXPOSES STORAGE FACTS AND DECIDES NOTHING. It holds bytes and answers whether the medium is
//    present / readable / writable / failing. It does not format output, route a sink, derive an identity or
//    answer any question production owns — `regen` still calls the real `mrnv::save_id`, whose real `write_slot`
//    calls the methods below, and every verdict about the RESULT is asserted in `probe_main.cpp`.
//    ⓘ WHY IT HAD TO GROW AT ALL: the pre-0b file answered "the least capable honest value" to everything
//      (`begin()` -> false), so `mrnv::save_id` could only ever FAIL — one of `regen`'s two arms was structurally
//      unreachable and the byte-exact SUCCESS line could not be observed at all.
// ⛔ THE DEFAULT STATE IS THE OLD BEHAVIOUR, EXACTLY: no namespace, no key, no writable medium. `begin()` answers
//    false on both modes, `isKey`/`getBytesLength`/`getBytes` answer 0/false and `remove`/`clear` answer true —
//    so every pre-0b inbox-verb check reads an identical platform and keeps its previous verdict. A test that
//    wants a store must ASK for one (`mrprobe_nv().ns_present/rw_ok`), which is what keeps the widening visible.
// ⛔ THE TWO `DISHONEST` SWITCHES EXIST TO BE MUTATED, NOT USED: `retain_on_fail` / `drop_on_ok` are the shapes a
//    lying storage instrument would take (bytes kept while the write reports failure; success reported over stale
//    bytes). The probe asserts against BOTH, and run.sh's controls turn each one on in a COPY of this file and
//    require the probe to go RED — i.e. the probe refuses its own dishonest instrument. ⛔ Neither switch may be
//    set by a normal run.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>

// ---- the probe-local NV medium ---------------------------------------------------------------------------------
// A small fixed table of (namespace, key) -> bytes. Fixed-size and no-heap, like everything else in these probes.
// ★★ §RADMIN slice 4 — THE PAYLOAD CAPACITY IS A **FACT ABOUT THE MEDIUM**, and it moved 512 -> 2304 for one
//    measured reason: `/mrtargets` is a 2056-byte whole-blob record, and a 512-byte slot would have made every
//    write of it a SHORT write — i.e. the fake would have manufactured a failure production never sees, and the
//    controller rows would have measured the fake instead of the router. ⛔ NO POLICY MOVED: `fail_write`,
//    `retain_on_fail` and `drop_on_ok` are untouched, the byte counts are untouched, and a record LARGER than the
//    slot is still an honest short write.
//    2304 = 2056 rounded up past the largest record any arm stores (/mrpeers is 1160, /mrui 372, /mrmkeys 368).
// ★★ W6 / [[B477]] — 2304 -> 2852, AND FOR THE SAME MEASURED REASON: the `/mrui` v2 record is 2852 B
//    (`sizeof(mrnv::UiPresetBlob)`, owner-ruled D15), the largest record any arm now stores, and a 2304-B slot
//    would have skipped it SILENTLY while `putBytes` still reported the full length — a save that "worked" and
//    stored nothing. `probe_main.cpp` asserts AT COMPILE TIME that every record an arm stores fits this slot.
// ★ SIX SLOTS, not four: an arm now touches /mrid, /mrcfg, /mrpeers, /mrmkeys, /mrtargets and (on the ACCEPT arm)
//   /mradmid + /mracl. A table that ran out would report a write failure production never sees.
struct MrProbeNvSlot {
    char          ns[16]    = {};
    char          key[16]   = {};
    unsigned char data[2852] = {};
    size_t        len       = 0;
    bool          used      = false;
};

struct MrProbeNv {
    void (*observe)(const char*) = nullptr; // optional operation trace for the real-action probe
    MrProbeNvSlot slot[8];
    // --- the medium's honest, controllable facts ---
    bool ns_present = false;   // a READ-ONLY begin() succeeds: this namespace has been written before
    bool rw_ok      = false;   // a READ-WRITE begin() succeeds: the medium accepts writes at all
    bool fail_write = false;   // putBytes reports a SHORT write (the medium refused the record)
    // --- ⛔ the two DISHONEST shapes, for run.sh's controls only (see the header note) ---
    bool retain_on_fail = false;   // keep the bytes although the write is reported as failed
    bool drop_on_ok     = false;   // report a full write while keeping the OLD bytes
    // --- what production actually did ---
    int  writes = 0;
    int  reads  = 0;

    void reset() { *this = MrProbeNv(); }

    static bool same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

    MrProbeNvSlot* find(const char* ns, const char* key) {
        for (auto& s : slot) if (s.used && same(s.ns, ns) && same(s.key, key)) return &s;
        return nullptr;
    }
    // ★ W6 / [[B477]]: answers whether the medium STORED the bytes. Over-capacity or no free slot stores NOTHING
    //   and answers false, which `putBytes` then reports as a SHORT write — never a full one.
    bool put(const char* ns, const char* key, const unsigned char* src, size_t n) {
        MrProbeNvSlot* s = find(ns, key);
        if (!s) for (auto& c : slot) if (!c.used) { s = &c; break; }
        if (!s || n > sizeof s->data) return false;
        s->used = true;
        std::snprintf(s->ns, sizeof s->ns, "%s", ns ? ns : "");
        std::snprintf(s->key, sizeof s->key, "%s", key ? key : "");
        std::memcpy(s->data, src, n);
        s->len = n;
        return true;
    }
    // "does the medium hold EXACTLY these bytes under this key?" — the probe's storage-honesty question.
    bool holds(const char* ns, const char* key, const void* want, size_t n) {
        MrProbeNvSlot* s = find(ns, key);
        return s && s->len == n && std::memcmp(s->data, want, n) == 0;
    }
};
inline MrProbeNv& mrprobe_nv() { static MrProbeNv nv; return nv; }

class Preferences {
public:
    bool begin(const char* ns, bool readOnly = false) {
        if (mrprobe_nv().observe && ns && !std::strcmp(ns, "mr")) mrprobe_nv().observe("nv-open");
        _ns = ns ? ns : "";
        _open = readOnly ? mrprobe_nv().ns_present : mrprobe_nv().rw_ok;
        return _open;
    }
    void end() { _open = false; }
    bool isKey(const char* key) { return mrprobe_nv().find(_ns, key) != nullptr; }
    size_t getBytesLength(const char* key) {
        MrProbeNvSlot* s = mrprobe_nv().find(_ns, key);
        return s ? s->len : 0;
    }
    // ⓘ FAITHFUL TO THE REAL API on the one edge `nvs_read_slot` reasons about: `Preferences::getBytes` answers 0
    //   when the stored blob is LONGER than the destination (src/device_inbox_seam.h:139 records the same fact).
    size_t getBytes(const char* key, void* dst, size_t cap) {
        MrProbeNvSlot* s = mrprobe_nv().find(_ns, key);
        if (!s || s->len > cap) return 0;
        std::memcpy(dst, s->data, s->len);
        ++mrprobe_nv().reads;
        return s->len;
    }
    // ★★ W6 / [[B477]] — THE FULL LENGTH ONLY WHEN THE MEDIUM ACTUALLY STORED THE BYTES. An over-capacity or
    //    no-slot write reports a SHORT write (0) and stores nothing. ⛔ The two DISHONEST switches keep their exact
    //    meaning: `retain_on_fail` keeps bytes under a reported failure, `drop_on_ok` reports success over stale ones.
    size_t putBytes(const char* key, const void* src, size_t n) {
        MrProbeNv& nv = mrprobe_nv();
        ++nv.writes;
        const unsigned char* b = static_cast<const unsigned char*>(src);
        if (nv.fail_write) { if (nv.retain_on_fail) (void)nv.put(_ns, key, b, n); return 0; }
        if (nv.drop_on_ok) return n;
        return nv.put(_ns, key, b, n) ? n : 0;
    }
    bool remove(const char*) { return true; }
    bool clear() { if (mrprobe_nv().observe) mrprobe_nv().observe("nv-clear"); return true; }
private:
    const char* _ns  = "";
    bool        _open = false;
};
