<!-- Independent QA/Author: OpenAI Codex; no production implementation -->
# Slice 7b-3-0 — independent author pre-check — 2026-09-13

**Author baseline/source pre-check PASS.** The [revision-1 codec brief](2026-09-13-radmin-slice7b3-0-action-busy-codec.md)
is ready for coder source-validation. This is not the implementation gate, a B391 closure, or permission to
start 7b-3 behavior. R-RA-37 is the allocation authority; R-RA-38/39 remain the separate behavior rulings.

## 1. Actual source and preservation

The user's preparation report described uncommitted inputs. Inspection instead found a clean checkout at
**`b9d75aaca55a0e350d9707512e9b6eec8a81ab22`**, owner commit `7b-3 prep`. Its 70 changed paths relative to
`f993191be7f6980870f440f6032bca72278539a7` are documentation/instructions/evidence; no `lib/`, `src/`, `test/`,
`tools/`, `simulation/` or `platformio.ini` change. The codec brief is pinned to the actual successor.
Simulator **`06746a97de5764415d6fcef10b97bca90569b9c7`** remains clean and unchanged.

Private complete snapshot: **`/tmp/mr-qa-s7b30-author-x6wfc8ai/snapshot`**. The input manifest records
**1312** tracked/untracked MeshRoute files and **285** simulator files, with byte hashes and modes.
All snapshot input bytes/modes were verified before running instruments. Initial copying encountered the
repository's relative `spec/dv_dual_sf.lua` link without a private sibling simulator. No checks ran on that
incomplete copy. The corrected snapshot binds that link to the exact original simulator resource; its target,
original spelling and verified content are recorded in `receipt.json`. Shared sources were never changed.

The original 7b-3 revision-3 brief and all coder receipts remain untouched. That brief is still the typed-plan
enumeration contract; its production inputs match b9d75aa. Formal behavior reissue waits for the codec commit.
The new codec brief explicitly permits QA's design edit as a preparation input and the separately attributed
B391 preflight repair/report. It does not permit other production changes from concurrent work.

Durable raw evidence: [pre-check artifacts](../evidence/2026-09-13-radmin-slice7b3-0-precheck/README.md).
Scripts, commands, expected exits, hashes, source/mode inventories and exact compressed logs are retained.
The final preservation report identifies only QA's documentation/evidence additions and edits.

## 2. Fresh independent measurements

| Instrument | Executed result | Limit |
| --- | --- | --- |
| `pio test -e native`, actual private binary | **2909 cases /174485 assertions /0 failures /0 skipped** | Fresh build and execution; wrapper's zero-case summary is not used. |
| Fresh Release/Ninja normal + gateway simulator | **64 actual compiler actions** | Fresh build directory; no stale overlay objects. Simulator sources unchanged. |
| Canonical corpus + manifest validation + comparison | **36/36 current anchors; 36/36 byte-identical** | Actual streams compared to the independently checked 7b-3 author baseline after both manifests validate. |
| Existing independent codec reference | **89/89 arrays unchanged**, both external primitive anchors pass | Original 87 plus two admission arrays; no new production meaning verified. |
| Comparator controls | Changed/missing/extra/duplicate controls RED; separate one-byte corruption exits **1** | Comparator controls, not native mutation counts. |
| Public codec boundary reproduction | **9781 checks PASS** | All 256 result bytes through each of four domains, plus publication/buffer and bad-tag controls. |
| Future-allocation negative control | `--allocated` exits **1**, `FAIL check 2` | Correctly detects that current ceiling is 07; no private production patch. |
| Mutation source-cardinality audit | **52 batteries /815 patterns; one zero-match X09 (B391)** | No native mutation executed. B342's unusable control is a separate known limit. |

