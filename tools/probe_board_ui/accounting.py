#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# [[B459]] THE BOARD-UI PROBE'S STATIC DECLARATION CENSUS. It reads what the SOURCE declares — the runner's call sites,
# the arm-preprocessed `probe_main.cpp` and `negctl.py`'s two lists — and prints every declared identity with its
# namespace's accepted outcome, one `id<TAB>outcome` per line, before anything is compiled or run.
#
# ⛔ IT COMPARES NOTHING (U1). The runner hands this list to the ONE shared comparator (`tools/probe_accounting.sh`,
#   `pa_compare`) with the checked-in manifest (`expected.tsv`) as the expected set, exactly as the run's own
#   observations are compared later. A second comparator here is what B459's brief forbids.
# ⛔ IT IS FAIL-CLOSED, AND ITS SYNTAX IS DELIBERATELY NARROW. Anything it does not recognise exits 1 with the reason —
#   it never guesses. The recognised declaration shapes, and nothing else:
#   · runner — a comment line (`#` after optional blanks) or a blank line is ignored; a definition line
#     `<name>() {` of the five check functions is ignored;
#     `trait_control "T<n> …"` on one line at column 0                                      -> trait/T<n>
#     the missing-trait loop: `for <var> in <MACRO …>; do` (continued with `\`), a body line
#     `  missing_trait_control "$<var>"` and `done`                                          -> missing/<MACRO>
#     `schk "<id>" …` at any indentation, <id> literal (`S1`), or `S<k>/$<var>` / `S<k>/${<var>%%|*}` inside a
#     column-0 literal `for <var> in <words>; do` … `done` loop (for `%%|*` the word's part before `|`) -> struct/<id>
#     `wchk "W… …" <pred> '<sed>' …` or `wchk_in "$<VAR>[/<literal path>]" "W… …" <pred> '<sed>' …` at column 0,
#     continued with `\`
#                                                    -> wiring/<W-id> and wiring/<W-id>/control/1 … /<number of seds>
#     and ANY other non-comment line naming one of the five functions is an unrecognised shape: exit 1.
#   · probe — the preprocessed text of `probe_main.cpp` itself (line markers select it), directives skipped:
#     `CHK("<id> …"`                                                                           -> canvas/<arm>/<id>
#     `for (int <i> = 0; <i> < <N>; ++<i>) CHK_ITER("<id> …", <i>, …`                 -> canvas/<arm>/<id>/0 … /<N-1>
#   · negctl — the module-level `MUT_V3` / `MUT_V4` list literals; each element a tuple led by its label string
#                                                                                 -> negctl/v3/<id>, negctl/v4/<id>
# ⛔ THE HONEST LIMIT: a declaration removed TOGETHER with its manifest line is invisible here and at runtime — nothing
#   can infer the deleted intent. The manifest diff is what shows such a removal to a reviewer.
#
# Usage: accounting.py census <run.sh> <probe_v3.i> <probe_v4.i> <negctl.py>
import ast, os, re, sys

ACCEPTED = {'canvas': 'pass', 'trait': 'red', 'missing': 'named_compile_error', 'struct': 'pass',
            'wiring': 'pass', 'wiring-control': 'red', 'negctl': 'red'}
FUNCS = ('wchk_in', 'wchk', 'schk', 'trait_control', 'missing_trait_control')
NAMED = re.compile(r'(?<![A-Za-z0-9_])(%s)(?![A-Za-z0-9_])' % '|'.join(FUNCS))


class Unsupported(Exception):
    pass


# ---------------------------------------------------------------------------------------------- shell words (narrow)
def _scan_sq(s, j):          # j at the opening ' -> index after the closing '
    k = s.find("'", j + 1)
    if k < 0: raise Unsupported('unterminated single quote')
    return k + 1

def _scan_cmdsub(s, j):      # j just after `$(` -> index after the matching `)`
    depth = 1
    while j < len(s):
        c = s[j]
        if c == "'": j = _scan_sq(s, j); continue
        if c == '"': j = _scan_dq(s, j); continue
        if c == '\\': j += 2; continue
        if c == '(': depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0: return j + 1
        j += 1
    raise Unsupported('unterminated $( … )')

def _scan_dq(s, j):          # j at the opening " -> index after the closing "
    j += 1
    while j < len(s):
        c = s[j]
        if c == '\\': j += 2; continue
        if c == '"': return j + 1
        if s.startswith('$(', j): j = _scan_cmdsub(s, j + 2); continue
        j += 1
    raise Unsupported('unterminated double quote')

def shell_words(s):
    """Split one statement into words: '…', "…" (with \\-escapes and $( … ) nesting) and bare runs. Returns
    [(raw, first-segment-kind)] with the quotes KEPT in raw (the census reads, never expands)."""
    words, i = [], 0
    while i < len(s):
        if s[i] in ' \t': i += 1; continue
        start, kind = i, None
        while i < len(s) and s[i] not in ' \t':
            c = s[i]
            if c == "'": k = 'sq'; i = _scan_sq(s, i)
            elif c == '"': k = 'dq'; i = _scan_dq(s, i)
            elif c in ';&|<>()`': raise Unsupported(f'operator {c!r} in a declaration statement')
            else: k = 'bare'; i += 1
            kind = kind or k
        words.append((s[start:i], kind))
    return words

