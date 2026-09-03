// MeshRoute — src/firmware_custody_push.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §B278 S4 (design `docs/superpowers/specs/2026-09-01-b278-mobile-custody-feedback-design.md` §8.4, brief S4-4) —
// **THE OPERATOR-FACING CUSTODY LINE, DIRECT AND TRANSLATED, AS ONE EXECUTABLE FUNCTION.**
//
// ★★★ WHY IT IS A FILE OF ITS OWN, and it is the §B115 rule this `src/` cluster states at this spot nine times:
//     `src/fw_main.cpp` is compiled by NEITHER the native suite (`test_build_src = no`) NOR the simulator, so a
//     decision taken inside its push switch is a decision no automated gate can execute. Until S4 the custody arm
//     was three facts and a fixed sentence, and "the glue makes no decisions" was arguable. S4 adds a MODE
//     SELECTION (direct vs translated), a KIND selection (`target_id` vs `target_hash`) and a second fail-loud
//     arm — three decisions — so the renderer moves HERE, where `tools/probe_custody_usb/` compiles and RUNS it
//     against the real codec through the real Arduino `Print` seam.
//     ⛔ `fw_main.cpp`'s `case PushKind::custody_failure` now calls this ONCE and keeps no parsing, no target
//        selection and no wording of its own. That is a structural pin in the probe's runner, and it is only the
//        NECESSARY half — the behavioural half is the executed golden lines.
//
// ★★ WHAT THE LINE MAY AND MAY NOT SAY (design §14.3, verbatim: *"No output may call it a NACK or claim
//    non-delivery"*). The wording is about CUSTODY TRANSFER at a named relay and says nothing about the
//    destination: the DATA may well have arrived, another path may have delivered a copy, and an E2E ack may
//    still land. `E2E-ACKED` remains the only line on this console that means delivery. The trailing warning is
//    part of the contract and is printed on EVERY arm that prints a report at all.
//
// ⛔ ONE CODEC (§9.2) — `parse_custody_failure` + `parse_custody_translated_tail`, the SAME two calls the receiver
//    and both JSON emitters make. ⛔ No offset indexing of `pu.body` anywhere in this file.
// ⛔ ONE NAME TABLE for `stage`/`reason` — `console_json.cpp`'s (U1), reached through `console_json.h`.
//    The target-kind word is the one string this file spells itself, because its table is TU-local to
//    `console_json.cpp` and a `lib/` header may not be grown from `src/`; `tools/probe_custody_usb/` therefore
//    compiles BOTH renderers and asserts at run time that the two words agree for the same record.
//
// ⓘ FIELD NAMES AND VALUE TYPES ARE THE JSON ONES (§8.4): `delegated` · `target_kind` · exactly one of
//   `target_id` (a number) / `target_hash` (the canonical 8-digit lower-case hex `console_json.cpp`'s
//   `key_hex32` writes) · `mobile_ctr`. ⛔ No `via_home`, no `home_ctr`, and `ctr=` keeps meaning ctrH.
//
// DEVICE-layer header (needs `Print`). Pure otherwise: no globals, no Serial, no node state.
#pragma once
#include <Arduino.h>        // Print / F()
#include <cstdio>           // snprintf — the hash form
#include <cstdint>
#include <optional>
#include <span>
#include "frame_codec.h"    // the ONE custody codec + `custody_record_is_translated`
#include "command.h"        // meshroute::Push
#include "console_json.h"   // custodystage_name / custodyreason_name — the ONE name table (U1)

