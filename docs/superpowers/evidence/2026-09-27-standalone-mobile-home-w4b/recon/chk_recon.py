# Probe-check reconciliation for tools/probe_firmware_ui/probe_main.cpp: every CHK("label", expr) and every
# snprintf'd dynamic label format, base (git HEAD) vs working tree. Expressions are compared whitespace-normalised.
import re, subprocess
def chks(text):
    out = {}
    for m in re.finditer(r'CHK\(\s*"((?:[^"\\]|\\.)*)"\s*,', text):
        # the expression: balanced to the closing paren of CHK(
        i = m.end(); depth = 1; j = i; st = None
        while j < len(text) and depth:
            ch = text[j]
            if st:
                if ch == '\\': j += 1
                elif ch == st: st = None
            elif ch in '"\'': st = ch
            elif ch == '(': depth += 1
            elif ch == ')': depth -= 1
            j += 1
        out.setdefault(m.group(1), []).append(' '.join(text[i:j - 1].split()))
    for m in re.finditer(r'snprintf\(lab, sizeof lab, "((?:[^"\\]|\\.)*)"', text):
        out.setdefault('[fmt] ' + m.group(1), []).append('(dynamic)')
    return out
base = chks(subprocess.run(['git', 'show', 'HEAD:tools/probe_firmware_ui/probe_main.cpp'], capture_output=True, text=True).stdout)
cur = chks(open('tools/probe_firmware_ui/probe_main.cpp').read())
k = {'kept': 0, 'expr': [], 'new': [], 'retired': []}
for lab in base:
    if lab not in cur: k['retired'].append(lab)
    elif base[lab] == cur[lab]: k['kept'] += 1
    else: k['expr'].append(lab)
for lab in cur:
    if lab not in base: k['new'].append(lab)
print(f"labels: base {len(base)}, now {len(cur)}; kept verbatim {k['kept']}; same label, changed expression {len(k['expr'])}; retired {len(k['retired'])}; new {len(k['new'])}")
for t in ('expr', 'retired', 'new'):
    print(f'--- {t}')
    for lab in k[t]: print('  ' + lab)