def dq_body(raw):
    if not (raw.startswith('"') and raw.endswith('"')): raise Unsupported(f'expected one double-quoted word: {raw[:60]}')
    return raw[1:-1]


# ---------------------------------------------------------------------------------------------- the runner
def loop_words(header):
    m = re.match(r'^for (\w+) in (.*); do$', header)
    if not m: raise Unsupported(f'unrecognised loop header: {header[:80]}')
    words = []
    for raw, kind in shell_words(m.group(2)):
        if '$' in raw or '`' in raw: raise Unsupported(f'a loop word is not literal: {raw[:60]}')
        words.append(raw[1:-1] if kind == 'sq' and raw.endswith("'") and raw.count("'") == 2 else raw)
    if not words: raise Unsupported(f'empty loop list: {header[:80]}')
    return m.group(1), words

def enclosing(loop, var, n, what):
    """-> (var, words) of the column-0 literal loop around a templated call, or Unsupported."""
    if loop is None: raise Unsupported(f'line {n}: {what} outside a column-0 literal for-loop')
    lv, words = loop_words(loop[0])
    if lv != var: raise Unsupported(f'line {n}: {what} uses ${var}, but the enclosing loop (line {loop[1]}) binds ${lv}')
    return lv, words

def runner_census(path):
    src = open(path, encoding='utf-8').read().split('\n')
    out, loop, i = [], None, 0     # loop = (var, words) of the enclosing column-0 literal `for`
    while i < len(src):
        line = src[i]
        if not line.strip() or re.match(r'^\s*#', line): i += 1; continue
        if re.match(r'^done\b', line): loop = None; i += 1; continue
        if re.match(r'^for \w+ in ', line):
            j, header = i, line
            while header.endswith('\\'):
                j += 1; header = header[:-1].rstrip() + ' ' + src[j].strip()
            loop = (header.rstrip(), i + 1)          # parsed only if a templated call inside needs its words
            i = j + 1; continue
        if not NAMED.search(line): i += 1; continue
        n = i + 1
        if re.match(r'^(%s)\(\) \{' % '|'.join(FUNCS), line): i += 1; continue          # a definition
        m = re.match(r'^trait_control "(T\d+) [^"]*"( |$)', line)
        if m:
            if line.endswith('\\'): raise Unsupported(f'line {n}: a continued trait_control call')
            out.append(('trait/' + m.group(1), ACCEPTED['trait'])); i += 1; continue
        m = re.match(r'^  missing_trait_control "\$(\w+)"$', line)
        if m:
            var, words = enclosing(loop, m.group(1), n, 'missing_trait_control')
            for w in words:
                if not re.match(r'^[A-Z][A-Z0-9_]*$', w): raise Unsupported(f'line {n}: a missing-trait word is not a macro: {w}')
                out.append(('missing/' + w, ACCEPTED['missing']))
            i += 1; continue
        m = re.match(r'^\s*schk "([^"]*)" ', line)
        if m:
            tmpl = m.group(1)
            lit = re.match(r'^S\d+[a-z]?$', tmpl)
            var = re.match(r'^(S\d+[a-z]?)/\$(\w+)$', tmpl) or re.match(r'^(S\d+[a-z]?)/\$\{(\w+)%%\|\*\}$', tmpl)
            if lit:
                out.append(('struct/' + tmpl, ACCEPTED['struct']))
            elif var:
                _, words = enclosing(loop, var.group(2), n, f'schk "{tmpl}"')
                for w in words:
                    key = w.split('|', 1)[0] if '%%|*' in tmpl else w
                    if '%%|*' in tmpl and '|' not in w: raise Unsupported(f'line {n}: loop word without `id|…`: {w}')
                    if not re.match(r'^[A-Za-z0-9_]+$', key): raise Unsupported(f'line {n}: an unusable identity key: {key}')
                    out.append((f'struct/{var.group(1)}/{key}', ACCEPTED['struct']))
            else:
                raise Unsupported(f'line {n}: schk identity {tmpl!r} is not a recognised shape')
            j = i
            while src[j].endswith('\\'): j += 1
            i = j + 1; continue
        if re.match(r'^(wchk|wchk_in) ', line):
            j, stmt = i, line
            while stmt.endswith('\\'):
                j += 1; stmt = stmt[:-1] + ' ' + src[j]
            w = shell_words(stmt)
            if w[0][0] == 'wchk_in':
                if len(w) < 5 or not re.match(r'^"\$[A-Z_]+(/[A-Za-z0-9_./-]+)?"$', w[1][0]):
                    raise Unsupported(f'line {n}: wchk_in without a "$VAR" or "$VAR/literal/path" file')
                w = w[2:]
            else:
                w = w[1:]
            label = dq_body(w[0][0])
            mid = re.match(r'^(W\d+[a-z]*(?:-[a-z]+)?) ', label)
            if not mid: raise Unsupported(f'line {n}: a wiring label without a W-identity: {label[:60]}')
            if len(w) < 3 or w[1][1] != 'bare' or not re.match(r'^\w+$', w[1][0]):
                raise Unsupported(f'line {n}: {mid.group(1)} has no predicate name followed by a sed script')
            for raw, kind in w[2:]:
                if kind != 'sq': raise Unsupported(f'line {n}: {mid.group(1)} has a control that is not a quoted sed script')
            wid = mid.group(1)
            out.append((f'wiring/{wid}', ACCEPTED['wiring']))
            out += [(f'wiring/{wid}/control/{k}', ACCEPTED['wiring-control']) for k in range(1, len(w) - 1)]
            i = j + 1; continue
        raise Unsupported(f'line {n}: an unrecognised declaration shape: {line.strip()[:100]}')
    return out


