# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
# W2 §4.1 step 2 — comment-only proof for the two C++ files, both ways the brief allows:
#   (a) the final file with ONLY the approved comment delta reverted is byte-identical to the base (HEAD);
#   (b) `g++ -fpreprocessed -dD -E -P` of base and final yields the same TOKENS (whitespace-split; never file names or
#       line markers — -P emits none, and both runs read the same temporary file name).
# Also: every changed line (difflib) is a `//` comment line. Usage: python3 comment_only.py <out.json>
import difflib, json, os, subprocess, sys, tempfile
R = '/home/staszek/MeshRoute'
FILES = ['tools/probe_board_ui/fakes/Arduino.h', 'tools/probe_inbox_verbs/transcript_main.cpp']
def base(p): return subprocess.check_output(['git', 'show', 'HEAD:' + p], cwd=R)
def tokens(src: bytes, suffix: str):
    with tempfile.TemporaryDirectory() as d:
        f = os.path.join(d, 'x' + suffix)
        open(f, 'wb').write(src)
        r = subprocess.run(['g++', '-x', 'c++', '-fpreprocessed', '-dD', '-E', '-P', f], capture_output=True)
        assert r.returncode == 0, r.stderr.decode()
        return r.stdout.split()
res = {}; ok_all = True
for p in FILES:
    b = base(p); f = open(os.path.join(R, p), 'rb').read()
    bl = b.decode().splitlines(keepends=True); fl = f.decode().splitlines(keepends=True)
    sm = difflib.SequenceMatcher(a=bl, b=fl, autojunk=False)
    hunks = [op for op in sm.get_opcodes() if op[0] != 'equal']
    changed_lines = [l for op in hunks for l in bl[op[1]:op[2]] + fl[op[3]:op[4]]]
    all_comment = all(l.lstrip().startswith('//') for l in changed_lines)
    # (a) revert exactly the hunks: splice the base's lines back over the final's changed ranges
    rebuilt = []; pos = 0
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        rebuilt += fl[j1:j2] if tag == 'equal' else bl[i1:i2]
    a_identical = ''.join(rebuilt).encode() == b
    tb = tokens(b, os.path.splitext(p)[1]); tf = tokens(f, os.path.splitext(p)[1])
    b_equal = tb == tf
    ok = all_comment and a_identical and b_equal and len(hunks) == 1
    ok_all &= ok
    res[p] = {'hunks': [{'base_lines': [i1 + 1, i2], 'final_lines': [j1 + 1, j2]} for _, i1, i2, j1, j2 in hunks],
              'removed': [l.rstrip('\n') for op in hunks for l in bl[op[1]:op[2]]],
              'added': [l.rstrip('\n') for op in hunks for l in fl[op[3]:op[4]]],
              'every_changed_line_is_a_comment': all_comment,
              'a_delta_reverted_byte_identical_to_HEAD': a_identical,
              'b_preprocessed_tokens': {'base': len(tb), 'final': len(tf), 'equal': b_equal},
              'verdict': 'PASS' if ok else 'FAIL'}
    print(f"{p}: hunks {len(hunks)} {res[p]['hunks']}; -{len(res[p]['removed'])}/+{len(res[p]['added'])} lines, all comments {all_comment}; "
          f"(a) delta-reverted == HEAD {a_identical}; (b) tokens {len(tb)} == {len(tf)} {b_equal} -> {res[p]['verdict']}")
json.dump(res, open(sys.argv[1], 'w'), indent=1, ensure_ascii=False)
print('COMMENT-ONLY', 'PASS' if ok_all else 'FAIL')