namespace mrfw {

// The §8.4 target word. ⛔ It must stay identical to `console_json.cpp`'s `custodytarget_name`, and that identity
// is MEASURED (`tools/probe_custody_usb/` renders both for the same record and compares), not asserted here.
// Takes the ENUM, so -Wswitch fails the build if §6.2 ever grows a third kind.
inline const char* custody_target_word(meshroute::CustodyTranslatedTargetKind k) {
    switch (k) {
        case meshroute::CustodyTranslatedTargetKind::node_id:  return "node_id";
        case meshroute::CustodyTranslatedTargetKind::key_hash: return "hash";
    }
    return "invalid";   // ⛔ the sentinel, never a neighbour; the codec has already refused anything else
}

// ★ THE ONE RENDERER. `out` is the `Print&` seam `dispatch()` already uses, so the firmware passes `mrcon` and the
// probe passes its own `Print` — the SAME code path, not a parallel one.
inline void print_custody_failure(Print& out, const meshroute::Push& pu) {
    const std::optional<meshroute::CustodyFailureRecord> cf =
        meshroute::parse_custody_failure(std::span<const uint8_t>(pu.body, pu.body_len));
    // C2: loud; structurally unreachable (the receiver validated these bytes with this function before pushing).
    if (!cf) { out.println(F("CUSTODY FAILURE (unparseable record)")); return; }
    // ⓘ `origin=` is the FAILED DATA's origin. On a DIRECT report §13.11 has already proven that is THIS node; on
    //   a TRANSLATED one it is the HOME that originated the failed flight on this mobile's behalf. It is printed
    //   in both cases because an operator reading a log needs the report to be self-contained.
    out.print(F("CUSTODY FAILURE reporter=")); out.print(pu.origin);
    out.print(F(" layer="));  out.print(cf->reporter_layer);
    out.print(F(" origin=")); out.print(cf->failed_origin);
    out.print(F(" dst="));    out.print(cf->failed_dst);
    out.print(F(" ctr="));    out.print(cf->failed_ctr);
    out.print(F(" stage="));  out.print(meshroute::console::custodystage_name(
                                  meshroute::custody_stage_of_flags(cf->notice_flags)));
    out.print(F(" reason=")); out.print(meshroute::console::custodyreason_name(cf->terminal_reason));
    out.print(F(" prev="));   out.print(cf->previous_hop);
    out.print(F(" next="));   out.print(cf->failed_next_hop);
    out.print(F(" repair=")); out.print((cf->notice_flags & meshroute::CUSTODY_FLAG_REPAIR_ATTEMPTED)
                                        ? F("attempted") : F("none"));
    out.print(F(" one_way=")); out.print((cf->notice_flags & meshroute::CUSTODY_FLAG_NEXT_WAS_ONE_WAY) ? 1 : 0);
    // ---- §B278 S4 / §8.4: the TRANSLATED additions, appended. ⛔ A direct record has bit 6 clear, so this block
    //      does not run and the line above is the pre-S4 line, character for character.
    if (meshroute::custody_record_is_translated(cf->notice_flags)) {
        const std::optional<meshroute::CustodyTranslatedTail> t =
            meshroute::parse_custody_translated_tail(std::span<const uint8_t>(pu.body, pu.body_len), *cf);
        if (!t) {
            // C2, mirroring the JSON emitter's `"error":"unparseable_tail"` — structurally unreachable, because
            // `parse_custody_failure` validated this record THROUGH this same reader. Said out loud rather than
            // rendered as an ordinary (direct-looking) report, which is the "a success that isn't" shape.
            out.print(F(" error=unparseable_tail"));
        } else {
            out.print(F(" delegated=true"));
            out.print(F(" target_kind=")); out.print(custody_target_word(t->target_kind));
            if (t->target_kind == meshroute::CustodyTranslatedTargetKind::key_hash) {
                // ⛔ EXACTLY ONE target field. The hex form is `console_json.cpp`'s `key_hex32` form — the same
                //    `"%08x"` (lower case, 8 digits, zero-padded) — so an operator and the companion read the
                //    same string. The probe compares them character for character.
                char hex[9];
                std::snprintf(hex, sizeof hex, "%08x", static_cast<unsigned>(t->target_value));
                out.print(F(" target_hash=")); out.print(hex);
            } else {
                out.print(F(" target_id=")); out.print(static_cast<unsigned long>(t->target_value));
            }
            out.print(F(" mobile_ctr=")); out.print(t->mobile_ctr);   // ctrM; `ctr=` above stays ctrH (§8.4)
        }
    }
    if (pu.seq) { out.print(F(" seq=")); out.print(pu.seq); }   // omitted when storage is disabled, matching JSON
    out.println(F(" — the relay could not complete onward custody; NOT proof the destination missed it (an e2e ack may still arrive)"));
}

}  // namespace mrfw
