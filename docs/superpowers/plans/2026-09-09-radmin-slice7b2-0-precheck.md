<!-- Independent QA/Author: OpenAI Codex; no production implementation -->
# Remote-admin Slice 7b-2-0 — independent author pre-check — 2026-09-09

**Historical author/source-validation pre-check, completed before implementation.** Sections 1–3 retain
that baseline checkpoint; §4 records QA's focused B383 verification and revision-2 fold-in. The codec
implementation has since passed its [full independent QA gate](../evidence/2026-09-09-radmin-slice7b2-0-qa-gate.md)
at base `1d4b3ad`, with the frozen changes still uncommitted. This pre-check is not that implementation gate
or a B378/B379 behavior closure. R-RA-35/R-RA-36 remain unchanged.

## 1. Source state and preservation

MeshRoute **`1d4b3ad5a74c2f24121d8fbe28b8d6e4b04f8dfe`**. Simulator
**`06746a97de5764415d6fcef10b97bca90569b9c7`**, clean. The initial shared dirty files are documentation and
the prior authoring reproductions, plus the coder receipt including its 2026-09-09 addendum. The codec brief
§1 enumerates them. No code/test/tool/simulator source, reset, clean or commit is part of this authoring turn.

Raw artifacts: **`/tmp/mr-qa-s7b20-author-d52_n7cw`**. `before.json` hashes every tracked/untracked input before
edits. QA matched **380** tracked files under `lib/`, `src/`, `test/`, `tools/`, `simulation/`, plus
`platformio.ini`, to the existing clean private checkout at
`/tmp/mr-qa-s7b2-precheck-no59tyku/source`. That private native binary and two-variant simulator were freshly
built in the preceding independent 7b-2 pre-check and are reused here after source/hash verification.
No new native or simulator build is claimed. This is a baseline authoring reuse, not permission to reuse
old artifacts when gating a changed implementation.

The coder receipt remains unchanged by QA, SHA-256
**`ee0f42aa690f0a84b7f2b8580402c85e9a0f34054afa67a0352ef573ca544ab6`**. It records owner approval of behavior
brief revision 2, SHA-256 `1802c25a99e0d470b299a769a072b56e1bd6f12d8df7e230962255de82fa387a`.
The subsequent behavior revision 3 records those rulings and the codec prerequisite; it does not dispatch
the behavior implementation.

## 2. Independently executed instruments

| Instrument | This QA run | Scope |
| --- | --- | --- |
| Actual private native binary | **2883 cases / 127709 assertions / 0 failed / 0 skipped** | Fresh execution of source-matched prior build; no new wrapper/build claim |
| Original independent codec reference | External BLAKE2b and XChaCha anchors pass; **87/87 literals byte-identical** | Original Slice-2 domains, no new admission implementation |
| Reference corruption control | Flip first `kRefBaseKey` byte `fb→fa` in a private test copy: exactly **one mismatch**, exit **1** | A deliberately bad literal is rejected; shared test unchanged |
| Fresh canonical corpus | **36/36 anchors**, zero failures, 52.7 s / jobs=4 | Reused source-matched simulator binary; runner snapshots/validates inputs |
| Canonical base/new corpus comparison | **36/36 byte-identical** on md5, SHA-256, size, events and assertion counts | Both manifests revalidated against their actual streams |

Simulator binary SHA-256:
`7ad152073f13a76192ff2185488326910d28a759c5c882b1cfa59b012b95e2b9`.
Current s18 (read from BASELINE and reproduced): **269517 events**, MD5
`32afbf11e43b4bf9d0bd470ad502ba0a`. No anchor was edited. Commands:

```sh
/tmp/mr-qa-s7b2-precheck-no59tyku/source/.pio/build/native/program
/home/staszek/mr-slice2-ref/bin/python /tmp/mr-qa-s7b20-author-d52_n7cw/reference_slice2.py --quiet-vectors --compare /home/staszek/MeshRoute/test/test_remote_codec.cpp
/home/staszek/mr-slice2-ref/bin/python /tmp/mr-qa-s7b20-author-d52_n7cw/reference_slice2.py --quiet-vectors --compare /tmp/mr-qa-s7b20-author-d52_n7cw/corrupt-reference-literal.cpp
python3 tools/run_corpus.py --out /tmp/mr-qa-s7b20-author-d52_n7cw/corpus --lus /tmp/mr-qa-s7b2-precheck-no59tyku/sim/orchestrator/lus --jobs 4 --require-anchors
python3 tools/run_corpus.py --compare /tmp/mr-qa-s7b2-precheck-no59tyku/corpus /tmp/mr-qa-s7b20-author-d52_n7cw/corpus
```

