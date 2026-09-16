#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §B95 NEGATIVE CONTROLS. Revert ONE behaviour at a time and prove the instrument turns RED. A probe that stays green
# against a broken source is measuring nothing — the failure mode this project keeps finding (three times in one
# session, most recently §UI-5).
#
# Two families, because the fix has two halves:
#   • SINK controls  — mutate a COPY of `src/console_sink.h`, rebuild the REPO's probe_main.cpp against the copy, and
#     require at least one CHK to fail. Each names the probe row it is meant to break.
#   • SOURCE controls — mutate a COPY of `src/firmware_commands.cpp` / `src/fw_main.cpp` (the TUs no host build can
#     compile) and require the NAMED structural check to flip. They run the very same structural.py the gate runs, so
#     a control that stays green indicts the checker, not the mutation.
#
# ⚠ Every path arrives by argv, every mutation lands on a COPY under the caller's temp dir, and the real sources are
#   opened READ-ONLY and asserted unchanged at the end. There is nothing to restore because nothing is modified.
#   (An earlier probe's controls hardcoded an absolute path and a session scratch dir, then wrote the mutation into
#   the REAL working tree with no try/finally. Both are structurally impossible here.)
import hashlib
import os
import shutil
import subprocess
import sys

import collections

import ble_guard
import structural

# §0g: the ORACLE. `tools/gen_command_inventory.py` projects the primary command names out of the REAL dispatchers,
# so a help control is judged against SOURCE, never against another copy of the help header ([[B291]]).
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import gen_command_inventory as GEN   # noqa: E402

if '--' not in sys.argv:
    sys.exit('usage: negctl.py <scratch> <cxx> <sink.h> <cmds.cpp> <cmds.h> <fw_main.cpp> <firmware_help.h> '
             '-- <flags...>')
cut = sys.argv.index('--')
if cut != 8:
    sys.exit(f'usage error: expected 7 paths before "--", got {cut - 1}')
OUT, CXX, SINK, CMDS, CMDSH, FWMAIN, HELP = (os.path.abspath(sys.argv[1]), sys.argv[2],
                                             *[os.path.abspath(p) for p in sys.argv[3:8]])
FLAGS = sys.argv[cut + 1:]
# ★ §0a: the profile a help control runs under. Each control names the ONE profile in which its mutated decision is
#   observable at all — `mobile` availability, for instance, is only a question on a reduced (gateway) build.
PROFILE_FLAGS = {
    'full_oled':     ['-DMR_FEAT_OLED=1'],
    'full_headless': [],
    'gateway':       ['-DMR_N_LAYERS=2', '-DMR_PROFILE_GATEWAY'],
    'mobile':        ['-DMR_PROFILE_MOBILE'],
}
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
# §RADMIN slice 3: structural.py's device-boundary rows (S30..S39) read these two as well. Resolved from ROOT
# rather than taken on argv so the runner's command line is unchanged; a mutant COPY is substituted per control.
NVH = os.path.join(ROOT, 'src', 'device_nv.h')
CFGCPP = os.path.join(ROOT, 'src', 'firmware_config.cpp')
JSONCPP = os.path.join(ROOT, 'lib', 'console', 'console_json.cpp')
INBOXCPP = os.path.join(ROOT, 'src', 'firmware_inbox.cpp')
CONTEXT = os.path.join(ROOT, 'src', 'firmware_command_context.h')
PROBE_MAIN = os.path.join(HERE, 'probe_main.cpp')      # the REPO's probe, never a scratch copy

# §RADMIN slice 3 added NVH + CFGCPP: `mutate()` copies from this map, and the tail asserts every one of them
# is byte-identical afterwards — so the two new control targets are protected by the same guard as the five.
ORIG = {p: open(p).read() for p in (SINK, CMDS, CMDSH, FWMAIN, HELP, NVH, CFGCPP, JSONCPP, INBOXCPP, CONTEXT)}
for p, t in ORIG.items():
    print(f'baseline {os.path.relpath(p, ROOT)} md5 = {hashlib.md5(t.encode()).hexdigest()[:8]}  ({len(t)} bytes)')
print()

rc_all = 0

def mutate(path, find, repl, dest_name, subdir=None):
    """Write a one-substitution COPY under OUT; None if the anchor is not unique (the control then FAILS loudly).

    ⚠ `subdir` exists because of a defect in the FIRST version of this file: the mutated headers were written as
    `ctlN_console_sink.h`, so `#include "console_sink.h"` could not resolve and ALL EIGHT sink controls reported
    "COMPILE FAILS (the strongest failing-first form)" — which reads like a pass and measured nothing whatsoever.
    A mutated header must keep its REAL BASENAME inside its own directory, and a missing-file error must be treated
    as an instrument failure (see `run_sink_control`), never as a control result.
    """
    src = ORIG[path]
    n = src.count(find)
    if n != 1:
        return None, f'anchor matched {n} times, expected 1'
    d = os.path.join(OUT, subdir) if subdir else OUT
    os.makedirs(d, exist_ok=True)
    dest = os.path.join(d, dest_name)
    open(dest, 'w').write(src.replace(find, repl, 1))
    return dest, ''

def mutate_steps(path, steps, dest_name):
    """Write a COPY after several individually-unique substitutions; fail loud before writing on any bad anchor."""
    src = ORIG[path]
    for step, (find, repl) in enumerate(steps, 1):
        n = src.count(find)
        if n != 1:
            return None, f'step {step} anchor matched {n} times, expected 1'
        src = src.replace(find, repl, 1)
    dest = os.path.join(OUT, dest_name)
    open(dest, 'w').write(src)
    return dest, ''

# ============================================================== SINK CONTROLS (behavioural) =========================
LEGACY_PAIR = '''    size_t write(uint8_t b) override {
        if (!Serial || Serial.availableForWrite() < 1) return 0;
        return Serial.write(b);
    }
    size_t write(const uint8_t* buf, size_t n) override {
        if (!Serial || static_cast<size_t>(Serial.availableForWrite()) < n) return n;
        return Serial.write(buf, n);
    }'''

SINK_CTL = [
    ('C1 no staging at all — revert to the per-write guard (the pre-fix sink)',
     '''    size_t write(uint8_t b) override { stage(b); return 1; }
    size_t write(const uint8_t* buf, size_t n) override { for (size_t i = 0; i < n; ++i) stage(buf[i]); return n; }''',
     LEGACY_PAIR, 'P2a/P2b — the response is corrupted again'),

    ('C2 admit every fragment as a line (commit without the terminator)',
     "        if (b == '\\n') commit();", '        commit();',
     'P2a/P2c — fragments reach the wire out of shape'),

    ('C3 let an over-long line leak its PREFIX instead of dropping whole',
     '        else                              _over = true;', '        else                              { }',
     'P8a — bytes of the oversized line appear'),

    ('C4 stop counting dropped lines',
     '        if (_over)     { _line = 0; _over = false; ++_dropped; return; }   // never started ⇒ safe to drop whole',
     '        if (_over)     { _line = 0; _over = false; return; }',
     'P6b/P8d — the loss becomes silent'),

    # ⓘ The `_out == _pend` guard alone is NOT independently testable: pump() leaves either the queue empty or the
    #   FIFO full, so a report checked AFTER the drain can never fit mid-queue. What IS load-bearing is the ORDER, so
    #   that is what this control reverts — and P13 had to be given a drain wider than the FIFO before it could see it.
    ('C5 emit the drop report BEFORE draining the queue (ordering reverted)',
     '''        if (_line) { stage('\\r'); stage('\\n'); }
        pump();
        if (_dropped && _out == _pend && _line == 0) {''',
     '''        if (_line) { stage('\\r'); stage('\\n'); }
        if (_dropped) {''',
     'P13c — the report cuts into a half-drained line'),

    ('C6 hand Serial more than availableForWrite() (the blocking hazard)',
     '        if (static_cast<size_t>(avail) < n) n = static_cast<size_t>(avail);', '        (void)avail;',
     'P2e — over-capacity writes, which the real cores answer by blocking/yielding'),

    ('C7 abandon the unsent residue when the stage fills (mid-line give-up)',
     '        if (_pend + _line >= sizeof _buf) compact();          // reclaim what has already gone out, then re-test',
     '        if (_pend + _line >= sizeof _buf) { _out = _pend = 0; }',
     'P9d/P13 — the abandoned residue is neither delivered nor counted'),

    ('C8 never terminate a partial line at the boundary',
     "        if (_line) { stage('\\r'); stage('\\n'); }\n        pump();",
     '        pump();',
     'P5a — response A fuses into response B'),
]

for idx, (label, find, repl, expect) in enumerate(SINK_CTL):
    print(label)
    dest, err = mutate(SINK, find, repl, os.path.basename(SINK), subdir=f'ctl{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    md5 = hashlib.md5(open(dest).read().encode()).hexdigest()[:8]
    assert md5 != hashlib.md5(ORIG[SINK].encode()).hexdigest()[:8], 'mutation did not change the file'
    binary = os.path.join(OUT, f'ctl{idx}.bin')
    # ★ -I the mutation's OWN directory FIRST and give the compiler no other console_sink.h to find. The md5 is
    #   compiled in and printed by the probe, so the run itself states which text was measured.
    # ⚠ THE MUTATION DIRECTORY GOES FIRST, ahead of FLAGS (which now carry `-I<repo>/src`). Put it last and the
    #   REAL src/console_sink.h shadows the mutant, every sink control compiles the unmutated header, and all eight
    #   report a green probe against "broken" source — the exact vacuity these controls exist to detect.
    b = subprocess.run([CXX, '-I' + os.path.dirname(dest), *FLAGS, f'-DPROBE_SINK_MD5="{md5}"',
                        '-DPROBE_HELP_MD5="realhelp"', '-DPROBE_PROFILE="full_headless"',
                        PROBE_MAIN, '-o', binary], capture_output=True, text=True)
    if b.returncode != 0:
        first = (b.stderr.strip().splitlines() or ['(no stderr)'])[0]
        # A compile failure counts as a control result ONLY if the diagnostic is IN the mutated header. Anything else
        # (a missing include, a broken probe) is an INSTRUMENT failure that would otherwise masquerade as a pass.
        if 'No such file' in b.stderr or os.path.basename(SINK) + ':' not in first:
            print(f'   !! INSTRUMENT FAILURE, not a control result: {first[:120]}')
            rc_all = 1
        else:
            print(f'   -> COMPILE FAILS in the mutated header (the strongest failing-first form): {first[-90:]}')
        continue
    r = subprocess.run([binary], capture_output=True, text=True)
    if f'md5 = {md5}' not in r.stdout:
        print('   !! the binary does not report the mutated md5 -- a stale build was measured')
        rc_all = 1
        continue
    fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith('FAIL')]
    if not fails:
        print(f'   !! STAYED GREEN (sink md5 {md5}) -- this control proves NOTHING  [expected: {expect}]')
        rc_all = 1
    else:
        print(f'   -> sink md5 {md5}: {len(fails)} check(s) fail: ' + '; '.join(f[5:58] for f in fails[:3]))

