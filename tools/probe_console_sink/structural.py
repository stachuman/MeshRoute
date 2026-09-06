#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §B95 STRUCTURAL CHECKS — brief §7 tests 7 and 8 plus invariant 9.
#
# WHY GREP AND NOT A BEHAVIOURAL TEST, STATED PLAINLY: `dump_help`, `print_sf_list` and `ble_dispatch_line` live in
# `src/firmware_commands.cpp` / `src/fw_main.cpp`, TUs that cannot be host-compiled (they need g_node, mrnv, the JSON
# writers, RadioLib, the board glue). No native test and no simulator compiles them either. So these three facts are
# asserted against the SOURCE. They are weaker than the behavioural rows in probe_main.cpp and are labelled as such —
# but each one has a NEGATIVE CONTROL that reinstates the old code in a COPY and proves the check turns red, which is
# what makes them worth having at all.
#
# Every path arrives by argv (never hardcoded), and nothing is ever written: the file objects are opened read-only.
import re
import sys

import ble_guard   # §0a: the BLE guard's condition is EXTRACTED, never re-typed here

def _neutral(txt):
    """A same-LENGTH copy in which comments are blanked and braces inside string/char literals are blanked.

    ★ BOTH halves of this are BUG FIXES, found by the controls in this very file's first run:
      • a lone `}` in a COMMENT inside `service_console()` truncated the brace-matched body, so the S9 check read a
        body that stopped 6 lines early and reported the `mrcon.service()` call missing when it was right there;
      • a `Serial.` written in a COMMENT (this fix's own explanation of the deleted bypass) made the "no direct Serial
        call" check report a call that does not exist.
    Offsets are preserved (characters are replaced, never removed) so indices computed here address the raw text.
    """
    out = list(txt)
    i, n = 0, len(txt)
    while i < n:
        c = txt[i]
        if c == '/' and i + 1 < n and txt[i + 1] == '/':
            while i < n and txt[i] != '\n':
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and txt[i + 1] == '*':
            while i < n and not (txt[i] == '*' and i + 1 < n and txt[i + 1] == '/'):
                if txt[i] != '\n':
                    out[i] = ' '
                i += 1
            out[i] = out[min(i + 1, n - 1)] = ' '
            i += 2
        elif c == '"' or c == "'":
            q = c
            i += 1
            while i < n and txt[i] != q:
                if txt[i] == '\\':
                    i += 1
                elif txt[i] in '{}':
                    out[i] = ' '
                i += 1
            i += 1
        else:
            i += 1
    return ''.join(out)

def _no_strings(txt):
    """`_neutral` plus: the BODY of every string/char literal is blanked too (same length).

    ⚠ WHY IT IS SEPARATE: most rows here must SEE string literals (S12..S16 count display labels), so `_neutral`
      deliberately keeps them. But a row that looks for a forbidden IDENTIFIER must not: S19's `new ` matched the
      help text `team new                   mint a team …` on its first run and reported an allocation that does
      not exist. A token check reads code; a label check reads text. They need different views.
    """
    out = list(_neutral(txt))
    src = txt
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                i += 1
        elif c == '/' and i + 1 < n and src[i + 1] == '*':
            i += 2
            while i < n and not (src[i] == '*' and i + 1 < n and src[i + 1] == '/'):
                i += 1
            i += 2
        elif c == '"' or c == "'":
            q = c
            i += 1
            while i < n and src[i] != q:
                if src[i] == '\\':
                    out[i] = ' '
                    i += 1
                if i < n:
                    if src[i] != '\n':
                        out[i] = ' '
                    i += 1
            i += 1
        else:
            i += 1
    return ''.join(out)

def _args_of(txt, call_start):
    """Return the argument text of a call whose '(' follows call_start, honouring nesting."""
    i = txt.index('(', call_start) + 1
    depth, j = 1, i
    while j < len(txt) and depth:
        if txt[j] == '(':
            depth += 1
        elif txt[j] == ')':
            depth -= 1
            if depth == 0:
                break
        j += 1
    return txt[i:j]

def _body(txt, signature):
    """The text of a function body located by its SIGNATURE (never by line number), brace-balanced."""
    i = txt.index(signature)
    i = txt.index('{', i)
    depth, j = 1, i + 1
    while j < len(txt) and depth:
        if txt[j] == '{':
            depth += 1
        elif txt[j] == '}':
            depth -= 1
        j += 1
    return txt[i:j]

