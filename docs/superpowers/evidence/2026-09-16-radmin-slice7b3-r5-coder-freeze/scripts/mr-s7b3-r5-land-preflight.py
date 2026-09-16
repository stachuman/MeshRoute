from pathlib import Path
import hashlib,json,shutil,subprocess,os,stat
r=Path('/home/staszek/MeshRoute');q=Path(Path('/tmp/mr-codex-s7b3-r5-preflight-active').read_text().strip());out=r/'docs/superpowers/evidence/2026-09-16-radmin-slice7b3-r5-preflight'
assert not out.exists()
shutil.copy2('/tmp/mr-s7b3-r5-preflight.py',q/'reproduce.py')
(q/'README.md').write_text('''# Revision-5 focused source-validation — STOP-1

Base: 7442e6f570abdcd74ceed20d4c0cb9e2855d0719. No production edits or full gate.

`state.json` binds the consumed brief. `permitted-preparation.json` inventories every one of the 59
permitted dirty documentation files individually; `inputs-before.json` inventories 1,571 checkout inputs.
The existing dirty documents are preserved, not rewritten by this preflight.

`checked-anchors.json`: 23 of 24 focused anchors match. Revision 5 §3.2 names remote_codec.cpp:654,
but the unchanged decoder detail assignment is at :652. This is an anchor error, not a decoder defect.
The complete source-validation remains unfinished at STOP-1; these 24 checks are not a claim to have
validated every referenced design section or every anchor in the brief.

`policy-check.json`: unchanged policy SHA, 180 entries, 48 disruptive rows; all 48 historical dispositions
match source (12 selected, 36 retained refusals). This is static coverage, not executed behavior proof.

`action-plan-definitions.json` identifies the sole ActionPlan definition. The dependency compile fixtures
are intentionally tiny language probes, not proposed production edits or native/board gates:

- core_baseline: existing RemoteSessionState 8824 and TranscriptHeader 24 compile unchanged.
- core_only: a forward declaration cannot support an embedded ActionPlan; expected incomplete-type error.
- firmware_complete: the actual P1 definition compiles and measures 2 bytes. Its compiler dependency file
  includes src/firmware_action_effects.h and src/firmware_config_parse.h.

That dependency makes the requested embedded carrier's permitted implementation route unclear: the brief
allows a pure shared carrier but also says core must not include firmware and P1 must stay unchanged.
Explicitly permitting a source-only extraction of ActionKind/ActionBackend/ActionPlan to a pure shared
header, with the original firmware header including it and all qualified names/values/layout/API/behavior
unchanged, would resolve the fence ambiguity without changing R-RA-40. This is a proposal for QA's fold-in,
not permission inferred or a production change made here. A forward-declaration/pointer workaround does
not provide the required complete owned value; duplicating the definition is not proposed.

The expected failing fixture is NOT a production build failure, effective mutation RED, full ABI result,
new allocation measurement, independent QA result, or proof that the existing firmware is defective.
No baseline figures from the earlier P1 report are represented as rerun here.

Run reproduce.py to recreate the focused read-only check in a new /tmp directory (it expects the pinned
preparation set and must be adapted after an author reissue). Every attempted command and compiler output
is retained. See the appended checkpoint in ../2026-09-13-radmin-slice7b3.md.
''')
shutil.copytree(q,out)
receipt=r/'docs/superpowers/evidence/2026-09-13-radmin-slice7b3.md'
before=receipt.read_bytes()
addition='''

## Revision 5 source-validation checkpoint — 2026-09-16 — STOP-1

**Implementation not started.** This checkpoint supersedes the historical dispatch state above without
rewriting its evidence. The actual base is **7442e6f570abdcd74ceed20d4c0cb9e2855d0719**; revision 5 SHA-256
**bfd0fd5dd06595328e925b328c3993a3c00a6dc2a229fe7637869e0a9e776410**. Simulator is
**06746a97de5764415d6fcef10b97bca90569b9c7**, clean and unchanged. No commit prerequisite is imposed.

All **59** dirty preparation files are within the permitted QA set: six modified tracked documents and
53 untracked documentation/evidence files (51 inside the P1 QA directory, its gate markdown, and the retained
revision-4 brief). Every file has an individual SHA-256 inventory; the complete initial checkout inventory
has **1,571** tracked/untracked entries. The archived revision-4 content hash matches the brief exactly.
[Focused evidence and reproduction](2026-09-16-radmin-slice7b3-r5-preflight/README.md).

**STOP-1 — two author fold-ins, proposed B402 for QA registration (no owner allocation change):**

1. **Confirmed anchor disagreement.** Revision 5 §3.2 cites `lib/core/remote_codec.cpp:654` for
   `d.result_detail = payload.subspan(1)`. At this exact base the statement is **line 652**; 654 is blank.
   The decoder behavior is as described. Correct the anchor, preserve codec behavior. Of the **24 focused
   source-anchor checks executed here, 23 match and this one does not**. This is not a complete anchor audit.
2. **Carrier fence clarification needed.** The requested core-owned row embeds P1's `mrfw::ActionPlan`;
   its sole definition is `src/firmware_action_effects.h:18–21` (kind/backend enums at :12/:16), and that
   header imports `firmware_config_parse.h` at :6. Brief §5 simultaneously allows a pure shared carrier,
   forbids core firmware/NV includes, and says to reuse P1 as is without changing it. A concrete route needs
   to be stated: preferably explicitly fence the definition-only extraction of those three types into a
   pure shared header and the P1 header's include adjustment, preserving all names, enum values, two-byte
   layout, admission/effect signatures, wrappers and local behavior. That would be a source-map/fence
   clarification; the coder has not inferred it or edited P1. The original layout model uses separate
   QA kind/backend enums, so it does not prove the requested production include boundary.

The dependency fixture independently compiles the unchanged core baseline (state **8824**, header **24**),
reproduces the expected incomplete-type failure when embedding a merely forward-declared ActionPlan, and
compiles the actual P1 include as the positive control (**ActionPlan 2 bytes**). Compiler dependencies show
both firmware headers. This is a focused language/include proof, **not a broken existing production build,
mutation RED, three-ABI allocation result or software gate**. The required +80 B allocation remains approved;
no additional owned state is proposed. No new owner ruling is requested.

Static policy checks independently confirm **180 entries /48 disruptive**, with all **48** retained disposition
rows matching the current source and the same policy hash (**12 selected /36 refusals**). They do not replace
runtime coverage. B401 remains the permitted future comment fix; it has not been changed here.

**Scope and preservation.** Only this append-only coder receipt and the new focused evidence directory were
added by this turn. Every other input is preserved; production, tests, tools, maintained QA documents and the
simulator are untouched. Registration/reissue is handed back to QA under §5's documentation ownership;
B402 is proposed, not represented as an already-landed register row. The current register's authoritative
next-free marker is B402; its older B401 marker is historical drift, not used to allocate an ID here.

**Not run (D3):** full native suite, corpus, independent reference, ABI/standing probes, tools discovery,
boards, census, mutation union, linker/stack attribution or independent QA. Source-validation stopped before
implementation and remains incomplete. No `PIN re-synced? YES` or implementation freeze is claimed. Resume
source-validation against the corrected author inputs using HEAD plus their SHA-256 inventory; no commit
is required. The +80 B/Node pins and the 36-refusal scope stay unchanged.
'''
receipt.write_bytes(before+addition.encode())
sha=lambda b:hashlib.sha256(b).hexdigest()
initial=json.loads((out/'inputs-before.json').read_text())
def record(p):
 s=p.lstat();v={'mode':oct(stat.S_IMODE(s.st_mode))}
 if p.is_symlink():
  v.update(kind='symlink',target=os.readlink(p))
  if p.is_file():v['resolved_sha256']=sha(p.read_bytes())
 elif p.is_file():v.update(kind='file',sha256=sha(p.read_bytes()),size=s.st_size)
 else:v.update(kind='other')
 return v
