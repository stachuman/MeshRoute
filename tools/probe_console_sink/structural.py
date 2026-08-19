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

def check(cmds_cpp_path, cmds_h_path, fw_main_path):
    """-> list of (id, description, ok, detail)."""
    # Every check below reads the NEUTRALISED text: a comment is not a call, and a brace in a string is not a block.
    cmds = _neutral(open(cmds_cpp_path).read())
    hdr = _neutral(open(cmds_h_path).read())
    fwm = _neutral(open(fw_main_path).read())
    out = []

    def add(cid, desc, ok, detail=''):
        out.append((cid, desc, bool(ok), detail))

    # ---- brief test 7: help performs NO direct Serial write, and writes through its sink ---------------------------
    direct = [m.start() for m in re.finditer(r'\bSerial\s*\.', cmds)]
    add('S1', 'firmware_commands.cpp makes NO direct Serial call',
        not direct, f'{len(direct)} occurrence(s)')
    add('S2', 'the hl() direct-Serial help bypass is gone',
        not re.search(r'\bhl\s*\(\s*F\s*\(', cmds), '')
    try:
        help_body = _body(cmds, 'static void dump_help(Print& out)')
    except ValueError:
        help_body = ''
    n_sink = len(re.findall(r'\bout\.print(?:ln)?\s*\(', help_body))
    add('S3', 'every dump_help line goes through its Print& out sink',
        help_body and n_sink >= 70 and 'mrcon.' not in help_body and 'Serial.' not in help_body,
        f'{n_sink} out.print* calls')
    # Every help line must be a println (a print() without a terminator would leave the tail to the pass boundary).
    n_bare = len(re.findall(r'\bout\.print\s*\(\s*F\s*\(', help_body))
    add('S4', 'no unterminated out.print() inside dump_help', n_bare == 0, f'{n_bare} bare print(F(...))')

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
    fallback = ble.find('dispatch(line, len, ls)')
    add('S10', 'BLE refuses `help` with a bounded console_only answer', refusal >= 0, '')
    add('S11', '... and does so BEFORE the dispatch text fallback',
        refusal >= 0 and fallback >= 0 and refusal < fallback, f'refusal@{refusal} fallback@{fallback}')
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
    return out

def main(argv):
    if len(argv) != 4:
        sys.exit('usage: structural.py <firmware_commands.cpp> <firmware_commands.h> <fw_main.cpp>')
    rows = check(argv[1], argv[2], argv[3])
    bad = 0
    for cid, desc, ok, detail in rows:
        if not ok:
            bad += 1
        print(f'   {"ok  " if ok else "FAIL"} {cid} {desc}' + (f'   [{detail}]' if detail else ''))
    print(f'   structural: {len(rows) - bad} passed / {bad} failed / {len(rows)} total')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
