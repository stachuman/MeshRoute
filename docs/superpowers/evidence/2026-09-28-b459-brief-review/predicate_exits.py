"""Labelled temporary-copy measurement of the existing predicates; no implementation changes."""
from pathlib import Path
import tempfile,subprocess,json,hashlib,collections,shlex
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent;RAW=R/'artifacts/2026-09-28-b459-brief-review'
p=R/'tools/probe_board_ui/run.sh';s=p.read_text();original=s
old='cd "$(dirname "$0")" || exit 1';assert s.count(old)==1;s=s.replace(old,'cd '+shlex.quote(str(p.parent))+' || exit 1')
old='  if ! "$pred" "$file"; then';assert s.count(old)==1
s=s.replace(old,'  "$pred" "$file"; probe_review_rc=$?\n  printf "live\\t%s\\t%s\\n" "${label%% *}" "$probe_review_rc" >&9\n  if [ "$probe_review_rc" -ne 0 ]; then')
old='    if "$pred" "$OUT/fw_ui_revert.cpp"; then';assert s.count(old)==1
s=s.replace(old,'    "$pred" "$OUT/fw_ui_revert.cpp"; probe_review_rc=$?\n    printf "mutant\\t%s/%s\\t%s\\n" "${label%% *}" "$n" "$probe_review_rc" >&9\n    if [ "$probe_review_rc" -eq 0 ]; then')
with tempfile.TemporaryDirectory(prefix='b459-review-predicates-') as td:
 copy=Path(td)/'run.sh';copy.write_text(s)
 with (RAW/'predicate-exits.log').open('w') as log:
  proc=subprocess.run(['bash','-c','exec 9>"$1"; bash "$2" --no-neg','b459-review',str(E/'predicate-exits.tsv'),str(copy)],stdout=log,stderr=subprocess.STDOUT)
rows=[line.split('\t') for line in (E/'predicate-exits.tsv').read_text().splitlines()]
counts={kind:dict(collections.Counter(int(rc) for k,ident,rc in rows if k==kind)) for kind in ('live','mutant')}
assert p.read_text()==original
(E/'predicate-exits.json').write_text(json.dumps({'basis':'Actual predicates called by an instrumented temporary copy of the frozen runner; --no-neg, not a release gate. Only path setup and status logging changed.','exit':proc.returncode,'counts':counts,'unique_live_ids':len({i for k,i,c in rows if k=='live'}),'unique_mutant_ids':len({i for k,i,c in rows if k=='mutant'}),'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest()},indent=2)+'\n')
print(counts,proc.returncode)
