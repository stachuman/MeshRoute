#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
"""W7+W8 report §6: every PRE-EXISTING native test case whose text changed (HEAD -> working tree), per file, with the
added/removed CHECK line counts; and the added/retired case names. Usage: python3 -B changed_cases.py <out.json>."""
import json, re, subprocess, sys
R = "/home/staszek/MeshRoute"
FILES = ["test/test_firmware_ui_model.cpp", "test/test_firmware_ui_send.cpp", "test/test_firmware_ui_chrome.cpp",
         "test/test_firmware_ui_presets.cpp"]
def cases(text):
    out, cur, buf = {}, None, []
    for line in text.splitlines():
        m = re.match(r'^TEST_CASE\("(.*)"\)\s*\{', line)
        if m:
            if cur: out[cur] = "\n".join(buf)
            cur, buf = m.group(1), [line]
        elif cur is not None:
            buf.append(line)
            if line == "}":
                out[cur] = "\n".join(buf); cur, buf = None, []
    if cur: out[cur] = "\n".join(buf)
    return out
res = {}
for f in FILES:
    old = cases(subprocess.run(["git", "show", "HEAD:" + f], cwd=R, capture_output=True, text=True).stdout)
    new = cases(open(f"{R}/{f}", encoding="utf-8").read())
    changed = []
    for name in old:
        if name in new and old[name] != new[name]:
            o = [l.strip() for l in old[name].splitlines() if "CHECK" in l]; n = [l.strip() for l in new[name].splitlines() if "CHECK" in l]
            changed.append({"case": name, "checks_removed": len([l for l in o if l not in n]), "checks_added": len([l for l in n if l not in o])})
    gone = [n for n in old if n not in new]; added = [n for n in new if n not in old]
    res[f] = {"changed": changed, "retitled_or_retired": gone, "added": added}
json.dump(res, open(sys.argv[1], "w"), indent=1, ensure_ascii=False)
for f, v in res.items():
    print(f"== {f}: {len(v['changed'])} changed, {len(v['retitled_or_retired'])} old titles gone, {len(v['added'])} new titles")
    for c in v["changed"]: print(f"   ~ {c['case'][:110]}  (-{c['checks_removed']} +{c['checks_added']})")
    for g in v["retitled_or_retired"]: print(f"   - {g[:120]}")
