// MeshRoute — src/firmware_inbox.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// The inbox/companion-sync cluster (see firmware_inbox.h) moved VERBATIM from fw_main.cpp (cleanup 2026-07-14).
// Shared state (g_node, g_hal, the s_inbox_jb NDJSON scratch) comes from fw_context.h; the JSON writers from
// console_json.h. Behaviour-preserving: only relocated.
#include "firmware_inbox.h"
#include "fw_context.h"       // g_node, g_hal, s_inbox_jb (shared NDJSON scratch)
#include "console_json.h"     // meshroute::console::write_inbox_dm/channel/end/marked/err
#include "firmware_config_parse.h"   // mrfw::parse_seq_arg — the §B136 strict destructive-target parser (pure, native-tested)
#include <cstdlib>            // strtoul
#include <cstring>            // strncmp

namespace mrfw {

namespace { struct PullCtx { Print& out; uint32_t count; }; }

// pull() callback: format ONE record -> JSON -> sink. The body ptr is valid only for this call (the encoder copies it).
static bool inbox_pull_cb(void* vctx, const meshroute::InboxEntry& e) {
    PullCtx* c = static_cast<PullCtx*>(vctx);
    const size_t n = (e.kind == meshroute::InboxKind::dm)
        ? meshroute::console::write_inbox_dm(s_inbox_jb, sizeof s_inbox_jb, e.seq, e.origin, e.layer_id,
              static_cast<uint16_t>(e.msg_id), e.sender_hash, e.rx_time_ms,
              reinterpret_cast<const char*>(e.body), e.body_len, e.enc != 0, e.type, e.origin_layer)   // §8b enc + the DATA_TYPE (E2E-ack receipt = "e2e_ack") + §GapA-durable origin_layer
        : meshroute::console::write_inbox_channel(s_inbox_jb, sizeof s_inbox_jb, e.seq, e.origin, e.layer_id,
              e.channel_id, e.msg_id, e.rx_time_ms, reinterpret_cast<const char*>(e.body), e.body_len, e.team_id);   // §S5 durable team scoping
    if (n) { c->out.write(s_inbox_jb, n); ++c->count; }
    else {                                                // UNREACHABLE for a valid body (<=241 B fits 1700), but
        char eb[48];                                      // NEVER drop a record silently: tell the app one didn't encode.
        const size_t en = meshroute::console::write_err(eb, sizeof eb, "inbox_encode", nullptr);
        if (en) c->out.write(eb, en);
    }
    return true;                                          // never stop early — the app pulls the whole delta
}

void handle_pull_inbox(const char* args, Print& out) {
    char* end;
    const uint32_t dm_since   = strtoul(args, &end, 10);  // missing/garbled args -> 0 (a full pull is always safe; the app dedups)
    const uint32_t chan_since = strtoul(end, nullptr, 10);
    meshroute::Inbox& ib = g_node.inbox();
    PullCtx ctx{ out, 0 };
    ib.pull(dm_since, chan_since, inbox_pull_cb, &ctx);
    // inbox_end carries the store's NEWEST seq per store (contract §"newest seq per store"), NOT a cursor echo —
    // so an empty store / a stale-high cursor self-heals (the app advances to the real high-water, re-syncing).
    const size_t n = meshroute::console::write_inbox_end(s_inbox_jb, sizeof s_inbox_jb,
                       ib.dm_newest_seq(), ib.chan_newest_seq(), ib.storage_epoch(), ctx.count, g_hal.now());
    if (n) out.write(s_inbox_jb, n);
}

// ★ U1: ONE spelling of the kind gate. `mark_read` and `del_msg` both take `<dm|chan> <seq>`, and the word-boundary
// trap below is exactly the kind of check that rots when it is written twice. Advances `args` past the keyword.
// The kind must be EXACTLY "dm" or "chan" (word boundary = next char is space or end). Without the boundary check,
// "dm5"/"dme" match strncmp("dm",2) and "channel" matches strncmp("chan",4) -> wrong/zero seq parsed.
static bool parse_inbox_kind(const char*& args, meshroute::InboxKind& kind, const char*& kstr) {
    while (*args == ' ') ++args;
    if      (!strncmp(args, "dm", 2)   && (args[2] == ' ' || args[2] == '\0')) { kind = meshroute::InboxKind::dm;      kstr = "dm";   args += 2; return true; }
    else if (!strncmp(args, "chan", 4) && (args[4] == ' ' || args[4] == '\0')) { kind = meshroute::InboxKind::channel; kstr = "chan"; args += 4; return true; }
    return false;
}

void handle_mark_read(const char* args, Print& out) {
    meshroute::InboxKind kind; const char* kstr;
    if (!parse_inbox_kind(args, kind, kstr)) {
        char eb[64]; const size_t n = meshroute::console::write_err(eb, sizeof eb, "mark_read", "kind must be dm|chan");
        if (n) out.write(eb, n); return;                  // fail loud on a bad kind
    }
    const uint32_t seq = strtoul(args, nullptr, 10);
    // ⛔ THE VERDICT IS RELAYED ([[B134]] QG round 7). This used to call `mark_read` and ack unconditionally, so a
    //    cursor the store could not persist was reported to the companion as marked — and because the durable
    //    cursor had not moved, the app's unread badge silently disagreed with the node across every reboot.
    //    The result vocabulary is `del_msg`'s, reused: "marked" | "io_error", never a bool.
    const bool persisted = g_node.inbox().mark_read(kind, seq);
    char ab[80]; const size_t n = meshroute::console::write_inbox_marked(
        ab, sizeof ab, kstr, seq, meshroute::console::inbox_mark_result(persisted));
    if (n) out.write(ab, n);
}

// §3.5 / §6.2 durable single-record delete. `del_msg <dm|chan> <seq>` — the identity is the PAIR (kind, seq); the two
// seq spaces are independent, so the kind is mandatory, never inferred. Reports the erase's THREE outcomes verbatim
// (erased | not_found | io_error): the companion and the bench must be able to tell "evicted already" from "the
// write failed", and neither of those from success. This is also the only operator-reachable delete until the §3.5
// OLED modal (slice B) lands, which is what makes the reboot-persistence bench check executable at all (M2).
void handle_del_msg(const char* args, Print& out) {
    meshroute::InboxKind kind; const char* kstr;
    if (!parse_inbox_kind(args, kind, kstr)) {
        char eb[64]; const size_t n = meshroute::console::write_err(eb, sizeof eb, "del_msg", "kind must be dm|chan");
        if (n) out.write(eb, n);
        return;                                           // fail loud on a bad kind
    }
    // ★ §B136 (QA 2026-08-07): a DESTRUCTIVE target is parsed STRICTLY — exactly one unsigned decimal token, no
    // sign, no trailing junk, no second token, no overflow. The bare `strtoul(args, nullptr, 10)` this replaces
    // deleted seq 1 for `del_msg dm 1oops`, `del_msg dm 1 extra` and `del_msg dm +1` alike. Fail LOUD (C2): a typo
    // must produce an error, never a deletion of whatever the prefix happened to evaluate to. The predicate is
    // shared with the config parsers (firmware_config_parse.h) so the native suite can reach it — src/*.cpp is
    // outside the native build, a pure header is not (U1/U3, the firmware_config_parse.h pattern).
    uint32_t seq = 0;
    if (!parse_seq_arg(args, seq)) {
        char eb[96]; const size_t n = meshroute::console::write_err(eb, sizeof eb, "del_msg", "seq must be one unsigned decimal number");
        if (n) out.write(eb, n);
        return;
    }
    const meshroute::InboxEraseResult r = g_node.inbox().erase(kind, seq);
    const char* res = (r == meshroute::InboxEraseResult::erased)    ? "erased"
                    : (r == meshroute::InboxEraseResult::not_found) ? "not_found"
                                                                    : "io_error";
    char ab[80]; const size_t n = meshroute::console::write_inbox_deleted(ab, sizeof ab, kstr, seq, res);
    if (n) out.write(ab, n);
}

// §CUSTODY-D (spec §7.5) — `clear_inbox confirm`. Deliberately THIN: every decision it makes is one of
//   (a) `mrfw::parse_confirm_token` (firmware_config_parse.h) — the safety interlock,
//   (b) `Inbox::clear()` (lib/core)                            — the orchestration + the persistence order,
//   (c) `console::inbox_clear_result` + the two writers        — the frozen ack family,
// all three of which the native suite compiles, because THIS function is not (§B115: `src/*.cpp` is outside the
// native build).
// ⛔⛔ CORRECTED IN PLACE 2026-08-31 (QG HOLD). THIS COMMENT USED TO END: *"What is left here is plumbing, so there
//    is nothing in the §B115 blind spot to get wrong."* THAT WAS FALSE, and it was the most expensive kind of
//    false — a claim that a gap needed no gate. The plumbing IS the destructive command: whether the interlock is
//    consulted, whether the refusal returns before `clear()`, whether the real verdict reaches the lexeme, and
//    whether `dispatch()` routes here at all are FOUR decisions, all of them in the blind spot, and every gate the
//    slice shipped would have stayed green with any of the four broken.
// ✅ WHAT GATES IT NOW: `tools/probe_inbox_verbs/run.sh` host-links THIS TU together with the real
//    `firmware_commands.cpp` and drives `mrfw::dispatch()`, pinning the exact ack bytes on every arm plus
//    "the store was not touched" on a refusal. Its controls C1..C7 are the tempting wrong shapes, each RED.
// ⇒ ⛔ IF YOU EDIT THIS FUNCTION, RUN THAT PROBE. It is the only thing that compiles it.
// ⛔ THE REFUSAL RETURNS BEFORE `clear()` IS EVEN NAMED — and that is now a MEASURED property (probe W2b/W2c:
//    `wipe_calls == 0` and no high-water persist), not a structural assertion about the shape of the code.
void handle_clear_inbox(const char* args, size_t n, Print& out) {
    if (!parse_confirm_token(args, n)) {
        char cb[64]; const size_t m = meshroute::console::write_inbox_needs_confirm(cb, sizeof cb);
        if (m) out.write(cb, m);
        return;                                           // ⛔ NO state is read and nothing is touched
    }
    meshroute::Inbox& ib = g_node.inbox();
    const bool cleared = ib.clear();
    // ★ The high-waters are read AFTER the clear precisely because §7.5.3 says they SURVIVE it — printing them is
    //   what makes the preservation observable to the operator and the companion on the spot.
    char ab[112]; const size_t m = meshroute::console::write_inbox_cleared(
        ab, sizeof ab, ib.storage_epoch(), ib.dm_newest_seq(), ib.chan_newest_seq(),
        meshroute::console::inbox_clear_result(cleared));
    if (m) out.write(ab, m);
}

}  // namespace mrfw