# ============================================================== SOURCE CONTROLS (structural) ========================
HL_OLD = '''static void hl(const __FlashStringHelper* fs) {
    if (!Serial) return;
    const char* s = reinterpret_cast<const char*>(fs);
    const size_t len = strlen(s); size_t off = 0; const uint32_t t0 = millis();
    while (off < len && Serial && (uint32_t)(millis() - t0) < 40) {
        const int a = Serial.availableForWrite();
        if (a > 0) { const size_t rem = len - off; const size_t chunk = (static_cast<size_t>(a) < rem) ? static_cast<size_t>(a) : rem;
                     off += Serial.write(reinterpret_cast<const uint8_t*>(s) + off, chunk); }
        yield();
    }
    if (Serial && Serial.availableForWrite() >= 2) Serial.write(reinterpret_cast<const uint8_t*>("\\r\\n"), 2);
}
bool dispatch(const char* line, size_t len, Print& out) {
    hl(F("===== MeshRoute console ====="));'''

SRC_CTL = [
    # ⓘ §0a re-aim: `dump_help()` is gone, so the bypass is reinstated in front of `dispatch()` instead. The
    #   control still asks the one question it always asked — does S1/S2 notice a direct-Serial help path?
    ('X1 reinstate the direct-Serial hl() help bypass', CMDS,
     'bool dispatch(const char* line, size_t len, Print& out) {', HL_OLD, ('S1', 'S2')),

    ('X2 restore the global-writing print_sf_list(bitmap)', CMDS,
     'void print_sf_list(Print& out, uint16_t bitmap) {\n    bool first = true;\n'
     '    for (uint8_t sf = 5; sf <= 12; ++sf)\n'
     "        if (bitmap & (1u << sf)) { if (!first) out.print(','); out.print(sf); first = false; }\n"
     "    if (first) out.print('-');",
     'void print_sf_list(uint16_t bitmap) {\n    bool first = true;\n'
     '    for (uint8_t sf = 5; sf <= 12; ++sf)\n'
     "        if (bitmap & (1u << sf)) { if (!first) mrcon.print(','); mrcon.print(sf); first = false; }\n"
     "    if (first) mrcon.print('-');",
     ('S5', 'S7')),

    # ⓘ §0a re-aim (owner ruling 2026-09-04): the guard is now a PREFIX test over the whole help family, so the
    #   old exact-length anchor matched 0 times and this control silently stopped applying. Re-anchored on the
    #   widened arm; it still asks the one question it always asked — does S10/S11 notice the refusal vanishing?
    ('X3 drop the BLE console_only refusal for the help family', FWMAIN,
     '    if (((len == 4 || (len > 4 && line[4] == \' \')) && !strncmp(line, "help", 4)) || (len == 1 && line[0] == \'?\'))\n'
     '        return write_err(out, cap, "help", "console_only");\n', '',
     ('S10', 'S11')),

    ('X4 remove the per-pass mrcon.service() from service_console', FWMAIN,
     '    mrcon.service();\n}', '}', ('S9',)),

    # ⓘ §0b/[[B279]]: the boot caller must NAME its sink. Routing it to a DIFFERENT Print& is compile-valid in the
    #   real build (`Serial` is a Print), so no compiler, no native test and no board build would refuse it — which
    #   is exactly why S21 has to. ⛔ Deliberately NOT "delete the argument": after 0b that would not compile, and a
    #   compile failure is not a behavioural result (the sibling probe's [[B237]] rule, applied to a source control).
    ('X12 route the boot identity formatter to a different sink', FWMAIN,
     '    print_identity(idb, mrcon);', '    print_identity(idb, Serial);', ('S21',)),

    # ⓘ §0a re-aim: the help text moved to firmware_help.h and S4 now asks whether every RESPONSE ends
    #   terminated. Making the index's LAST emission a bare print() is exactly the shape S4 must reject.
    ('X5 leave the index unterminated (print instead of println on its last line)', HELP,
     '    out.println(F("whoami"));',
     '    out.print(F("whoami"));', ('S4',)),

    # ==================================================== §RADMIN-0c — THE TWO ONE-CALL ADAPTERS AND THE SEAM ====
    # ⛔ EVERY ONE OF THESE IS A SHAPE THAT COMPILES. `src/fw_main.cpp` is host-uncompilable, so nothing but these
    #    rows stands between a caller quietly growing a second fork and a green gate.

    # ---- X13: THE SERIAL CALLER OPENS ITS OWN FORK AGAIN — the exact pre-0c shape, restored beside the seam call.
    #          The command would then execute TWICE on USB, and both answers would print.
    ('X13 the serial adapter re-opens its own router/parser fork beside the seam call', FWMAIN,
     '            if (ex.state == mrfw::LineExec::State::unmatched) {',
     '            meshroute::Command dup{};\n'
     '            if (meshroute::console::parse_command(line, pos, dup) == meshroute::console::ParseErr::ok)\n'
     '                (void)g_node.on_command(dup);\n'
     '            if (ex.state == mrfw::LineExec::State::unmatched) {', ('S22',)),

    # ---- X14: THE BLE CALLER BYPASSES THE SEAM for the router half — the "helper plus a residual second path"
    #          failure the brief names explicitly. The seam still runs; so does a second router offer.
    ('X14 the BLE adapter keeps a residual direct dispatch() beside the seam call', FWMAIN,
     '\n    const mrfw::LineExec ex = mrfw::exec_console_line(',
     '\n    if (mrfw::dispatch(line, len, ls)) { ls.flush(); return 0; }\n'
     '    const mrfw::LineExec ex = mrfw::exec_console_line(', ('S23',)),

    # ---- X15: THE BLE CALLER LOSES ITS ONE FLUSH. Every streamed router response would end one partial line short
    #          — invisible on a '\n'-terminated response, fatal on anything else.
    ('X15 the BLE adapter drops the seam response flush', FWMAIN,
     '    if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }',
     '    if (ex.state == mrfw::LineExec::State::streamed) { return 0; }', ('S23',)),

    # ---- X16: THE BLE CALLER ROUTES THE STREAM THROUGH THE 256-B DIRECT BUFFER — [[B292]]'s hazard, made real:
    #          a multi-kilobyte `routes` dump handed to a single 244-byte notification.
    ('X16 the BLE adapter hands the seam its direct buffer where the stream belongs', FWMAIN,
     'mrfw::exec_console_line(line, len, mrfw::LineFormat::json, ls, out, cap, ctx)',
     'mrfw::exec_console_line(line, len, mrfw::LineFormat::json, ls, nullptr, 0, ctx)', ('S23',)),

    # ---- X17: THE SEAM'S FORK IS REVERSED. ⛔ THIS IS THE ONE CONTROL WITH NO BEHAVIOURAL TWIN, and that is a
    #          MEASUREMENT, not an omission: with the router/parser intersection measured EMPTY on all six real
    #          profiles (tools/probe_console_sink/ownership.py), a reversal changes no byte on either transport —
    #          which is exactly why 0c could unify the two orders at all. A structural pin is therefore the only
    #          honest instrument for it, and this control is what proves the pin is not decorative.
    ('X17 the seam asks the parser BEFORE the router (the order the pin exists to hold)', CMDS,
     '    if (dispatch(line, len, stream)) { r.state = LineExec::State::streamed; r.outcome = DispatchOutcome::completed; return r; }\n'
     '\n'
     '    // (2) the command parser.',
     '    // (2) the command parser.', ('S24',)),

    # ---- X18: THE SEAM RE-CHOOSES THE SINK. Compile-valid, board-valid, and wrong: [[B279]] one layer up.
    ('X18 the seam writes to the global console instead of the sink it was handed', CMDS,
     '    stream.print(F("> "));', '    mrcon.print(F("> "));', ('S25',)),

    # ---- X19: THE SEAM RETAINS THE BORROWED BODY. `Command::body` points into the caller's line buffer, which the
    #          serial caller reuses on the very next character; a returned pointer is a use-after-reuse.
    ('X19 LineExec carries the borrowed Command::body out of the call', CMDSH,
     '    size_t                       n         = 0;                                  // valid only on `buffered`',
     '    size_t                       n         = 0;                                  // valid only on `buffered`\n'
     '    const uint8_t*               body      = nullptr;', ('S26',)),

    # ---- X20: THE HEX RADIX IS DROPPED from the send handle. ⛔ INVISIBLE TO EVERY EXECUTED CHECK IN THIS TREE:
    #          the probes' shared Arduino fake ignores a print radix, so the transcript renders `print(x, HEX)` and
    #          `print(x)` identically. S27 is the only thing that can see it.
    ('X20 the text arm prints the send handle in decimal (the fake cannot tell)', CMDS,
     'stream.print(cr.dst_hash, HEX);', 'stream.print(cr.dst_hash);', ('S27',)),

    # ---- X21/X22: [[B298]] — a retired topic-help claim made ACTIVE again, in each file. The correction idiom keeps
    #          the old design VISIBLE; what must not come back is the present tense.
    ('X21 a retired topic-help claim becomes an ACTIVE present-tense comment again (cpp)', CMDS,
     '// `dispatch()` below keeps exactly one call to it and parses no help of its own.',
     '// `dispatch()` below keeps exactly one call to it and parses no help of its own.\n'
     '// The header renders one whole topic section per `help <topic>` request.', ('S28',)),

    ('X22 the header claims again that the two callers are deliberately not retrofitted', CMDSH,
     '// ⛔ V1 CORRECTION (§RADMIN-0c / [[B298]], 2026-09-05) — THE OLD CLAIM IS KEPT VISIBLE AND IS NOW FALSE. This',
     '// The two existing call sites are DELIBERATELY NOT retrofitted onto this helper.\n'
     '// ⛔ NOTE (§RADMIN-0c) — THE OLD CLAIM IS KEPT VISIBLE AND IS NOW FALSE. This', ('S29',)),
]

BLE_VALIDATION = ('    const LineErr e = validate_command_line(line, len, mrble::kLineStorageBytes - 1);\n'
                  '    if (e != LineErr::ok) return write_err(out, cap, "bad_line", line_err_name(e));\n')
