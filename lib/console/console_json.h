// MeshRoute — lib/console/console_json.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Bounded, heap-free NDJSON line writers shared by the device console and the
// sim's FirmwareNode (one serializer, two backends — schema cannot drift).
// hal.h discipline: no std::string/json/heap, C++17-includable, -fno-exceptions.
// See docs/specs/2026-05-30-device-console-design.md.
#pragma once
#include "command.h"   // CmdResult, Push, CmdCode, PushKind  (lib/core)
#include "hal.h"       // EventField                          (lib/core)
#include "node.h"      // NodeConfig                          (lib/core)
#include <cstddef>
#include <cstdint>

namespace meshroute::console {

// Bounded, heap-free JSON writer. Every append is overflow-safe: once `cap`
// is reached `overflow` latches and further appends are no-ops; finish()
// then returns 0 so callers never emit a truncated line.
struct JsonBuf {
    char*  buf;
    size_t cap;
    size_t pos = 0;
    bool   overflow = false;
    JsonBuf(char* b, size_t c) : buf(b), cap(c) {}
    void   ch(char c);
    void   lit(const char* s);            // raw literal, no escaping
    void   str(const char* s, size_t n);  // quoted, JSON-escaped string value
    void   key(const char* k);            // `"k":`
    void   i64(int64_t v);
    void   u32(uint32_t v);
    void   f64(double v);
    size_t finish();                       // append '\n', NUL-terminate if room; 0 if overflow
};

// Complete NDJSON line serializers (return bytes written incl. '\n', 0 on overflow).
size_t write_ack   (char* buf, size_t cap, const CmdResult& r);
size_t write_push  (char* buf, size_t cap, const Push& p);
size_t write_event (char* buf, size_t cap, const char* type, const EventField* f, size_t n);
size_t write_log   (char* buf, size_t cap, const char* msg);
size_t write_err   (char* buf, size_t cap, const char* code, const char* msg);  // msg nullable
size_t write_ready (char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* mode,
                    uint32_t inbox_epoch);
size_t write_status(char* buf, size_t cap, uint8_t id, uint32_t key, const NodeConfig& c, const char* state);

// Phase-3 inbox sync (schema: ios-companion/INBOX_SYNC_CONTRACT.md). The pull stream = inbox_dm* then
// inbox_channel* (oldest-first) then inbox_end; mark_read acks via write_inbox_marked. Fields individual to
// keep this file free of inbox.h.
size_t write_inbox_dm     (char* buf, size_t cap, uint32_t seq, uint8_t origin, uint16_t ctr, uint32_t sender_hash,
                           uint64_t rx_ms, const char* body, size_t body_len);
size_t write_inbox_channel(char* buf, size_t cap, uint32_t seq, uint8_t origin, uint8_t channel_id,
                           uint32_t channel_msg_id, uint64_t rx_ms, const char* body, size_t body_len);
size_t write_inbox_end    (char* buf, size_t cap, uint32_t dm_seq, uint32_t chan_seq, uint32_t epoch, uint32_t count);
size_t write_inbox_marked (char* buf, size_t cap, const char* kind, uint32_t seq);

const char* cmdcode_name(CmdCode c);
const char* pushkind_name(PushKind k);

}  // namespace meshroute::console
