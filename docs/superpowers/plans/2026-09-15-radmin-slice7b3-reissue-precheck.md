<!-- QA/Author: OpenAI Codex; no production implementation -->
# Slice 7b-3 revision-4 reissue — independent author pre-check — 2026-09-15

**AUTHOR PRE-CHECK COMPLETE; revision 4 and P1 ready for coder source-validation. Behavior implementation
HOLD B389 allocation + B394 preparation.** No full implementation gate is claimed here. The owner committed
the QA-passed codec at **`ac5f9a592065d08e7cc8c06ef395e79891d41b34`** (parent b9d75aa); simulator stays clean at
**`06746a97de5764415d6fcef10b97bca90569b9c7`**. Both trees were clean before these documentation edits.

[Behavior revision 4](2026-09-13-radmin-slice7b3-deferred-actions.md) carries the exact contract, allocation
proposal and 36 exceptions. [P1 revision 1](2026-09-15-radmin-slice7b3-p1-simple-action-preparation.md) fences
the behavior-preserving preparation. [Retained evidence](../evidence/2026-09-15-radmin-slice7b3-reissue/README.md)
contains commands, raw-log hashes, results, full input manifests, prior brief and model patches.

## 1. Input identity and independent scope

I inspected the actual commit, source, current roles/gate rules, R-RA-37/38/39 and both prior coder/QA reports.
All four fenced codec/test/tool SHA-256 values exactly match the coder freeze. Independently recomputed
hashes for **25 enumeration source inputs** match the preserved coder source audit. Thus the source reasons
in that enumeration remain applicable at ac5f9a5; the later codec commit did not add preparation interfaces.
The initial receipt lists twelve principal seams; the full 25-input comparison is recorded in
row-dispositions.json and its retained typed-plan/source-audit.json input.

A private snapshot at `/tmp/mr-qa-s7b3-r4-zwfe7qky/snapshot` includes all **1,403 tracked/untracked inputs**,
with modes and SHA-256 recorded before work; simulator inventory is **285**. A shared clone plus complete
input overlay was used, not a HEAD-only assumption. The relative spec/dv_dual_sf.lua symlink was bound to
its original resolved simulator file in the private snapshot; the original link is unchanged. Builds,
source extraction and shadow layout compiles occurred privately. No shared production/test/tool change,
normal shared build, simulator edit, staging or commit is part of this author landing.

The committed codec QA report remains its reviewer's evidence: native 2912/184461/0, reference 94/94,
domain proof 9787, full 816 RED + known unusable B342 /817, unchanged board pair and the other named gates.
B391 is closed by that executed X09 proof. The reviewer explicitly did not reproduce the coder's single-byte
gateway ELF attribution; only section/object/linked totals were reproduced. Neither that limit nor the full
gate figures are silently converted into this turn's measurements.

## 2. Fresh executed author instruments

| Instrument | Independently obtained result |
| --- | --- |
| pio native wrapper, then actual binary | **2912 cases /184461 assertions /0 failures /0 skips**; separate logs/commands |
| Fresh Release/Ninja simulator | **64 actual compiler actions** across normal/gateway namespaces; no stale shared objects |
| Corpus run, validate both manifests, canonical compare and direct stream comparison | **36/36 current anchors and actual byte-identical streams** against the preserved codec final corpus |
| Current BASELINE identity | SHA-256 `71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`; live s18 `32afbf11` /269517 /0 |
| Fresh simulator executable | SHA-256 `07d68fc561f352ceea4b0dd32e7685035f777d3053096ecdb987e6a09136728e`; matches the validated codec final manifest |
| Independent PyNaCl extended reference, freeze/compare/selftest | **94/94**, old **89/89** unchanged, all five comparator controls RED |
| Actual policy extraction vs coder enumeration | **180 policy rows /48 disruptive /12 selected +36 exact refusal exceptions**, every row/alias/class/source line matched |
| Historical mutation source-cardinality audit | **52 batteries /817 configured patterns, all match exactly once**; no mutation was executed this turn |
| Private baseline/proposed layout compiles | All six compiles pass: native, ARM gateway, Xtensa mobile; sizes/alignment below |

Corpus comparison source is `/tmp/mr-codex-s7b30-C40uYn/corpus-final`; both manifests and all 36 stream
SHA-256/byte lengths are retained. It was validated before comparison, not accepted from a summary.
Logs are losslessly compressed to retain original output bytes, including any diagnostic whitespace.
No fresh board links, warning census, full tools/probes/ABI-control suites, domain proof or mutation union
were run. Those belong to the separately frozen P1 and behavior implementation gates. The layout compiles
use the board instrument's real idedata/compiler options but are not a run of its full controlled probe.

