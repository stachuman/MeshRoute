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

import ble_guard
import help_manifest
import structural

if '--' not in sys.argv:
    sys.exit('usage: negctl.py <scratch> <cxx> <sink.h> <cmds.cpp> <cmds.h> <fw_main.cpp> <firmware_help.h> '
             '<help_baseline.json> -- <flags...>')
cut = sys.argv.index('--')
if cut != 9:
    sys.exit(f'usage error: expected 8 paths before "--", got {cut - 1}')
OUT, CXX, SINK, CMDS, CMDSH, FWMAIN, HELP, BASELINE = (os.path.abspath(sys.argv[1]), sys.argv[2],
                                                       *[os.path.abspath(p) for p in sys.argv[3:9]])
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
PROBE_MAIN = os.path.join(HERE, 'probe_main.cpp')      # the REPO's probe, never a scratch copy

ORIG = {p: open(p).read() for p in (SINK, CMDS, CMDSH, FWMAIN, HELP, BASELINE)}
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

    # ⓘ §0a re-aim: the help text moved to firmware_help.h and S4 now asks whether every RESPONSE ends
    #   terminated. Making the index's LAST emission a bare print() is exactly the shape S4 must reject.
    ('X5 leave the index unterminated (print instead of println on its last line)', HELP,
     '    out.println(F("    cfg            the `cfg set <key>` catalog"));',
     '    out.print(F("    cfg            the `cfg set <key>` catalog"));', ('S4',)),
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
            structural.check(paths[CMDS], paths[CMDSH], paths[FWMAIN], paths[HELP])}
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
    rows = structural.check(paths[CMDS], paths[CMDSH], paths[FWMAIN], paths[HELP])
    status = {cid: ok for cid, _d, ok, _x in rows}
    flipped = [cid for cid in expect_ids if not status.get(cid, True)]
    failed = [cid for cid, _d, ok, _x in rows if not ok]
    if not flipped:
        print(f'{label}\n   !! STAYED GREEN -- {"/".join(expect_ids)} did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print(f'{label}\n   -> structural {"+".join(failed)} now FAIL (required {"+".join(flipped)})')

# ============================================================== §0a HELP CONTROLS ==================================
# ★ THE STRONGEST FAMILY IN THIS FILE, and the reason slice 0a chose a header seam at all: each control reverts ONE
#   decision of `src/firmware_help.h`, REBUILDS the probe against the mutant, RUNS it, and requires the run to turn
#   red — either a named CHK row or the frozen-baseline content comparison. A mutation that leaves both green is not
#   a "minor" control, it is proof that the corresponding gate row measures nothing, and it fails this file.
# ⚠ Each control names the PROFILE it is observable in. `mobile` availability cannot be tested on a full build (the
#   topic is available there), so those two controls run on the gateway profile — the reduced build B208's ruling is
#   actually about.
LONG = 'Z' * 1400   # two of these inside one section put it past the 2048-B stage

HELP_CTL = [
    ('H-C1 delete ONE inherited help line from a topic', 'full_headless',
     '    out.println(F("  faults             the flash fault ring"));\n', '',
     'the frozen-baseline content multiset (MISSING)'),

    ('H-C2 duplicate ONE inherited line into a SECOND topic', 'full_headless',
     '    out.println(F("  route add <dest> <next_hop> <hops> [score_q4] | route del <dest>"));\n',
     '    out.println(F("  route add <dest> <next_hop> <hops> [score_q4] | route del <dest>"));\n'
     '    out.println(F("  faults             the flash fault ring"));\n',
     'the frozen-baseline content multiset (UNEXPECTED)'),

    ('H-C3 grow ONE topic past MR_CONSOLE_STAGE_BYTES', 'full_headless',
     '    out.println(F("  clear_inbox confirm                         WIPE both stores (inbox ONLY; keeps everything else)"));\n',
     '    out.println(F("  clear_inbox confirm                         WIPE both stores (inbox ONLY; keeps everything else)"));\n'
     f'    out.println(F("  {LONG}"));\n    out.println(F("  {LONG}"));\n',
     'H3e (the section no longer fits) + H7 (GuardedConsole drops it)'),

    ('H-C4 list an UNAVAILABLE topic in a reduced build', 'gateway',
     '#if MR_HELP_HAS_MOBILE\n'
     '    out.println(F("    mobile         mobile register/gateways/query/status/unregister"));\n'
     '#endif\n',
     '    out.println(F("    mobile         mobile register/gateways/query/status/unregister"));\n',
     'H2a/H2b (the index advertises a family this build refuses)'),

    ('H-C5 ACCEPT an unavailable topic although it is not listed', 'gateway',
     '    else if (an ==  5 && !strncmp(a, "inbox",         5)) help::topic_inbox(out);\n',
     '    else if (an ==  5 && !strncmp(a, "inbox",         5)) help::topic_inbox(out);\n'
     '    else if (an ==  6 && !strncmp(a, "mobile",        6)) help::topic_inbox(out);\n',
     'H4a (an unavailable topic must take the refusal arm)'),

    ('H-C6 OMIT an available topic from the index', 'full_headless',
     '    out.println(F("    identity       whoami, lookup/hashof/nameof/resolve, peers, pubkeys and names"));\n', '',
     'H2a (the index no longer names every available topic)'),

    ('H-C7 make bare `?` differ from bare `help`', 'full_headless',
     '    if (len == 1 || len == 4) { help::render_index(out); return true; }   // the two BARE index spellings',
     '    if (len == 4) { help::render_index(out); return true; }\n'
     '    if (len == 1) { help::topic_inbox(out); return true; }',
     'H1c (the two bare spellings must be byte-identical)'),

    ('H-C8 let a PREFIX select a real section', 'full_headless',
     '    if      (an ==  9 && !strncmp(a, "messaging",     9)) help::topic_messaging(out);',
     '    if      (an >=  9 && !strncmp(a, "messaging",     9)) help::topic_messaging(out);',
     'H5a (`help messagingx` / `help messaging x` must refuse)'),

    ('H-C9 drop the usage line from the refusal', 'full_headless',
     '    out.println(F("> help err unknown_topic (usage: help | ? | help <topic>)"));',
     '    out.println(F("> help err unknown_topic"));',
     'H5c (the refusal must carry usage)'),

    ('H-C10 omit ONE valid topic name from the refusal', 'full_headless',
     '    out.print(F(" messaging identity"));', '    out.print(F(" messaging"));',
     "H5d (the refusal must name EXACTLY this build's topics)"),
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
    # ...and the frozen-baseline comparison, which is where a lost/duplicated line shows up.
    content = help_manifest.content_block(r.stdout, profile)
    problems = help_manifest.compare_content(
        help_manifest.load_baseline(BASELINE)['profiles'][profile]['content'], content)
    if not fails and not problems:
        print(f'   !! STAYED GREEN (help md5 {md5}) -- this control proves NOTHING  [expected: {expect}]')
        rc_all = 1
    else:
        print(f'   -> help md5 {md5} [{profile}]: {len(fails)} CHK fail(s), {len(problems)} baseline problem(s): '
              + '; '.join([f[5:52] for f in fails[:2]] + [p[:52] for p in problems[:2]]))

# ---- H-C11: the seam's dependency fence + the supplied-Print& rule (STRUCTURAL, and honestly labelled so) ---------
# A direct `Serial.println` in the help renderer cannot even be COMPILED against the probe's transport model, so this
# one is asked of structural.py rather than dressed up as a behavioural row.
for label, steps, expect_ids in [
    ('H-C11 write one help line straight to Serial instead of the supplied Print&',
     [('    out.println(F("TEST"));', '    Serial.println(F("TEST"));')], ('S3', 'S19')),
    ('H-C12 reach into device state from the help renderer',
     [('inline void topic_inbox(Print& out) {',
       'inline void topic_inbox(Print& out) {\n    if (g_node.node_id()) out.println(F("x"));')], ('S19',)),
]:
    dest, err = mutate_steps(HELP, steps, 'hsrc_' + os.path.basename(HELP))
    if dest is None:
        print(f'{label}\n   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    rows = {cid: ok for cid, _d, ok, _x in structural.check(CMDS, CMDSH, FWMAIN, dest)}
    flipped = [cid for cid in expect_ids if not rows.get(cid, True)]
    if not flipped:
        print(f'{label}\n   !! STAYED GREEN -- {"/".join(expect_ids)} did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print(f'{label}\n   -> structural {"+".join(flipped)} now FAIL')

# ---- H-C13..15: the ONE dispatch() call — removed, duplicated, or bypassed by a second parser --------------------
ROUTER_CALL = ('    if (help_command(line, len, out)) return true;   // §0a/[[B208]] — the ONE help router '
               '(firmware_help.h):\n')
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
    rows = {cid: ok for cid, _d, ok, _x in structural.check(dest, CMDSH, FWMAIN, HELP)}
    flipped = [cid for cid in expect_ids if not rows.get(cid, True)]
    if not flipped:
        print(f'{label}\n   !! STAYED GREEN -- {"/".join(expect_ids)} did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print(f'{label}\n   -> structural {"+".join(flipped)} now FAIL')

# ---- H-C16/17: the frozen baseline itself, and the anti-vacuity of the comparison --------------------------------
# ⛔ THE FAILURE THIS CATCHES: a comparison that reads a stale/empty baseline and reports "0 problems" forever. Both
#   directions are exercised — a baseline missing a line, and an EMPTY rendered side.
base_real = help_manifest.load_baseline(BASELINE)['profiles']['full_headless']['content']
real_bin = os.path.join(OUT, 'help_real.bin')
b = subprocess.run([CXX, *FLAGS, '-DPROBE_SINK_MD5="realsink"', '-DPROBE_HELP_MD5="realhelp"',
                    '-DPROBE_PROFILE="full_headless"', PROBE_MAIN, '-o', real_bin], capture_output=True, text=True)
if b.returncode != 0:
    print('H-C16/17\n   !! INSTRUMENT FAILURE: the unmutated probe did not build')
    rc_all = 1
else:
    real_out = subprocess.run([real_bin], capture_output=True, text=True).stdout
    real_content = help_manifest.content_block(real_out, 'full_headless')
    for label, expected, actual, why in [
        ('H-C16 a STALE baseline (one line short) must not compare clean',
         base_real[:-1], real_content, 'the comparison would be reading a frozen list that no longer describes 0a'),
        ('H-C17 an EMPTY rendered side must not compare clean',
         base_real, [], 'a probe that emitted nothing would otherwise report success'),
    ]:
        problems = help_manifest.compare_content(expected, actual)
        if not problems:
            print(f'{label}\n   !! STAYED GREEN -- {why}; this control proves NOTHING')
            rc_all = 1
        else:
            print(f'{label}\n   -> {len(problems)} problem(s) reported, e.g. {problems[0][:70]}')
    if help_manifest.compare_content(base_real, real_content):
        print('H-C18 the UNMUTATED probe still matches the frozen baseline\n'
              '   !! it does NOT -- the controls above would all be measuring a broken tree')
        rc_all = 1
    else:
        print(f'H-C18 the UNMUTATED probe still matches the frozen baseline\n'
              f'   -> ok ({len(real_content)} content lines, full_headless)')

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
        rows = {cid: ok for cid, _d, ok, _x in structural.check(CMDS, CMDSH, _dest, HELP)}
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
    rows = {cid: ok for cid, _d, ok, _x in structural.check(CMDS, CMDSH, _dest, HELP)}
    if rows.get('S20', True):
        print('   !! STAYED GREEN -- S20 did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print('   -> structural S20 now FAIL (and S10/S11 stay green, which is exactly why S20 had to exist)')

# ★ The real sources must be untouched, and we assert it rather than trusting that we never wrote them.
for p, t in ORIG.items():
    assert hashlib.md5(open(p).read().encode()).hexdigest() == hashlib.md5(t.encode()).hexdigest(), \
        f'FATAL: {p} changed -- controls must only ever mutate a copy'
print(f'\nreal sources verified UNCHANGED; {len(SINK_CTL)} sink + '
      f'{len(SRC_CTL) + len(B214_CTL)} source + {len(HELP_CTL) + 2 + 3 + 3} help + {len(BLE_CTL) + 2} BLE '
      f'controls run '
      f'(CONTROLS-TOTAL {len(SINK_CTL) + len(SRC_CTL) + len(B214_CTL) + len(HELP_CTL) + 8 + len(BLE_CTL) + 2})')
sys.exit(rc_all)
