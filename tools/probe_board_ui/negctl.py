#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §UI-5 NEGATIVE CONTROLS. Revert ONE behaviour at a time and prove the probe turns RED. A probe that stays green
# against a broken source is measuring nothing — the failure mode this project keeps finding.
#
# ⚠⚠ REWRITTEN 2026-08-04 after QA found the rescued version UNSOUND in two ways, both dangerous:
#   (1) it HARDCODED `/home/staszek/MeshRoute` and a session SCRATCH directory, and compiled the SCRATCH copies of
#       `probe_main.cpp`/`probe_ctl` — so it ignored the paths its runner passed and would keep "passing" off files
#       that vanish with the session. QA proved it by passing nonexistent paths: every control still "ran".
#   (2) it wrote the mutation into the REAL `variants/heltec_v3/board_ui.cpp` with no try/finally, so an interrupt
#       LEFT THE WORKING TREE POISONED.
# ⇒ Now: every path arrives by argv, every mutation lands on a COPY under the caller's temp dir, and the real source
#   is opened READ-ONLY and never written. There is nothing to restore because nothing is modified.
import hashlib, os, shutil, subprocess, sys

if len(sys.argv) < 3:
    sys.exit('usage: negctl.py <path/to/board_ui.cpp> <scratch-dir> [cxx] [flags...]')
SRC  = os.path.abspath(sys.argv[1])
OUT  = os.path.abspath(sys.argv[2])
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
CXX  = sys.argv[3] if len(sys.argv) > 3 else os.environ.get('CXX', 'g++')
# ★ Share the runner's configuration rather than restating it — a drift here measures a build the board never makes.
FLAGS = sys.argv[4:] or ['-std=gnu++20', '-fno-exceptions', '-fno-rtti', '-Wall', '-Wextra', '-Werror',
                         '-DMR_FEAT_OLED=1', '-DMR_UI_BTN_PIN=0',
                         '-I' + os.path.join(HERE, 'fakes'), '-I' + os.path.dirname(SRC),
                         '-I' + os.path.join(ROOT, 'lib/hal'), '-I' + os.path.join(ROOT, 'lib/core'),
                         '-I' + os.path.join(ROOT, 'src')]
PROBE_MAIN = os.path.join(HERE, 'probe_main.cpp')     # the REPO's probe, never a scratch copy
orig = open(SRC).read()                                # READ-ONLY. The real file is never written.
print(f'baseline {os.path.relpath(SRC, ROOT)} md5 = {hashlib.md5(orig.encode()).hexdigest()}  ({len(orig)} bytes)\n')

MUT=[
 ('C1 drop the blanking LATCH (edge -> level triggered)',
  '    if (on == s_asleep) return;   // repeat calls are GENUINE no-ops, not merely cheap ones\n', ''),
 ('C2 drop "a blank abandons the open page loop"',
  '    if (on) s_painting = false;', '    if (on) { /* control: latch removed */ }'),
 ('C3 drop next_page()\'s no-frame guard',
  '    if (!s_painting) return false;          // no frame open, or one abandoned by a blank -> touch NO bus\n', ''),
 # ⚠⚠ §UI-6 REPLACED THE OLD C4, and the reason is a real coverage LOSS worth naming rather than papering over. C4 used
 # to mutate `mr_ui_init()`'s boot page loop — "draw the scene ONCE per frame instead of once per page". Task 6 MOVED
 # that loop out of this TU into src/firmware_ui.cpp, which cannot be host-compiled here (fw_context.h pulls RadioLib),
 # so no mutation OF THIS FILE can revert it any more. The caller-side obligation is now covered only STRUCTURALLY, by
 # run.sh, which is weaker; it is registered rather than assumed. What replaces it is a control with a real subject in
 # this TU: the page loop cannot even START without firstPage().
 ('C4 begin_frame() forgets firstPage() (no page loop is ever armed)',
  '    s_u8g2.firstPage();     // clears the page buffer and rewinds to tile row 0 — composes only, touches NO bus',
  '    /* control: firstPage() dropped */;'),
 ('C5 park Vext HIGH instead of the proven LOW',
  'static constexpr uint8_t kVextOnLevel = LOW;', 'static constexpr uint8_t kVextOnLevel = HIGH;'),
 ('C6 button compared against HIGH (wrong polarity)',
  'return digitalRead(MR_UI_BTN_PIN) == LOW;', 'return digitalRead(MR_UI_BTN_PIN) == HIGH;'),
 ('C7 battery stub returns a fabricated millivolt value',
  'int32_t battery_sample_mv() { return -1; }', 'int32_t battery_sample_mv() { return 3900; }'),
 # §UI-6 / §B91: the presence test must be able to say NO. A board_init() that always reports "panel present" is exactly
 # the instrument-that-cannot-fail this project keeps finding — and it is what UI-5 shipped, by having a void return.
 ('C8 board_init() claims the panel is present without asking',
  '    Wire.beginTransmission(kOledAddr);\n    return Wire.endTransmission() == 0;',
  '    return true;'),
]
rc_all = 0
for idx, (label, find, repl) in enumerate(MUT):
    n = orig.count(find)
    if n != 1:
        print(f'!! {label}: substitution matched {n} times, expected 1 -- CONTROL NOT APPLIED'); rc_all = 1; continue
    mut_src = os.path.join(OUT, f'ctl{idx}_board_ui.cpp')
    open(mut_src, 'w').write(orig.replace(find, repl, 1))       # the COPY, under the caller's temp dir
    binary = os.path.join(OUT, f'ctl{idx}')
    b = subprocess.run([CXX] + FLAGS + [PROBE_MAIN, mut_src, '-o', binary],
                       capture_output=True, text=True)
    if b.returncode != 0:
        first = (b.stderr.strip().splitlines() or ['(no stderr)'])[0][:120]
        print(f'{label}\n   -> COMPILE FAILS (the strongest failing-first form): {first}')
        continue
    r = subprocess.run([binary], capture_output=True, text=True)
    fails = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith('FAIL')]
    if not fails:
        print(f'{label}\n   !! STAYED GREEN -- this control proves NOTHING'); rc_all = 1
    else:
        print(f'{label}\n   -> {len(fails)} check(s) fail: ' + '; '.join(f[:70] for f in fails[:3]))

# ★ The real source must be untouched, and we assert it rather than trusting that we never wrote it.
assert hashlib.md5(open(SRC).read().encode()).hexdigest() == hashlib.md5(orig.encode()).hexdigest(), \
       'FATAL: the real board_ui.cpp changed -- controls must only ever mutate a copy'
print(f'\nreal source verified UNCHANGED; {len(MUT)} controls run')
sys.exit(rc_all)