SRC_CTL += [
    ('S6-C1 seam validator call deleted', CMDS,
     '    r.line_err = meshroute::console::validate_command_line(line, len, ctx.line_max_bytes);',
     '    r.line_err = meshroute::console::LineErr::ok;', ('S53',)),
    ('S6-C2 panel validator call deleted', CMDS,
     '    r.line_err = meshroute::console::validate_command_line(line, len, meshroute::console::local_command_max_bytes);',
     '    r.line_err = meshroute::console::LineErr::ok;', ('S54',)),
    ('S6-C3 admission inverted', CMDS,
     '!command_authority_admits(*policy, ctx, line, len)',
     'command_authority_admits(*policy, ctx, line, len)', ('S55',)),
    ('S6-C4 local commands consult the table', CMDS,
     '    if (ctx.authority != CommandAuthority::local) {',
     '    if (ctx.authority == CommandAuthority::local) {', ('S55',)),
    ('S6-C5 remote refusal writes a local envelope', CMDS,
     '            r.refuse = policy ? RefuseReason::authority : RefuseReason::unclassified;',
     '            r.refuse = policy ? RefuseReason::authority : RefuseReason::unclassified;\n'
     '            stream.print("refused");', ('S56',)),
    ('S6-C6 USB bad_line token drifts', CMDS,
     'stream.print(F("> err bad_line "));', 'stream.print(F("> err invalid "));', ('S57',)),
    ('S6-C7 context becomes optional', CMDSH,
     'char* reply, size_t reply_cap, const CommandContext& ctx);',
     'char* reply, size_t reply_cap, const CommandContext& ctx = {});', ('S58',)),
    ('S6-C8 USB bound becomes a literal', FWMAIN,
     'true, 0, sizeof(line) - 1};', 'true, 0, 1023};', ('S59',)),
    ('S6-C9 BLE bound becomes a literal', FWMAIN,
     'false, 0, mrble::kLineStorageBytes - 1};', 'false, 0, 274};', ('S60',)),
    ('S6-C10 BLE head validator deleted', FWMAIN, BLE_VALIDATION, '', ('S61',)),
    ('S6-C11 BLE validator moved below the first command-owning arm', FWMAIN,
     BLE_VALIDATION + '    if (len == 6 && !strncmp(line, "whoami", 6)) {',
     '    if (len == 6 && !strncmp(line, "whoami", 6)) {\n' + BLE_VALIDATION, ('S61',)),
    ('S6-C12 BLE refusal replaced with fall-through', FWMAIN,
     '    if (e != LineErr::ok) return write_err(out, cap, "bad_line", line_err_name(e));',
     '    (void)e;', ('S61',)),
    ('S6-C13 a local completion incorrectly becomes scheduled', CMDS,
     '\n    r.outcome = DispatchOutcome::completed;\n',
     '\n    r.outcome = DispatchOutcome::scheduled;\n', ('S62',)),
]