def check(cmds_cpp_path, cmds_h_path, fw_main_path, help_h_path, device_nv_path=None, config_cpp_path=None):
    """-> list of (id, description, ok, detail).

    ★ §RADMIN slice 3 added the last two paths. They are OPTIONAL only so an older caller still runs; the runner
      always passes them, and when they are absent the S30..S39 device-boundary rows are simply not produced —
      ⛔ never silently reported as green (a missing row moves the pinned count, which is the refusal).
    """
    # Every check below reads the NEUTRALISED text: a comment is not a call, and a brace in a string is not a block.
    cmds_raw = open(cmds_cpp_path).read()      # RAW: the B298 comment census below reads COMMENTS, so it must not
    hdr_raw = open(cmds_h_path).read()          #      use the neutralised view every other row needs.
    cmds = _neutral(cmds_raw)
    hdr = _neutral(hdr_raw)
    fwm = _neutral(open(fw_main_path).read())
    help_raw = open(help_h_path).read()
    helph = _neutral(help_raw)
    helph_code = _no_strings(help_raw)     # identifier-level view: no comments AND no string bodies
    out = []

    def add(cid, desc, ok, detail=''):
        out.append((cid, desc, bool(ok), detail))

    # ---- brief test 7: help performs NO direct Serial write, and writes through its sink ---------------------------
    direct = [m.start() for m in re.finditer(r'\bSerial\s*\.', cmds)]
    add('S1', 'firmware_commands.cpp makes NO direct Serial call',
        not direct, f'{len(direct)} occurrence(s)')
    add('S2', 'the hl() direct-Serial help bypass is gone',
        not re.search(r'\bhl\s*\(\s*F\s*\(', cmds), '')
    # ★ §0a/[[B208]]: `dump_help()` no longer exists; §0g (owner 2026-09-05) then RETIRED the nine topic sections
    #   and the topic index. What remains in `src/firmware_help.h` is the bare primary-name list, the manual pointer
    #   and the one refusal — which the probe binary COMPILES AND RUNS, so S3/S4 are the weaker, STRUCTURAL half of
    #   a check whose strong half is behavioural (probe_main.cpp) plus the inventory comparison (run.sh).
    #   ⛔ S18 below is what keeps the old location from quietly coming back.
    # ⓘ THE FLOOR MOVED 90 -> 45 IN §0g, and it is a floor, not a count: the descriptive text is gone, so the file
    #   now emits ~one line per primary command (51 on this tree). The EXACT set is proven against the generated
    #   command inventory, never by this row — a threshold cannot tell a lost name from a renamed one.
    n_sink = len(re.findall(r'\bout\.print(?:ln)?\s*\(', helph))
    add('S3', 'every firmware_help.h emission goes through its Print& out sink',
        n_sink >= 45 and 'mrcon.' not in helph_code and 'Serial.' not in helph_code,
        f'{n_sink} out.print* calls')
    # Every RESPONSE must end terminated: an unterminated tail would be closed only at the next service() pass and
    # could fuse with whatever the console prints next (§B95 invariant 4). §0g retired `topic_names`, the one
    # deliberate FRAGMENT, so the rule is now absolute: no `out.print(` at all, and every renderer ends in println.
    unterminated = []
    for m in re.finditer(r'\binline\s+void\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*Print&\s*out\s*\)', helph):
        fname = m.group(1)
        try:
            fbody = _body(helph, m.group(0))
        except ValueError:
            fbody = ''
        emissions = re.findall(r'\bout\.(print|println)\s*\(', fbody)
        if not emissions:
            continue
        if emissions[-1] != 'println':
            unterminated.append(fname)
    fragments = re.findall(r'\bout\.print\s*\(', helph)
    add('S4', 'every help RESPONSE ends terminated; §0g leaves NO fragment renderer',
        not unterminated and not fragments,
        f'unterminated={unterminated or "none"} bare out.print calls={len(fragments)}')

    # ---- §0a/[[B208]]: the ONE help-router call, at the head of the real dispatch() ------------------------------
    # ⛔ THE POINT OF THIS ROW: the behavioural rows in probe_main.cpp prove the ROUTER is correct; only this one
    #    proves the FIRMWARE ACTUALLY CALLS IT — and calls it once, before every other verb, with no second parser
    #    left behind in the caller. Removing, duplicating or bypassing the call is a controlled mutation (negctl X11).
    try:
        disp = _body(cmds, 'bool dispatch(const char* line, size_t len, Print& out)')
    except ValueError:
        disp = ''
    n_router = len(re.findall(r'\bhelp_command\s*\(\s*line\s*,\s*len\s*,\s*out\s*\)', disp))
    router_at = disp.find('help_command(')
    first_cmp = min([i for i in (disp.find('strncmp('), disp.find('strcmp(')) if i >= 0] or [-1])
    add('S17', 'dispatch() calls the help router EXACTLY ONCE, before every other verb',
        bool(disp) and n_router == 1 and router_at >= 0 and first_cmp > router_at,
        f'calls={n_router} router@{router_at} first_verb_compare@{first_cmp}')
    # The help TEXT must not have been left behind (or come back) in the un-compilable TU: no dump_help, and no
    # `help`/`?` literal recognition anywhere in this file's dispatch.
    leftovers = []
    if 'dump_help' in cmds:
        leftovers.append('dump_help')
    if re.search(r'strncmp\s*\(\s*line\s*,\s*"help"', disp):
        leftovers.append('a second "help" compare in dispatch()')
    add('S18', 'the help text and its recognition have LEFT firmware_commands.cpp for good', not leftovers,
        ';'.join(leftovers))
    # The seam is only compilable-by-a-probe while it stays narrow. Each name below re-welds it to the TU that no
    # host build can compile, and would silently kill the executable half of the gate.
    forbidden = [n for n in (r'\bg_node\b', r'\bmrnv\b', r'\bSerial\b', r'\bmrcon\b', r'\bmalloc\b',
                             r'\bnew\b', r'\bstd::string\b', r'\bEEPROM\b', r'\bstatic\b',
                             'firmware_commands\\.h', 'device_nv\\.h')
                 if re.search(n, helph_code)]
    add('S19', 'firmware_help.h stays a narrow Print&-only unit (no device state, no allocation)', not forbidden,
        ';'.join(forbidden))

    # ---- brief test 8: print_sf_list takes its sink, and no global-console path remains ----------------------------
    add('S5', 'print_sf_list is DEFINED as (Print& out, uint16_t)',
        re.search(r'void\s+print_sf_list\s*\(\s*Print&\s*out\s*,\s*uint16_t', cmds), '')
    add('S6', 'print_sf_list is DECLARED with the sink in the header',
        re.search(r'void\s+print_sf_list\s*\(\s*Print&\s*', hdr), '')
    try:
        sf_body = _body(cmds, 'void print_sf_list(Print& out, uint16_t bitmap)')
    except ValueError:
        sf_body = ''
    add('S7', 'print_sf_list writes ONLY to its sink (no mrcon/Serial)',
        sf_body and 'mrcon.' not in sf_body and 'Serial.' not in sf_body, '')
    bad = []
    for path, txt in ((cmds_cpp_path, cmds), (fw_main_path, fwm)):
        for m in re.finditer(r'\bprint_sf_list\s*\(', txt):
            a = _args_of(txt, m.start())
            if a.count(',') < 1:                       # a call/decl with fewer than two arguments = the old shape
                bad.append(f'{path.split("/")[-1]}:{txt[:m.start()].count(chr(10)) + 1}')
    add('S8', 'EVERY print_sf_list site passes a sink', not bad, ';'.join(bad))

    # ---- §0b/[[B279]]: the BOOT caller NAMES its sink ---------------------------------------------------------------
    # ⛔ WHY A STRUCTURAL ROW AND NOT A BEHAVIOURAL ONE — the same reason S5..S8 are structural. `print_identity` has
    #    exactly two callers: `do_regen`, which `tools/probe_inbox_verbs` drives THROUGH THE REAL `dispatch()` (its
    #    R1..R32 rows are the behavioural half, including the formatter's own bytes), and `setup()`'s boot banner,
    #    which lives in `fw_main.cpp` — a TU no host build compiles. So "boot passes `mrcon`, explicitly, exactly
    #    once" is asserted against the SOURCE, and negctl X12 is its control.
    # ⓘ The count is what carries it: after 0b there is NO parameterless overload and NO default sink, so a call site
    #   that named no sink would not compile — but one that named a DIFFERENT sink would, and only this row refuses it.
    pid_args = [_args_of(fwm, m.start()) for m in re.finditer(r'\bprint_identity\s*\(', fwm)]
    pid_boot = [a for a in pid_args if re.fullmatch(r'\s*idb\s*,\s*mrcon\s*', a)]
    add('S21', 'fw_main.cpp passes the boot identity formatter its sink EXPLICITLY, exactly once',
        len(pid_args) == 1 and len(pid_boot) == 1,
        f'call_sites={len(pid_args)} explicit_mrcon={len(pid_boot)}')

    # ---- the sink's per-pass service is wired into the console loop -------------------------------------------------
    try:
        sc = _body(fwm, 'static void service_console() {')
    except ValueError:
        sc = ''
    add('S9', 'service_console() calls mrcon.service() once per pass',
        'mrcon.service()' in sc, '')

    # ---- invariant 9: the multi-kilobyte help is refused BEFORE the BLE text fallback -------------------------------
    try:
        ble = _body(fwm, 'static size_t ble_dispatch_line(')
    except ValueError:
        ble = ''
    refusal = ble.find('write_err(out, cap, "help", "console_only")')
    # ⛔ V1 RE-AIM (§RADMIN-0c): the anchor used to be `dispatch(line, len, ls)`, the open-coded router fallback at
    #   the bottom of this caller. 0c replaced it with ONE call to the shared seam, so that string no longer exists
    #   and S11 would have been silently unmeasurable (`find` returning -1 on both sides is not a comparison). The
    #   question is unchanged and so is its answer: is the help family refused BEFORE the line can reach the router?
    #   ⓘ 0c also moved the guard ABOVE the parse. That is neutral by construction — every help spelling is a token
    #     `parse_command` does not know, so it reached the old refusal as `unknown_verb` — and `ble_guard.py` proves
    #     the composition (router owns X ⇒ BLE refuses X) by EXECUTION, which is the strong half of this row.
    fallback = ble.find('exec_console_line(')
    add('S10', 'BLE refuses the help family with a bounded console_only answer', refusal >= 0, '')
    add('S11', '... and does so BEFORE the shared execution seam can reach the router',
        refusal >= 0 and fallback >= 0 and refusal < fallback, f'refusal@{refusal} seam@{fallback}')

    # ---- §RADMIN-0c: the two ONE-CALL adapters, and the seam they share -----------------------------------------
    # ⛔ WHY THESE ARE STRUCTURAL AND NOT BEHAVIOURAL, said plainly. `src/fw_main.cpp` is compiled by NO host build,
    #    so "each caller makes exactly one seam call and keeps no fork of its own" cannot be executed here. What IS
    #    executed is the seam itself — `tools/probe_inbox_verbs` links the real `firmware_commands.cpp` and drives
    #    `exec_console_line` through a real `GuardedConsole` and a real `LineSink`, in both format arms. ⇒ this block
    #    is the wiring half of that pair, exactly as S21 is for the boot identity formatter, and every row below has
    #    a negative control in negctl.py.
    seam_sig = 'LineExec exec_console_line('
    try:
        seam = _body(cmds, seam_sig)
    except ValueError:
        seam = ''

    def _fork_calls(txt):
        return {name: len(re.findall(pat, txt)) for name, pat in
                (('dispatch', r'(?<![A-Za-z_])dispatch\s*\('),
                 ('parse_command', r'\bparse_command\s*\('),
                 ('on_command', r'\bon_command\s*\('))}

    n_seam_sc = len(re.findall(r'\bexec_console_line\s*\(', sc))
    sc_fork = _fork_calls(sc)
    sc_sink = len(re.findall(r'\bexec_console_line\s*\([^;]*?\bmrcon\b', sc, re.S))
    add('S22', 'service_console is a ONE-CALL adapter: one seam call, no router/parser/Node fork of its own',
        bool(sc) and n_seam_sc == 1 and sum(sc_fork.values()) == 0 and sc_sink == 1,
        f'seam_calls={n_seam_sc} residual={sc_fork} passes_mrcon={sc_sink}')

    n_seam_ble = len(re.findall(r'\bexec_console_line\s*\(', ble))
    ble_fork = _fork_calls(ble)
    # The seam's own flush, isolated from the five direct handlers' (`routes`/`peers`/`pull_inbox`/`mark_read`/
    # `del_msg`) by looking only AFTER the seam call: exactly one, so the streamed arm ships once and the buffered
    # and unmatched arms ship nothing.
    after_seam = ble[ble.find('exec_console_line('):] if n_seam_ble else ''
    n_flush_after = len(re.findall(r'\bls\.flush\s*\(\s*\)', after_seam))
    ble_sink_arg = len(re.findall(r'\bexec_console_line\s*\([^;]*?\bls\b[^;]*?\bout\b[^;]*?\bcap\b', ble, re.S))
    add('S23', 'ble_dispatch_line is a ONE-CALL adapter: one seam call, its own sinks, exactly one seam flush',
        bool(ble) and n_seam_ble == 1 and sum(ble_fork.values()) == 0 and n_flush_after == 1
        and ble_sink_arg == 1,
        f'seam_calls={n_seam_ble} residual={ble_fork} flush_after_seam={n_flush_after} '
        f'passes_linesink_and_reply={ble_sink_arg}')

    seam_fork = _fork_calls(seam)
    # ⓘ TWO `on_command` calls is the RIGHT number and is pinned as two, not as one: the seam has two mutually
    #   exclusive format arms and each executes the command once. (`handle_peerkey`/`handle_peername` run their own
    #   `on_command` and are therefore reached INSTEAD of these, never as well as — which is why they sit ahead of
    #   the call on both arms.)
    # ★★ THE ORDER IS PINNED HERE AND ONLY HERE, AND THE REASON IS WORTH STATING: with the measured-EMPTY
    #    router/parser intersection (tools/probe_console_sink/ownership.py, six real profiles), reversing the fork
    #    is BEHAVIOURALLY INVISIBLE — which is precisely why the 0c unification was safe, and precisely why no
    #    executed control can catch a reversal. ⇒ a structural pin is the honest instrument for it, and the
    #    permanent ownership gate is what keeps the licence for that order true.
    seam_order_ok = bool(seam) and 0 <= seam.find('dispatch(line, len, stream)') < seam.find('parse_command(')
    add('S24', 'the seam makes the router/parser fork exactly ONCE, ROUTER-FIRST, and executes per format arm',
        bool(seam) and seam_fork['dispatch'] == 1 and seam_fork['parse_command'] == 1
        and seam_fork['on_command'] == 2 and seam_order_ok,
        f'{seam_fork} router_before_parser={seam_order_ok}')

    add('S25', 'the seam NEVER names a global sink — it writes only to the Print& and the buffer it is handed',
        bool(seam) and 'mrcon' not in seam and 'Serial' not in seam,
        'mrcon=%d Serial=%d' % (seam.count('mrcon'), seam.count('Serial')))

    # The borrowed-body rule: `Command::body` points into the caller's line, so nothing the seam RETURNS may carry a
    # pointer, and the seam may hold no static state that could outlive the call.
    try:
        lx = _body(hdr, 'struct LineExec {')
    except ValueError:
        lx = ''
    add('S26', 'the seam keeps no borrowed body and no static state (LineExec carries no pointer)',
        bool(lx) and '*' not in lx and bool(seam) and not re.search(r'(?<![A-Za-z_])static\b', seam),
        f'LineExec_pointers={lx.count("*")} static_in_seam={len(re.findall(r"(?<![A-Za-z_])static", seam))}')

    # The `HEX` radix, pinned STRUCTURALLY because the probes' shared Arduino fake ignores a print radix — so the
    # executed transcript cannot tell `print(x, HEX)` from `print(x)` and this row is the only thing that can.
    n_dh = len(re.findall(r'\bprint\s*\(\s*cr\.dst_hash\s*,\s*HEX\s*\)', seam))
    n_lp = len(re.findall(r'\bprint\s*\(\s*cr\.layer_path\s*,\s*HEX\s*\)', seam))
    add('S27', 'the text arm still renders dh/lp in HEX (the probe fake cannot see a radix)',
        n_dh == 1 and n_lp == 1, f'dst_hash_HEX={n_dh} layer_path_HEX={n_lp}')

    # ---- [[B298]]: the retired topic-help design must survive only as a WITHDRAWN claim ------------------------
    # ⛔ THE ROW IS ABOUT TENSE, NOT ABOUT WORDS. `#0g` deleted the topic index and the nine `help <topic>` sections;
    #    three comments in `firmware_commands.cpp` went on describing them in the present tense, and one paragraph
    #    in `firmware_commands.h` went on saying the two callers are deliberately NOT retrofitted. The correction
    #    idiom keeps both old designs VISIBLE, so a checker that simply banned the words would force the history to
    #    be deleted — the opposite of what M1/V1 want. ⇒ the rule is: every mention of a retired noun must sit
    #    INSIDE a correction block, i.e. the run of comment lines opened by a `V1 CORRECTION` marker.
    def _outside_corrections(raw, nouns):
        stray, in_block = [], False
        for i, ln in enumerate(raw.split('\n'), 1):
            if 'V1 CORRECTION' in ln:
                in_block = True
            elif in_block and '//' not in ln:
                in_block = False
            if in_block:
                continue
            low = ln.lower()
            if '//' in ln and any(n in low for n in nouns):
                stray.append('%d: %s' % (i, ln.strip()[:70]))
        return stray

    # ★ THE COUNT IS FOUR AND IS DERIVED, NOT ROUNDED: (1) the `firmware_help.h` include annotation, (2) the
    #   §B95/§0a policy block's "help TEXT / compact TOPIC INDEX / `help <topic>` recognition" sentence, (3) that
    #   block's [[B208]] CONTENT half ("a compact index of THIS BUILD's topics … one complete section"), and
    #   (4) the `dispatch()` call-site annotation ("the index, one whole topic section, or the bounded refusal").
    #   ⛔ A pin of 3 would have passed while one of the four went uncorrected — measured, then written down.
    b298 = re.compile(r'V1 CORRECTION \(§RADMIN-0c[^)\n]*\[\[B298\]\]')
    stray_cpp = _outside_corrections(cmds_raw, ('help <topic>', 'topic section', 'topic index', "build's topics"))
    n_corr_cpp = len(b298.findall(cmds_raw))
    add('S28', '[[B298]] no ACTIVE topic-help claim survives in firmware_commands.cpp (4 withdrawn, 0 stray)',
        not stray_cpp and n_corr_cpp == 4,
        'stray=%s corrections=%d' % (stray_cpp or 'none', n_corr_cpp))

    stray_h = _outside_corrections(hdr_raw, ('deliberately not retrofitted', 'opposite orderings'))
    withdrawn_h = bool(b298.search(hdr_raw))
    seam_decl = len(re.findall(r'\bLineExec\s+exec_console_line\s*\(', hdr_raw))
    add('S29', '[[B298]] the header no longer claims the two callers are un-retrofitted, and declares the seam',
        not stray_h and withdrawn_h and seam_decl == 1,
        'stray=%s withdrawn=%s seam_decl=%d' % (stray_h or 'none', withdrawn_h, seam_decl))
    # ---- §0a owner ruling 2026-09-04: the refusal covers the WHOLE help family, not just the bare spellings -----
    # ⛔ WHY THIS ROW HAD TO EXIST. S10/S11 only ever asked WHETHER a refusal is present and WHERE. They were both
    #    green throughout the slice-0a defect, in which `help messaging` sailed past a `len == 4` guard into the
    #    shared dispatch() and streamed 1365 B over BLE-NUS. The SHAPE of the condition is the fact that matters,
    #    and the executed proof is tools/probe_console_sink/ble_guard.py; this row is its structural half.
    guard, guard_err = '', ''
    try:
        with open(fw_main_path) as fh:
            guard = ble_guard.extract_guard(fh.read())
    except Exception as exc:                       # noqa: BLE001 — any extraction failure is a RED row, not a skip
        guard_err = str(exc)
    prefix_ok = bool(guard) and bool(re.search(
        r"len\s*==\s*4\s*\|\|\s*\(\s*len\s*>\s*4\s*&&\s*line\s*\[\s*4\s*\]\s*==\s*' '\s*\)", guard))
    alias_ok = bool(guard) and "'?'" in guard
    add('S20', 'the BLE refusal is a PREFIX test over the whole help family (`help <topic>` included) + the `?` alias',
        prefix_ok and alias_ok, (guard[:110] if guard else 'EXTRACTION FAILED: ' + guard_err))
    # ---- §B214: cfg mobile-reg is derived from the attachment FSM, never from home-id presence alone -------------
    # The historical source comment deliberately quotes the old `UNREGISTERED (scanning)` defect. `cmds` is the
    # neutralised source, so every count below is executable text only. Keep these discriminators counted: deletion
    # must not turn an absence-only check green, and each controlled mutation in negctl.py names the row it reddens.
    try:
        cfg = _body(cmds, 'static void dump_cfg(Print& out)')
        mobile = _body(cfg, 'if (c.is_mobile)')
        attach_switch = _body(mobile, 'switch (as)')
    except ValueError:
        mobile = attach_switch = ''

    n_state_reads = len(re.findall(r'\bg_node\.mobile_attach_state\s*\(\s*\)', mobile))
    n_state_bindings = len(re.findall(
        r'\bconst\s+meshroute::Node::MobileAttachState\s+as\s*=\s*'
        r'g_node\.mobile_attach_state\s*\(\s*\)\s*;', mobile))
    n_as_assignments = len(re.findall(r'\bas\s*=(?!=)', mobile))
    n_home_reads = len(re.findall(r'\bg_node\.mobile_home_id\s*\(\s*\)', mobile))
    n_scanning = mobile.count('"UNREGISTERED (scanning)"')
    add('S12', 'B214 mobile-reg binds its sole state variable directly to the FSM authority',
        bool(mobile) and n_state_reads == 1 and n_state_bindings == 1 and n_as_assignments == 1
        and n_home_reads == 1 and n_scanning == 0,
        f'state_reads={n_state_reads} state_bindings={n_state_bindings} as_assignments={n_as_assignments} '
        f'home_reads={n_home_reads} scanning_literals={n_scanning}')

    states = ('attached', 'dormant', 'seeking', 'claiming', 'recovering')
    case_token = {s: f'case meshroute::Node::MobileAttachState::{s}:' for s in states}
    case_count = {s: attach_switch.count(case_token[s]) for s in states}
    n_switches = len(re.findall(r'\bswitch\s*\(\s*as\s*\)', mobile))
    n_defaults = len(re.findall(r'\bdefault\s*:', attach_switch))
    add('S13', 'B214 attachment dispatch is one exhaustive five-arm switch with no default',
        bool(attach_switch) and n_switches == 1 and all(case_count[s] == 1 for s in states)
        and sum(case_count.values()) == 5 and n_defaults == 0,
        'switches={} cases={} default={}'.format(n_switches, ','.join(f'{s}:{case_count[s]}' for s in states),
                                                 n_defaults))

    pos = {s: attach_switch.find(case_token[s]) for s in states}
    other_pos = [pos[s] for s in states[1:] if pos[s] >= 0]
    attached_end = min(other_pos) if other_pos else len(attach_switch)
    attached_arm = attach_switch[pos['attached']:attached_end] if pos['attached'] >= 0 else ''
    nonattached = attach_switch[attached_end:] if attached_end < len(attach_switch) else ''
    n_registered = attach_switch.count('"REGISTERED home="')
    n_attached_registered = attached_arm.count('"REGISTERED home="')
    n_if_home = len(re.findall(r'\bif\s*\(\s*h\s*\)', attached_arm))
    n_print_home = len(re.findall(r'\bout\.println\s*\(\s*h\s*\)', attached_arm))
    add('S14', 'B214 REGISTERED is attached-only and still requires a nonzero home id',
        bool(attached_arm) and n_registered == 1 and n_attached_registered == 1
        and '"REGISTERED home="' not in nonattached and n_if_home == 1 and n_print_home == 1,
        f'registered={n_registered} attached={n_attached_registered} if_h={n_if_home} println_h={n_print_home}')

    n_unregistered = attach_switch.count('"UNREGISTERED ("')
    n_names = len(re.findall(r'\bmeshroute::Node::attach_state_name\s*\(\s*as\s*\)', attach_switch))
    unreg_at = attach_switch.find('"UNREGISTERED ("')
    name_at = attach_switch.find('meshroute::Node::attach_state_name')
    four_pos = [pos[s] for s in states[1:]]
    grouped = all(p >= 0 and p < unreg_at for p in four_pos) if unreg_at >= 0 else False
    if grouped:
        grouped = 'break;' not in attach_switch[min(four_pos):unreg_at]
    add('S15', 'B214 four non-attached states share UNREGISTERED plus the one state-name formatter',
        grouped and n_unregistered == 1 and n_names == 1 and name_at > unreg_at,
        f'labels_before_output={sum(p >= 0 and p < unreg_at for p in four_pos)}/4 '
        f'unregistered={n_unregistered} attach_state_name={n_names}')

    diagnostic = '"INCONSISTENT: attached with no home id"'
    n_inconsistent = attach_switch.count(diagnostic)
    n_attached_inconsistent = attached_arm.count(diagnostic)
    add('S16', 'B214 attached without a home id emits the explicit inconsistency diagnostic',
        bool(attached_arm) and n_inconsistent == 1 and n_attached_inconsistent == 1
        and re.search(r'\belse\b[^;{}]*out\.println\s*\(\s*F\s*\(\s*'
                      r'"INCONSISTENT: attached with no home id"\s*\)\s*\)', attached_arm, re.S),
        f'inconsistent={n_inconsistent} attached={n_attached_inconsistent}')

    # ================================================================================================
    # §RADMIN slice 3 — THE DEVICE BOUNDARY OF THE TWO TARGET STORES.
    # ⛔ WHAT THESE ROWS ARE NOT: they are ⛔ NOT an execution of `setup()`, ⛔ NOT a power-cut test and ⛔ NOT a
    #    proof that flash behaves. `src/fw_main.cpp` cannot be host-compiled (g_node, mrnv, RadioLib, board glue)
    #    and no native or simulator build touches it, so these are SOURCE facts — the same weaker, explicitly
    #    labelled class as S10/S11/S17..S29 above, and each has a deliberate sabotage control in `negctl.py`.
    # ⓘ The STRONG half lives elsewhere and is named so nobody reads these as the whole gate: the services'
    #   behaviour is `test/test_firmware_admin_{identity,acl,verbs}.cpp`, the REAL router/store/entropy wiring is
    #   `tools/probe_inbox_verbs`, and the executed BLE refusal is `ble_guard.py`.
    # ================================================================================================
    if device_nv_path and config_cpp_path:
        nvr = open(device_nv_path).read()
        nv = _neutral(nvr)
        cfg_cpp = _neutral(open(config_cpp_path).read())

        # ---- the boot call: exactly once, ACCEPT-gated, inside setup(), AFTER the filesystem is mounted -------
        boot_calls = [m.start() for m in re.finditer(r'\badmin_stores_boot_report_console\s*\(', fwm)]
        setup_body = _body(fwm, 'void setup()')
        in_setup = [m.start() for m in re.finditer(r'\badmin_stores_boot_report_console\s*\(', setup_body)]
        gated = re.search(r'#if\s+MR_FEAT_RADMIN_ACCEPT[^#]*?\badmin_stores_boot_report_console\s*\('
                          r'\s*\)\s*;[^#]*?#endif', fwm, re.S)
        add('S30', 'the target-store boot report is called EXACTLY ONCE, from setup(), under MR_FEAT_RADMIN_ACCEPT',
            len(boot_calls) == 1 and len(in_setup) == 1 and bool(gated),
            f'calls={len(boot_calls)} in_setup={len(in_setup)} gated={bool(gated)}')
        mount = setup_body.find('mount_or_repair')
        add('S31', '... and it runs AFTER the filesystem mount/self-heal, never before it',
            mount >= 0 and len(in_setup) == 1 and in_setup[0] > mount, f'mount@{mount} call@{in_setup[:1]}')

        # ---- the boot path WRITES NOTHING and DRAWS NOTHING ---------------------------------------------------
        boot_body = _body(cmds, 'void admin_stores_boot_report_console()')
        add('S32', 'the boot report body performs NO durable write and NO entropy draw',
            not re.search(r'\bsave_(admin_id|acl|id|team_keys|ui_presets|peers|faults)\s*\(', boot_body)
            and not re.search(r'\bmrrng\s*::', boot_body)
            and not re.search(r'\b(generate|rotate|recover)\s*\(', boot_body), '')

        # ---- NO RESIDENT STATE: no file-scope service, blob, identity or static buffer -------------------------
        # ⛔ The search is over the WHOLE TU, not the block: a cache smuggled in anywhere would break design §6.2's
        #    ruling just as thoroughly. `static` locals are included — that is exactly the `s_peers` shape.
        # ★★ WHAT COUNTS AS RESIDENT, AND WHAT DELIBERATELY DOES NOT. The three entry points construct their
        #    services as AUTOMATIC locals — that is the design's ruling working, ⛔ not a violation — so the
        #    detector must catch exactly two shapes and no third:
        #      · anything declared `static` (a function-local static is `s_peers`' shape and is just as resident);
        #      · anything at FILE SCOPE, i.e. column 0.
        #    ⛔ The first cut used `^[^\S\n]*(?:static\s+)?…`, which matched the indented locals too and reported
        #       five "resident" objects on a tree that has none — a check that would have been red forever.
        resident = re.findall(r'(?m)^\s*static\s+(?:mrfw::)?(?:AdminIdService|AclService)\b', cmds)
        resident += re.findall(r'(?m)^(?:mrfw::)?(?:AdminIdService|AclService)\s+\w+\s*[;=(]', cmds)
        resident += re.findall(r'\bstatic\s+(?:mrnv::)?(?:AdminIdBlob|AclBlob|AclRow)\b', cmds)
        resident += re.findall(r'\bstatic\s+(?:meshroute::)?Identity\b', cmds)
        add('S33', 'no RESIDENT administration identity, ACL, service or static record buffer exists (design §6.2)',
            not resident, f'{len(resident)} occurrence(s): {resident[:3]}')
        # ⛔ AND NO Node LINK: the target stores are `src/` state and touch no core member (no sizeof(Node) move).
        acc_blocks = re.findall(r'#if\s+MR_FEAT_RADMIN_ACCEPT(.*?)#endif', cmds, re.S)
        add('S34', 'the ACCEPT bindings touch NO Node state and no legacy single-admin symbol',
            all(not re.search(r'\bg_node\b|\badmin_load\b|\bg_admin_id\b|\bremote_exec\b', b)
                for b in acc_blocks), f'{len(acc_blocks)} ACCEPT block(s)')

        # ---- the typed wrappers address the CORRECT slots -----------------------------------------------------
        admid_body = _body(nv, 'inline AdminIdRead load_admin_id(AdminIdBlob& out)')
        acl_body = _body(nv, 'inline AclRead load_acl(AclBlob& out)')
        save_admid = _line_of(nv, 'inline bool save_admin_id(')
        save_acl = _line_of(nv, 'inline bool save_acl(')
        add('S35', 'load/save_admin_id address kSlotAdmid and NOTHING else; load/save_acl address kSlotAcl',
            ('kSlotAdmid' in admid_body and 'kSlotAcl' not in admid_body and 'kSlotId' not in admid_body
             and 'kSlotAdmid' in save_admid and 'kSlotAcl' not in save_admid
             and 'kSlotAcl' in acl_body and 'kSlotAdmid' not in acl_body
             and 'kSlotAcl' in save_acl and 'kSlotAdmid' not in save_acl), '')

        # ---- the nRF52 self-heal probe list is UNCHANGED ------------------------------------------------------
        # ⛔ [[B317]]: `mount_or_repair()` recovers by formatting the WHOLE FS, so adding an optional store to its
        #    probe list would make THAT store's corruption destroy identity AND config. The list must stay the six.
        kfiles = re.search(r'kFiles\[\]\s*=\s*\{([^}]*)\}', nv)
        kf = kfiles.group(1) if kfiles else ''
        n_kf = kf.count('"') // 2
        add('S36', 'mount_or_repair()\'s self-heal probe list still names the SAME SIX files — neither store added',
            n_kf == 6 and 'mradmid' not in kf and 'mracl' not in kf, f'{n_kf} entries')

        # ---- regen / leave write sets are UNCHANGED -----------------------------------------------------------
        regen = _body(cmds, 'static void do_regen(Print& out)')
        add('S37', 'do_regen()\'s write set is still exactly {/mrid} — it touches neither target store',
            'save_id' in regen
            and not re.search(r'\bsave_(admin_id|acl|team_keys|ui_presets|peers)\s*\(', regen)
            and 'kSlotAdmid' not in regen and 'kSlotAcl' not in regen, '')
        leave = _body(cfg_cpp, 'void handle_leave(')
        add('S38', 'handle_leave()\'s write set is still exactly {/mrcfg} — it touches neither target store',
            not re.search(r'\bsave_(admin_id|acl|id|team_keys|ui_presets|peers)\s*\(', leave)
            and 'kSlotAdmid' not in leave and 'kSlotAcl' not in leave, '')

        # ---- factory_reset still erases BOTH stores with ZERO new code ----------------------------------------
        # ★ The erasure is delivered by the `"mr"` NAMESPACE (ESP32 `clear()`) and the whole-FS `format()` (nRF52),
        #   so a factory reset that started naming records one by one would SILENTLY STOP erasing the new two.
        fe_bodies = re.findall(r'inline bool factory_erase\(\)\s*\{', nv)
        wholesale = ('InternalFS.format()' in nv) and re.search(r'\bp\.clear\s*\(\s*\)', nv)
        add('S39', 'factory_erase() still erases WHOLESALE (namespace clear / whole-FS format), so both new '
                   'records are covered with ⛔ zero new code',
            len(fe_bodies) == 3 and bool(wholesale)   # nRF52 format · ESP32 namespace clear · the host no-op stub
            and 'kSlotAdmid' not in nv[nv.index('inline bool factory_erase()'):]
            .split('mount_or_repair')[0], f'{len(fe_bodies)} arm(s)')
    return out

def _line_of(txt, needle):
    """The single source LINE containing `needle` — for one-line inline wrappers `_body` cannot bracket."""
    i = txt.index(needle)
    return txt[txt.rfind('\n', 0, i) + 1:txt.find('\n', i)]


def main(argv):
    if len(argv) not in (5, 7):
        sys.exit('usage: structural.py <firmware_commands.cpp> <firmware_commands.h> <fw_main.cpp> '
                 '<firmware_help.h> [<device_nv.h> <firmware_config.cpp>]')
    rows = check(*argv[1:])
    bad = 0
    for cid, desc, ok, detail in rows:
        if not ok:
            bad += 1
        print(f'   {"ok  " if ok else "FAIL"} {cid} {desc}' + (f'   [{detail}]' if detail else ''))
    print(f'   structural: {len(rows) - bad} passed / {bad} failed / {len(rows)} total')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
