# Control reconciliation for tools/probe_firmware_ui/run.sh: every `ctl "<label>" yes|no \` call with the script text
# that follows it (resolving `"$var"` to the variable's last assignment above the call), base (HEAD) vs working tree.
import re, subprocess
def ctls(text):
    lines = text.split('\n'); out = {}; vars_ = {}
    for i, l in enumerate(lines):
        m = re.match(r"^\s*(\w+)='(.*)'\s*$", l) or re.match(r'^\s*(\w+)="(.*)"\s*$', l)
        if m: vars_[m.group(1)] = m.group(2)
        m = re.match(r'^[ \t]*ctl "(.*)" (yes|no) \\$', l)
        if not m: continue
        script = []; j = i + 1
        while j < len(lines) and lines[j].strip() and not re.match(r'^\s*(ctl |once |#|[a-z0-9_]+=)', lines[j]):
            script.append(lines[j].strip()); j += 1
        s = ' '.join(script)
        s = re.sub(r'"\$(\w+)"', lambda v: vars_.get(v.group(1), '$' + v.group(1)), s)
        s = re.sub(r'\$(\w+)', lambda v: vars_.get(v.group(1), '$' + v.group(1)), s)
        out[m.group(1)] = (m.group(2), s)
    return out
b = ctls(subprocess.run(['git', 'show', 'HEAD:tools/probe_firmware_ui/run.sh'], capture_output=True, text=True).stdout)
c = ctls(open('tools/probe_firmware_ui/run.sh').read())
def cid(l): return l.split()[0]
bid = {cid(l): l for l in b}; nid = {cid(l): l for l in c}
kept = [i for i in bid if i in nid and b[bid[i]] == c[nid[i]] and bid[i] == nid[i]]
rean = [i for i in bid if i in nid and (b[bid[i]] != c[nid[i]] or bid[i] != nid[i])]
ret = [i for i in bid if i not in nid]; new = [i for i in nid if i not in bid]
print(f'controls: base {len(b)}, now {len(c)}; kept verbatim {len(kept)}; re-anchored/relabelled {len(rean)}; retired {len(ret)}; new {len(new)}')
print('re-anchored:'); [print(f'  {i}: label {"same" if bid[i]==nid[i] else "CHANGED -> " + nid[i]}; script {"same" if b[bid[i]][1]==c[nid[i]][1] else "CHANGED"}') for i in rean]
print('retired:', ' '.join(ret)); print('new:', ' '.join(new))
