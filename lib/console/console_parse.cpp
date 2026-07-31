// MeshRoute — lib/console/console_parse.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#include "console_parse.h"
#include "frame_codec.h"   // DATA_FLAG_E2E_ACK_REQ (the canonical wire flag the RX acts on)
#include <cstring>

namespace meshroute::console {

// Tok + parse_u32_tok have EXTERNAL linkage (hoisted out of the anonymous namespace, §3-A.7): the L2 overflow-guard
// unit test drives parse_u32_tok directly now that parse_cfg (its only externally-reachable driver) is deleted.
struct Tok { const char* s; size_t n; };
// Parse a decimal token into [0,max]; false on empty/non-digit/overflow.
// NB: check overflow BEFORE the multiply/add — the post-multiply `v > max` guard is inert when
// max == 0xFFFFFFFF (a u32 accumulator can never exceed it), so `4294967296` would wrap to 0 and
// silently "parse" instead of failing. Guarding on `v > (max - digit) / 10` catches the wrap.
bool parse_u32_tok(const Tok& t, uint32_t max, uint32_t& out);

namespace {

struct Scan { const char* p; const char* end; };
void skip_ws(Scan& s) { while (s.p < s.end && (*s.p == ' ' || *s.p == '\t')) ++s.p; }

// Reads a non-space token; returns {start,len}. len==0 at end-of-line.
Tok token(Scan& s) {
    skip_ws(s);
    const char* b = s.p;
    while (s.p < s.end && *s.p != ' ' && *s.p != '\t') ++s.p;
    return { b, static_cast<size_t>(s.p - b) };
}
bool tok_eq(const Tok& t, const char* lit) {
    return t.n == std::strlen(lit) && std::memcmp(t.s, lit, t.n) == 0;
}
}  // namespace

bool parse_u32_tok(const Tok& t, uint32_t max, uint32_t& out) {
    if (t.n == 0) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < t.n; ++i) {
        char c = t.s[i];
        if (c < '0' || c > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(c - '0');
        if (v > (max - digit) / 10) return false;      // would exceed max (or wrap the u32) -> reject
        v = v * 10 + digit;
    }
    out = v;
    return true;
}

namespace {   // the remaining parse helpers stay internal (TU-local)

// Parse up to 8 hex digits into a u32; false on empty/non-hex/overflow (>8 digits).
bool parse_hex32_tok(const Tok& t, uint32_t& out) {
    if (t.n == 0 || t.n > 8) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < t.n; ++i) {
        char c = t.s[i]; uint32_t d;
        if (c >= '0' && c <= '9')      d = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + static_cast<uint32_t>(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + static_cast<uint32_t>(c - 'A');
        else return false;
        v = (v << 4) | d;
    }
    out = v;
    return true;
}

// Parse a 0x-PREFIXED hex u32 (`0x` + 1..8 hex digits) -> out. false if the `0x`/`0X` prefix is absent or the hex is
// bad. Requiring the prefix KILLS the id-vs-hash ambiguity: a bare decimal is always an id, `0x…` is always a hash.
bool parse_hex32_0x(const Tok& t, uint32_t& out) {
    if (t.n < 3 || t.s[0] != '0' || (t.s[1] != 'x' && t.s[1] != 'X')) return false;
    Tok sub{ t.s + 2, t.n - 2 };
    return parse_hex32_tok(sub, out);
}

// Decode EXACTLY 2*n hex chars into out[0..n). false on a wrong length or a non-hex char (e.g. peerkey's 64-hex ed_pub).
bool parse_hex_bytes_tok(const Tok& t, uint8_t* out, size_t n) {
    if (t.n != 2 * n) return false;
    auto nib = [](char c, uint8_t& d) -> bool {
        if (c >= '0' && c <= '9') { d = static_cast<uint8_t>(c - '0');      return true; }
        if (c >= 'a' && c <= 'f') { d = static_cast<uint8_t>(10 + c - 'a'); return true; }
        if (c >= 'A' && c <= 'F') { d = static_cast<uint8_t>(10 + c - 'A'); return true; }
        return false;
    };
    for (size_t i = 0; i < n; ++i) {
        uint8_t hi, lo;
        if (!nib(t.s[2 * i], hi) || !nib(t.s[2 * i + 1], lo)) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

// §2 send cleanup: parse the trailing `[-a] [-e] "<body>"` (flags + a QUOTED body, in ANY order) from the scan cursor.
// `-a`=ack, `-e`=encrypt — each gated by allow_a/allow_e (an off-target flag is an error). The body is the verbatim
// text between the quotes (spaces allowed). Returns false on: a disallowed/unknown flag, an unquoted token, an
// unterminated/duplicate quote, or no body at all (the body is required).
bool parse_send_tail(Scan& s, bool allow_a, bool allow_e, bool& ack, bool& enc,
                     const uint8_t*& body, uint8_t& body_len, bool* team = nullptr, bool* no_intro = nullptr,
                     bool* global = nullptr, bool* loc = nullptr) {
    ack = false; enc = false; if (team) *team = false; if (no_intro) *no_intro = false; if (global) *global = false; if (loc) *loc = false; body = nullptr; body_len = 0; bool body_seen = false;
    for (;;) {
        skip_ws(s);
        if (s.p >= s.end) break;
        if (*s.p == '"') {                                       // the quoted body
            if (body_seen) return false;                         // two bodies
            ++s.p;
            const char* b = s.p;
            while (s.p < s.end && *s.p != '"') ++s.p;
            if (s.p >= s.end) return false;                      // unterminated quote
            size_t n = static_cast<size_t>(s.p - b);
            if (n > protocol::max_payload_bytes_hard_cap) n = protocol::max_payload_bytes_hard_cap;
            body = reinterpret_cast<const uint8_t*>(b); body_len = static_cast<uint8_t>(n);
            body_seen = true;
            ++s.p;                                               // past the closing quote
        } else if (*s.p == '-') {                                // a flag: -a / -e (lone, single-char)
            ++s.p;
            if (s.p >= s.end) return false;
            const char f = *s.p; ++s.p;
            if (s.p < s.end && *s.p != ' ' && *s.p != '\t') return false;   // must be a lone token
            if      (f == 'a') { if (!allow_a) return false; ack = true; }
            else if (f == 'e') { if (!allow_e) return false; enc = true; }
            else if (f == 't') { if (!team) return false; *team = true; }   // §6.4: -t = TEAM plane (send + §S7 send_channel; send_layer rejects it)
            else if (f == 'g') { if (!global) return false; *global = true; }   // §S7 T-B: -g = explicit GLOBAL plane (send_channel only; `-t -g` => BOTH)
            else if (f == 'K') { if (!no_intro) return false; *no_intro = true; }   // §D1: -K = suppress the INTRO first-contact attach for this send (send/send_layer only; a no-op on a sealed send)
            // ★★ §loc-per-send (2026-07-31, register B0): -l = attach THIS node's position to THIS message
            // (DATA_FLAG_LOCATION). Replaces `cfg set loc_dm`, a global toggle that attached the position to every DM on
            // a size check alone — with no crypt gate — so a plaintext DM aired coordinates in the clear. Per-send means
            // an ordinary `send` is untouched and a refusal is attributable to the one message that asked for a position.
            // Accepted on `send` and `send_layer`; ✖ MISSING on `send_channel` — TRIGGER: CL2. ⚠ V1: the reason given
            // here until 2026-07-31 was *"there a location is an alternative inner TYPE … T-K5's job"*, and the OWNER
            // STRUCK IT (spec §2.2.1): `send_channel -t -l -e` — text AND position in one sealed post — is the target.
            // What actually blocks it is a PAYLOAD FORMAT, not a flag meaning: T-K2's sealed channel inner is
            // `[inner_type u8][payload]` with 0=text XOR 1=location, which cannot express BOTH. CL2 must make that byte
            // a FLAGS byte (bit0 text, bit1 location) carrying `pack_loc6` — after which `-l` lands here unchanged.
            // Adding `-l` before CL2 would have nothing to seal into, and an unsealed channel location is the very leak
            // this arc closed on the DM plane (register B0).
            // `send_layer -l` parses but on_command REFUSES it (no cross-layer builder can carry a position), so the
            // operator gets an explanation instead of a bare bad_args.
            else if (f == 'l') { if (!loc) return false; *loc = true; }
            else return false;                                   // unknown flag
        } else {
            return false;                                        // unquoted text -> error (body must be quoted)
        }
    }
    return body_seen;                                            // the quoted body is mandatory
}

}  // namespace

ParseErr parse_command(const char* line, size_t len, Command& out) {
    Scan s{ line, line + len };
    Tok verb = token(s);
    if (verb.n == 0) return ParseErr::empty;

    //   resolve <hash> [hard] — diagnostic hash-locate (H flood); the answer arrives async via hash_resolved.
    if (tok_eq(verb, "resolve")) {
        Tok arg = token(s);
        uint32_t hash = 0;
        if (!parse_hex32_0x(arg, hash)) return ParseErr::bad_args;   // hash MUST be 0x-prefixed
        Tok opt = token(s);
        const bool hard = (opt.n != 0) && tok_eq(opt, "hard");
        if (opt.n != 0 && !hard) return ParseErr::bad_args;        // the only valid 2nd arg is `hard`
        out = Command{};
        out.kind = CmdKind::resolve;
        out.u.resolve.dst_hash = hash;
        out.u.resolve.hard     = hard;
        return ParseErr::ok;
    }

    //   peerkey <ed_pub hex64> ["<name>"] — §3: install a scanned peer's full pubkey as a PINNED (verified) key.
    //   hash = ed_pub[:4]. ★ §AB2 (spec §2.3): the trailing QUOTED name is the OPTIONAL one-shot for the QR-import flow
    //   where the key and the label arrive together; it lands in the same peer_key_set name parameter `peername` uses.
    //   ⚠ GRAMMAR DEVIATION, reported not hidden: the spec writes `name="<text>"`. A `key=value` scanner does exist —
    //   kv_next — but it lives in src/firmware_config_parse.h, a DEVICE-layer header this library must not include, so
    //   honouring that spelling here would mean forking kv_next into lib/console (the exact U1 rot that header's own
    //   line-207 note already complains about in the other direction). A bare quoted tail reuses parse_send_tail
    //   verbatim and matches `send` / `send_channel` / `peername` — one grammar per library.
    if (tok_eq(verb, "peerkey")) {
        Tok arg = token(s);
        uint8_t ed[32];
        if (!parse_hex_bytes_tok(arg, ed, 32)) return ParseErr::bad_args;
        out = Command{};
        out.kind = CmdKind::peerkey;
        for (int i = 0; i < 32; ++i) out.u.peerkey.ed_pub[i] = ed[i];
        skip_ws(s);
        if (s.p < s.end) {                                   // the name is OPTIONAL -> only parsed when something follows
            bool ack = false, enc = false;
            if (!parse_send_tail(s, /*allow_a=*/false, /*allow_e=*/false, ack, enc, out.body, out.body_len))
                return ParseErr::bad_args;                    // an unquoted token, a stray flag, or an unterminated quote
            if (out.body_len == 0) return ParseErr::bad_args;  // `peerkey <hex64> ""` — same rule as peername below
        }
        return ParseErr::ok;                                 // over-cap names are refused by on_command (err_too_large -> "too_long"), not here
    }

    //   ★ §AB2 (spec 2026-07-29 §2.3): peername 0x<hash> "<text>" — set/overwrite a CACHED peer's name, without
    //   touching its key or its confidence. Chosen over extending `peerkey` because rename-WITHOUT-rekey is the common
    //   case (a peer advertises the default `MeshRoute node: 0x…` and the operator wants a real label), and with
    //   `peerkey` alone that would mean re-sending the whole 64-hex pubkey to change a string.
    //   C2 refusals: the hash MUST be 0x-prefixed and non-zero (parse_hex32_0x — the same rule that kills the
    //   id-vs-hash ambiguity everywhere in this file); the name MUST be a non-empty quoted token. An unknown hash and an
    //   over-cap name are on_command's to refuse, so the app gets `unknown_hash` / `too_long` instead of a flat
    //   `bad_args` — a distinction the operator needs, since the two have different remedies (reqpubkey vs shorten).
    //   ⚠ AN EMPTY NAME IS REFUSED, NOT TREATED AS "CLEAR". v1 has no clear-the-name operation and inventing one as a
    //   side effect of `peername 0x… ""` would let a UI that passes an empty string by accident silently wipe a label.
    //   If clearing is wanted it should be its own explicit, agreed spelling.
    if (tok_eq(verb, "peername")) {
        uint32_t hash = 0;
        if (!parse_hex32_0x(token(s), hash) || hash == 0) return ParseErr::bad_args;
        bool ack = false, enc = false; const uint8_t* body = nullptr; uint8_t blen = 0;
        if (!parse_send_tail(s, /*allow_a=*/false, /*allow_e=*/false, ack, enc, body, blen)) return ParseErr::bad_args;
        if (blen == 0) return ParseErr::bad_args;
        out = Command{};
        out.kind = CmdKind::peername;
        out.u.peername.key_hash32 = hash;
        out.body = body; out.body_len = blen;                // BORROWED into `line`, exactly like send's body
        return ParseErr::ok;
    }

    //   reqpubkey <hash> — §6: user-triggered on-air pubkey request (a HARD WANT_PUBKEY H flood). The only auto-source.
    if (tok_eq(verb, "reqpubkey")) {                            // reqpubkey <0xhash|team-id> — 0x-hex => key_hash32; bare decimal <=254 => a teammate's team_local_id (resolve the hash from the team key cache)
        Tok arg = token(s);
        uint32_t hash = 0, id = 0;
        out = Command{};
        out.kind = CmdKind::reqpubkey;
        if (parse_hex32_0x(arg, hash) && hash != 0) {                 // 0x-hex -> a key_hash32
            out.u.resolve.dst_hash = hash; out.u.resolve.dst_id = 0;
        } else if (parse_u32_tok(arg, 254u, id) && id != 0) {         // decimal <=254 -> a team_local_id
            out.u.resolve.dst_hash = 0; out.u.resolve.dst_id = static_cast<uint8_t>(id);
        } else return ParseErr::bad_args;
        // §6.4 HARD SPLIT: optional trailing -t = TEAM plane (team-scoped pubkey req -> origin=team_local_id, answer via _rt_team).
        // A bare team-id target is implicitly TEAM; else default GLOBAL (via home/static). Consistent with `send`.
        bool team = (out.u.resolve.dst_id != 0);
        skip_ws(s);
        if (s.p < s.end) { Tok fl = token(s); if (tok_eq(fl, "-t")) team = true; else return ParseErr::bad_args; }
        out.u.resolve.plane = team ? 1 /*TEAM*/ : 2 /*GLOBAL*/;
        return ParseErr::ok;
    }

    //   §2 send cleanup — 3 orthogonal verbs, QUOTED body, -a (ack) / -e (encrypt) flags in ANY order. HARD SWITCH:
    //   the old send_ack/sendhash/sendhash_ack/sendhashx/sendhashx_ack/send_layer_ack verbs are GONE (-> unknown_verb).
    //   send <id|0xhash> "<text>" [-a] [-e] [-t] [-l]   — id (<=254 dec) vs hash (0x-prefixed); -e=crypt (hash only); §6.4 -t=TEAM plane, plain=GLOBAL/home (fail if no home); §loc-per-send -l=attach position (REFUSED unless the DM is sealed)
    //   send_channel <ch> "<text>" [-t] [-g] [-e]  — channel gossip; §S7 -t=TEAM/-g=GLOBAL/`-t -g`=BOTH/plain=GLOBAL; §chan-crypt -e=seal to the team content key (`-t -e` only — the other two `-e` forms REFUSE in on_command). No -a (O3), no -l (CL2).
    //   send_layer <0xhash> <l1,l2,…> "<text>" [-a] [-e] — explicit cross-layer path; -e = sealed (DATA_TYPE_SEALED_RELAY); -l parses but is REFUSED (cross-layer carries no position)
    {
        const bool is_send    = tok_eq(verb, "send");
        const bool is_channel = tok_eq(verb, "send_channel");
        const bool is_layer   = tok_eq(verb, "send_layer");
        if (!is_send && !is_channel && !is_layer) return ParseErr::unknown_verb;

        if (is_channel) {                                       // §S7 send_channel <ch> "<text>" [-t] [-g] [-e] — -t=TEAM, -g=explicit GLOBAL, `-t -g`=BOTH, plain=GLOBAL; §chan-crypt -e=seal to the team key
            uint32_t ch = 0;
            if (!parse_u32_tok(token(s), 255u, ch)) return ParseErr::bad_args;
            bool ack = false, enc = false, team = false, global = false; const uint8_t* body = nullptr; uint8_t blen = 0;
            // ★★ §chan-crypt CL1 (spec 2026-07-30 §2.1/§2.2): `-e` IS NOW ACCEPTED. It was `allow_e=false`, so the team
            // content key that §team-ch-key T-K1 shipped had NO way to be used on a channel post at all — every team
            // channel message went in clear, on a shared PHY, to anyone in range.
            // ⚠ ACCEPTED HERE ≠ HONOURED: the four-case matrix lives in on_command (node.cpp), because that is the seam
            // ALL THREE producers of CmdKind::send_channel pass — this parser, `testch`'s hand-built Command
            // (src/fw_main.cpp), and the simulator's NodeRuntimeWrapper. A refusal spelled as a ParseErr would protect
            // typed console lines only. `-e` without `-t`, and `-t -g -e`, both REFUSE there (`err_unsupported` +
            // send_failed{unsealable}); `-t -e` refuses too until CL2 builds the seal. So this is never a silent
            // cleartext downgrade in any combination.
            // `-a` stays REFUSED (allow_a=false): open decision O3 — a channel post has no single recipient to ack, and
            // QA recorded NO so it is not re-asked. `-l` stays REFUSED (loc=nullptr) — see the `f == 'l'` arm above.
            if (!parse_send_tail(s, /*allow_a=*/false, /*allow_e=*/true, ack, enc, body, blen, &team, /*no_intro=*/nullptr, &global)) return ParseErr::bad_args;
            out = Command{};
            out.kind = CmdKind::send_channel;
            out.u.channel.channel_id = static_cast<uint8_t>(ch);
            out.u.channel.team = team; out.u.channel.global = global;
            out.body = body; out.body_len = blen;
            out.crypt = enc ? CryptIntent::on : CryptIntent::def;   // §chan-crypt, same rule as `send`/`send_layer`: -e => sealed-or-refused; absent => `def` (byte-identical to the pre-CL1 hardcoded `def`)
            return ParseErr::ok;
        }

        if (is_layer) {                                        // send_layer <0xhash> <l1,l2,…> "<text>" [-a] [-e] [-K]
            uint32_t h = 0;
            if (!parse_hex32_0x(token(s), h) || h == 0) return ParseErr::bad_args;   // <0xhash>: key_hash32, nonzero, 0x-prefixed
            Tok ptok = token(s);
            if (ptok.n == 0) return ParseErr::bad_args;                               // <l1,l2,…> required (no empty path)
            out = Command{};
            out.kind = CmdKind::send_layer;
            out.u.layer.dst_hash = h; out.u.layer.hop_count = 0;
            // comma-separated decimal layer ids -> hops[]; cap at gw_env_max_hops-1 (originate_layer_path prepends ours).
            uint32_t v = 0; bool digit = false;
            for (size_t i = 0; i < ptok.n; ++i) {
                const char ch = ptok.s[i];
                if (ch == ',') {
                    if (!digit || v == 0 || v > 255) return ParseErr::bad_args;
                    if (out.u.layer.hop_count >= protocol::gw_env_max_hops - 1) return ParseErr::bad_args;
                    out.u.layer.hops[out.u.layer.hop_count++] = static_cast<uint8_t>(v);
                    v = 0; digit = false;
                } else if (ch >= '0' && ch <= '9') {
                    v = v * 10 + static_cast<uint32_t>(ch - '0');
                    if (v > 255) return ParseErr::bad_args;
                    digit = true;
                } else {
                    return ParseErr::bad_args;
                }
            }
            if (!digit || v == 0 || v > 255) return ParseErr::bad_args;
            if (out.u.layer.hop_count >= protocol::gw_env_max_hops - 1) return ParseErr::bad_args;
            out.u.layer.hops[out.u.layer.hop_count++] = static_cast<uint8_t>(v);
            bool ack = false, enc = false, no_intro = false, loc = false; const uint8_t* body = nullptr; uint8_t blen = 0;
            // ★ §xl-crypt-intent (2026-07-29): `-e` IS ACCEPTED HERE. It was `allow_e=false`, so the console could not
            // ask for a sealed cross-layer DM at all while the SIM could (`send_layerx`) — a metal-vs-sim divergence on a
            // CONFIDENTIALITY feature. It is NOT a no-op: on_command's send_layer already seals a want_crypt send into a
            // DATA_TYPE_SEALED_RELAY (node.cpp §S4), and fails LOUD (err_unsupported / send_failed{no_pubkey|…}) when it
            // cannot — so `-e` here is sealed-or-refused, never a cleartext downgrade. Target ALWAYS a 0x-hash, so there
            // is no id-target carve-out to make (unlike `send`, where allow_e is by_hash).
            if (!parse_send_tail(s, /*allow_a=*/true, /*allow_e=*/true, ack, enc, body, blen, /*team=*/nullptr, /*no_intro=*/&no_intro,
                                 /*global=*/nullptr, /*loc=*/&loc)) return ParseErr::bad_args;
            // §loc-per-send: `-l` is ACCEPTED here and REFUSED by on_command (err_unsupported + send_failed{unsealable}),
            // not rejected as a parse error — a cross-layer frame genuinely cannot carry a position and the operator
            // deserves that explanation rather than "bad args". Threading it (instead of dropping it at the parser) is
            // also what keeps ONE meaning for the letter across the verbs.
            out.u.layer.flags = static_cast<uint8_t>((ack ? DATA_FLAG_E2E_ACK_REQ : 0) | (loc ? DATA_FLAG_LOCATION : 0));
            out.no_intro = no_intro;   // §D1 `-K`
            out.body = body; out.body_len = blen;
            out.crypt = enc ? CryptIntent::on : CryptIntent::def;   // §8b, same rule as `send`: -e => CRYPTED; absent => the node's e2e_dm default
            return ParseErr::ok;
        }

        // send <id|0xhash> "<text>" [-a] [-e]: `0x…` => key_hash32; a bare decimal <=254 => id (no ambiguity).
        Tok arg = token(s);
        uint32_t h = 0, id = 0; bool by_hash = false;
        if (parse_hex32_0x(arg, h))            by_hash = true;              // 0x-hex -> hash
        else if (parse_u32_tok(arg, 254u, id)) by_hash = false;            // bare decimal <=254 -> id
        else return ParseErr::bad_args;
        if (by_hash && h == 0) return ParseErr::bad_args;   // `send 00000000`: an all-zero hash would fall through to a unicast to reserved id 0 (mirror send_layer's h==0 guard)
        bool ack = false, enc = false, team = false, no_intro = false, loc = false; const uint8_t* body = nullptr; uint8_t blen = 0;
        if (!parse_send_tail(s, /*allow_a=*/true, /*allow_e=*/by_hash, ack, enc, body, blen, &team, &no_intro,
                             /*global=*/nullptr, /*loc=*/&loc)) return ParseErr::bad_args;  // -e only on a hash target; -t = team plane; -K = suppress INTRO attach; -l = attach position
        out = Command{};
        out.kind = CmdKind::send;
        out.u.send.dst_id   = by_hash ? 0 : static_cast<uint8_t>(id);
        out.u.send.dst_hash = by_hash ? h : 0u;            // on_command routes dst_hash!=0 to send_by_hash
        // §loc-per-send `-l`: DATA_FLAG_LOCATION rides the EXISTING flags word all the way to enqueue_data (no signature
        // change anywhere) — accepted on BOTH the id and hash forms. It is validated there, and the send is REFUSED if the
        // DM would not be sealed / there is no fix / the +6 B does not fit. Note `-l` is allowed WITHOUT `-e` on an id
        // target (where `-e` is not even accepted): a node with `e2e_dm` on seals by default, so `send 5 "…" -l` is the
        // normal sealed case there, and on an e2e_dm-off node it refuses loudly — which is the rule, not a limitation.
        out.u.send.flags    = static_cast<uint8_t>((ack ? DATA_FLAG_E2E_ACK_REQ : 0) | (loc ? DATA_FLAG_LOCATION : 0));
        out.u.send.plane    = team ? 1 /*TEAM*/ : 2 /*GLOBAL*/;   // §6.4 HARD SPLIT: -t => team-only; plain send => global/home (fails loud if no home)
        out.no_intro = no_intro;   // §D1 `-K`: suppress the INTRO attach for this send (accepted on a sealed send too -> harmless no-op)
        out.body = body; out.body_len = blen;
        out.crypt = enc ? CryptIntent::on : CryptIntent::def;   // -e => CRYPTED; absent => the node's e2e_dm default (force-plain dropped)
        return ParseErr::ok;
    }
}

// parse_cfg DELETED (§3-A.7, 2026-07-21): zero production callers, test-maintained, with a DIVERGENT key set and
// validation from the live `cfg set` handler (src/firmware_config.cpp handle_cfg) — removing beats maintaining a
// lying twin. The parse_u32_tok overflow guard it exercised is now unit-tested directly (test_console_parse.cpp).

}  // namespace meshroute::console
