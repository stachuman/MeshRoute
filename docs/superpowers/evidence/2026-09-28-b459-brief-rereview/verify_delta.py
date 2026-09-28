"""Reverse the declared revision-2 edits and require the exact previously reviewed revision-1 hash."""
from pathlib import Path
import hashlib,difflib,json
R=Path('/home/staszek/MeshRoute');E=Path(__file__).resolve().parent
p=R/'docs/superpowers/plans/2026-09-28-b459-board-ui-accounting.md';current=p.read_text();s=current;edits=[]
def undo(name,new,old):
 global s
 assert s.count(new)==1,(name,s.count(new))
 s=s.replace(new,old);edits.append(name)
undo('revision header',"**Revision 2 — 2026-09-28 — for QA's scoped re-review.** The [brief review](../evidence/2026-09-28-b459-brief-review.md)\nheld revision 1 (`f485c494…1453`) on B459R-1–B459R-3; this revision folds them in (§8).",'**Revision 1 — 2026-09-28 — draft for QA review.**')
undo('test table permission','| 178 | 1 — the library\'s resolution from a temporary copy only |','| 178 | 1 — the library resolution only |')
undo('register pin','ebed97ce326b3a0d2be664d76e756388a5d9756462a12b3dbb8703d78f19bab5','86837517b071729925d5b9bbb67e68cce8f6eed64209ec8e74dc523b7de4d3ed')
undo('preparation explanation','number (B474) and a B459 entry in §0, which now names revision 2. This brief is pinned by the hash QA issues on PASS, not listed here.','number (B474) and a B459 entry in §0. This brief is pinned by the hash QA issues on PASS, not listed here.')
undo('B459R-2 adapter and test binding', '''- `ACCOUNTING_STATEMENT`, the exact final-accounting call that B456's bypass test removes, stays **byte-identical and
  exactly once** in the firmware-UI runner, and removing it still disables the real final comparison. Keeping
  `account_controls` as the firmware-UI adapter's name and signature makes this possible; the test's constant does
  not change (B459R-2).
- The bypass test copies `run.sh` alone into a temporary directory. That copy must still reach the **real** shared
  library through an explicit mechanism that the test sets up — the **only** change allowed in
  `tools/test_probe_firmware_ui.py`. No assertion is weakened and no test is retired. A missing library must fail
  loudly with its own message, and must never be what makes the regression pass.''', '''- `ACCOUNTING_STATEMENT`, the exact final-accounting call that B456's bypass test removes, stays **exactly once** in
  the firmware-UI runner and still disables the real final comparison. If its text must change, only the test's
  constant changes with it.
- The bypass test copies `run.sh` alone into a temporary directory. That copy must still reach the **real** shared
  library through an explicit mechanism that the test sets up. A missing library must fail loudly with its own
  message, and must never be what makes the regression pass.''')
undo('B459R-3 removal case', '''- a source call and its declaration removed while the checked-in manifest stays intact — the census rejects the
  mismatch (B459R-3). Removing both the source and its manifest line stays the §2.2.1 limit, visible only in the
  frozen coverage diff and review;''','- a declaration and call removed together, which the manifest comparison rejects;')
undo('common evidence fence','**IN — both increments:** the report and evidence (§5), including increment 1\'s baselines and freeze snapshots.\n\n','')
undo('increment-2 evidence relocation', '''  `tools/test_probe_board_ui.py`.

**OUT:**''','''  `tools/test_probe_board_ui.py`;
- the report and evidence (§5).

**OUT:**''')
undo('increment-1 scope comparison',"1. Only increment 1's three files differ, apart from the report and evidence (§3); `git diff --check` is clean.",'1. Only increment-1 files differ; `git diff --check` is clean.')
undo('B459R-1 discovery before freeze', '''5. **Tools discovery (D5, B459R-1):** `python3 -m unittest discover -s tools -p 'test_*.py'` on increment 1's own
   candidate. Expect the same 375 tests, OK, 0 skipped, no import failure — increment 1 adds no test. Record the run
   against the three files' hashes.
6. **Freeze:** record the three files' hashes and snapshot them into the evidence. The receipt keeps this
   first-freeze evidence separate from the final candidate's.''','5. **Freeze:** record the three files\' hashes and snapshot them into the evidence.')
undo('B459R-2 STOP constraints', '''  disabling the real call; a missing library satisfying a regression; `ACCOUNTING_STATEMENT` changed, or any change
  to `tools/test_probe_firmware_ui.py` beyond the library's resolution.''','  disabling the real call; a missing library satisfying a regression.')
marker='\n**Revision 2 (2026-09-28)** folds in the brief review (HOLD, B459R-1–B459R-3):';assert s.count(marker)==1
s=s[:s.index(marker)];edits.append('revision-2 history')
# The original ends with one newline; the new history is separated by a blank line.
s=s.rstrip('\n')+'\n'
old_hash=hashlib.sha256(s.encode()).hexdigest();assert old_hash=='f485c49427838f1480fb5a8b32a0dd454f3e88a4c7eab65b54aaeeb46c7e1453',old_hash
(E/'revision-1-reconstructed.md').write_text(s)
(E/'brief-delta.diff').write_text(''.join(difflib.unified_diff(s.splitlines(True),current.splitlines(True),fromfile='reviewed-revision-1',tofile='revision-2')))
(E/'delta-verification.json').write_text(json.dumps({'method':'Reverse only the declared edits, then require the cryptographic identity of the result with the previously reviewed brief. No git HEAD substitution.','reconstructed_revision_1_sha256':old_hash,'revision_2_sha256':hashlib.sha256(current.encode()).hexdigest(),'edits':edits,'other_brief_changes':False},indent=2)+'\n')
print('PASS: reversing only the declared edits reproduces revision 1 exactly')