# ---------------------------------------------------------------------------------------------- the canvas probe
def canvas_census(pp_path, arm):
    out, cur = [], None
    for line in open(pp_path, encoding='utf-8', errors='surrogateescape'):
        mk = re.match(r'^# \d+ "([^"]+)"', line)
        if mk: cur = os.path.basename(mk.group(1)); continue
        if cur != 'probe_main.cpp' or re.match(r'^\s*#', line): continue
        code = strip_comments(line)
        for m in re.finditer(r'(?<![A-Za-z0-9_])(CHK|CHK_ITER)\s*\(', code):
            if m.group(1) == 'CHK':
                lab = re.match(r'\s*"([^"\\]*)', code[m.end():])
                if not lab: raise Unsupported(f'{arm}: a CHK whose label is not a string literal: {code.strip()[:80]}')
                out.append((f'canvas/{arm}/' + lab.group(1).split(' ', 1)[0], ACCEPTED['canvas']))
            else:
                it = re.search(r'for \(int (\w+) = 0; \1 < (\d+); \+\+\1\) CHK_ITER\("([^"\\]*)", \1, ', code)
                if not it: raise Unsupported(f'{arm}: CHK_ITER outside the one recognised loop shape: {code.strip()[:80]}')
                key = it.group(3).split(' ', 1)[0]
                out += [(f'canvas/{arm}/{key}/{k}', ACCEPTED['canvas']) for k in range(int(it.group(2)))]
    if not out: raise Unsupported(f'{arm}: no CHK declared in the preprocessed probe (line markers missing?)')
    return out

def strip_comments(line):
    """Drop a `//` comment outside string literals (block comments do not occur in the probe's CHK lines)."""
    q, i = False, 0
    while i < len(line):
        c = line[i]
        if c == '\\' and q: i += 2; continue
        if c == '"': q = not q
        elif not q and line.startswith('//', i): return line[:i]
        elif not q and line.startswith('/*', i): raise Unsupported(f'a block comment on a probe line: {line.strip()[:80]}')
        i += 1
    return line


# ---------------------------------------------------------------------------------------------- negctl
def negctl_census(path):
    tree = ast.parse(open(path, encoding='utf-8').read())
    lists = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name) \
           and node.targets[0].id in ('MUT_V3', 'MUT_V4'):
            if node.targets[0].id in lists: raise Unsupported(f'{node.targets[0].id} assigned twice')
            if not isinstance(node.value, ast.List): raise Unsupported(f'{node.targets[0].id} is not a list literal')
            lists[node.targets[0].id] = node.value
    if set(lists) != {'MUT_V3', 'MUT_V4'}: raise Unsupported('negctl.py does not declare both MUT_V3 and MUT_V4')
    out = []
    for name, arm in (('MUT_V3', 'v3'), ('MUT_V4', 'v4')):
        for el in lists[name].elts:
            if not (isinstance(el, ast.Tuple) and el.elts and isinstance(el.elts[0], ast.Constant)
                    and isinstance(el.elts[0].value, str)):
                raise Unsupported(f'{name}: an element that is not a tuple led by its label')
            out.append((f'negctl/{arm}/' + el.elts[0].value.split(' ', 1)[0], ACCEPTED['negctl']))
    return out


def main(argv):
    if len(argv) != 6 or argv[1] != 'census':
        sys.exit('usage: accounting.py census <run.sh> <probe_v3.i> <probe_v4.i> <negctl.py>')
    try:
        rows = (canvas_census(argv[3], 'v3') + canvas_census(argv[4], 'v4') + runner_census(argv[2])
                + negctl_census(argv[5]))
    except Unsupported as e:
        print(f'  FAIL [[B459]] census: {e}', file=sys.stderr)
        return 1
    for ident, outcome in rows:
        print(f'{ident}\t{outcome}')
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
