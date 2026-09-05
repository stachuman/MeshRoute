// MeshRoute — src/firmware_help.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★★ THE CONSOLE HELP AUTHORITY — the compact topic index, the nine `help <topic>` sections, and the ONE
// recognition of `help` / `?` / `help <topic>`. Remote-admin v2 slice 0a, B208's owner ruling (2026-08-17):
//
//     "bare `help` and `?` print only a compact index of the help topics available in that build; `help <topic>`
//      prints exactly one complete section. […] feature-gated topics are listed/accepted only when compiled. An
//      unknown topic must print usage plus the valid topic names. Each individual response must fit the existing
//      MR_CONSOLE_STAGE_BYTES and produce no CONSOLE_DROP. Do not enlarge the 2-KB stage, add a
//      blocking/asynchronous pager, or bypass GuardedConsole."
//
// ★ WHY THE TEXT LIVES IN A HEADER OF ITS OWN AND NOT IN firmware_commands.cpp, WHERE IT USED TO. That TU cannot be
//   host-compiled — it needs g_node, mrnv, the JSON writers, RadioLib and the whole Arduino world — and
//   `platformio.ini`'s `test_build_src = no` means NO native target compiles `src/` either. So while the help text
//   sat inside it, "no help line was lost in the split" was unprovable by anything but grep. This header depends on
//   `Print`, three feature macros and the board's RF-envelope text and NOTHING else, so `tools/probe_console_sink/`
//   compiles the REAL file, renders every topic through the REAL `src/console_sink.h` GuardedConsole, and compares
//   the emitted content multiset against the frozen pre-slice baseline. That executable proof is the whole reason
//   for the seam. ⇒ ⛔ DO NOT reach for `g_node`, NV, radio state, `Serial`, `mrcon`, dynamic storage or a command
//   handler from this file: each one of those re-welds the text to the un-compilable TU and kills the gate.
//
// ★ EVERYTHING IS COMPILE-TIME. Availability is `#if`, never a runtime table of `const char*` — a table would put
//   the topic list in RAM on every board to save an if-chain that costs nothing, and `if constexpr` would keep the
//   gated-out text in flash. The router is an exhaustive if-chain over the nine owner-named topics, matching the
//   verb-router idiom of firmware_commands.cpp (and `peers`' skip-leading-spaces-then-EXACT-length compare).
//
// ⓘ SIZE, AND WHY IT IS NOW A NON-EVENT. The pre-slice single dump was 88 lines / 7332 B on a full OLED build —
//   3.6x the 2048-B console stage — so its tail was DROPPED as whole lines with a loud `!! CONSOLE_DROP lines=N`
//   every single time it ran. Every response this file can produce is measured by the probe and is well inside the
//   stage. ⛔ A new topic section must stay inside it too: that is a gate assertion, not a hope.
#pragma once
#include <Arduino.h>            // Print + F()
#include <cstddef>              // size_t
#include <cstring>              // strncmp — the router's token compare
#include "mr_features.h"        // MR_FEAT_MOBILE / MR_FEAT_REMOTE_MGMT / MR_FEAT_OLED
#include "protocol_constants.h" // MR_N_LAYERS
#include "rf_capabilities.h"    // MR_RF_FREQ_RANGE_TEXT / MR_RF_OUTPUT_RANGE_TEXT (the cfg envelope line)

// ---- topic availability: ONE compile-time authority, read by the index, the valid-name list AND the router ------
// ⚠ `mobile` is the ONE topic whose gate MOVED in slice 0a, and the move is deliberate. The pre-slice help printed
//   the `mobile …` lines under `MR_N_LAYERS < 2` alone, while the `mobile ` dispatch arm has always been compiled
//   under `MR_N_LAYERS < 2` AND `MR_FEAT_MOBILE` (firmware_commands.cpp). ⇒ the topic now carries the HANDLER's
//   exact condition, so the index can never advertise a family this build refuses. The two expressions coincide in
//   every real product profile (only MR_PROFILE_GATEWAY clears MR_FEAT_MOBILE, and every gateway_* env also sets
//   MR_N_LAYERS=2), so no board's command availability changes — see the slice evidence's profile matrix.
#define MR_HELP_HAS_MOBILE ((MR_N_LAYERS < 2) && MR_FEAT_MOBILE)
#define MR_HELP_HAS_REMOTE (MR_FEAT_REMOTE_MGMT)