`reference_slice2.py` is extracted unchanged from the complete executable Python block in
`docs/superpowers/evidence/2026-09-06-radmin-slice2.md` §3.7, beginning `#!/usr/bin/env python3`.
The existing disposable reference venv supplies PyNaCl; no project/system dependency changed. Positive logs:
`native.log`, `old-reference.log`, `corpus.log`, `corpus-compare.log`. The expected-negative log is
`reference-negative.log`. Initial setup error is preserved in `setup-error.txt`: an overly broad comparison
attempt included the private checkout's omitted `spec/dv_dual_sf.lua` and stopped before running tests;
the corrected check explicitly binds the 380 relevant files above. An initial rulings append patch also
failed its context match and made no edit; the corrected patch used the actual final line.

No full mutation union, board links, ABI controls, six-probe chain, tools suite or warning census ran in this
authoring turn. The codec brief requires them for implementation and independent QA. The previous B378
candidate +4976 B and B379 116-check negative-producer proof remain the preceding pre-check's measurements;
they were not rerun or relabelled as linked RAM/new-domain verification today.

## 3. Source conclusions and authoring decisions

The codec brief §2 records the verified source anchors. The codec's one layout/header/nonce/AAD path already
handles fixed, session-key-authenticated empty payloads. The approved 12-byte clear header fits the existing
41-byte header / 46-byte AAD maxima; its three-byte nonce suffix fits the existing eight-byte maximum suffix.
No new persistent buffer, Node member, target producer or carrier change is necessary for codec preparation.
The new typed result must avoid the current payload-result fallback into authenticated PROTOCOL_ERROR.

Old response callers are real: `remote_session.cpp:592/797/835/862/926` encode/decode executed transcripts,
bootstrap and ingress. Hence the introductions at `remote_codec.h:11` and `test_remote_codec.cpp:18` claiming
there are no callers are stale. **B382** registers their correction inside the two already-touched files;
it does not authorize a production caller change. Existing linked codec functions also invalidate an
assumed zero gateway-flash delta; the implementation gate must measure/attribute it.

The new allocation is an append-only response opcode under the existing remote subprotocol discriminator.
Design §§8.1/8.9a/8.11/9 now record the exact approved layout/nonce/domain; §15 records the target-wide rate
and storage. B378/B379 remain open implementation/gate obligations. The codec brief requires source-validation
before coding and a complete frozen handoff before QA reruns every implementation instrument.

## 4. Independent B383 verification and revision-2 fold-in — 2026-09-09

The coder's [revision-1 preflight](../evidence/2026-09-09-radmin-slice7b2-0.md) is preserved unchanged, SHA-256
`dfdaf0409099bda8dcf28783a5cd90e5dc3fc3432f1c40104bbe32e74512c853`. QA checked the current shared files
against all **13/13** preparation hashes in that report before editing documentation. Production remains
at `1d4b3ad`; simulator remains clean at `06746a9`.

QA read `remote_codec.h:314–322` and the actual `remote_codec.cpp:554–594` control flow. `dm_open` writes
valid-tag plaintext before the TERMINAL/PROTOCOL_ERROR result-code checks. Those checks can return
`bad_result_code`; `out = d` is later and never executes on that return. A bad tag instead returns
`auth_failed` without writing caller plaintext. Thus the header's all-failures preservation promise is
wrong, while decoded-result publication remains conditional on complete success.

QA extracted the complete fixture from coder evidence §5 unchanged, SHA-256
`155ac8f5c5fbb87cd2d040eaa3e5d83bb4c304f224a29de7f1f87967b459966c`, into
**`/tmp/mr-qa-b383-8zc6x8eq/failure-output-proof.cpp`**. It was freshly compiled against the existing private
base archives after matching all **342** tracked `lib/`, `src/`, `test/`, `tools/` files plus `platformio.ini`
and the exact HEAD to the shared checkout. The private source was clean. Commands, archive hashes and logs
are `compile-command.json`, `inputs.json`, `compile.log` and `proof.log` in that directory.

Independent result, exit **0**:

```text
opcode 1: valid tag / invalid result -> bad_result_code; plaintext 08 d1; decoded sentinel retained
opcode 4: valid tag / invalid result -> bad_result_code; plaintext 01 d1; decoded sentinel retained
PASS: 24 checks; bad-tag controls preserve the plaintext buffer
```

The fixture checks the two decrypted bytes, untouched trailing canaries and named decoded sentinels; it does
not measure every decoded field. The complete-publication conclusion additionally follows from the source's
single final assignment. New-domain tests retain the brief's stronger complete logical-output comparison.

Revision 2 explicitly authorizes the **coder** to correct the public decoder comment in the already-fenced
header. Old variable-body decode behavior and caller-owned wiping stay unchanged. The new fixed, empty-body
ADMISSION_RESULT can preserve plaintext on every failure without changing the old contract. B383 is registered
as OPEN/READY pending that comment edit and independent final verification; B313 remains a separate primitive
comment finding. No new ruling, production fix, API refactor or full gate is part of this fold-in.

Native 2883/127709/0 and reference 87/87 in the coder report are that coder's fresh executions; §2 records
QA's earlier independent baseline runs. This turn reran the focused fixture only, not the full native suite,
reference, corpus, boards, probes or mutations. No test PIN was changed or new-domain PASS claimed.