for idx, (label, path, find, repl, expect_ids) in enumerate(SRC_CTL):
    dest, err = mutate(path, find, repl, f'src_ctl{idx}_' + os.path.basename(path))
    if dest is None:
        print(f'{label}\n   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    paths = {CMDS: CMDS, CMDSH: CMDSH, FWMAIN: FWMAIN, HELP: HELP}
    paths[path] = dest                                      # only the mutated file is swapped
    rows = {cid: ok for cid, _d, ok, _x in
            structural.check(paths[CMDS], paths[CMDSH], paths[FWMAIN], paths[HELP], NVH, CFGCPP)}
    flipped = [cid for cid in expect_ids if not rows.get(cid, True)]
    if not flipped:
        print(f'{label}\n   !! STAYED GREEN -- {"/".join(expect_ids)} did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print(f'{label}\n   -> structural {"+".join(flipped)} now FAIL')

# ============================================================== §B214 SOURCE CONTROLS ==============================
# These are semantic regressions in scratch copies, not syntax-error stand-ins. The positive checker reads the same
# neutralised executable source for the real file and every mutant, so comments cannot satisfy either side.
B214_CTL = [
    ('X6 B214 restore the old home-id-only authority and scanning label', CMDS, (
        ('        const meshroute::Node::MobileAttachState as = g_node.mobile_attach_state();\n',
         '        const meshroute::Node::MobileAttachState as = h ? '
         'meshroute::Node::MobileAttachState::attached : meshroute::Node::MobileAttachState::dormant;\n'),
        ('                out.print(F("UNREGISTERED (")); '
         'out.print(meshroute::Node::attach_state_name(as)); out.println(\')\');\n',
         '                out.println(F("UNREGISTERED (scanning)"));\n'),
    ), ('S12',)),

    ('X6b B214 retain a dead state read but derive the switch authority from home id', CMDS, (
        ('        const meshroute::Node::MobileAttachState as = g_node.mobile_attach_state();\n',
         '        (void)g_node.mobile_attach_state();\n'
         '        const meshroute::Node::MobileAttachState as = h ?\n'
         '            meshroute::Node::MobileAttachState::attached :\n'
         '            meshroute::Node::MobileAttachState::dormant;\n'),
    ), ('S12',)),

    ('X7 B214 make claiming report REGISTERED before confirmation', CMDS, (
        ('                out.print(F("UNREGISTERED (")); '
         'out.print(meshroute::Node::attach_state_name(as)); out.println(\')\');\n',
         '                if (as == meshroute::Node::MobileAttachState::claiming) {\n'
         '                    if (h) { out.print(F("REGISTERED home=")); out.println(h); }\n'
         '                    else     out.println(F("REGISTERED home=?"));\n'
         '                } else {\n'
         '                    out.print(F("UNREGISTERED (")); '
         'out.print(meshroute::Node::attach_state_name(as)); out.println(\')\');\n'
         '                }\n'),
    ), ('S14',)),

    ('X8 B214 hide recovering behind a default arm', CMDS, (
        ('            case meshroute::Node::MobileAttachState::recovering:\n',
         '            default:\n'),
    ), ('S13',)),

    ('X9 B214 silence attached-without-home instead of reporting inconsistency', CMDS, (
        ('                else     out.println(F("INCONSISTENT: attached with no home id"));\n',
         '                else     { }\n'),
    ), ('S16',)),

    ('X10 B214 hand-spell attachment labels instead of using attach_state_name', CMDS, (
        ('out.print(meshroute::Node::attach_state_name(as));',
         'out.print(as == meshroute::Node::MobileAttachState::dormant ? "dormant" : '
         'as == meshroute::Node::MobileAttachState::seeking ? "seeking" : '
         'as == meshroute::Node::MobileAttachState::claiming ? "claiming" : "recovering");'),
    ), ('S15',)),
]

for idx, (label, path, steps, expect_ids) in enumerate(B214_CTL):
    dest, err = mutate_steps(path, steps, f'b214_ctl{idx}_' + os.path.basename(path))
    if dest is None:
        print(f'{label}\n   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    paths = {CMDS: CMDS, CMDSH: CMDSH, FWMAIN: FWMAIN, HELP: HELP}
    paths[path] = dest
    rows = structural.check(paths[CMDS], paths[CMDSH], paths[FWMAIN], paths[HELP], NVH, CFGCPP)
    status = {cid: ok for cid, _d, ok, _x in rows}
    flipped = [cid for cid in expect_ids if not status.get(cid, True)]
    failed = [cid for cid, _d, ok, _x in rows if not ok]
    if not flipped:
        print(f'{label}\n   !! STAYED GREEN -- {"/".join(expect_ids)} did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print(f'{label}\n   -> structural {"+".join(failed)} now FAIL (required {"+".join(flipped)})')

# ============================================================== §0g HELP CONTROLS ==================================
# ★ THE STRONGEST FAMILY IN THIS FILE, and the reason slice 0a chose a header seam at all: each control reverts ONE
#   decision of `src/firmware_help.h`, REBUILDS the probe against the mutant, RUNS it, and requires the run to turn
#   red — either a named CHK row or the INVENTORY COMPARISON. A mutation that leaves both green is not a "minor"
#   control, it is proof that the corresponding gate row measures nothing, and it fails this file.
# ★★ THE ORACLE CHANGED IN §0g ([[B291]]). It was a FROZEN pre-slice content multiset that only main's history could
#   regenerate; it is now the GENERATED COMMAND INVENTORY, projected from the REAL dispatchers on the CURRENT tree.
#   ⇒ a control is judged by comparing PRODUCTION OUTPUT against SOURCE — never one help header against another.
# ⚠ Each control names the PROFILE it is observable in. A gated name's absence cannot be tested on a build that
#   compiles it, so those controls run on the reduced profile the owner's rule is actually about.

MANUAL_POINTER = 'docs/manual/command-reference.md'
LONG_A = 'zzza' + 'Z' * 1400     # two of these put the BARE index past the 2048-B stage, and both sort after
LONG_B = 'zzzb' + 'Z' * 1400     # `whoami`, so the ordering row stays green and the STAGE row is what speaks


def _name_line(n):
    """One rendered primary-name line of the real header, byte for byte."""
    return '    out.println(F("%s"));\n' % n


# ---- the ORACLE, and the comparison every help control is judged by ----------------------------------------------
ROWS, _notes, _values, _retests = GEN.build_rows(GEN.REPO_ROOT)


def projected(profile):
    """The primary command names the GENERATOR derives from the real dispatchers for one product profile."""
    return GEN.primary_names(ROWS, GEN.PROFILES[profile])


def rendered_names(stdout, profile):
    """The names a probe binary actually printed, minus the trailing manual pointer.

    ⛔ REFUSES on a missing/duplicated marker or a declared-vs-printed count mismatch. Returning [] from a probe that
      never ran would otherwise flow straight into compare_names() and look like a clean comparison against nothing.
    """
    begin, end = 'HELP-NAMES-BEGIN %s ' % profile, 'HELP-NAMES-END'
    lines = stdout.split('\n')
    starts = [i for i, l in enumerate(lines) if l.startswith(begin)]
    if len(starts) != 1:
        raise RuntimeError('expected exactly one %r marker, found %d' % (begin, len(starts)))
    i = starts[0]
    declared = int(lines[i].split()[-1])
    body = []
    for l in lines[i + 1:]:
        if l == end:
            break
        body.append(l)
    else:
        raise RuntimeError('unterminated HELP-NAMES block')
    if len(body) != declared:
        raise RuntimeError('declared %d index lines, printed %d' % (declared, len(body)))
    return body[:-1] if (body and body[-1] == MANUAL_POINTER) else body


def compare_names(expected, actual):
    """MULTISET **and** ORDER: a deleted name, an extra one, a duplicate and a swap are each caught separately."""
    problems = []
    ce, ca = collections.Counter(expected), collections.Counter(actual)
    for n, k in sorted((ce - ca).items()):
        problems.append('MISSING x%d: %s' % (k, n))
    for n, k in sorted((ca - ce).items()):
        problems.append('UNEXPECTED x%d: %s' % (k, n))
    if not problems and list(expected) != list(actual):
        problems.append('ORDER differs (same multiset, wrong sequence)')
    return problems


HELP_CTL = [
    ('H-C1 DELETE one primary name from the rendered index', 'full_headless',
     _name_line('faults'), '',
     'the inventory comparison (MISSING faults)'),

    ('H-C2 ADD a name no dispatcher accepts', 'full_headless',
     _name_line('whoami'), _name_line('whoami') + _name_line('zzz_not_a_command'),
     'the inventory comparison (UNEXPECTED zzz_not_a_command)'),

    ('H-C3 DUPLICATE one primary name', 'full_headless',
     _name_line('faults'), _name_line('faults') * 2,
     'H2e (strictly ascending => unique) + the inventory comparison'),

    ('H-C4 SWAP two adjacent names (same set, wrong order)', 'full_headless',
     _name_line('cfg') + _name_line('clear_inbox'), _name_line('clear_inbox') + _name_line('cfg'),
     'H2e (bytewise ascending) + the comparison ORDER problem'),

    ('H-C5 a GATED name appears on a build that REFUSES it', 'gateway',
     '#if MR_N_LAYERS < 2\n' + _name_line('team') + '#endif   // MR_N_LAYERS < 2\n', _name_line('team'),
     'H3a `team` + the inventory comparison (UNEXPECTED team on gateway)'),

    ('H-C6 a GATED name DISAPPEARS on a build that compiles it', 'full_headless',
     '#if MR_N_LAYERS < 2 && MR_FEAT_MOBILE\n' + _name_line('mobile')
     + '#endif   // MR_N_LAYERS < 2 && MR_FEAT_MOBILE\n', '',
     'H3a `mobile` + the inventory comparison (MISSING mobile)'),

    ('H-C7 the manual pointer is REMOVED from the index', 'full_headless',
     _name_line('whoami') + '    manual_pointer(out);\n', _name_line('whoami'),
     'H2b/H2c (the last line must be the manual pointer)'),

    ('H-C8 the manual pointer is MOVED off the end', 'full_headless',
     _name_line('whoami') + '    manual_pointer(out);\n',
     '    manual_pointer(out);\n' + _name_line('whoami'),
     'H2b (the pointer must be LAST)'),

    ('H-C9 the manual pointer text is CHANGED', 'full_headless',
     '    out.println(F("docs/manual/command-reference.md"));\n',
     '    out.println(F("docs/manual/COMMANDS.md"));\n',
     'H2b/H2c + the runner\'s own manual-pointer check'),

    ('H-C10 a DESCRIPTION comes back onto a name line', 'full_headless',
     _name_line('faults'), '    out.println(F("faults             the flash fault ring"));\n',
     'H2d (every line must be a BARE name) + the inventory comparison'),

    ('H-C11a `help <topic>` renders a SUCCESSFUL index instead of the retired-form refusal', 'full_headless',
     '    help::render_usage(out);                           // `help <anything>` — the retired form, answered loudly\n',
     '    help::render_index(out);\n',
     'H4b/H4c/H4e (the refusal must not be the index)'),

    ('H-C12a bare `help` and bare `?` DIVERGE', 'full_headless',
     '    if (len == 1 || len == 4) { help::render_index(out); return true; }   // the two BARE index spellings\n',
     '    if (len == 4) { help::render_index(out); return true; }\n'
     '    if (len == 1) { help::render_usage(out); return true; }\n',
     'H1c (the two bare spellings must be byte-identical)'),

    ('H-C13a the index grows PAST MR_CONSOLE_STAGE_BYTES', 'full_headless',
     _name_line('whoami'), _name_line('whoami') + _name_line(LONG_A) + _name_line(LONG_B),
     'H8a (the response no longer fits) + H7 (GuardedConsole drops it)'),
]

for idx, (label, profile, find, repl, expect) in enumerate(HELP_CTL):
    print(label)
    dest, err = mutate(HELP, find, repl, os.path.basename(HELP), subdir=f'hctl{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    md5 = hashlib.md5(open(dest).read().encode()).hexdigest()[:8]
    assert md5 != hashlib.md5(ORIG[HELP].encode()).hexdigest()[:8], 'mutation did not change the file'
    binary = os.path.join(OUT, f'hctl{idx}.bin')
    # The mutated header's directory FIRST; the repo's real src/ (inside FLAGS) then supplies console_sink.h.
    b = subprocess.run([CXX, '-I' + os.path.dirname(dest), *FLAGS, *PROFILE_FLAGS[profile],
                        '-DPROBE_SINK_MD5="realsink"', f'-DPROBE_HELP_MD5="{md5}"',
                        f'-DPROBE_PROFILE="{profile}"', PROBE_MAIN, '-o', binary],
                       capture_output=True, text=True)
    if b.returncode != 0:
        first = (b.stderr.strip().splitlines() or ['(no stderr)'])[0]
        if 'No such file' in b.stderr or os.path.basename(HELP) + ':' not in first:
            print(f'   !! INSTRUMENT FAILURE, not a control result: {first[:120]}')
            rc_all = 1
        else:
            print(f'   -> COMPILE FAILS in the mutated header (the strongest failing-first form): {first[-90:]}')
        continue
    r = subprocess.run([binary], capture_output=True, text=True)
    if f'help md5 = {md5}' not in r.stdout:
        print('   !! the binary does not report the mutated help md5 -- a stale build was measured')
        rc_all = 1
        continue
    fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith('FAIL')]
    # ...and the INVENTORY comparison, which is where a lost/extra/duplicated/reordered name shows up.
    try:
        problems = compare_names(projected(profile), rendered_names(r.stdout, profile))
    except RuntimeError as exc:
        problems = ['name block REFUSED: %s' % exc]
    if not fails and not problems:
        print(f'   !! STAYED GREEN (help md5 {md5}) -- this control proves NOTHING  [expected: {expect}]')
        rc_all = 1
    else:
        print(f'   -> help md5 {md5} [{profile}]: {len(fails)} CHK fail(s), {len(problems)} inventory problem(s): '
              + '; '.join([f[5:52] for f in fails[:2]] + [pr[:52] for pr in problems[:2]]))

# ---- H-C11: the seam's dependency fence + the supplied-Print& rule (STRUCTURAL, and honestly labelled so) ---------
# A direct `Serial.println` in the help renderer cannot even be COMPILED against the probe's transport model, so this
# one is asked of structural.py rather than dressed up as a behavioural row.
for label, steps, expect_ids in [
    ('H-C11 write one help line straight to Serial instead of the supplied Print&',
     [('    out.println(F("faults"));', '    Serial.println(F("faults"));')], ('S3', 'S19')),
    ('H-C12 reach into device state from the help renderer',
     [('inline void render_usage(Print& out) {',
       'inline void render_usage(Print& out) {\n    if (g_node.node_id()) out.println(F("x"));')], ('S19',)),
]:
    dest, err = mutate_steps(HELP, steps, 'hsrc_' + os.path.basename(HELP))
    if dest is None:
        print(f'{label}\n   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    rows = {cid: ok for cid, _d, ok, _x in structural.check(CMDS, CMDSH, FWMAIN, dest, NVH, CFGCPP)}
    flipped = [cid for cid in expect_ids if not rows.get(cid, True)]
    if not flipped:
        print(f'{label}\n   !! STAYED GREEN -- {"/".join(expect_ids)} did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print(f'{label}\n   -> structural {"+".join(flipped)} now FAIL')

# ---- H-C13..15: the ONE dispatch() call — removed, duplicated, or bypassed by a second parser --------------------
# ⛔ RE-ANCHORED (§RADMIN-0c / [[B298]]): the anchor carried the call's TRAILING COMMENT, so correcting that
#   comment's retired topic-help wording made all three controls match 0 times — "CONTROL NOT APPLIED", i.e. three
#   silently unmeasured properties. The anchor is now the CALL ALONE, which is the thing these controls are about;
#   a comment edit can no longer disarm them. (Same lesson as the X3 re-aim above, arriving through a different door.)
ROUTER_CALL = '    if (help_command(line, len, out)) return true;'
for label, steps, expect_ids in [
    ('H-C13 remove the real dispatch() help-router call', [(ROUTER_CALL, '')], ('S17',)),
    ('H-C14 duplicate the dispatch() help-router call', [(ROUTER_CALL, ROUTER_CALL + ROUTER_CALL)], ('S17',)),
    ('H-C15 bypass the router: dispatch() parses `help` itself again', [(
        ROUTER_CALL,
        '    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == \'?\')) '
        '{ dump_help(out); return true; }\n')], ('S17', 'S18')),
]:
    dest, err = mutate_steps(CMDS, steps, 'rtr_' + os.path.basename(CMDS))
    if dest is None:
        print(f'{label}\n   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    rows = {cid: ok for cid, _d, ok, _x in structural.check(dest, CMDSH, FWMAIN, HELP, NVH, CFGCPP)}
    flipped = [cid for cid in expect_ids if not rows.get(cid, True)]
    if not flipped:
        print(f'{label}\n   !! STAYED GREEN -- {"/".join(expect_ids)} did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print(f'{label}\n   -> structural {"+".join(flipped)} now FAIL')

# ---- H-C14a..H-C18a: the ORACLE side. The projection must MOVE when source moves, must never be empty, and must
#      match the real render on an unmutated tree. ⛔ THE FAILURE THIS FAMILY CATCHES: a comparison that reads a
#      stale or empty expectation and reports "0 problems" forever — the exact rot that retired [[B291]]'s baseline.
def _scan_tree(dest):
    """A scratch copy of exactly the generator's SCAN_FILES, so build_rows() can read a MUTATED source tree."""
    for rel in GEN.SCAN_FILES:
        dst = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(os.path.join(GEN.REPO_ROOT, rel), dst)
    return dest


FAULTS_ARM = '    if (len == 6 && !strncmp(line, "faults", 6))   { fw_faults_dump(out);  return true; }\n'
NEW_ARM = '    if (len == 9 && !strncmp(line, "zzznewcmd", 9)) { return true; }\n'

# The real, unmutated render for the two profiles the positive control covers.
real_names, real_ok = {}, True
for prof in ('full_headless', 'gateway'):
    rb = os.path.join(OUT, f'help_real_{prof}.bin')
    b = subprocess.run([CXX, *FLAGS, *PROFILE_FLAGS[prof], '-DPROBE_SINK_MD5="realsink"',
                        '-DPROBE_HELP_MD5="realhelp"', f'-DPROBE_PROFILE="{prof}"', PROBE_MAIN, '-o', rb],
                       capture_output=True, text=True)
    if b.returncode != 0:
        print(f'H-C14a..18a\n   !! INSTRUMENT FAILURE: the unmutated probe did not build for {prof}')
        rc_all = 1
        real_ok = False
        break
    try:
        real_names[prof] = rendered_names(subprocess.run([rb], capture_output=True, text=True).stdout, prof)
    except RuntimeError as exc:
        print(f'H-C14a..18a\n   !! INSTRUMENT FAILURE: {prof} name block REFUSED: {exc}')
        rc_all = 1
        real_ok = False
        break

if real_ok:
    for label, rel, find, repl, why in [
        ('H-C14a a NEW source command must change the EXPECTED help list',
         'src/firmware_commands.cpp', FAULTS_ARM, FAULTS_ARM + NEW_ARM,
         'a command added to a dispatcher would never reach help'),
        ('H-C15a a REMOVED source command must change the EXPECTED help list',
         'src/firmware_commands.cpp', FAULTS_ARM, '',
         'help would keep advertising a verb the build no longer accepts'),
    ]:
        tree = _scan_tree(os.path.join(OUT, 'tree_' + label.split()[0].replace('-', '_')))
        path = os.path.join(tree, rel)
        text = open(path).read()
        if text.count(find) != 1:
            print(f'{label}\n   !! CONTROL NOT APPLIED: anchor found {text.count(find)} times in {rel}')
            rc_all = 1
            continue
        open(path, 'w').write(text.replace(find, repl))
        try:
            mrows, _n, _v, _r = GEN.build_rows(tree)
            problems = compare_names(GEN.primary_names(mrows, GEN.PROFILES['full_headless']),
                                     real_names['full_headless'])
        except Exception as exc:                                     # noqa: BLE001 — a refusal is also a RED
            problems = ['projection REFUSED: %s' % exc]
        if not problems:
            print(f'{label}\n   !! STAYED GREEN -- {why}; this control proves NOTHING')
            rc_all = 1
        else:
            print(f'{label}\n   -> {len(problems)} problem(s) reported, e.g. {problems[0][:70]}')

    # H-C16a: the projection itself must never quietly come back empty.
    try:
        GEN.primary_projection([], GEN.PROFILES['full_headless'])
        print('H-C16a an EMPTY projection must REFUSE\n   !! STAYED GREEN -- it returned a result; an empty '
              'expectation compares clean against anything')
        rc_all = 1
    except GEN.GeneratorError as exc:
        print(f'H-C16a an EMPTY projection must REFUSE\n   -> refused: {str(exc)[:70]}')

    # H-C17a: and an empty RENDERED side must never compare clean against a real projection.
    problems = compare_names(projected('full_headless'), [])
    if not problems:
        print('H-C17a an EMPTY rendered side must not compare clean\n   !! STAYED GREEN -- a probe that emitted '
              'nothing would report success')
        rc_all = 1
    else:
        print(f'H-C17a an EMPTY rendered side must not compare clean\n   -> {len(problems)} problem(s) reported, '
              f'e.g. {problems[0][:60]}')

    # H-C18a: the POSITIVE. Without it every control above could be measuring an already-broken tree.
    bad = {p: compare_names(projected(p), real_names[p]) for p in real_names}
    if any(bad.values()):
        print('H-C18a the UNMUTATED probe equals its SOURCE projection\n   !! it does NOT -- '
              + '; '.join(f'{p}: {v[:2]}' for p, v in bad.items() if v))
        rc_all = 1
    else:
        print('H-C18a the UNMUTATED probe equals its SOURCE projection\n   -> ok ('
              + ', '.join(f'{p}: {len(real_names[p])} names' for p in sorted(real_names)) + ')')

# ============================================================== §0a BLE-REFUSAL CONTROLS ===========================
# ★ The owner ruled 2026-09-04 that help must not be transferred by BLE. These controls revert ONE decision of the
#   guard at a time, re-EXTRACT it from the mutated copy, recompile it beside the real router and require the run to
#   turn red — so the row that caught the slice-0a defect cannot itself rot into a shape that catches nothing.
OLD_GUARD = '    if (((len == 4 || (len > 4 && line[4] == \' \')) && !strncmp(line, "help", 4)) || (len == 1 && line[0] == \'?\'))'

BLE_CTL = [
    ('B-C1 revert to the PRE-0a exact-length guard (`len == 4` only) — the slice-0a defect itself', OLD_GUARD,
     '    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == \'?\'))',
     'B1/B2/B3 — `help <topic>` sails past the guard into dispatch() and streams over NUS'),

    ('B-C2 drop the length test entirely (the guard becomes too WIDE)', OLD_GUARD,
     '    if (!strncmp(line, "help", 4) || (len == 1 && line[0] == \'?\'))',
     'B4 — `helpful` would be swallowed as help'),

    ('B-C3 drop the bare `?` alias from the refusal', OLD_GUARD,
     '    if ((len == 4 || (len > 4 && line[4] == \' \')) && !strncmp(line, "help", 4))',
     'B1/B2 — `?` reaches dispatch() and renders the index over BLE'),
]

for idx, (label, find, repl, expect) in enumerate(BLE_CTL):
    print(label)
    dest, err = mutate(FWMAIN, find, repl, os.path.basename(FWMAIN), subdir=f'bctl{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    try:
        rc, text = ble_guard.build_and_run(dest, CXX, FLAGS, OUT, tag=f'ctl{idx}')
    except ble_guard.GuardError as exc:
        print(f'   -> the guard could not be EXTRACTED from the mutant, which is fail-loud RED: {exc}')
        continue
    if rc == 2:
        print(f'   !! INSTRUMENT FAILURE, not a control result: {text.splitlines()[:1]}')
        rc_all = 1
        continue
    fails = [l.strip() for l in text.splitlines() if l.strip().startswith('FAIL')]
    if not fails:
        print(f'   !! STAYED GREEN -- this control proves NOTHING  [expected: {expect}]')
        rc_all = 1
    else:
        print(f'   -> {len(fails)} executed check(s) fail: ' + '; '.join(f[5:74] for f in fails[:2]))

# B-C4: delete the refusal outright. The extraction must REFUSE (fail loud) — a missing anchor must never read as
#       "no rows to check", which is how an instrument silently stops measuring.
_dest, _err = mutate(FWMAIN,
                     '        return write_err(out, cap, "help", "console_only");\n', '        return 0;\n',
                     os.path.basename(FWMAIN), subdir='bctl_gone')
print('B-C4 delete the BLE console_only refusal outright')
if _dest is None:
    print(f'   !! CONTROL NOT APPLIED: {_err}')
    rc_all = 1
else:
    try:
        ble_guard.extract_guard(open(_dest).read())
        print('   !! STAYED GREEN -- the extractor found a guard that no longer exists; it proves NOTHING')
        rc_all = 1
    except ble_guard.GuardError as exc:
        rows = {cid: ok for cid, _d, ok, _x in structural.check(CMDS, CMDSH, _dest, HELP, NVH, CFGCPP)}
        flipped = [cid for cid in ('S10', 'S11', 'S20') if not rows.get(cid, True)]
        print(f'   -> extraction REFUSES ({str(exc)[:70]}…) and structural {"+".join(flipped) or "NONE"} now FAIL')
        if not flipped:
            print('   !! ...but no structural row flipped; the grep half proves NOTHING')
            rc_all = 1

# B-C5: the same revert, asked of the STRUCTURAL half, so S20 is controlled independently of the executed rows.
_dest, _err = mutate(FWMAIN, OLD_GUARD,
                     '    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == \'?\'))',
                     os.path.basename(FWMAIN), subdir='bctl_s20')
print('B-C5 revert the guard shape — the STRUCTURAL half (S20) must notice')
if _dest is None:
    print(f'   !! CONTROL NOT APPLIED: {_err}')
    rc_all = 1
else:
    rows = {cid: ok for cid, _d, ok, _x in structural.check(CMDS, CMDSH, _dest, HELP, NVH, CFGCPP)}
    if rows.get('S20', True):
        print('   !! STAYED GREEN -- S20 did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print('   -> structural S20 now FAIL (and S10/S11 stay green, which is exactly why S20 had to exist)')


# ==================================================== §RADMIN slice 3 CONTROLS =====================================
# ★★★ NINE CONTROLS ON THE ADMIN BLE GUARD AND TEN ON THE DEVICE-BOUNDARY STRUCTURAL ROWS. Each reverts ONE decision
#     and must turn its OWN named invariant red. ⛔ A control that merely fails to compile, or that reddens some
#     unrelated row, is NOT a control — the classifications below say which half must fire.
ADMIN_GUARD_SRC = ('#if MR_FEAT_RADMIN_ACCEPT\n'
                   '    if (mrfw::admin_verb_owns(line, len))\n'
                   '        return write_err(out, cap, "admin", "console_only");\n'
                   '#endif   // MR_FEAT_RADMIN_ACCEPT\n')

# ---- (a) EXECUTED-ROW controls: the guard still extracts, but its rows must fail ------------------------------
ADMIN_EXEC_CTL = [
    ('A-C1 PARTIAL FAMILY: only `acl` is refused, so the whole `admin-id` half leaks onto the link',
     '    if (mrfw::admin_verb_owns(line, len))',
     '    if (mrfw::admin_verb_owns(line, len) && line[0] == \'a\' && line[1] == \'c\')',
     'A1/A2 — every `admin-id …` form reaches the seam'),
    ('A-C2 LISTING-ONLY ESCAPE: `acl list` / `admin-id show` are let through as "public material"',
     '    if (mrfw::admin_verb_owns(line, len))',
     '    if (mrfw::admin_verb_owns(line, len) && !strstr(line, "list") && !strstr(line, "show"))',
     'A1/A2 — the two listing forms reach the seam, which R-RA-29 refuses'),
    ('A-C3 BROAD PREFIX: a bare `admin` prefix test swallows `admin-key`, the CONTROLLER verb (Slice 4)',
     '    if (mrfw::admin_verb_owns(line, len))',
     '    if (len >= 3 && (!strncmp(line, "admin", 5) || !strncmp(line, "acl", 3)))',
     'A1/A3 — `admin-key show self` is refused as if this node owned it'),
]
for idx, (label, find, repl, expect) in enumerate(ADMIN_EXEC_CTL):
    print(label)
    dest, err = mutate(FWMAIN, find, repl, os.path.basename(FWMAIN), subdir=f'actl{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    try:
        rc, text = ble_guard.build_and_run(dest, CXX, FLAGS, OUT, tag=f'actl{idx}', family='admin')
    except ble_guard.GuardError as exc:
        print(f'   -> the guard could not be EXTRACTED from the mutant, which is fail-loud RED: {exc}')
        continue
    if rc == 2:
        print(f'   !! INSTRUMENT FAILURE, not a control result: {text.splitlines()[:1]}')
        rc_all = 1
        continue
    fails = [l.strip() for l in text.splitlines() if l.strip().startswith('FAIL')]
    if not fails:
        print(f'   !! STAYED GREEN -- this control proves NOTHING  [expected: {expect}]')
        rc_all = 1
    else:
        print(f'   -> {len(fails)} executed check(s) fail: ' + '; '.join(f[5:74] for f in fails[:2]))

# ---- (b) EXTRACTION-REFUSAL controls: the instrument must STOP, never report "no rows" ------------------------
ADMIN_EXTRACT_CTL = [
    ('A-C4 the admin refusal is DELETED outright', ADMIN_GUARD_SRC, ''),
    ('A-C5 the admin refusal is DUPLICATED (two executable anchors — which one is the guard?)',
     ADMIN_GUARD_SRC, ADMIN_GUARD_SRC + ADMIN_GUARD_SRC),
    ('A-C6 the admin refusal is COMMENTED OUT (a guard that reads as present but never runs)',
     '        return write_err(out, cap, "admin", "console_only");',
     '        // return write_err(out, cap, "admin", "console_only");\n        return 0;'),
    ('A-C7 the ENVELOPE is wrong (`acl` instead of the ruled `admin` name)',
     'return write_err(out, cap, "admin", "console_only");',
     'return write_err(out, cap, "acl", "console_only");'),
    ('A-C8 the guard is MOVED BELOW the transport seam, where it can refuse nothing',
     ADMIN_GUARD_SRC, ''),
    ('A-C9 the guard is gated on the WRONG capability (MR_FEAT_RADMIN_CLIENT — the R-RA-8 inversion)',
     '#if MR_FEAT_RADMIN_ACCEPT\n    if (mrfw::admin_verb_owns(line, len))',
     '#if MR_FEAT_RADMIN_CLIENT\n    if (mrfw::admin_verb_owns(line, len))'),
]
for idx, (label, find, repl) in enumerate(ADMIN_EXTRACT_CTL):
    print(label)
    if label.startswith('A-C8'):
        # Move it: delete in place, then re-insert AFTER the seam call. Two steps, one mutant.
        dest, err = mutate_steps(FWMAIN, [
            (ADMIN_GUARD_SRC, ''),
            ('    if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }',
             '    if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }\n' + ADMIN_GUARD_SRC),
        ], os.path.basename(FWMAIN))
    else:
        dest, err = mutate(FWMAIN, find, repl, os.path.basename(FWMAIN), subdir=f'axctl{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    try:
        ble_guard.extract_guard(open(dest).read(), ble_guard.ADMIN_REFUSAL_CALL, ble_guard.ADMIN_GATE)
        print('   !! STAYED GREEN -- the extractor accepted a guard it must refuse; it proves NOTHING')
        rc_all = 1
    except ble_guard.GuardError as exc:
        print(f'   -> extraction REFUSES: {str(exc)[:96]}')

# ---- (c) the DEVICE-BOUNDARY structural rows: one sabotage each ------------------------------------------------
# (file-under-mutation, find, replace, the row that MUST flip)
S3_CTL = [
    ('S-C30 the boot report call is DUPLICATED in setup()', FWMAIN,
     '    mrfw::admin_stores_boot_report_console();',
     '    mrfw::admin_stores_boot_report_console();\n    mrfw::admin_stores_boot_report_console();', 'S30'),
    ('S-C30b the boot call loses its ACCEPT gate (a CLIENT board would run a target-store boot path)', FWMAIN,
     '#if MR_FEAT_RADMIN_ACCEPT\n    mrfw::admin_stores_boot_report_console();\n#endif   // MR_FEAT_RADMIN_ACCEPT',
     '    mrfw::admin_stores_boot_report_console();', 'S30'),
    ('S-C31 the boot report is hoisted ABOVE the filesystem mount/self-heal', FWMAIN,
     '    if (mrnv::mount_or_repair()) {',
     '    mrfw::admin_stores_boot_report_console();\n    if (mrnv::mount_or_repair()) {', 'S31'),
    ('S-C32 the boot report starts WRITING (an auto-generate on a fresh device — design §6.4 forbids it)', CMDS,
     '    mrfw::admin_boot_report(id, acl, lines);',
     '    mrfw::admin_boot_report(id, acl, lines);\n    (void)mrnv::save_acl(mrnv::AclBlob{});', 'S32'),
    ('S-C33 a RESIDENT service is introduced (design §6.2\'s ruling reversed)', CMDS,
     'void admin_stores_boot_report_console() {',
     'static DeviceAclStore s_admin_resident_store;\nstatic mrfw::AclService s_admin_resident(s_admin_resident_store);\n'
     'void admin_stores_boot_report_console() {', 'S33'),
    # ⛔⛔ RE-POINTED 2026-09-07 BY §RADMIN SLICE 5, the old form kept visible. This control injected
    #    `(void)g_node.node_id();` and was checked by S34, whose rule was "the ACCEPT bindings touch NO Node
    #    state" — a rule the slice's ruled live-install seam legitimately ends (design §6.5, R-RA-31). ⇒ the
    #    INJECTED DEFECT is unchanged in shape (an unreviewed Node reach inside an ACCEPT binding) and it is now
    #    checked by **S51**, which pins the Node surface to EXACTLY the three ruled session entry points.
    #    ⓘ Left as-is it STAYED GREEN, i.e. it proved nothing — the runner reported that itself and refused to
    #      score it, which is why this note exists instead of a quietly passing control.
    ('S-C34 the ACCEPT bindings reach into Node state OUTSIDE the three ruled session entry points', CMDS,
     '    mrfw::acl_verb(acl, id, args, len, lines);',
     '    (void)g_node.node_id();\n    mrfw::acl_verb(acl, id, args, len, lines);', 'S51'),
    # ★ AND THE OTHER DIRECTION: the ruled seam is DELETED, so `acl` mutations stop activating anything live.
    ('S-C34b ★★ §RADMIN slice 5: the ACL service loses its live-install seam, so a durable ACL change never '
     'reaches the running node (design §6.5 activation silently gone)', CMDS,
     '    mrfw::AclService     acl(acl_store, &live);',
     '    mrfw::AclService     acl(acl_store);', 'S52'),
    ('S-C34c ★★ §RADMIN slice 5: the IDENTITY service loses its live-install seam, so a root rotation never '
     'reaches the running node and the old pair keeps authenticating', CMDS,
     '    mrfw::AdminIdService svc(store, seed, &live);',
     '    mrfw::AdminIdService svc(store, seed);', 'S52'),
    ('S-C35 load_acl is re-pointed at the ADMIN slot (one record read through the other\'s name)', NVH,
     '    const int n = read_slot(kSlotAcl, &out, sizeof out, &io);',
     '    const int n = read_slot(kSlotAdmid, &out, sizeof out, &io);', 'S35'),
    ('S-C36 /mradmid is added to mount_or_repair()\'s probe list — its corruption would reformat the FS ([[B317]])',
     NVH, '"/mri_dm", "/mri_ch", "/mrfault" }', '"/mri_dm", "/mri_ch", "/mrfault", "/mradmid" }', 'S36'),
    ('S-C37 do_regen starts writing the ACL too (regen would silently reset the authority list)', CMDS,
     'static void do_regen(Print& out) {',
     'static void do_regen(Print& out) {\n    (void)mrnv::save_acl(mrnv::AclBlob{});', 'S37'),
    ('S-C38 handle_leave starts writing /mradmid (leaving a team would rotate the administration root)', CFGCPP,
     'void handle_leave(Print& out) {\n    mrnv::Blob b{}; nv_load_stamped(b);',
     'void handle_leave(Print& out) {\n    (void)mrnv::save_admin_id(mrnv::AdminIdBlob{});\n'
     '    mrnv::Blob b{}; nv_load_stamped(b);', 'S38'),
    ('S-C39 factory_erase stops erasing wholesale on the ESP32 arm (the new records would SURVIVE a factory reset)',
     NVH, 'const bool ok = p.clear();',
     'const bool ok = p.remove("cfg") && p.remove("id") && p.remove("peers");   /* per-record, not wholesale */',
     'S39'),
]
for idx, (label, target, find, repl, row) in enumerate(S3_CTL):
    print(label)
    dest, err = mutate(target, find, repl, os.path.basename(target), subdir=f'sctl3{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    args = {CMDS: [dest, CMDSH, FWMAIN, HELP, NVH, CFGCPP],
            CMDSH: [CMDS, dest, FWMAIN, HELP, NVH, CFGCPP],
            FWMAIN: [CMDS, CMDSH, dest, HELP, NVH, CFGCPP],
            HELP: [CMDS, CMDSH, FWMAIN, dest, NVH, CFGCPP],
            NVH: [CMDS, CMDSH, FWMAIN, HELP, dest, CFGCPP],
            CFGCPP: [CMDS, CMDSH, FWMAIN, HELP, NVH, dest]}[target]
    try:
        rows = {cid: ok for cid, _d, ok, _x in structural.check(*args)}
    except Exception as exc:                                   # noqa: BLE001 — an instrument crash is never a RED
        print(f'   !! INSTRUMENT FAILURE, not a control result: {type(exc).__name__}: {str(exc)[:70]}')
        rc_all = 1
        continue
    if rows.get(row, True):
        print(f'   !! STAYED GREEN -- {row} did not notice; this control proves NOTHING')
        rc_all = 1
    else:
        flipped = [c for c, ok in rows.items() if not ok]
        print(f'   -> structural {"+".join(flipped)} now FAIL (required: {row})')

# ★ The real sources must be untouched, and we assert it rather than trusting that we never wrote them.
for p, t in ORIG.items():
    assert hashlib.md5(open(p).read().encode()).hexdigest() == hashlib.md5(t.encode()).hexdigest(), \
        f'FATAL: {p} changed -- controls must only ever mutate a copy'
# §0g: the help family is HELP_CTL (13 rendered-index mutations) + 2 structural (H-C11/H-C12)
# + 3 router (H-C13..H-C15) + 5 oracle (H-C14a..H-C18a) = len(HELP_CTL) + 10.
# §RADMIN slice 3: + the ADMIN BLE family (3 executed-row + 6 extraction-refusal) and the 11 device-boundary
# structural sabotages, one per new claim (S30 twice: duplicated call AND lost gate).

# ================================================================================================================
# §RADMIN SLICE 4 — the CONTROLLER guard's and the CONTROLLER boundary's own sabotage controls.
# ⛔ NOT A COPY of the Slice 3 set: R-RA-30's rule is a SUB-VERB SPLIT, so it has two failure directions the
#    whole-family rule does not — a MUTATION escaping (too narrow) and the ruled PUBLIC listing being disabled
#    (too wide). Both are controlled below, and D4 is the row that can see the second.
# ================================================================================================================
CLIENT_GUARD_SRC = ('#if MR_FEAT_RADMIN_CLIENT\n'
                    '    if (mrfw::admin_client_ble_refuses(line, len))\n'
                    '        return write_err(out, cap, "admin-client", "console_only");\n'
                    '#endif   // MR_FEAT_RADMIN_CLIENT\n')

CLIENT_EXEC_CTL = [
    ('D-C1 ★★★ MUTATION ESCAPE: the guard refuses nothing at all — `admin-key export key0` crosses BLE',
     '    if (mrfw::admin_client_ble_refuses(line, len))',
     '    if (false && mrfw::admin_client_ble_refuses(line, len))',
     'D1/D2 — every owned SECRET form reaches the seam'),
    ('D-C2 ★★ PUBLIC LISTING BLOCKED: the guard refuses the WHOLE family, disabling R-RA-30\'s ruled list/show',
     '    if (mrfw::admin_client_ble_refuses(line, len))',
     '    if (mrfw::admin_client_verb_owns(line, len))',
     'D1/D4 — the two ruled public forms are refused'),
    ('D-C3 PARTIAL FAMILY: only `admin-key` is guarded, so the whole `admin-target` half leaks',
     '    if (mrfw::admin_client_ble_refuses(line, len))',
     '    if (mrfw::admin_client_ble_refuses(line, len) && mrfw::admin_primary_is(line, len, "admin-key"))',
     'D1/D2 — every `admin-target …` mutation reaches the seam'),
    ('D-C4 SUB-VERB PREFIX: `list`/`show` become prefixes, so `listen`/`shownothing` sail through',
     '    if (mrfw::admin_client_ble_refuses(line, len))',
     '    if (mrfw::admin_client_ble_refuses(line, len) && !(len > 10 && (strstr(line, " list") '
     '|| strstr(line, " show"))))',
     'D1/D2 — an owned mutation whose text contains `list`/`show` escapes'),
    ('D-C5 BROAD PREFIX: a bare `admin` test swallows `admin-id`/`acl`, the TARGET half\'s own verbs',
     '    if (mrfw::admin_client_ble_refuses(line, len))',
     '    if (len >= 5 && !strncmp(line, "admin", 5))',
     'D1/D3 — `admin-id show` is refused under the CONTROLLER envelope'),
]
for idx, (label, find, repl, expect) in enumerate(CLIENT_EXEC_CTL):
    print(label)
    dest, err = mutate(FWMAIN, find, repl, os.path.basename(FWMAIN), subdir=f'dctl{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    try:
        rc, text = ble_guard.build_and_run(dest, CXX, FLAGS, OUT, tag=f'dctl{idx}', family='client')
    except ble_guard.GuardError as exc:
        print(f'   -> the guard could not be EXTRACTED from the mutant, which is fail-loud RED: {exc}')
        continue
    if rc == 2:
        print(f'   !! INSTRUMENT FAILURE, not a control result: {text.splitlines()[:1]}')
        rc_all = 1
        continue
    fails = [l.strip() for l in text.splitlines() if l.strip().startswith('FAIL')]
    if not fails:
        print(f'   !! STAYED GREEN -- this control proves NOTHING  [expected: {expect}]')
        rc_all = 1
    else:
        print(f'   -> {len(fails)} executed check(s) fail: ' + '; '.join(f[5:74] for f in fails[:2]))

CLIENT_EXTRACT_CTL = [
    ('D-C6 the client refusal is DELETED outright', CLIENT_GUARD_SRC, ''),
    ('D-C7 the client refusal is DUPLICATED (two executable anchors — which one is the guard?)',
     CLIENT_GUARD_SRC, CLIENT_GUARD_SRC + CLIENT_GUARD_SRC),
    ('D-C8 the client refusal is COMMENTED OUT (a guard that reads as present but never runs)',
     '        return write_err(out, cap, "admin-client", "console_only");',
     '        // return write_err(out, cap, "admin-client", "console_only");\n        return 0;'),
    ('D-C9 ★★ THE ENVELOPE COLLIDES with the TARGET family\'s (`admin` for both — a leak becomes unattributable)',
     'return write_err(out, cap, "admin-client", "console_only");',
     'return write_err(out, cap, "admin", "console_only");'),
    ('D-C10 the guard is MOVED BELOW the transport seam, where it can refuse nothing', CLIENT_GUARD_SRC, ''),
    ('D-C11 the guard is gated on the WRONG capability (MR_FEAT_RADMIN_ACCEPT — the R-RA-8 inversion)',
     '#if MR_FEAT_RADMIN_CLIENT\n    if (mrfw::admin_client_ble_refuses(line, len))',
     '#if MR_FEAT_RADMIN_ACCEPT\n    if (mrfw::admin_client_ble_refuses(line, len))'),
]
for idx, (label, find, repl) in enumerate(CLIENT_EXTRACT_CTL):
    print(label)
    if label.startswith('D-C10'):
        dest, err = mutate_steps(FWMAIN, [
            (CLIENT_GUARD_SRC, ''),
            ('    if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }',
             '    if (ex.state == mrfw::LineExec::State::streamed) { ls.flush(); return 0; }\n' + CLIENT_GUARD_SRC),
        ], os.path.basename(FWMAIN))
    else:
        dest, err = mutate(FWMAIN, find, repl, os.path.basename(FWMAIN), subdir=f'dxctl{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    try:
        ble_guard.build_and_run(dest, CXX, FLAGS, OUT, tag=f'dxctl{idx}', family='client')
    except ble_guard.GuardError as exc:
        print(f'   -> extraction REFUSES: {str(exc)[:110]}')
        continue
    print('   !! STAYED GREEN -- the extractor accepted a guard it must have refused')
    rc_all = 1

# ---- (c) the CONTROLLER device-boundary rows S40..S50 --------------------------------------------------------
S4_CTL = [
    ('S-C40 the CLIENT boot report call is DUPLICATED in setup()',
     FWMAIN, '    mrfw::admin_client_stores_boot_report_console();',
     '    mrfw::admin_client_stores_boot_report_console();\n'
     '    mrfw::admin_client_stores_boot_report_console();', 'S40'),
    ('S-C40b the CLIENT boot call loses its gate (an ACCEPT board would run a controller boot path)',
     FWMAIN, '#if MR_FEAT_RADMIN_CLIENT\n    mrfw::admin_client_stores_boot_report_console();\n'
     '#endif   // MR_FEAT_RADMIN_CLIENT',
     '    mrfw::admin_client_stores_boot_report_console();', 'S40'),
    ('S-C42 the CLIENT boot report starts DRAWING entropy (an auto-generate on a fresh controller)',
     CMDS, '    mrfw::admin_client_boot_report(keys, targets, s_targets, lines);',
     '    uint8_t z[32]; mrrng::fill(z, 32);\n'
     '    mrfw::admin_client_boot_report(keys, targets, s_targets, lines);', 'S42'),
    ('S-C43 ★★ A SECOND resident book is introduced (the one-scratch residency ruling reversed)',
     CMDS, 'static mrnv::TargetBlob s_targets;',
     'static mrnv::TargetBlob s_targets;\nstatic mrnv::TargetBlob s_targets_scratch2;', 'S43'),
    ('S-C44 ★★★ THE KEYRING BECOMES RESIDENT — ten master SEEDS move into .bss (design §6.2 reversed)',
     CMDS, 'static mrnv::TargetBlob s_targets;',
     'static mrnv::TargetBlob s_targets;\nstatic mrnv::MgmtKeyBlob s_keys;', 'S44'),
    ('S-C45 the CLIENT bindings reach into Node state',
     CMDS, '    mrfw::admin_client_boot_report(keys, targets, s_targets, lines);',
     '    (void)g_node.node_id();\n    mrfw::admin_client_boot_report(keys, targets, s_targets, lines);', 'S45'),
    ('S-C46 ★★★ THE TWO SEEDS CROSS: load_mgmt_keys is re-pointed at the TARGET-side /mradmid slot',
     NVH, '    const int n = read_slot(kSlotMgmtKeys, &out, sizeof out, &io);',
     '    const int n = read_slot(kSlotAdmid, &out, sizeof out, &io);', 'S46'),
    ('S-C47 /mrtargets is added to mount_or_repair()\'s probe list — its corruption would reformat the FS',
     NVH, 'static const char* const kFiles[] = { "/mrcfg",',
     'static const char* const kFiles[] = { "/mrtargets", "/mrcfg",', 'S47'),
    ('S-C48 ★★ do_regen starts rewriting the keyring — the `preserved` warning becomes a FALSE claim',
     CMDS, '    meshroute::identity_from_seed(g_identity, idb.seed);',
     '    { mrnv::MgmtKeyBlob mk{}; mrnv::mgmt_key_blob_init(mk); (void)mrnv::save_mgmt_keys(mk); }\n'
     '    meshroute::identity_from_seed(g_identity, idb.seed);', 'S48'),
    ('S-C50 ★★ the CLIENT BLE refusal reuses the TARGET family\'s `admin` envelope',
     FWMAIN, 'return write_err(out, cap, "admin-client", "console_only");',
     'return write_err(out, cap, "admin", "console_only");', 'S50'),
]
for idx, (label, target, find, repl, row) in enumerate(S4_CTL):
    print(label)
    dest, err = mutate(target, find, repl, os.path.basename(target), subdir=f'sctl4{idx}')
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    args = {CMDS: [dest, CMDSH, FWMAIN, HELP, NVH, CFGCPP],
            CMDSH: [CMDS, dest, FWMAIN, HELP, NVH, CFGCPP],
            FWMAIN: [CMDS, CMDSH, dest, HELP, NVH, CFGCPP],
            HELP: [CMDS, CMDSH, FWMAIN, dest, NVH, CFGCPP],
            NVH: [CMDS, CMDSH, FWMAIN, HELP, dest, CFGCPP],
            CFGCPP: [CMDS, CMDSH, FWMAIN, HELP, NVH, dest]}[target]
    try:
        rows = {cid: ok for cid, _d, ok, _x in structural.check(*args)}
    except Exception as exc:                                   # noqa: BLE001 — an instrument crash is never a RED
        print(f'   -> the structural reader REFUSED the mutant, which is fail-loud RED: {str(exc)[:100]}')
        continue
    broke = [cid for cid, ok in rows.items() if not ok]
    if row not in broke:
        print(f'   !! STAYED GREEN -- this control proves NOTHING  [required: {row}]')
        rc_all = 1
    else:
        print(f'   -> structural {"+".join(broke)} now FAIL (required: {row})')

# Slice 7a: exact-anchor structural controls. A crashed reader is UNUSABLE, never policy RED.
S7A_CTL = [
    ('A1 strict numeric parser bypassed', CFGCPP, 'const bool parsed = parse_seq_arg(val, value);', 'const bool parsed = true;', 'S63'),
    ('A2 resolver ignores the requested value', CFGCPP, 'remote_activation_resolve(value, remote_activation_live_inputs())', 'remote_activation_resolve(0, remote_activation_live_inputs())', 'S63'),
    ('A3 impossible PHY falls through', CFGCPP, 'out.println(F(" ms at this PHY)")); return;', 'out.println(F(" ms at this PHY)"));', 'S64'),
    ('A4 lower-bound refusal bypassed', CFGCPP, 'activation.state == ActivationState::below_floor ||', 'false ||', 'S65'),
    ('A5 upper-bound refusal bypassed', CFGCPP, 'activation.state == ActivationState::above_ceiling)', 'false)', 'S65'),
    ('A6 bounds clamp instead of refusing', CFGCPP, 'out.println(F(" ms or 0=default)")); return;', 'out.println(F(" ms or 0=default)")); value = activation.floor_ms;', 'S65'),
    ('A7 raw NV assignment lost', CFGCPP, 'b.remote_action_activation_ms = value;', 'b.remote_action_activation_ms = 0;', 'S66'),
    ('A8 checked save bypassed', CFGCPP, 'if (persist && !mrnv::save(b))', 'if (false)', 'S66'),
    ('A9 data turnaround omitted by binding', CMDS, 'g_hal.rx_window_slop_ms(cfg.routing_sf), g_hal.rx_window_slop_ms(data_sf)', 'g_hal.rx_window_slop_ms(cfg.routing_sf), 0', 'S67'),
    ('A10 text raw value substituted', CMDS, 'out.print(g_remote_action_activation_ms);', 'out.print(0);', 'S68'),
    ('A11 JSON binding loses resolved value', CMDS, 'x.remote_action_activation_ms = activation.effective_ms;', 'x.remote_action_activation_ms = 0;', 'S69'),
    ('A12 JSON writer substitutes value', JSONCPP, 'j.u32(x.remote_action_activation_ms);', 'j.u32(0);', 'S70'),
    ('A13 JSON state key renamed', JSONCPP, 'j.lit(",\\"remote_action_activation_state\\":");', 'j.lit(",\\"activation_state\\":");', 'S70'),
    ('A14 restore discards persisted value', FWMAIN, 'g_remote_action_activation_ms = nv.remote_action_activation_ms;', 'g_remote_action_activation_ms = 0;', 'S71'),
    ('A15 boot envelope drifts', FWMAIN, 'F("> remote-activation state=")', 'F("> activation state=")', 'S73'),
    ('A16 cfg buffer shrinks again', CFGCPP, 'char key[kCfgKeyMaxBytes]', 'char key[20]', 'S74'),
    ('A17 overlong token becomes a valid key plus value', CFGCPP, 'if (args[k] && args[k] != \' \')', 'if (false)', 'S74'),
    ('A18 gateway interval stops persisting', CFGCPP, 'b.gw_announce_min_interval_ms = lc.gw_announce_min_interval_ms;', 'b.gw_announce_min_interval_ms = 0;', 'S75'),
    ('A19 canonical seed loses raw value', CFGCPP, 'b.remote_action_activation_ms = g_remote_action_activation_ms;', 'b.remote_action_activation_ms = 0;', 'S76'),
]
boot_at = ORIG[FWMAIN].index('const auto activation = mrfw::remote_activation_resolve(')
boot_start = ORIG[FWMAIN].rfind('#if MR_FEAT_RADMIN_ACCEPT', 0, boot_at)
boot_end = ORIG[FWMAIN].index('\n', ORIG[FWMAIN].index('#endif', boot_at)) + 1
boot_block = ORIG[FWMAIN][boot_start:boot_end]
S7A_STEPS = [
    ('A20 boot report precedes initialization', FWMAIN,
     [(boot_block, ''), ('    if (!g_node.on_init(cfg))', boot_block + '    if (!g_node.on_init(cfg))')], 'S72'),
    ('A21 boot report also runs on refused initialization', FWMAIN,
     [(boot_block, ''), ('    // Install the inbox stores', boot_block + '    // Install the inbox stores')], 'S72'),
    ('A22 boot report loses ACCEPT guard', FWMAIN,
     [(boot_block, boot_block.replace('#if MR_FEAT_RADMIN_ACCEPT', '#if 1'))], 'S73'),
]
for index, (label, target, steps, required) in enumerate(
        [(label, target, [(find, repl)], row) for label, target, find, repl, row in S7A_CTL] + S7A_STEPS):
    print(label)
    dest, err = mutate_steps(target, steps, 's7a_' + str(index) + '_' + os.path.basename(target))
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}'); rc_all = 1; continue
    paths = [CMDS, CMDSH, FWMAIN, HELP, NVH, CFGCPP, JSONCPP]
    paths[paths.index(target)] = dest
    try:
        rows = {cid: ok for cid, _d, ok, _x in structural.check(*paths)}
    except Exception as exc:
        print(f'   !! INSTRUMENT FAILURE, not RED: {exc}'); rc_all = 1; continue
    if rows.get(required, True):
        print(f'   !! STAYED GREEN -- {required} did not flip'); rc_all = 1
    else:
        print(f'   -> structural {required} now FAIL')
S7B_CTL = [
    ('T1 seam scope missing', CMDS, [('CommandContextScope scope(ctx);', '')], 'S77'),
    ('T2 second scope publisher outside seam', FWMAIN,
     [('static void mesh_service_once() {', 'static void mesh_service_once() { mrfw::CommandContextScope extra(ctx);')], 'S77'),
    ('T3 scope never installs context', CONTEXT, [('previous_(exchange(&ctx))', 'previous_(nullptr)')], 'S78'),
    ('T4 scope fails to restore previous context', CONTEXT, [('(void)exchange(previous_);', '(void)previous_;')], 'S78'),
    ('T5 main-loop service call missing', FWMAIN, [('mrfw::remote_executor_service_once();', '')], 'S79'),
    ('T6 executor precedes RX', FWMAIN,
     [('mrfw::remote_executor_service_once();', ''),
      ('static void mesh_service_once() {', 'static void mesh_service_once() { mrfw::remote_executor_service_once();')], 'S79'),
    ('T7 executor loses ACCEPT guard', FWMAIN,
     [('#if MR_FEAT_RADMIN_ACCEPT\n    mrfw::remote_executor_service_once();', '#if 1\n    mrfw::remote_executor_service_once();')], 'S80'),
    ('T8 private filter hides diagnostics too', INBOXCPP,
     [('&& !meshroute::inbox_record_is_internal(e.type)', '&& true')], 'S81'),
    ('T9 remote mark guard removed', INBOXCPP,
     [('if (remote_inbox_refuses(kind, "mark_read", out)) return;', '')], 'S82'),
    ('T10 remote delete guard removed', INBOXCPP,
     [('if (remote_inbox_refuses(kind, "del_msg", out)) return;', '')], 'S82'),
    ('T11 status scalar label changes', CMDS,
     [(' radmin_open_rate_refusal=', ' radmin_wrong_rate_refusal=')], 'S83'),
    ('T12 status reads secret-bearing state instead of scalar snapshot', CMDS,
     [('g_node.radmin_counters()', 'g_node.admin_session_state()')], 'S83'),
    ('T13 status counters lose ACCEPT gate', CMDS,
     [('#if MR_FEAT_RADMIN_ACCEPT\n    const auto radmin', '#if 1\n    const auto radmin')], 'S83'),
]
for index, (label, target, steps, required) in enumerate(S7B_CTL):
    print(label)
    dest, err = mutate_steps(target, steps, 's7b_' + str(index) + '_' + os.path.basename(target))
    if dest is None:
        print(f'   !! CONTROL NOT APPLIED: {err}'); rc_all = 1; continue
    paths = [CMDS, CMDSH, FWMAIN, HELP, NVH, CFGCPP, JSONCPP, INBOXCPP, CONTEXT]
    paths[paths.index(target)] = dest
    try:
        rows = {cid: ok for cid, _d, ok, _x in structural.check(*paths)}
    except Exception as exc:
        print(f'   !! INSTRUMENT FAILURE, not RED: {exc}'); rc_all = 1; continue
    if rows.get(required, True):
        print(f'   !! STAYED GREEN -- {required} did not flip'); rc_all = 1
    else:
        print(f'   -> structural {required} now FAIL')
for path, original in ORIG.items():
    assert open(path).read() == original, f'FATAL: {path} changed'
n_s7a = len(S7A_CTL) + len(S7A_STEPS)
n_radmin3 = len(ADMIN_EXEC_CTL) + len(ADMIN_EXTRACT_CTL) + len(S3_CTL)
n_radmin4 = len(CLIENT_EXEC_CTL) + len(CLIENT_EXTRACT_CTL) + len(S4_CTL)
print(f'\nreal sources verified UNCHANGED; {len(SINK_CTL)} sink + '
      f'{len(SRC_CTL) + len(B214_CTL)} source + {len(HELP_CTL) + 2 + 3 + 5} help + {len(BLE_CTL) + 2} BLE + '
      f'{n_radmin3} radmin3 + {n_radmin4} radmin4 controls run '
      f'+ {n_s7a} activation controls '
      f'+ {len(S7B_CTL)} executor ownership controls '
      f'(CONTROLS-TOTAL {len(SINK_CTL) + len(SRC_CTL) + len(B214_CTL) + len(HELP_CTL) + 10 + len(BLE_CTL) + 2 + n_radmin3 + n_radmin4 + n_s7a + len(S7B_CTL)})')
sys.exit(rc_all)
