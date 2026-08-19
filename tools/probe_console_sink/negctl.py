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

import structural

if '--' not in sys.argv:
    sys.exit('usage: negctl.py <scratch> <cxx> <sink.h> <cmds.cpp> <cmds.h> <fw_main.cpp> -- <flags...>')
cut = sys.argv.index('--')
if cut != 7:
    sys.exit(f'usage error: expected 6 paths before "--", got {cut - 1}')
OUT, CXX, SINK, CMDS, CMDSH, FWMAIN = (os.path.abspath(sys.argv[1]), sys.argv[2],
                                       *[os.path.abspath(p) for p in sys.argv[3:7]])
FLAGS = sys.argv[cut + 1:]
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PROBE_MAIN = os.path.join(HERE, 'probe_main.cpp')      # the REPO's probe, never a scratch copy

ORIG = {p: open(p).read() for p in (SINK, CMDS, CMDSH, FWMAIN)}
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
    b = subprocess.run([CXX, *FLAGS, f'-DPROBE_SINK_MD5="{md5}"', '-I' + os.path.dirname(dest),
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
static void dump_help(Print& out) {'''

SRC_CTL = [
    ('X1 reinstate the direct-Serial hl() help bypass', CMDS,
     'static void dump_help(Print& out) {', HL_OLD, ('S1', 'S2')),

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

    ('X3 drop the BLE console_only refusal for `help`', FWMAIN,
     '    if ((len == 4 && !strncmp(line, "help", 4)) || (len == 1 && line[0] == \'?\'))\n'
     '        return write_err(out, cap, "help", "console_only");\n', '',
     ('S10', 'S11')),

    ('X4 remove the per-pass mrcon.service() from service_console', FWMAIN,
     '    mrcon.service();\n}', '}', ('S9',)),

    ('X5 leave one help line unterminated (print instead of println)', CMDS,
     '    out.println(F("===== MeshRoute console ====="));', '    out.print(F("===== MeshRoute console ====="));',
     ('S4',)),
]

for idx, (label, path, find, repl, expect_ids) in enumerate(SRC_CTL):
    dest, err = mutate(path, find, repl, f'src_ctl{idx}_' + os.path.basename(path))
    if dest is None:
        print(f'{label}\n   !! CONTROL NOT APPLIED: {err}')
        rc_all = 1
        continue
    paths = {CMDS: CMDS, CMDSH: CMDSH, FWMAIN: FWMAIN}
    paths[path] = dest                                      # only the mutated file is swapped
    rows = {cid: ok for cid, _d, ok, _x in structural.check(paths[CMDS], paths[CMDSH], paths[FWMAIN])}
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
    paths = {CMDS: CMDS, CMDSH: CMDSH, FWMAIN: FWMAIN}
    paths[path] = dest
    rows = structural.check(paths[CMDS], paths[CMDSH], paths[FWMAIN])
    status = {cid: ok for cid, _d, ok, _x in rows}
    flipped = [cid for cid in expect_ids if not status.get(cid, True)]
    failed = [cid for cid, _d, ok, _x in rows if not ok]
    if not flipped:
        print(f'{label}\n   !! STAYED GREEN -- {"/".join(expect_ids)} did not flip; this control proves NOTHING')
        rc_all = 1
    else:
        print(f'{label}\n   -> structural {"+".join(failed)} now FAIL (required {"+".join(flipped)})')

# ★ The real sources must be untouched, and we assert it rather than trusting that we never wrote them.
for p, t in ORIG.items():
    assert hashlib.md5(open(p).read().encode()).hexdigest() == hashlib.md5(t.encode()).hexdigest(), \
        f'FATAL: {p} changed -- controls must only ever mutate a copy'
print(f'\nreal sources verified UNCHANGED; {len(SINK_CTL)} sink + '
      f'{len(SRC_CTL) + len(B214_CTL)} source controls run')
sys.exit(rc_all)
