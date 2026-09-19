// MeshRoute — src/firmware_help.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★ THE CONSOLE HELP AUTHORITY — the BARE PRIMARY-COMMAND INDEX, the one bounded refusal for every argument-bearing
// form, and the ONE recognition of `help` / `?`. Remote-admin v2 slice 0g, the owner's ruling (2026-09-05):
//
//     "We need to fix help, and I'd do that quite dramatically - by removing any description, keeping bare command
//      name - we will keep full help in manual." […] "Agree - primary verbs only."
//
// ⇒ bare `help` and bare `?` print ONE LINE PER PRIMARY COMMAND NAME compiled into this build, bytewise sorted, and
//   then the manual pointer. Nothing else: no flag, no argument, no sub-verb, no topic, no description, no grouping.
//   `docs/manual/command-reference.md` is the SOLE detailed reference, and the last line of every response says so.
//
// ★ THIS SUPERSEDES THE CONTENT HALF OF B208's 2026-08-17 RULING. The nine `help <topic>` sections and the topic
//   index are RETIRED — `help <anything>` is now a retired form that takes the bounded refusal below and never
//   renders a section. B208's size/no-drop/no-pager/no-bypass half STANDS and is measured, unchanged, by the probe.
//
// ★ WHY THE LIST LIVES IN A HEADER OF ITS OWN AND NOT IN firmware_commands.cpp, WHERE IT USED TO. That TU cannot be
//   host-compiled — it needs g_node, mrnv, the JSON writers, RadioLib and the whole Arduino world — and
//   `platformio.ini`'s `test_build_src = no` means NO native target compiles `src/` either. This header depends on
//   `Print` and two feature macros and NOTHING else, so `tools/probe_console_sink/` compiles the REAL file, renders
//   every response through the REAL `src/console_sink.h` GuardedConsole, and compares the emitted names against the
//   INDEPENDENTLY DERIVED command inventory (`tools/gen_command_inventory.py`, which scans the real dispatchers).
//   ⇒ ⛔ DO NOT reach for `g_node`, NV, radio state, `Serial`, `mrcon`, dynamic storage or a command handler from
//   this file: each one re-welds the list to the un-compilable TU and kills the gate.
//
// ★★ THIS FILE IS A PRESENTATION COPY, NOT THE COMPLETENESS AUTHORITY. The generator projects the primary verbs out
//   of the REAL dispatch arms (`src/firmware_commands.cpp::dispatch`, `src/firmware_help.h::help_command` and
//   `lib/console/console_parse.cpp::parse_command`) and the gate proves this list equals that projection IN BOTH
//   DIRECTIONS, per product profile. ⇒ adding or removing a command changes the expected help list automatically;
//   a name added here that no dispatcher accepts, or a command that never reaches this list, REDDENS the gate.
//   ⛔ Never "fix" a gate failure by editing this list alone — the projection is the side that read the source.
//
// ★ EVERYTHING IS COMPILE-TIME. Availability is `#if`, never a runtime table of `const char*` — a table would put
//   the whole list in RAM on every board to save an if-chain that costs nothing. Each gated name carries the EXACT
//   condition its dispatch arm carries (`mobile` = `MR_N_LAYERS < 2 && MR_FEAT_MOBILE`, `team` = `MR_N_LAYERS < 2`,
//   `remote` = `MR_FEAT_RADMIN_CLIENT`, `ui` = `MR_FEAT_OLED`), so the index can never advertise a
//   family this build refuses, nor hide one it accepts. ⛔ The two `MR_HELP_HAS_*` macros of slice 0a are RETIRED:
//   they existed to gate topic sections, and a second spelling of a handler's gate is one more thing to drift.
//
// ⓘ SIZE. The pre-0a single dump was 88 lines / 7332 B — 3.6x the 2048-B console stage — so its tail was DROPPED
//   every time it ran; 0a's largest topic was 1673 B. The bare index is ~460 B with ~1590 B of headroom, measured
//   per profile by the probe. ⛔ It must stay inside the stage: that is a gate assertion, not a hope.
#pragma once
#include <Arduino.h>            // Print + F()
#include <cstddef>              // size_t
#include <cstring>              // strncmp — the router's token compare
#include "mr_features.h"        // MR_FEAT_MOBILE/REMOTE_MGMT/OLED/RADMIN_ACCEPT/RADMIN_CLIENT — the gated names
#include "protocol_constants.h" // MR_N_LAYERS

