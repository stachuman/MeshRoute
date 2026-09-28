# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 evidence helper — the board-UI runner's DECLARED wiring checks, read from its SOURCE (never from a run).
# Shared by reconcile.py (declared vs executed) and ordinal_map.py (base vs final): one parser, not two.
import re, subprocess

def statements(src, prefixes):
    """-> [(line, text)] for every column-0 call to one of `prefixes`, with `\\`-newline continuations joined."""
    out, i = [], 0
    while i < len(src):
        if re.match(r'^(%s) ' % '|'.join(prefixes), src[i]):
            j = i; text = src[i]
            while text.endswith('\\'):
                j += 1; text = text[:-1] + ' ' + src[j]   # a `\`-newline is a continuation: join as bash does
            out.append((i + 1, text)); i = j + 1
        else:
            i += 1
    return out

def bash_args(stmt):
    """Tokenize ONE call statement with bash's own parser; the call is replaced by an arg printer. Command
       substitutions inside double-quoted labels (W44) reach no runner function here — only the ID word matters."""
    name, rest = stmt.split(' ', 1)
    prog = 'set -f; __p() { printf "%s\\0" "$@"; }; __p ' + rest
    r = subprocess.run(['env', '-i', 'bash', '--norc', '--noprofile', '-c', prog], capture_output=True)
    return name, [a.decode('utf-8', 'surrogateescape') for a in r.stdout.split(b'\0')[:-1]]

def declared_wiring(src):
    """-> [{'line','id','label','file','pred','scripts'}] in source order (`wchk` = `wchk_in "$FW_UI"`)."""
    out = []
    for ln, st in statements(src, ['wchk_in', 'wchk']):
        name, a = bash_args(st)
        file_, label, pred, scripts = (st.split(' ', 2)[1], a[1], a[2], a[3:]) if name == 'wchk_in' else ('"$FW_UI"', a[0], a[1], a[2:])
        out.append({'line': ln, 'id': label.split(' ', 1)[0], 'label': label, 'file': file_, 'pred': pred, 'scripts': scripts})
    return out