The corpus baseline was read from current `simulation/BASELINE.md`, SHA-256
`71f140988e9d1a5bf1be49dd1feb8fc7ebb8e46c7a4b119e16db62f0df76962f`.
No anchor was edited. The fresh simulator executable has SHA-256
`862241173963b3c30a501849dea9e5e6184227575225aa8e263f0f5c48d1170a`.
Previous matching streams: `/tmp/mr-qa-s7b3-author-8srh2_5f/corpus`; current streams are under this snapshot's
parent `corpus/`. `corpus-identity.json` retains all 36 actual byte comparisons and the fresh compiler count.

The main driver is `precheck.py`; `codec-audit.py` runs the reference, corruption, source audit and real
codec proof using the existing `/home/staszek/mr-slice2-ref/bin/python` environment. It links the reproduction
to this snapshot's freshly built native libraries. `corpus-audit.py` performs both validations/comparison and
an additional direct byte comparison. Exact argv/cwd/exit records are in the accompanying JSON files.

**Not run:** board links, ABI controls, six-probe chain, full tools discovery, warning census or full native
mutation union. No fresh linked RAM/flash measurement is claimed. All are mandatory for the codec
implementation and subsequent independent QA under brief §6. The current X09 defect prevents a full PASS.

## 3. Codec boundaries and independent expectations

Source anchors and exact changes are in brief §§2–4. The real-codec proof confirms:

| Domain | Current encoder accepted | Current decoder accepted/rejected |
| --- | --- | --- |
| Authenticated TERMINAL | 256/256 | **8 /248** |
| Open TERMINAL | 256/256 | **8 /248** |
| Authenticated PROTOCOL_ERROR | 256/256 | **1 /255** |
| Open PROTOCOL_ERROR | 256/256 | **256 /0**, untyped |

Only the two terminal decoder rows become 9/247 after allocation. The encoder already seals 08, 09 and FF;
it has no terminal semantic gate to change. Authenticated protocol-error 08 still rejects, admission remains
unchanged and open protocol-error remains untyped. B383's valid-tag failure leaves plaintext while the decoded
sentinels stay unpublished; bad-tag controls preserve the buffer. The reproduction checks named sentinels,
not the entire logical result; full failed-object coverage remains a final native-test obligation.

The existing auth-terminal 08 KAT must keep its name/bytes and become positive. Rejection coverage moves to
new terminal 09 fixtures; protocol-error 08 negatives do not move. Frozen independent expectations for five
additional arrays are in `independent-boundary-vectors.json`, derived from the existing hashlib/PyNaCl stack
before consulting the reproduction's production output. Header/nonce/AAD/body bytes and all old 89 array
hashes are retained. The reference generator's historical comments are preserved as history; the new
extension explains the changed interpretation. The old 89-only strict whole-file comparison is replaced by
an extended strict old+new comparison, never weakened to ignore extras.

Source consumers retain typed values or map DispatchOutcome; none requires a production exhaustive
RemoteTerminal switch repair in this pre-check. R60 does consume the reserved-range comment being changed:
its pattern must become code-based and retain the same effect. B391 is independently re-confirmed as the
existing X09 zero-match; this author pass does not repair it or claim its closure.

## 4. Documentation finding and dispatch

**B393:** R-RA-37–39 were recorded, but three live indexes (register §0, MEMORY dispatch, design header/§19.1)
still called their settled choices proposals or pending owner calls. Design §8.9 still forbade terminal 08.
QA aligns those current entries and the allocation table, explicitly distinguishing approved allocation from
unimplemented codec/producer behavior. The owner ledger, original revision-3 behavior brief and all historical
receipts stay unchanged. No new ruling is needed; B393 closes as a documentation correction, not runtime QA.

Next: coder source-validates the separate codec brief, implements within its two-file production fence and
provides the full frozen handoff. Typed-plan enumeration and B391 preflight can proceed independently.
B389 allocation still needs measured owner approval; B390 implementation and B392 metal/controller obligations
remain open. After independent codec PASS and the owner's separate commit, QA reissues 7b-3 at that hash.