## 3. B394 source correction and exact scope

The preserved coder typed-plan report explicitly separates **four reusable no-argument effects**, **eight
simple-action rows needing C1 extraction**, and **36 complex-service rows**. Its +80-byte comparison was
for four rows. The codec completion summary's twelve-existing-seams assertion is inconsistent with that
report and actual source. Factory reset parses and erases inline (firmware_commands.cpp:1071–1089), sleep
parses and assigns inline (:1096–1105), and crash checks mutable debug admission, parses and acts in the
board TU (fw_main.cpp:395–416). The reusable reboot/prep/OTA effects also need bounded typed outcome/sink
bindings for the future non-forwarding remote apply. No reliable prepared effect interface is implied by
having a void local wrapper. See B394 and P1 for exact source anchors and executed equivalence obligations.

Revision 4 keeps twelve selected policy rows behind that separate preparation, its own full gate and owner
commit. QA then refreshes behavior at the P1 hash. Under R-RA-39, QA now records **36 precise retained-refusal
exceptions**, not a blanket family ban: cfg 22 + gateway 1 (B395), join 2 + create 7 + leave 1 (B396), team 2
(B397), regen 1 (B398). Their missing seams, separate fences and closure proofs are in revision 4 §2.2 and
all-48-dispositions.md. Local forms, non-disruptive forms and authority classifications remain unchanged;
zero early effects and retained refusal/retry coverage remain required. None of these rows is called complete.
Next free finding is **B399**. B390 runtime and B392 metal/8a obligations stay open; B391 remains closed.

## 4. Complete twelve-row model and the unruled B389 allocation

The new author model owns request/authority identity, frozen delay/deadline, action kind, resolved backend,
phase and activation trigger. Kind encodes all sleep/crash arguments; the twelve selected policy rows need
no raw command, secret, borrowed pointer or NV Blob. One row is **40 bytes /alignment 8**. Four independent
transcript u32 details grow each header **24→32**. Two retained u8 last-kind/outcome fields plus final
alignment are included: **40 +4×8 +2 +6 padding =80** bytes, RemoteSessionState **8824→8904** on each ABI.

| ABI | Baseline Node | Complete model Node | Delta |
| --- | ---: | ---: | ---: |
| native | 230896/8 | 230976/8 | +80 |
| gateway ARM | 157264/8 | 157344/8 | +80 |
| heltec_mobile Xtensa | 117912/8 | 117912/8 | 0 |

The model modifies only private shadow headers; production Node and pins remain unchanged. The first
model compile failed on the current native Node assertion (230896 versus modeled 230976). That was a
model-setup omission, **not an intentional negative control or a production defect**. The failing command/
output is preserved in plan-old-pin-control.json and native-proposed-old-pin-control.log.gz despite their
historical filenames. After the explicit private model pin correction, all six compiles pass. The executed
driver and exact shadow patches are retained; the model does not implement the P1 APIs or scheduler.

**Recommendation, still unruled:** one 40-byte ACCEPT row, four independent details and two diagnostic
bytes/alignment; **+80 resident session bytes**, native/gateway re-pins **230976/157344**, mobile Node
**117912** and RAM unchanged, with final linked attribution and separately measured transient stack.
The committed linked baselines are gateway 203956 RAM /570588 flash and mobile 207756 /1372992, attributed
to the prior full codec gate. +80 gateway linked RAM is only a prediction. P1 adds zero resident state.
The coder must source-validate the entire proposed shape against the gated P1 APIs; extra state returns for
measurement. R-RA-37/38/39 or general access permission does not supply the still-missing B389 ruling.

## 5. Author landing and remaining gates

Revision 4, separate P1, this pre-check/evidence, register B389–B398/dispatch, design/MEMORY/ledger completion,
codec commit-status note and Part 57b are the permitted documentation landing. Revision 3 is archived at
its original SHA-256; original coder/QA reports and owner ruling blocks are preserved. R-RA-38's chosen
lockout/recovery and exact 8a warning replace stale conditional wording in the living documents. B394 is
open until real preparation is gated; correcting the summary alone does not close it. B395–B398 are planned
follow-ups, not immediate production dispatch.

Final document/preservation checks are in document-audit.json and preservation.json in the artifact directory.
No behavior implementation or independent behavior PASS has been produced. The coder next source-validates
these briefs; P1 precedes feature implementation, and B389's concrete allocation remains an owner decision.