namespace mrfw {
namespace help {

// ---- the nine sections. Each opens with its canonical display heading, then its inherited lines in source order.

inline void topic_messaging(Print& out) {
    out.println(F("MESSAGING"));
    out.println(F("  send <id|0xhash> \"<text>\" [-a] [-e] [-t] [-l] -a=ack  -e=encrypt(hash only)  -t=team plane; plain send=global/home (fails if no home)"));
    out.println(F("    -l = attach this node's position to THIS message. REFUSED unless the DM is SEALED (use -e, or `cfg set e2e_dm 1`),"));
    out.println(F("         refused if this node has no fix (set lat/lon), and refused as too_large if the +6 B no longer fits."));
    out.println(F("         Replaces the removed `cfg set loc_dm`, which aired coordinates in the clear."));
    out.println(F("  send_channel <ch> \"<text>\" [-t] [-g] [-e] [-l]   -t=team plane  -g=explicit global  `-t -g`=BOTH  plain=global"));
    out.println(F("    -e = seal the post to the TEAM content key. Valid ONLY as `-t -e`: a global channel has no key, and"));
    out.println(F("         `-t -g -e` is refused because the global copy would air the same text in the clear. With a key held,"));
    out.println(F("         `-t` seals by DEFAULT (`cfg set team_channel_crypt 0` opts out).  No -a on this verb."));
    out.println(F("    -l = attach this node's position INSIDE the seal. `-t -l -e` sends text AND position in one sealed post."));
    out.println(F("         REFUSED whenever the post would not actually be sealed (no team, no key, crypt off, or `-t -g`),"));
    out.println(F("         and refused `no_location` with no fix. Max text 173 B sealed, 167 B with -l."));
    out.println(F("  send_layer <0xhash> <l1,l2,…> \"<text>\" [-a] [-e]   explicit cross-layer destination path; -e=encrypt (sealed relay); -l is refused (cross-layer carries no position)"));
}

inline void topic_identity(Print& out) {
    out.println(F("IDENTITY / KEYS"));
    out.println(F("  whoami | lookup 0x<hash> | hashof <id> [-t|-s] | nameof 0x<hash> | resolve 0x<hash> [hard]   (hashes are 0x-prefixed; hashof prints 0x…)"));
    out.println(F("    hashof searches BOTH id planes (static node_id and team local id) and NAMES the one that matched;"));
    out.println(F("    -t/-s narrow it. One number can legitimately answer in both — then both lines print."));
    out.println(F("  peers | peers all    the address book: known peers (hash, name, static/team id, key confidence)."));
    out.println(F("    plain = the 16 rows we hold a key for (also the JSON book over BLE); `all` adds id-only rows (console only)."));
    out.println(F("  peerkey <ed_pub hex64> [\"<name>\"]   pin a scanned/QR pubkey (optional one-shot label, max 32)"));
    out.println(F("  peername 0x<hash> \"<name>\"  rename a CACHED peer (key + confidence untouched; works on a pinned peer)"));
    out.println(F("  reqpubkey <0xhash|id> [-t|-s]  request a peer's key on-air. A bare id searches BOTH planes (-t/-s force one)."));
    out.println(F("    If the id has no binding yet, it floods a BY-ID query (\"who owns id N?\"); the owner's answer is a CLAIM,"));
    out.println(F("    so run `reqpubkey <id>` once more afterwards to fetch the key itself. `hashof <id>` shows what landed."));
}

#if MR_HELP_HAS_MOBILE
inline void topic_mobile(Print& out) {
    out.println(F("MOBILE  (the roaming-endpoint registration family; normal-node only)"));
    out.println(F("  mobile register [freq=<MHz> sf=<5-12> bw=<kHz> | scan]     arm registration: current PHY / a given PHY / scan known networks"));
    out.println(F("  mobile gateways            list learned gateways + networks"));
    out.println(F("  mobile query <gw>          pull a gateway's network directory"));
    out.println(F("  mobile status              attachment + home link + confirmation age + PHY"));
    out.println(F("  mobile unregister          end the attachment session -> dormant (no wire message)"));
}
#endif   // MR_HELP_HAS_MOBILE

inline void topic_inbox(Print& out) {
    out.println(F("INBOX"));
    out.println(F("  pull_inbox <dm_since> <chan_since> | mark_read <dm|chan> <seq>       NDJSON out"));
    out.println(F("  del_msg <dm|chan> <seq>                     delete one record (erased|not_found|io_error)"));
    out.println(F("  clear_inbox confirm                         WIPE both stores (inbox ONLY; keeps everything else)"));
}

inline void topic_diagnostics(Print& out) {
    out.println(F("DIAGNOSTICS"));
    out.println(F("  routes | status | duty | limits | cfg | cfg set <k> <v>"));
    out.println(F("  sleep [on|off] | debug [on|off] | regen | reboot | ota"));
    out.println(F("  version            build/git/board + last reset (no reset)"));
    out.println(F("  faults             the flash fault ring"));
    out.println(F("  crashtest <hang|fault|reboot>      (needs `debug on`)"));
    out.println(F("  rcmd <dst> <verb>      remote command via DM. status/routes = OPEN (cleartext); everything else = SEALED (needs `unlock`)"));
    out.println(F("  prep-restart       clear routes+inbox, KEEP join, go DORMANT (run fleet-wide, then power-cycle)"));
}

#if MR_HELP_HAS_REMOTE
inline void topic_remote(Print& out) {
    out.println(F("REMOTE MANAGEMENT  (authenticated; static/gateway builds only)"));
    out.println(F("  password <pass>        LOCAL-only: pin the fleet admin pubkey (derive from the passphrase) — set on every node"));
    out.println(F("  unlock <pass> | lock   operator device: derive the admin key into RAM to sign `rcmd`s / wipe it"));
    out.println(F("    then: `rcmd <dst> reboot` (etc.) is sealed to <dst>; `reqpubkey 0x<hash>` first if the target's pubkey isn't cached"));
}
#endif   // MR_HELP_HAS_REMOTE

inline void topic_test(Print& out) {
    out.println(F("TEST"));
    out.println(F("  route add <dest> <next_hop> <hops> [score_q4] | route del <dest>"));
    out.println(F("  testsend <dst> <run> [-a] [-e] -t ms1,ms2,… | testch <ch> <run> -t ms1,ms2,… | teststatus | testclear"));
    out.println(F("  factory_reset confirm      WIPE all flash (config+identity+peers+inbox) -> factory reboot"));
}

// ⓘ Two inherited groups share this topic: the `create`/`join`/`leave`/`gateway` provisioning verbs (always
//   compiled into the help, exactly as before) and the team lifecycle, which keeps its own pre-slice
//   `MR_N_LAYERS < 2` availability. The blank line between them is new layout, not an inherited separator.
inline void topic_provisioning(Print& out) {
    out.println(F("PROVISIONING     (key=value, order-free; LIVE, no reboot)"));
    out.println(F("  create layer= freq= bw= sf= sf_list= duty= name=\"<n>\" [active_fraction=] [ch_min_ms=] [dm_min_ms=]"));
    out.println(F("  join layer= freq= bw= sf=      |  leave              layer=1..255 network id (leaf = layer & 0x0F)"));
    out.println(F("  gateway l0=<layer>:<node>:<ctrl_sf>:<data_sfs> l1=…  [period=] [win0=ms:off] [win1=] [beacon=] [freq0=] [freq1=] [bw0=] [bw1=] [cr0=] [cr1=] [gateway_only=]"));
    out.println(F("    dual-layer -> NV, reboot to apply.  e.g. gateway l0=1:1:8:7,9 l1=2:1:9:9,10"));
#if MR_N_LAYERS < 2
    out.println(F(""));
    out.println(F("  team new                   mint a team (become its creator) — ALWAYS also mints its X25519 channel key"));
    out.println(F("  team <id> | team 0         join an existing team / leave (a join mints NO key — receive it by grant or QR)"));
    out.println(F("    [tkpub=<64 hex> tkpriv=<64 hex>]   adopt an EXISTING team channel keypair instead of minting (QR onboarding)"));
    out.println(F("  team exportkey             print this team's channel keypair as JSON (the app's team QR) — \xe2\x9a\xa0 discloses a PRIVATE key"));
    out.println(F("  team grantkey <0xhash|team-id> [name=\"<text>\"] [-t]   send this team's channel key to a teammate in a SEALED DM"));
    out.println(F("                             needs a VERIFIED pubkey for the target (`reqpubkey <0xhash>` or a QR import) — never sent in the clear"));
#endif
}

// ⓘ Two inherited groups again: the `cfg set` key catalog and — on a build that has a panel — the OLED UI-preset
//   verbs, which keep their pre-slice `MR_FEAT_OLED` gate ([[B255]]: help that advertises a verb this build
//   refuses is exactly the "uniform but unusable surface" the ruling rejected).
inline void topic_cfg(Print& out) {
    out.println(F("CFG KEYS  (`cfg set <key> <val>`; bool keys take on|off / 1|0)"));
    out.println(F("  node_id name freq routing_sf bw cr tx_power sf_list lbt beacon_ms duty nav nav_ignore hop_cap team_hop_cap leaf_id"));
    out.println(F("    RF envelope on this board: freq " MR_RF_FREQ_RANGE_TEXT " MHz; tx_power " MR_RF_OUTPUT_RANGE_TEXT " dBm nominal conducted"));
    out.println(F("  mobile mobile_autoregister host_mobiles intra_layer_relay gateway_only"));   // §team-id-cfg-removal: `team_id` REMOVED — a team is joined/left with the `team` verb (see `help provisioning`)
    out.println(F("  lat lon e2e_dm team_channel_crypt intro_attach ble_mode ble_period ble_pin gw_announce_pct gw_announce_interval gw_herd_slack"));   // §loc-per-send: `loc_in_dm` REMOVED — use `send … -l` per message
    out.println(F("  active_fraction ch_min_ms dm_min_ms leaf_name"));
    out.println(F("    `name`=node identity · `leaf_name`=managed leaf (bumps epoch) · identity via `regen` · NO team_id key -> `team new`/`team <id>`/`team 0`"));
    out.println(F("  gateway-only keys: n_layers layer0_id window_period_ms l0_window_ms l0_window_offset_ms l1_layer_id l1_node_id l1_routing_sf l1_sf_list l1_beacon_ms l1_window_ms l1_window_offset_ms l1_freq"));
#if MR_FEAT_OLED   // ★ [[B255]] the `help cfg` group
    out.println(F(""));
    out.println(F("UI PRESETS   (the OLED compose catalog: 17 stable slots — 1 emergency + 8 dm + 8 channel; NDJSON out)"));
    out.println(F("  ui preset list                                 all 17 records incl. DISABLED, then ui_presets_end"));
    out.println(F("  ui preset set <emergency|dmN|channelN> loc=<on|off> \"<text>\"   text = 1..17 printable ASCII, no \" \\ CR LF"));
    out.println(F("  ui preset clear <dmN|channelN> | ui preset reset <emergency|dmN|channelN|all>   N=1..8; clear emergency -> mandatory"));
    out.println(F("    a mutating verb answers with the RESULTING record (`reset all`: the full list), or"));
    out.println(F("    {\"ev\":\"ui_preset_err\",\"reason\":\"bad_slot|bad_text|bad_location|mandatory|busy|store\"}."));
    out.println(F("    While an emergency is ACTIVE every mutating verb answers `busy` — including a no-op: an alarm's"));
    out.println(F("    retry series must never have its body or its location policy changed halfway through."));
#endif   // MR_FEAT_OLED — [[B255]]
}

// ---- the valid-topic name list: the SECOND rendering of the availability authority above, and the reason the
//      refusal can never name a topic the index omits (or omit one it lists). Printed WITHOUT a terminator; its
//      caller closes the line.
inline void topic_names(Print& out) {
    out.print(F(" messaging identity"));
#if MR_HELP_HAS_MOBILE
    out.print(F(" mobile"));
#endif
    out.print(F(" inbox diagnostics"));
#if MR_HELP_HAS_REMOTE
    out.print(F(" remote"));
#endif
    out.print(F(" test provisioning cfg"));
}

// ---- the compact index (bare `help` and bare `?`, byte for byte the same response) ------------------------------
// ⓘ A topic row is indented FOUR spaces and the two prose rows TWO, which is what lets the probe read the index's
//   topic set out of the rendered bytes rather than trusting a list typed twice.
inline void render_index(Print& out) {
    out.println(F("===== MeshRoute console ====="));
    out.println(F("  help <topic>   prints that whole section; bare `help` or `?` prints this index."));
    out.println(F("  topics compiled into this build:"));
    out.println(F("    messaging      send / send_channel / send_layer — flags, sealing, attached position"));
    out.println(F("    identity       whoami, lookup/hashof/nameof/resolve, peers, pubkeys and names"));
#if MR_HELP_HAS_MOBILE
    out.println(F("    mobile         mobile register/gateways/query/status/unregister"));
#endif
    out.println(F("    inbox          pull_inbox / mark_read / del_msg / clear_inbox"));
    out.println(F("    diagnostics    routes status duty limits cfg sleep debug version faults rcmd prep-restart"));
#if MR_HELP_HAS_REMOTE
    out.println(F("    remote         password / unlock / lock — the authenticated remote-management keys"));
#endif
    out.println(F("    test           route injection, the scheduled-send workload, factory_reset"));
    out.println(F("    provisioning   create / join / leave / gateway, and the team lifecycle"));
    out.println(F("    cfg            the `cfg set <key>` catalog"));
}

// ---- the ONE bounded refusal: unknown, unavailable, empty or malformed topic tail ------------------------------
// C2: it never falls through to another verb and never guesses a section from a prefix.
inline void render_usage(Print& out) {
    out.println(F("> help err unknown_topic (usage: help | ? | help <topic>)"));
    out.print(F("> valid topics:")); topic_names(out); out.println();
}

}   // namespace help

// ★★ THE ONE HELP ENTRY POINT. `dispatch()` calls this once, in the position the old `help` arm held, and does no
// help parsing of its own. -> true when this router OWNED the line (index, section or refusal), false otherwise, so
// `helpful` and every non-help verb keep falling through to the rest of the verb map exactly as before.
// ⛔ `? <topic>` is deliberately NOT a form: B208 grants parity to the two BARE index spellings and grants topic
//    selection to `help <topic>` only.
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
    // The `peers ` idiom (firmware_commands.cpp): skip leading spaces, then require an EXACT-LENGTH token match —
    // so `help messagingx`, `help messaging x` and a bare `help ` all reach the refusal instead of a section.
    const char* a = line + 5; size_t an = len - 5;
    while (an && *a == ' ') { ++a; --an; }
    if      (an ==  9 && !strncmp(a, "messaging",     9)) help::topic_messaging(out);
    else if (an ==  8 && !strncmp(a, "identity",      8)) help::topic_identity(out);
#if MR_HELP_HAS_MOBILE
    else if (an ==  6 && !strncmp(a, "mobile",        6)) help::topic_mobile(out);
#endif
    else if (an ==  5 && !strncmp(a, "inbox",         5)) help::topic_inbox(out);
    else if (an == 11 && !strncmp(a, "diagnostics",  11)) help::topic_diagnostics(out);
#if MR_HELP_HAS_REMOTE
    else if (an ==  6 && !strncmp(a, "remote",        6)) help::topic_remote(out);
#endif
    else if (an ==  4 && !strncmp(a, "test",          4)) help::topic_test(out);
    else if (an == 12 && !strncmp(a, "provisioning", 12)) help::topic_provisioning(out);
    else if (an ==  3 && !strncmp(a, "cfg",           3)) help::topic_cfg(out);
    else                                                  help::render_usage(out);
    return true;
}

}   // namespace mrfw
