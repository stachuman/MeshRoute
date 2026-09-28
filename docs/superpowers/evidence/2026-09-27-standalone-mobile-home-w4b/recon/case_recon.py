# Per-TEST_CASE reconciliation, base (git HEAD) vs working tree: new / removed / renamed / prefix-only / changed.
import re, subprocess, sys, difflib
def cases(text):
    out = {}; order = []
    idx = [m.start() for m in re.finditer(r'^TEST_CASE\("', text, re.M)]
    idx.append(len(text))
    for a, b in zip(idx, idx[1:]):
        chunk = text[a:b]
        name = re.match(r'TEST_CASE\("((?:[^"\\]|\\.)*)"', chunk).group(1)
        # the case body: brace-match from the first '{' after the title (strings/chars/comments skipped)
        i = chunk.index('{'); depth = 0; j = i; st = None
        while j < len(chunk):
            ch = chunk[j]
            if st == 'line':
                if ch == '\n': st = None
            elif st in ('"', "'"):
                if ch == '\\': j += 1
                elif ch == st: st = None
            elif chunk.startswith('//', j): st = 'line'
            elif ch in '"\'': st = ch
            elif ch == '{': depth += 1
            elif ch == '}':
                depth -= 1
                if depth == 0: break
            j += 1
        body = chunk[:j + 1]
        out[name] = body; order.append(name)
    return out, order
def strip_comments(s):
    return '\n'.join(re.sub(r'\s*//.*$', '', l) for l in s.split('\n') if not re.match(r'^\s*//', l))
for f in sys.argv[1:]:
    base = subprocess.run(['git', 'show', 'HEAD:' + f], capture_output=True, text=True).stdout
    cur = open(f).read()
    b, bo = cases(base); c, co = cases(cur)
    print(f'## {f}: base {len(b)} cases, now {len(c)}')
    bset, cset = set(b), set(c)
    gone = [n for n in bo if n not in cset]; new = [n for n in co if n not in bset]
    # pair renamed cases by body similarity
    renamed = {}
    for g in gone:
        best = max(new, key=lambda n: difflib.SequenceMatcher(None, b[g], c[n]).ratio(), default=None)
        if best and difflib.SequenceMatcher(None, b[g], c[best]).ratio() > 0.3: renamed[g] = best
    for g, n in renamed.items(): new.remove(n)
    for n in co:
        if n in bset:
            if b[n] == c[n]: continue
            bl = strip_comments(b[n]).split('\n'); cl = strip_comments(c[n]).split('\n')
            d = [l for l in difflib.unified_diff(bl, cl, lineterm='', n=0) if l[:1] in '+-' and not l.startswith(('+++', '---'))]
            code = [l for l in d if l[1:].strip()]
            if not code: kind = 'COMMENT-ONLY'
            elif all('to_menu_home' in l or 'W4b fixture' in l for l in code if l.startswith('+')) and not any(l.startswith('-') for l in code): kind = 'FIXTURE-PREFIX'
            else: kind = 'CHANGED'
            print(f'{kind}\t{n[:120]}')
    for g, n in renamed.items(): print(f'RENAMED\t{g[:80]}  ->  {n[:80]}')
    for g in gone:
        if g not in renamed: print(f'REMOVED\t{g[:120]}')
    for n in new: print(f'NEW\t{n[:120]}')