namespace mrfw {
namespace help {

// ---- the ONE manual pointer. Both responses end with it, and it is the only line of the index that is not a
//      command name — which is exactly how the gate strips it before comparing against the source projection.
inline void manual_pointer(Print& out) {
    out.println(F("docs/manual/command-reference.md"));
}

// ---- the index (bare `help` and bare `?`, byte for byte the same response) --------------------------------------
// ⓘ ONE NAME PER LINE, BYTEWISE ASCENDING, nothing else on the line. The order is source-independent and reviewable,
//   and it is what lets the gate compare the rendered bytes against the generated inventory without parsing prose.
inline void render_index(Print& out) {
    // §RADMIN slice 3 — the two TARGET-STORE families, ACCEPT builds only (R-RA-8). BYTEWISE ASCENDING like every
    // other line: "acl" < "admin-id" < "cfg". ⛔ NAMES ONLY — the grammar lands in the manual, which the pointer at
    // the foot of this index already names (§0g retired the `help <topic>` sections).
#if MR_FEAT_RADMIN_ACCEPT
    out.println(F("acl"));
    out.println(F("admin-id"));
#endif   // MR_FEAT_RADMIN_ACCEPT
    // §RADMIN slice 4 — the two CONTROLLER-STORE families, CLIENT builds only (R-RA-8's other half). BYTEWISE
    // ASCENDING like every other line and CONTIGUOUS with the ACCEPT pair above: "acl" < "admin-id" <
    // "admin-key" < "admin-target" < "cfg". ⓘ The two gates are mutually exclusive on every BOARD, but the HOST
    // sets both to 1, so the ordering has to hold with all four names present — and it does.
    // ⛔ NAMES ONLY — the grammar lands in the manual, which the pointer at the foot of this index already names.
#if MR_FEAT_RADMIN_CLIENT
    out.println(F("admin-key"));
    out.println(F("admin-target"));
#endif   // MR_FEAT_RADMIN_CLIENT
    out.println(F("cfg"));
    out.println(F("clear_inbox"));
    out.println(F("crashtest"));
    out.println(F("create"));
    out.println(F("debug"));
    out.println(F("del_msg"));
    out.println(F("duty"));
    out.println(F("factory_reset"));
    out.println(F("faults"));
    out.println(F("gateway"));
    out.println(F("hashof"));
    out.println(F("help"));
    out.println(F("join"));
    out.println(F("joinprofile"));
    out.println(F("leave"));
    out.println(F("limits"));
    out.println(F("lookup"));
    out.println(F("mark_read"));
#if MR_N_LAYERS < 2 && MR_FEAT_MOBILE
    out.println(F("mobile"));
#endif   // MR_N_LAYERS < 2 && MR_FEAT_MOBILE
    out.println(F("nameof"));
    out.println(F("ota"));
    out.println(F("peerkey"));
    out.println(F("peername"));
    out.println(F("peers"));
    out.println(F("prep-restart"));
    out.println(F("pull_inbox"));
    out.println(F("reboot"));
    out.println(F("regen"));
#if MR_FEAT_RADMIN_CLIENT
    out.println(F("remote"));
    out.println(F("remote-ack"));
    out.println(F("remote-result"));
    out.println(F("remote-retry"));
#endif
    out.println(F("reqpubkey"));
    out.println(F("resolve"));
    out.println(F("route"));
    out.println(F("routes"));
    out.println(F("send"));
    out.println(F("send_channel"));
    out.println(F("send_layer"));
    out.println(F("sleep"));
    out.println(F("status"));
#if MR_N_LAYERS < 2
    out.println(F("team"));
#endif   // MR_N_LAYERS < 2
    out.println(F("testch"));
    out.println(F("testclear"));
    out.println(F("testsend"));
    out.println(F("teststatus"));
#if MR_FEAT_OLED
    out.println(F("ui"));
#endif   // MR_FEAT_OLED
    out.println(F("version"));
    out.println(F("whoami"));
    manual_pointer(out);
}

// ---- the ONE bounded refusal: EVERY argument-bearing help form, `help <retired topic>` included -----------------
// C2: it never falls through to another verb, never guesses a section from a prefix, and names no command, flag or
// syntax beyond the two spellings it does accept — the detail is the manual's job, and the pointer is right there.
inline void render_usage(Print& out) {
    out.println(F("> help err unsupported_form (only bare `help` or `?`; usage: help | ?)"));
    manual_pointer(out);
}

}   // namespace help

// ★★ THE ONE HELP ENTRY POINT. `dispatch()` calls this once, in the position the old `help` arm held, and does no
// help parsing of its own. -> true when this router OWNED the line (index or refusal), false otherwise, so
// `helpful` and every non-help verb keep falling through to the rest of the verb map exactly as before.
// ⛔ `? <tail>` is deliberately NOT a form: the two BARE spellings print the index, and topic selection is retired.
inline bool help_command(const char* line, size_t len, Print& out) {
    // ★ ONE RECOGNITION OF THE WHOLE FAMILY, IN THE GUARD SHAPE, AND THE `?` ALIAS BELONGS ON THIS LINE. Two
    //   readers depend on that placement: a human sees the complete set of spellings in one place, and
    //   `tools/gen_command_inventory.py` records an alias only when it sits beside the canonical literal it spells
    //   (its shape S5) — split across two lines, the generated command inventory would silently lose `? `.
    //   ⛔ Everything below is therefore reachable only for `?` or a line whose first token is exactly `help`;
    //      `helpful` never gets past here, exactly as it never got past the old `len == 4` arm.
    if (!(len == 1 && line[0] == '?') && (len < 4 || strncmp(line, "help", 4))) return false;
    if (len == 1 || len == 4) { help::render_index(out); return true; }   // the two BARE index spellings
    if (line[4] != ' ') return false;                  // `helpN…` is a different verb, not a malformed help
    help::render_usage(out);                           // `help <anything>` — the retired form, answered loudly
    return true;
}

}   // namespace mrfw