changed=[p for p,v in initial.items() if record(r/p)!=v]
assert changed==['docs/superpowers/evidence/2026-09-13-radmin-slice7b3.md'],changed
assert receipt.read_bytes().startswith(before)
checks={}
for name,path in [('meshroute',r),('simulator',Path('/home/staszek/lora-universal-simulator'))]:
 p=subprocess.run(['git','diff','--check'],cwd=path,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
 (out/(name+'-whitespace.log')).write_bytes(p.stdout);checks[name]=p.returncode;assert p.returncode==0
assert not subprocess.check_output(['git','status','--short'],cwd='/home/staszek/lora-universal-simulator')
(out/'preservation-after-landing.json').write_text(json.dumps({'initial_input_count':len(initial),'unchanged_inputs':len(initial)-len(changed),'changed_inputs':changed,'receipt_original_prefix_sha256':sha(before),'receipt_original_prefix_bytes':len(before),'receipt_append_only':True,'whitespace':checks,'simulator_clean':True,'production_changes':[]},indent=2)+'\n')
manifest={str(p.relative_to(out)):sha(p.read_bytes()) for p in sorted(out.rglob('*')) if p.is_file()}
(out/'artifact-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'receipt':str(receipt),'artifact_directory':str(out),'artifacts':len(manifest),'preserved_initial_inputs':len(initial)-1,'receipt_appended_bytes':len(addition.encode())},indent=2))
