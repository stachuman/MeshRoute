"""Reverse only the declared revision-3 passages; require the known revision-2 SHA-256."""
from pathlib import Path
import hashlib,json,difflib
E=Path(__file__).resolve().parent
new=(E/'approved-brief.md').read_text();s=new
changes=[]
def replace(old,newer):
 global s
 assert s.count(old)==1,old[:100]
 s=s.replace(old,newer);changes.append(old.splitlines()[0])
replace("**Revision 3 — 2026-09-28 — for QA's scoped re-review.** The [brief review](../evidence/2026-09-28-standalone-mobile-home-w2-brief-review.md)\nheld revision 2 (`0b8606ea…60c0`) on W2R-1–W2R-3; this revision folds them in (§8).", "**Revision 2 — 2026-09-28 — for QA review.** It supersedes revision 1 (`c52d573c…b1c`) before review, folding in the\nowner's B459 ruling; the contract, fence, ledger and gates are unchanged (§8).")
replace('a7b0515e633682bdb246f64b46dd345ca9b021764e078a10863a796d92796428','b52ab297be97c96bb9d410219080b3de2a8dafd89afde0b94ff35bec88fcd03b')
replace('56edcd1addf15879021a133b4726e2acf82aa9e483ca1cd4714e45ec7b7e5986','c6f96e29bff3a4157887df4daab7882f05a0fce8254a6f5e31a09b7ad8dd1ec0')
replace("The design and register changes are status pointers (now naming revision 3), B460's registration, the next-free\nnumber (B461) and the owner's B459 ruling. This brief is pinned by the hash QA issues on PASS, not listed here.", "The design and register changes are status pointers, B460's registration, the next-free number (B461) and the\nowner's B459 ruling. This brief is pinned by the hash QA issues on PASS, not listed here.")
a=s.index('- **All-or-nothing, in both directions (W2R-1).**');b=s.index('- **Structural only.**',a)
replace(s[a:b],'''- **All-or-nothing.** If either anchor is missing, the control must change nothing, so that `wchk_in` reports it
  vacuous and fails. It must never degrade into a plain deletion — that is how B418 hid it. One sed program can do
  this, because `handle_teststatus` precedes `dispatch()`: insert the copy after the neighbour's signature, and delete
  the original only if that insertion happened (for example, with a hold-space flag).
''')
replace('''   once the approved comment delta is removed, or the comment-stripped tokens are equal (`g++ -fpreprocessed -dD -E
   -P`, base versus final). Compare the tokens, never the file names or line markers of two temporary paths.''','''   once the approved comment delta is removed, or the preprocessed, comment-stripped tokens are equal
   (`g++ -fpreprocessed -dD -E`, base versus final).''')
a=s.index('   - **W49 relocation, both directions (W2R-1):**');b=s.index('5. **Console-sink,',a)
replace(s[a:b],'''   - **W49 relocation, all-or-nothing:** on a scratch copy with `handle_teststatus`'s signature altered, the control's
     output equals its input.
''')
replace('a W49 relocation that can become insertion-only or deletion-only.','a W49 relocation that can degrade into a deletion.')
a=s.index('6. **The pin line:**');b=s.index('7. **Freeze inventory:**',a)
replace(s[a:b],'')
replace('7. **Freeze inventory:**','6. **Freeze inventory:**')
replace('8. **Not run,**','7. **Not run,**')
a=s.index('\n**Revision 3 (2026-09-28)** folds in the brief review');replace(s[a:],'')
# Revision 2 ends in the final history sentence with its ordinary trailing newline.
h=hashlib.sha256(s.encode()).hexdigest();expected='0b8606ea274c7ab950f1cb4b2ad5bcc297e5a5d29ac654037ea99fdad62960c0'
out={'r3_sha256':hashlib.sha256(new.encode()).hexdigest(),'reconstructed_r2_sha256':h,'expected_r2_sha256':expected,'match':h==expected,'r2_lines':len(s.splitlines()),'r3_lines':len(new.splitlines()),'reversed_edits':changes}
(E/'delta-verification.json').write_text(json.dumps(out,indent=2)+'\n')
(E/'revision-2-to-3.diff').write_text(''.join(difflib.unified_diff(s.splitlines(keepends=True),new.splitlines(keepends=True),fromfile='reviewed-r2',tofile='proposed-r3')))
print(json.dumps(out,indent=2));assert h==expected
