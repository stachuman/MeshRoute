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
                         '-DMR_UI_ADC_CTRL=37', '-DMR_UI_VBAT_READ=1',   # §UI-9; board_ui.cpp #errors without them
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
 # ★★ §UI-9 REPLACED THE OLD C7, AND THE REPLACEMENT IS EIGHT CONTROLS (C7a-C7h), NOT ONE. C7 used to be
 # "the battery STUB returns a fabricated 3900 instead of -1" — a control with a one-line subject. Task 9 gave the
 # reader real hardware behaviour, so the wrong answers multiplied: the divider can be left enabled (a standing drain),
 # never enabled (every conversion reads a dead net), hardcoded to one polarity (wrong on every board past rev 3.2),
 # stripped of its plausibility guard (a disconnected divider then renders `0.0V` on a safety panel), reduced to one
 # sample, given the wrong combined ADC scale, or — [[B123]] round 2, C7n — parked at the V3.2 MEASURING level on the
 # very path that exists to refuse. Each is a plausible "simplification" and each must turn the probe RED.
 # ⚠ The two `digitalWrite(MR_UI_ADC_CTRL, …)` park/disable lines are IDENTICAL apart from their comments, so every
 #   mutation below carries the comment as part of its anchor — a bare match would hit twice and be refused.
 ('C7a the divider is never DISABLED after the burst (a standing drain)',
  '    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);    // DISABLE — no standing leak between samples',
  '    /* control: the divider is left enabled */;'),
 ('C7b the divider is never ENABLED (every conversion reads a dead net)',
  '    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? HIGH : LOW);    // ENABLE the divider',
  '    /* control: the divider is never enabled */;'),
 ('C7c polarity hardcoded instead of auto-detected (wrong past rev 3.2)',
  '    s_adc_active_high    = (with_pullup == LOW);                     // idle level probed, then inverted',
  '    s_adc_active_high    = false;'),
 # C7k-C7m ★★ THE POLARITY PROBE'S OWN GUARD (the QA finding). C7k is the REFERENCE PORT'S FORM — one read under a bare
 # `INPUT`. It compiles, it works on a board that holds the line, and on a floating one it parks the divider ENABLED
 # half the time. It MUST be red, or nothing separates this port from the defect it was written to avoid.
 ('C7k the probe reverts to ONE read under a bare INPUT (no pull)',
  '''    pinMode(MR_UI_ADC_CTRL, INPUT_PULLUP);
    delayMicroseconds(kPullSettleUs);
    const int with_pullup = digitalRead(MR_UI_ADC_CTRL);
    pinMode(MR_UI_ADC_CTRL, INPUT_PULLDOWN);
    delayMicroseconds(kPullSettleUs);
    const int with_pulldown = digitalRead(MR_UI_ADC_CTRL);

    s_adc_polarity_known = (with_pullup == with_pulldown);           // disagreement == nothing external holds the line
    s_adc_active_high    = (with_pullup == LOW);                     // idle level probed, then inverted''',
  '''    pinMode(MR_UI_ADC_CTRL, INPUT);
    const int with_pullup = digitalRead(MR_UI_ADC_CTRL);
    s_adc_polarity_known = true;
    s_adc_active_high    = (with_pullup == LOW);'''),
 # ⓘ C7l keeps `(void)with_pulldown` deliberately: without it `-Werror=unused-variable` kills the build, and a control
 #   that only proves the COMPILER objects is weaker than one that proves the PROBE objects. Both would be honest
 #   failures; this form measures the behaviour.
 ('C7l a floating line is treated as KNOWN (the coin flip is taken)',
  '    s_adc_polarity_known = (with_pullup == with_pulldown);           // disagreement == nothing external holds the line',
  '    s_adc_polarity_known = true; (void)with_pulldown;'),
 ('C7m the reader ignores the unknown-polarity refusal',
  '    if (!s_adc_polarity_known) return -1;', '    if (false) return -1;'),
 # C7n-C7p ★★★ THE FAIL-SAFE PARK ([[B123]] round 2, independent QA 2026-08-06). C7n IS THE SHIPPED DEFECT RESTORED —
 # one expression on every path, so a FLOATING line parks GPIO 37 HIGH, which Heltec's V3.2 hardware update log
 # documents as the MEASURING level ("now need to pull up the ADC_Ctrl(GPIO 37)") ⇒ the refusal path leaves the divider
 # enabled for ever. It MUST be red, or nothing separates this port from the defect it was written to avoid — and note
 # that the 20-control set shipped before this one was GREEN over exactly that source.
 ('C7n the floating fallback parks the DETECTED level (the shipped B123-round-2 defect)',
  '''    digitalWrite(MR_UI_ADC_CTRL, s_adc_polarity_known ? (s_adc_active_high ? LOW : HIGH)
                                                      : kAdcCtrlFailsafePark);''',
  '    digitalWrite(MR_UI_ADC_CTRL, s_adc_active_high ? LOW : HIGH);'),
 # C7o the fail-safe level itself is inverted. Separates "there is a constant" from "the constant is the right one".
 ('C7o the fail-safe park constant is inverted (HIGH = V3.2 MEASURING)',
  'static constexpr uint8_t kAdcCtrlFailsafePark = LOW;',
  'static constexpr uint8_t kAdcCtrlFailsafePark = HIGH;'),
 # ★★ C7p IS THE CONTROL THAT PROVES THE FIX IS *NOT* THE "HARDCODE THE POLARITY" SPEC §7 AND PLAN TASK 9 FORBID.
 #    Collapse the park to the fail-safe on EVERY path and the MEASUREMENT polarity stops being detected — P6f (an
 #    idle-HIGH board must park HIGH) goes red. Without this control the distinction is only a claim in a comment.
 ('C7p the park is hardcoded to the fail-safe even when detection SUCCEEDED',
  '''    digitalWrite(MR_UI_ADC_CTRL, s_adc_polarity_known ? (s_adc_active_high ? LOW : HIGH)
                                                      : kAdcCtrlFailsafePark);''',
  '    digitalWrite(MR_UI_ADC_CTRL, kAdcCtrlFailsafePark);'),
 ('C7d the plausibility guard is dropped (a dead divider renders 0.0V)',
  '    return (mv > kBattMinMv && mv < kBattMaxMv) ? mv : -1;', '    return mv;'),
 ('C7e an implausible read substitutes a plausible default voltage',
  '    return (mv > kBattMinMv && mv < kBattMaxMv) ? mv : -1;',
  '    return (mv > kBattMinMv && mv < kBattMaxMv) ? mv : 3900;'),
 ('C7f one sample instead of the mean of 8',
  'static constexpr uint8_t  kAdcSamples   = 8;', 'static constexpr uint8_t  kAdcSamples   = 1;'),
 ('C7g the combined ADC scale is dropped from the formula',
  'static constexpr float    kVbatAdcScale = 5.42f;', 'static constexpr float    kVbatAdcScale = 1.0f;'),
 ('C7h the ADC resolution the divisor assumes is never set',
  '    analogReadResolution(kAdcBits);\n', ''),
 # C7i is the CONFUSION the pin names invite and the header warns about in capitals: ADC_CTRL is a CONTROL line, not
 # the ADC input. Sampling it instead of VBAT_READ compiles, runs, and returns numbers.
 ('C7i the burst samples the CONTROL line instead of the ADC input',
  'raw += uint32_t(analogRead(MR_UI_VBAT_READ));', 'raw += uint32_t(analogRead(MR_UI_ADC_CTRL));'),
 ('C7j the control line is driven without ever being made an OUTPUT',
  '    pinMode(MR_UI_ADC_CTRL, OUTPUT);\n', ''),
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
