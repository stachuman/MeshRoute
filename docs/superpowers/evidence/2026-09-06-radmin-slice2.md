<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 2 — the remote codec and its independent known-answer tests · coder evidence · 2026-09-06

Brief (authority): `docs/superpowers/plans/2026-09-06-radmin-slice2-remote-codec.md` (Quality-Agent PASS; the
post-commit Author pin `9ea4947` is the dispatch base). Design: `2026-08-23-remote-admin-independent-rpc-design.md`
§8 / §8.9 / §8.11 / §9 / §19 item 2. Rulings: R-RA-3 / R-RA-4 / R-RA-5 / R-RA-13 / R-RA-25 / **R-RA-28** in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`. Pre-check ledger:
`docs/superpowers/plans/2026-09-06-radmin-slice2-precheck.md`, **its §4 resolutions controlling**. Prior evidence:
`docs/superpowers/evidence/2026-09-06-radmin-slice1b.md` (reference pins only; every figure below is re-derived).

**Result: every gate green. 36/36 corpus streams byte-identical (predicted before the first post-edit measurement),
and the paired `lus` executable is byte-identical too. Board RAM 0 / live flash 0 / ELF sha256 unchanged on BOTH
ruled boards, with exactly one new object each. Node ABI unmoved on all three ABIs. Mutation union 71 RED /
0 unusable. No STOP fired. One ordering deviation is recorded honestly in §12. All work UNCOMMITTED (D4).**

---

## §0 — Bases, starting provenance, and where the work was done

| Repository / tree | Commit | Status at start |
|---|---|---|
| `/home/staszek/MeshRoute` (shared checkout — **never edited**) | `9ea4947d99ec004f2952553ce6fd34b166eeebce` | ` M docs/superpowers/plans/2026-09-06-radmin-slice2-remote-codec.md` (the Author's post-commit pin edit, as the brief describes) |
| `/home/staszek/mr-slice2` (**the measured MeshRoute tree**, isolated worktree) | `9ea4947d99ec004f2952553ce6fd34b166eeebce` | **CLEAN** (`git status --porcelain` empty) |
| `/home/staszek/lora-universal-simulator` (**the measured simulator**) | `fd3295d8eaf71466270434f2cbf4f4eafe19edcc` | **CLEAN** |

```
$ git -C /home/staszek/MeshRoute worktree add /home/staszek/mr-slice2 9ea4947d99ec004f2952553ce6fd34b166eeebce
Preparing worktree (detached HEAD 9ea4947)
HEAD is now at 9ea4947 Slice 2 prep
$ git -C /home/staszek/mr-slice2 rev-parse HEAD
9ea4947d99ec004f2952553ce6fd34b166eeebce          # == the brief's pinned base   ✓
$ git -C /home/staszek/mr-slice2 status --short   # (empty)
$ git -C /home/staszek/lora-universal-simulator rev-parse HEAD
fd3295d8eaf71466270434f2cbf4f4eafe19edcc          # == the brief's pinned base   ✓
$ git -C /home/staszek/lora-universal-simulator status --porcelain   # (empty)
```

**STOP 1 does not fire.** Both measured trees started clean at their exact pinned commits; the shared checkout's
single Author edit was neither copied into the worktree nor reverted, and the worktree carries no copy of the brief
beyond the committed one. The `git worktree add` is the ONE git write made anywhere; the worktree is left in place
for the owner to integrate from. Other pre-existing worktrees (`b162-arms/*`, `.claude/worktrees/agent-ac70…`,
`/tmp/mr-arm`) were listed, not touched.

Other paths used, all OUTSIDE both repositories:

| Path | What it is |
|---|---|
| `/home/staszek/mr-slice2-lus` | the PAIRED simulator build (`MESHROUTE_DIR=/home/staszek/mr-slice2`) |
| `/home/staszek/mr-slice2-ref` | the disposable reference venv — **left in place for QA** |
| `/home/staszek/mr-slice2-stage` | the isolated mutation staging copy (B286 headroom; §9) |

### 0.1 Paired CMake resolution — proved, not assumed

The existing `build/CMakeCache.txt` points at `MESHROUTE_DIR:PATH=/home/staszek/lora-universal-simulator/../MeshRoute`
(the SHARED checkout) and was NOT reused. Its other configured options were read and mirrored: generator
`Unix Makefiles`, `CMAKE_BUILD_TYPE=Release`, `CMAKE_CXX_COMPILER=/usr/bin/c++` (GNU 12.2.0), empty `CMAKE_CXX_FLAGS`,
Lua 5.4 found at the same paths.

```
$ cmake -S /home/staszek/lora-universal-simulator -B /home/staszek/mr-slice2-lus \
        -DCMAKE_BUILD_TYPE=Release -DMESHROUTE_DIR=/home/staszek/mr-slice2
-- MeshRoute found at /home/staszek/mr-slice2 — engine:meshroute ENABLED (faithful two-lib: normal + gateway)
```

Every later compile line names `/home/staszek/mr-slice2/lib/core/...`, so the paired build demonstrably compiled the
worktree and not the shared checkout. The first paired build reproduced the Author's read-only `lus` md5 exactly —
`b1b1d92c541cc7f6f63864a2bcc6a355` — which is independent confirmation that the pairing changed nothing by itself.


---

## §1 — Base captures, taken BEFORE editing, and the prediction

| Capture | Base value | Command |
|---|---|---|
| native (wrapper lies; the binary is the measurement) | **2615 cases / 111354 assertions / 0 failed** | `pio test -e native` then `./.pio/build/native/program` |
| corpus, anchors required | **36/36 produced+validated, anchors 36/36 reproduce `simulation/BASELINE.md`**; s18 `32afbf11` / 269517 / 0 | `python3 tools/run_corpus.py --jobs=8 --require-anchors --out <base> --lus /home/staszek/mr-slice2-lus/orchestrator/lus` |
| board ABI | PASS, **191 checks, 9/9 controls RED, 0 unusable**; `sizeof(Node)` native **222072/8**, heltec_mobile **117912/8**, gateway **148680/8** | `python3 tools/probe_board_abi.py` |
| B278 row ABI | PASS, **42 measurements, 6/6 controls RED** | `python3 tools/probe_b278_row_abi.py` |
| deterministic board pair | gateway **195844 RAM / 512092 flash / 283 objects**; heltec_mobile **205684 / 1355292 / 327** | `python3 tools/measure_board.py pair --jobs=2 --output .pio-measure/capture-base` |
| the six probes | all PASS (per-probe pins in §10) | `tools/probe_*/run.sh` |

Every one of those equals 1b's published pin, so the base is the base the brief describes.

⚠ `measure_board.py --output` must live under the repository's own `.pio-measure/`; a scratchpad path is refused
(`MESHROUTE_MEASURE_COMPILER_STATE must be below the repository .pio-measure/`). The first attempt hit that and was
re-run at `.pio-measure/capture-base`; nothing was measured from the failed attempt. Both captures (base and final)
therefore live at **identical fixed paths** — the B254/B262 requirement — and are archived outside the repo.

### 1.1 Source reach and the consumer census (derived, then predicted)

```
$ grep -rn "remote_codec|remote_body_cap|remote_kdf|remote_body_encode|remote_body_decode|
            remote_make_request_id|RemoteCarrier|RemoteLayout" --include=*.cpp --include=*.h lib src
src/firmware_remote.h:13, :30      — comments about the LEGACY static `remote_encode`
src/firmware_remote.cpp:24, :152   — the LEGACY file-static `remote_encode(const char*, uint8_t*, size_t)`
src/device_ble.h:168               — a comment that names the future `remote_body_cap`
src/fw_main.cpp:428                — a comment about the legacy verb table
```

**Zero call sites, zero includes.** ★ The census also produced a real finding: `src/firmware_remote.cpp:24` already
defines a file-static `remote_encode`. It is a different layer (legacy, deleted in Slice 9) and could not collide at
link time, but two functions of that name in one codebase is a reading hazard, so the new public entry points are
named **`remote_body_encode` / `remote_body_decode`** — which also pairs them with `remote_body_cap`, the name
`src/device_ble.h:168` already anticipates. No `src/` file was touched.

### 1.2 The prediction (written before any post-edit measurement)

1. **36/36 byte-identical**, all anchors reproduce, s18 keystone unchanged at `32afbf11` / 269517 / 0; zero remote-v2
   events, zero entropy draws, zero runtime codec calls — by construction, since nothing calls the module.
2. Both simulator variants must COMPILE `remote_codec.cpp` and gain an archive member; the linked executable should
   not change behaviour, and any hash movement must be attributable to unreferenced-member metadata.
3. Both ruled boards compile the new TU (**object_count +1 each: 283→284 and 327→328**) with **RAM delta 0 and live
   flash delta 0** — no consumer, no static storage, no global constructor, no Node member.
4. Node's three ABI pins unchanged.
5. Native grows by the new cases only; the mutation PIN re-syncs from the measured additions.
6. Every probe pin unchanged except `probe_features`' scanned-SOURCE-FILE count, which grows with the new files; the
   ownership contract and check pins must not move (the codec names no `MR_FEAT_RADMIN_*`).
7. Mutation union = new `radmin2codec` + FULL `b20codec`; every entry RED, 0 unusable.

**Every prediction held. §6-§10 are the measurements.**

---

## §2 — The delivered API, its domain table and its carrier map (source-derived)

Two new production files, `lib/core/remote_codec.h` (declarations, types, frozen constants) and
`lib/core/remote_codec.cpp` (**every executable decision**), in `MESHROUTE_NS`, with no heap, no static mutable
state, no global constructor, no virtual dispatch, no `src/` include and no `MR_FEAT_RADMIN_*` token.

### 2.1 The one layout/domain decision

`remote_layout(outer_type, ctl, RemoteLayout&)` is the single place a body layout is chosen, from (direction,
opcode, slot class); encode, decode, the key selector, the nonce, the AAD builder and admission all read that one
struct. Its typed `RemoteDomainId` is what the decoder publishes beside a result, so `0x00` can never be read out of
its domain (design §8.9's Slice-2 contract).

| Outer / opcode | Slot | Typed domain | Clear header (wire order) | Body | Fixed overhead | Key | Epoch in nonce |
|---|---|---|---|---|---|---|---|
| CMD `0` AUTH_EXECUTE | 0..9 | `cmd_auth_execute` | ctl, id64 | ct N + tag | **25** | session | no |
| CMD `1` OPEN_EXECUTE | F | `cmd_open_execute` | ctl, id64 | clear N | **9** | — | — |
| CMD `2` BOOTSTRAP | F | `cmd_bootstrap` | ctl, id64, ctrl_pub32 | tag | **57** | **base** | no |
| CMD `3`/`4`/`5` ACK/SAFE/FORCE | 0..9 | `cmd_response_ack` / `cmd_safe_rollover` / `cmd_force_rollover` | ctl, id64 | tag | **25** | session | no |
| RESP `0`/`1`/`4` | 0..9 | `resp_output_auth` / `resp_terminal_auth` / `resp_protocol_error_auth` | ctl, id64, seq8 | ct N + tag | **26** | session | no |
| RESP `0`/`1`/`4` | F | `resp_*_open` | ctl, id64, seq8 | clear N | **10** | — | — |
| RESP `2` BOOTSTRAP | 0..9 | `resp_bootstrap` | ctl, id64, epoch64 | tag | **33** | **base** | **yes** |
| RESP `3` ROLLOVER_RESULT | 0..9 | `resp_rollover_result` | ctl, id64, epoch64, abandoned8 | tag | **34** | **base** | **yes** |

Every overhead is `static_assert`ed against the SAME `constexpr` field arithmetic the runtime uses
(`header_bytes_of()` / `fixed_overhead_of()` in `remote_codec.cpp`), so a field added without touching the table
fails to compile. CMD `6..F`, RESP `5..F` and slots `A..E` reject, as does every illegal pairing (execute on the
sentinel, bootstrap request on a session slot, bootstrap response / rollover result on the sentinel).

Frozen results: TERMINAL `00 completed · 01 scheduled · 02 unknown_command · 03 refused · 04 output_truncated ·
05 internal_error · 06 session_full · 07 session_busy` (`08..FF` reject); authenticated PROTOCOL_ERROR carries only
`00 already_acknowledged` (`01..FF` reject) in **its own** namespace. The OPEN protocol error is given **no** result
code domain — §8.9's separate open-validation policy stays with its later owner.

### 2.2 Keys, nonce, AAD, entropy

* `remote_kdf_base` = BLAKE2b-512(`"MeshRoute remote-admin v2 base"` ‖ shared32 ‖ **controller** ed_pub32 ‖
  **target admin** ed_pub32)[:32] — ordered full keys, never DM's sorted short hashes, never DM's `MR-E2E-v1` label.
  It REFUSES an all-zero shared point before a key exists.
* `remote_ecdh_shared` wraps the existing `ecdh_shared` (which returns void and checks nothing) and adds the
  degenerate-point refusal at the new boundary.
* `remote_kdf_session` = BLAKE2b-512(`"… v2 session"` ‖ base32 ‖ epoch LE64)[:32].
* `remote_nonce` = BLAKE2b-512(`"… v2 nonce"` ‖ selected_key32 ‖ outer ‖ ctl ‖ id LE64 ‖ seq ‖ source LE32
  [‖ epoch LE64 iff `epoch_in_nonce`])[:24]. It REFUSES an open domain (no fake zero nonce) and an absent source.
* `remote_aad` = outer ‖ the exact clear header ‖ source LE32; no ciphertext, no tag, no implicit sequence zero, no
  mutable forwarding header.
* `remote_make_request_id(uint64_t&, RemoteEntropyFn, void*)` — a caller-supplied, STATUS-RETURNING provider with
  caller-owned context and an exact 8-byte destination. Absent provider, `false`, or a false-after-partial-fill all
  return `entropy_failed` and leave the caller's committed value untouched. **B312 stays open**: `IHal::rand_bytes`
  is void, so no adapter may turn it into unconditional success; the first integration slice owes the real provider.
* Little-endian by explicit bytes throughout; the u64 operation the shared `wire::Writer/Reader` lacks is composed
  LOCALLY in this TU (`put_u64_le` / `get_u64_le`) — no shared-helper refactor (C1).
* Intermediate KDF/nonce message buffers and digests are `crypto_wipe`d on every exit; no secret reaches a
  diagnostic (the codec emits nothing at all).

### 2.3 `remote_body_cap(RemoteCarrier)` — the ONE capacity authority

```
governing = min( protocol::max_payload_bytes_hard_cap                       (STORAGE, 241)
               , data_inner_cap(flags, outer_type, lora_max_frame_bytes) )  (real AIR-FIT, 242 plain / 238 CRYPTED)
reserved  = dm_inner_origin_bytes + dm_inner_source_hash_bytes + dm_inner_dst_hash_bytes   = 1 + 4 + 4  (ALWAYS)
extras    = cross_layer ? 2 + path_depth : 0   +   wrapper ? 1 : 0
cap       = governing - reserved - extras                                   (underflow REFUSES)
```

⛔ It is **not** `dm_max_body_bytes - something` and not a copied 232; the terms are the named per-field constants
and the real air-fit formula. R-RA-28: the four DST_HASH bytes are reserved whether or not the leg transmits them.

**The live leg-by-leg map, measured (`§radmin-2/carrier`):**

| live carrier | outer DATA type | path | cap |
|---|---|---|---|
| home-originated request, by key hash / by node id (legally hash-less) | `REMOTE_CMD` | none | **232 / 232** |
| target response, by key hash / by node id | `REMOTE_RESP` | none | **232 / 232** |
| hosted-mobile last mile (`addr_len=1`), no destination override / hash present | `REMOTE_RESP` | none | **232 / 232** |
| typed mobile→home same-layer wrapper, enclosing `REMOTE_CMD` / `REMOTE_RESP` | **`MOBILE_SEND`** | none | **231 / 231** |
| full cross-layer request/response, depth 1 / 2 / 3 / 4 (both addressing forms) | `REMOTE_CMD`/`REMOTE_RESP` | 2+depth | **229 / 228 / 227 / 226** |
| typed mobile cross-layer wrapper, destination depth 1 / 2 / 3 | **`MOBILE_SEND`** | 2+depth, +enclosed type | **228 / 227 / 226** |
| typed cross-layer wrapper at destination depth 4 | — | — | **INVALID (`bad_carrier`), not 225** |

The wrapper at destination depth *d* equals the home's own full path at depth *d+1* — measured for d = 1,2,3 — which
is exactly the frame the home re-originates (`node_mac.cpp:928` requires `1 + hop_count <= gw_env_max_hops`).
Invalid descriptors all refuse and none is normalised: full depth 0/5, cursor ≥ depth, a path on a same-layer
descriptor, `source_hash_on_wire = false` (R-RA-13), `addr_len > 1`, outer type 0, `MOBILE_SEND` without the wrapper
flag, a wrapper whose outer type is not `MOBILE_SEND`, a wrapper enclosing a non-RPC type, an enclosed type without
a wrapper, and outer CRYPTED with no DST_HASH (`pack_data`'s structural rule).

### 2.4 Corrections to the pre-check's hypotheses

| pre-check statement | outcome |
|---|---|
| §2 "every v2 carrier is hash-addressed ⇒ the by-node-id rows are not v2 carriers" | **WITHDRAWN by R-RA-28 and by this slice's map**: the hash-less legs ARE live carriers; they simply do not earn a larger body. Both forms are in the map at 232. |
| §3 "the REAL packers accept exactly at the cap and refuse at cap+1 for every carrier" | **Superseded**: true for the ADMISSION authority, false for the raw packer on a hash-less leg — measured in §8, where raw **233 physically fits** while admission refuses it. |
| §3 "Add the new `.cpp` to the sim's CMake source list? — ⚠ check whether the lus CMakeLists globs" | **Resolved (§4.2)**: it LISTS files (`_meshroute_core_srcs`, `CMakeLists.txt:58`, consumed at `:83` for both variants). One line added. |
| §3 "RNG-failure path … testable via the HAL fake" | **Qualified by B312 (source fact)**: `IHal::rand_bytes` is void, so a HAL fake cannot report failure. The refusal lives at the codec's own explicit entropy boundary; the invariant is kept, the mechanism is not the HAL. |
| §1 "0e's 236 / 233 rows" | **Kept as historical raw-packer measurement**, superseded as an admission allowance (the comment-only 0e correction, §11). |
| §5 numbering note | B310 is the parked last-mile unification; the 1b harness finding keeps its own row (see §13). |

---

## §3 — Instrument **remote-v2-independent-reference** (complete and reproducible)

### 3.1 Provenance and environment

A DISPOSABLE virtualenv OUTSIDE both repositories; the system Python was not mutated and nothing was vendored.

```
$ python3 -m venv /home/staszek/mr-slice2-ref
$ /home/staszek/mr-slice2-ref/bin/pip install --disable-pip-version-check pynacl
Successfully installed pycparser-3.0 cffi-2.1.1 pynacl-1.6.2
$ /home/staszek/mr-slice2-ref/bin/pip freeze
cffi==2.1.1
pycparser==3.0
PyNaCl==1.6.2
$ /home/staszek/mr-slice2-ref/bin/python -V
Python 3.11.2
```

**The venv is LEFT IN PLACE at `/home/staszek/mr-slice2-ref` so QA can rerun the reference.** The instrument's own
source is reproduced verbatim in §3.5; it is deliberately NOT delivered into either repository (fence).

The reference is a different implementation stack from the firmware in every layer that matters:
CPython `hashlib.blake2b(digest_size=64)` (`_blake2`) for every BLAKE2b-512 derivation, PyNaCl/libsodium
`crypto_aead_xchacha20poly1305_ietf_encrypt` for every sealed body, and explicit Python byte concatenation for every
header and AAD. It never reads MeshRoute source, never links monocypher and never calls the production codec.

### 3.2 External primitive anchors — reproduced BEFORE any remote vector is trusted

The generator refuses to emit a single vector unless both anchors match (`raise SystemExit`).

```
ANCHOR A1  BLAKE2b-512("abc")            == RFC 7693 App. A          OK
           ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923
ANCHOR A2  XChaCha20-Poly1305 AEAD        == draft-irtf-cfrg-xchacha-03 A.3.1  OK
           ct[:32] bd6d179d3e83d43b9576579493c0e939572a1700252bfaccbed2902c21396cbb
           tag     c0875924c1c7987947deafd8780acf49
           versions: python 3.11.2  PyNaCl 1.6.2  hashlib.blake2b impl _blake2

---- pairwise nonce inequality, same request_id/source, authenticated domains ----
10 authenticated domains, 45 pairs, all distinct
  cmd_auth_execute             ctl=0x03 outer=0xa0 key=session epoch_in_nonce=False nonce=8ef2a105652e8e91f945088e0670c238c9f28112593d9189
  cmd_bootstrap                ctl=0x2f outer=0xa0 key=base epoch_in_nonce=False nonce=f3b81fab83e225c0cfdda47f64217a3e36d27ee08f6874a0
  cmd_response_ack             ctl=0x33 outer=0xa0 key=session epoch_in_nonce=False nonce=6b3c952f6d1b779c0306a44567a2f6d6746302892b36de1a
  cmd_safe_rollover            ctl=0x43 outer=0xa0 key=session epoch_in_nonce=False nonce=bf780af02f904230885e10f25a81d913e79d68626e6bd311
  cmd_force_rollover           ctl=0x53 outer=0xa0 key=session epoch_in_nonce=False nonce=b497b47903bed1c2eb31c894ba6f24c90a6dea0e49daacf3
  resp_output_auth             ctl=0x03 outer=0xa1 key=session epoch_in_nonce=False nonce=cc8175a34361c80f0744009890b44dc455bb508ccc97fe75
  resp_terminal_auth           ctl=0x13 outer=0xa1 key=session epoch_in_nonce=False nonce=94f6c3d9bebaff786a5c96a23a923530e4efa25b63c35e51
  resp_bootstrap               ctl=0x23 outer=0xa1 key=base epoch_in_nonce=True nonce=9af77ceb80a62baaca897655e0e2336f5fae180e2732cc3d
  resp_rollover_result         ctl=0x33 outer=0xa1 key=base epoch_in_nonce=True nonce=d90d087a5434593bea8aee21ea765afd37b668396ab9fe1a
  resp_protocol_error_auth     ctl=0x43 outer=0xa1 key=session epoch_in_nonce=False nonce=501a6c5d69e2c85ce0859bd0ad0084374669de26c9730d6b

---- request-id birthday bound (design §9) ----
n = 2^16 = 65536;  p ~= n(n-1)/2^65 = 1.1641354547009541e-10 = 2^-33.0000

87 vectors emitted
```

* **A1** RFC 7693 Appendix A, `BLAKE2b-512("abc")` — proves `hashlib.blake2b(digest_size=64)` is the standard
  function the design's `[:32]` / `[:24]` truncation is defined over.
* **A2** draft-irtf-cfrg-xchacha-03 §A.3.1 — proves libsodium's XChaCha20-Poly1305 is the same AEAD the firmware's
  monocypher `crypto_aead_lock` is separately pinned to in `test/test_dm_crypto.cpp:129`. Two independent
  implementations reproducing one published vector is what makes the sealed-body literals below meaningful.

### 3.3 Fixture inputs (recorded so every literal is reproducible)

| input | value | why |
|---|---|---|
| `shared32` | `40 41 … 5F` | a fixed 32-byte shared point |
| `controller_ed_pub32` | `80 81 … 9F` | non-zero high bytes |
| `target_admin_ed_pub32` | `C0 C1 … DF` | distinct from the controller's, so ORDER is observable |
| `admin_epoch` | `0x0123456789ABCDEF` (and `…F0` for the second) | non-zero high byte |
| `request_id` | `0xF0E1D2C3B4A59687` | non-zero high byte |
| `source_hash` | `0xDEADBEEF` (and `0x00C0FFEE`) | non-zero high byte; the second is the shared-credential case |
| `response_seq` | `0x2A` (plus 0 and 255 endpoints) | |
| `abandoned_count` | `0x07` | |
| slot | `3` for the main fixtures; every slot 0..9 pinned separately | both endpoints and all legal values |
| plaintexts | `"routes"` · `"status"` · `"line one\n"` · `00` · `01 DF 93 04 00` (scheduled + 299 999 ms) · 207 and 208 patterned bytes | |

### 3.4 Fixture mapping — 87 frozen literals, all matched byte-for-byte

| literal family | count | what it pins | native case |
|---|---|---|---|
| `kRefBaseKey`, `kRefBaseKeySwapped`, `kRefSessionKey`, `kRefSessionKeyEpoch2` | 4 | the two KDFs, the ORDER binding, the epoch input | `§radmin-2/kdf` |
| `kRefHeader_<domain>` × 14 | 14 | the exact clear header of every domain, both directions | `§radmin-2/wire` |
| `kRefNonce_<domain>` × 10 | 10 | the derived nonce of every AUTHENTICATED domain | `§radmin-2/wire`, `/nonce` |
| `kRefAad_<domain>` × 10 | 10 | the AAD of every authenticated domain | `§radmin-2/wire` |
| `kRefBody_<domain>` × 14 | 14 | the COMPLETE sealed/tag-only/clear body of every domain | `§radmin-2/wire` |
| `kRefNonce_cmd_auth_execute_src2`, `kRefNonce_resp_bootstrap_epoch2`, `kRefBody_resp_rollover_result_epoch2` | 3 | source-hash separation; epoch moves the response nonce AND the whole body | `/nonce`, `/wire` |
| `kRefNonceWRONG_cmd_bootstrap_with_epoch`, `kRefNonceWRONG_cmd_auth_execute_with_epoch` | 2 | ⛔ the nonces a WRONG codec would derive if a REQUEST took an epoch — production must NOT produce them | `§radmin-2/wire` |
| `kRefBody_cmd_auth_execute_slot0..slot9` | 10 | every legal session slot, independently sealed | `§radmin-2/slots` |
| `kRefBody_resp_output_auth_seq0`, `…_seq255` | 2 | `response_seq` is one byte, both endpoints | `§radmin-2/slots` |
| `kRefBody_cmd_auth_execute_empty` | 1 | tag-only empty ciphertext through a VARIABLE layout | `§radmin-2/length` |
| `kRefBody_resp_terminal_auth_t0..t7` | 8 | all eight terminal meanings | `§radmin-2/result` |
| `kRefBody_resp_terminal_auth_scheduled` | 1 | bounded detail bytes preserved exactly | `§radmin-2/result` |
| `kRefBody_resp_terminal_auth_code08`, `…_codeff` | 2 | unallocated terminal codes that still AUTHENTICATE | `§radmin-2/result` |
| `kRefBody_resp_protocol_error_auth_code01/07/08/ff` | 4 | invalid protocol-error codes that still AUTHENTICATE ⇒ the rejection is the CODE-DOMAIN check | `§radmin-2/result` |
| `kRefBody_cmd_auth_execute_atcap` (232), `…_oversize` (233) | 2 | decode ADMISSION on a genuinely valid sealed body | `§radmin-2/carrier` |

### 3.5 The comparison and its control

The reference is a CHECK, not only a generator: `--compare <TU>` re-reads every frozen `kRef…` literal out of
`test/test_remote_codec.cpp` and compares byte-for-byte.

```
$ /home/staszek/mr-slice2-ref/bin/python remote_v2_reference.py --quiet-vectors \
      --compare /home/staszek/mr-slice2/test/test_remote_codec.cpp
---- comparison: 87 reference vectors vs 87 frozen literals in …/test_remote_codec.cpp ----
  OK: every reference vector matches its frozen native literal byte-for-byte
$ echo $?
0
```

**CONTROL — a deliberately changed expected byte must fail it.** On a COPY of the TU, `kRefBaseKey[0]` was flipped
`0xfb -> 0xfa`:

```
  MISMATCH kRefBaseKey
     reference fb20732d92831f3585a66b8a55b18b01fb42895597635a610734bc9c4ef83b81
     frozen    fa20732d92831f3585a66b8a55b18b01fb42895597635a610734bc9c4ef83b81
  FAILED: 1 discrepancies
$ echo $?
1
```

The comparison was run again unchanged at the END of the full gate (after the last edit to the TU) and still
reports OK / exit 0. **STOP 4 does not fire**: the reference was available, its vectors agree, and the codec's real
encode/open/admission paths — not only the helpers — reproduce them (§7.2).

### 3.6 Ordering: the literals were produced BEFORE any production output existed

`reference-vectors-PREPRODUCTION.txt` (77 vectors, md5 `722c3ec8f3e11bc2886627a7441afb77`) was frozen at
**11:14:38 UTC**, and the extended `…PREPRODUCTION2.txt` (87 vectors, md5 `1210b8894c86d1058970fc5f5e207b5e`) at
**11:23:59 UTC**. `lib/core/remote_codec.cpp` did not compile until 11:3x and the native suite did not run against
it until 11:4x. **No expected value was ever adjusted to match production output.** The one literal that DID move
was a HAND-TYPED constant of mine, not a reference vector: the first run of the suite failed one assertion —

```
  logged: the bounded detail is preserved verbatim got=df930400 want=9f930400
  [doctest] test cases: 2640 | 2639 passed | 1 failed | assertions: 115288 | 1 failed
```

— because I mistyped 299 999 as `0x9F 93 04 00`. `299999 = 0x000493DF`, so the reference (and production) were right
and the test's own literal was wrong; it was corrected to `0xDF 93 04 00`. Reported here rather than quietly fixed.

### 3.7 The instrument's complete source (reproduce with the venv above)

```python
#!/usr/bin/env python3
"""remote-v2-independent-reference — MeshRoute remote-admin v2 Slice 2.

INDEPENDENT known-answer generator for the remote codec.  It implements the
design's formulas (2026-08-23-remote-admin-independent-rpc-design.md §8/§9)
from the SPEC TEXT, using a completely different implementation stack from the
firmware:

  * BLAKE2b-512  -> CPython `hashlib.blake2b(digest_size=64)`  (libb2/OpenSSL)
  * XChaCha20-Poly1305 -> PyNaCl / libsodium
    `nacl.bindings.crypto_aead_xchacha20poly1305_ietf_encrypt`
  * every header / AAD byte string is built by explicit concatenation here.

NOTHING in this file reads MeshRoute source, links monocypher, or calls the
production codec.  Two external primitive ANCHORS are reproduced FIRST and the
generator refuses to emit a single remote vector unless both match:

  A1  RFC 7693 App. A          BLAKE2b-512("abc")
  A2  draft-irtf-cfrg-xchacha-03 §A.3.1   XChaCha20-Poly1305 AEAD vector

Modes:
  (default)      run the anchors, print every vector as C++ literals
  --compare F    additionally re-read the frozen literals out of the native test
                 TU F and compare them byte-for-byte (the reference is then a
                 CHECK, not a generator)
"""
import argparse
import hashlib
import re
import sys

from nacl.bindings import (
    crypto_aead_xchacha20poly1305_ietf_encrypt as xseal,
)
import nacl

# ---------------------------------------------------------------- primitives
def b2b512(msg: bytes) -> bytes:
    return hashlib.blake2b(msg, digest_size=64).digest()


def le(v: int, n: int) -> bytes:
    return int(v).to_bytes(n, "little")


# ---------------------------------------------------------------- anchors
ANCHOR_BLAKE2B = bytes.fromhex(
    "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
    "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923")

XCHACHA_KEY = bytes(range(0x80, 0xA0))
XCHACHA_NONCE = bytes(range(0x40, 0x58))
XCHACHA_AAD = bytes.fromhex("50515253c0c1c2c3c4c5c6c7")
XCHACHA_PT = (b"Ladies and Gentlemen of the class of '99: If I could offer you "
              b"only one tip for the future, sunscreen would be it.")
XCHACHA_CT = bytes.fromhex(
    "bd6d179d3e83d43b957657949"
    "3c0e939572a1700252bfaccbed2902c21396cbb731c7f1b0b4aa6440bf3a82f4eda7e39ae"
    "64c6708c54c216cb96b72e1213b4522f8c9ba40db5d945b11b69b982c1bb9e3f3fac2bc36"
    "9488f76b2383565d3fff921f9664c97637da9768812f615c68b13b52e")
XCHACHA_TAG = bytes.fromhex("c0875924c1c7987947deafd8780acf49")


def run_anchors() -> None:
    a1 = b2b512(b"abc")
    if a1 != ANCHOR_BLAKE2B:
        raise SystemExit("ANCHOR A1 FAILED: hashlib.blake2b is not RFC 7693 BLAKE2b-512")
    got = xseal(XCHACHA_PT, XCHACHA_AAD, XCHACHA_NONCE, XCHACHA_KEY)
    if got != XCHACHA_CT + XCHACHA_TAG:
        raise SystemExit("ANCHOR A2 FAILED: libsodium XChaCha20-Poly1305 != draft-irtf-cfrg-xchacha-03 A.3.1")
    print("ANCHOR A1  BLAKE2b-512(\"abc\")            == RFC 7693 App. A          OK")
    print("           " + a1.hex())
    print("ANCHOR A2  XChaCha20-Poly1305 AEAD        == draft-irtf-cfrg-xchacha-03 A.3.1  OK")
    print("           ct[:32] " + got[:32].hex())
    print("           tag     " + got[-16:].hex())
    import nacl.bindings.sodium_core as _sc  # noqa: F401
    print(f"           versions: python {sys.version.split()[0]}  PyNaCl {nacl.__version__}  "
          f"hashlib.blake2b impl {hashlib.blake2b.__module__}")
    print()


# ---------------------------------------------------------------- v2 domain
LBL_BASE = b"MeshRoute remote-admin v2 base"
LBL_SESSION = b"MeshRoute remote-admin v2 session"
LBL_NONCE = b"MeshRoute remote-admin v2 nonce"

CMD = 0xA0
RESP = 0xA1

# fixture inputs — every one carries non-zero HIGH bytes on purpose
SHARED32 = bytes(range(0x40, 0x60))
CTRL_PUB = bytes(range(0x80, 0xA0))
TGT_PUB = bytes(range(0xC0, 0xE0))
EPOCH = 0x0123456789ABCDEF
EPOCH2 = 0x0123456789ABCDF0
REQ_ID = 0xF0E1D2C3B4A59687
SRC_HASH = 0xDEADBEEF
SRC_HASH2 = 0x00C0FFEE
SEQ = 0x2A
ABANDONED = 0x07
SLOT = 3


def kdf_base(shared: bytes, ctl_pub: bytes, tgt_pub: bytes) -> bytes:
    return b2b512(LBL_BASE + shared + ctl_pub + tgt_pub)[:32]


def kdf_session(base: bytes, epoch: int) -> bytes:
    return b2b512(LBL_SESSION + base + le(epoch, 8))[:32]


def nonce(key: bytes, outer: int, ctl: int, req: int, seq: int, src: int, epoch=None) -> bytes:
    msg = LBL_NONCE + key + bytes([outer, ctl]) + le(req, 8) + bytes([seq]) + le(src, 4)
    if epoch is not None:
        msg += le(epoch, 8)
    return b2b512(msg)[:24]


BASE_KEY = kdf_base(SHARED32, CTRL_PUB, TGT_PUB)
SESSION_KEY = kdf_session(BASE_KEY, EPOCH)


def ctl_byte(op: int, slot: int) -> int:
    return ((op & 0x0F) << 4) | (slot & 0x0F)


class Dom:
    """One authenticated or open domain: header bytes, key choice, epoch-in-nonce."""

    def __init__(self, name, outer, op, slot, *, authed, base_key, epoch_nonce,
                 seq=None, epoch=None, abandoned=None, ctrl_pub=None, variable=False):
        self.name = name
        self.outer = outer
        self.ctl = ctl_byte(op, slot)
        self.authed = authed
        self.base_key = base_key
        self.epoch_nonce = epoch_nonce
        self.seq = seq
        self.epoch = epoch
        self.abandoned = abandoned
        self.ctrl_pub = ctrl_pub
        self.variable = variable

    def header(self, req=REQ_ID) -> bytes:
        h = bytes([self.ctl]) + le(req, 8)
        if self.ctrl_pub is not None:
            h += self.ctrl_pub
        if self.epoch is not None:
            h += le(self.epoch, 8)
        if self.abandoned is not None:
            h += bytes([self.abandoned])
        if self.seq is not None:
            h += bytes([self.seq])
        return h

    def aad(self, req=REQ_ID, src=SRC_HASH) -> bytes:
        return bytes([self.outer]) + self.header(req) + le(src, 4)

    def key(self) -> bytes:
        return BASE_KEY if self.base_key else SESSION_KEY

    def nonce(self, req=REQ_ID, src=SRC_HASH, epoch=EPOCH) -> bytes:
        return nonce(self.key(), self.outer, self.ctl, req,
                     self.seq if self.seq is not None else 0, src,
                     epoch if self.epoch_nonce else None)

    def seal(self, pt: bytes, req=REQ_ID, src=SRC_HASH, epoch=EPOCH):
        out = xseal(pt, self.aad(req, src), self.nonce(req, src, epoch), self.key())
        return out[:len(pt)], out[len(pt):]

    def body(self, pt: bytes, req=REQ_ID, src=SRC_HASH, epoch=EPOCH) -> bytes:
        if not self.authed:
            return self.header(req) + pt
        ct, tag = self.seal(pt, req, src, epoch)
        return self.header(req) + ct + tag


# NOTE the wire ORDER of the response header is ctl | request_id | response_seq
# for the session response, but ctl | request_id | epoch [| abandoned] for the
# two base-key responses (design §8.6/§8.7): those carry no response_seq field.
DOMAINS = {
    # ---- REMOTE_CMD -------------------------------------------------------
    "cmd_auth_execute": Dom("CMD AUTH_EXECUTE slot3", CMD, 0x0, SLOT,
                            authed=True, base_key=False, epoch_nonce=False, variable=True),
    "cmd_open_execute": Dom("CMD OPEN_EXECUTE slotF", CMD, 0x1, 0xF,
                            authed=False, base_key=False, epoch_nonce=False, variable=True),
    "cmd_bootstrap": Dom("CMD BOOTSTRAP slotF", CMD, 0x2, 0xF,
                         authed=True, base_key=True, epoch_nonce=False, ctrl_pub=CTRL_PUB),
    "cmd_response_ack": Dom("CMD RESPONSE_ACK slot3", CMD, 0x3, SLOT,
                            authed=True, base_key=False, epoch_nonce=False),
    "cmd_safe_rollover": Dom("CMD SAFE_ROLLOVER slot3", CMD, 0x4, SLOT,
                             authed=True, base_key=False, epoch_nonce=False),
    "cmd_force_rollover": Dom("CMD FORCE_ROLLOVER slot3", CMD, 0x5, SLOT,
                              authed=True, base_key=False, epoch_nonce=False),
    # ---- REMOTE_RESP ------------------------------------------------------
    "resp_output_auth": Dom("RESP OUTPUT slot3", RESP, 0x0, SLOT,
                            authed=True, base_key=False, epoch_nonce=False, seq=SEQ, variable=True),
    "resp_output_open": Dom("RESP OUTPUT slotF", RESP, 0x0, 0xF,
                            authed=False, base_key=False, epoch_nonce=False, seq=SEQ, variable=True),
    "resp_terminal_auth": Dom("RESP TERMINAL slot3", RESP, 0x1, SLOT,
                              authed=True, base_key=False, epoch_nonce=False, seq=SEQ, variable=True),
    "resp_terminal_open": Dom("RESP TERMINAL slotF", RESP, 0x1, 0xF,
                              authed=False, base_key=False, epoch_nonce=False, seq=SEQ, variable=True),
    "resp_bootstrap": Dom("RESP BOOTSTRAP slot3", RESP, 0x2, SLOT,
                          authed=True, base_key=True, epoch_nonce=True, epoch=EPOCH),
    "resp_rollover_result": Dom("RESP ROLLOVER_RESULT slot3", RESP, 0x3, SLOT,
                                authed=True, base_key=True, epoch_nonce=True,
                                epoch=EPOCH, abandoned=ABANDONED),
    "resp_protocol_error_auth": Dom("RESP PROTOCOL_ERROR slot3", RESP, 0x4, SLOT,
                                    authed=True, base_key=False, epoch_nonce=False, seq=SEQ, variable=True),
    "resp_protocol_error_open": Dom("RESP PROTOCOL_ERROR slotF", RESP, 0x4, 0xF,
                                    authed=False, base_key=False, epoch_nonce=False, seq=SEQ, variable=True),
}

PT = {
    "cmd_auth_execute": b"routes",
    "cmd_open_execute": b"status",
    "cmd_bootstrap": b"",
    "cmd_response_ack": b"",
    "cmd_safe_rollover": b"",
    "cmd_force_rollover": b"",
    "resp_output_auth": b"line one\n",
    "resp_output_open": b"line one\n",
    "resp_terminal_auth": bytes([0x00]),
    "resp_terminal_open": bytes([0x00]),
    "resp_bootstrap": b"",
    "resp_rollover_result": b"",
    "resp_protocol_error_auth": bytes([0x00]),
    "resp_protocol_error_open": bytes([0x00]),
}

AUTH_DOMAINS = [k for k, d in DOMAINS.items() if d.authed]


# ---------------------------------------------------------------- emission
VECTORS: "dict[str, bytes]" = {}


def add(name: str, data: bytes) -> None:
    if name in VECTORS:
        raise SystemExit(f"duplicate vector name {name}")
    VECTORS[name] = data


def cpp(name: str, data: bytes) -> str:
    rows = []
    for i in range(0, len(data), 12):
        rows.append("    " + " ".join(f"0x{b:02x}," for b in data[i:i + 12]))
    return (f"// {name} ({len(data)} bytes)\n"
            f"const uint8_t {name}[{len(data)}] = {{\n" + "\n".join(rows) + "\n};")


def build() -> None:
    add("kRefBaseKey", BASE_KEY)
    add("kRefSessionKey", SESSION_KEY)
    # a second session key at a different epoch — proves the epoch is an input
    add("kRefSessionKeyEpoch2", kdf_session(BASE_KEY, EPOCH2))
    # a base key with the two public keys SWAPPED — proves the order is bound
    add("kRefBaseKeySwapped", kdf_base(SHARED32, TGT_PUB, CTRL_PUB))

    for k in DOMAINS:
        d = DOMAINS[k]
        add(f"kRefHeader_{k}", d.header())
        if d.authed:
            add(f"kRefNonce_{k}", d.nonce())
            add(f"kRefAad_{k}", d.aad())
        add(f"kRefBody_{k}", d.body(PT[k]))

    # a second source hash under the SAME key/header -> a different nonce
    add("kRefNonce_cmd_auth_execute_src2",
        DOMAINS["cmd_auth_execute"].nonce(src=SRC_HASH2))
    # bootstrap/rollover responses move when the epoch moves
    add("kRefNonce_resp_bootstrap_epoch2",
        DOMAINS["resp_bootstrap"].nonce(epoch=EPOCH2))
    add("kRefBody_resp_rollover_result_epoch2",
        Dom("RESP ROLLOVER_RESULT slot3 epoch2", RESP, 0x3, SLOT, authed=True,
            base_key=True, epoch_nonce=True, epoch=EPOCH2,
            abandoned=ABANDONED).body(b"", epoch=EPOCH2))
    # EVERY legal session slot of the authenticated execute domain (endpoints included)
    for s in range(0, 10):
        d = Dom(f"CMD AUTH_EXECUTE slot{s}", CMD, 0x0, s, authed=True,
                base_key=False, epoch_nonce=False, variable=True)
        add(f"kRefBody_cmd_auth_execute_slot{s}", d.body(b"routes"))
    # ⛔ TWO **WRONG** NONCES, generated on purpose: what a codec that fed the epoch into a
    #    REQUEST domain would derive.  The native test asserts production does NOT produce these,
    #    so "the bootstrap request carries no epoch" is a pinned byte fact, not a comment.
    add("kRefNonceWRONG_cmd_bootstrap_with_epoch",
        nonce(BASE_KEY, CMD, DOMAINS["cmd_bootstrap"].ctl, REQ_ID, 0, SRC_HASH, EPOCH))
    add("kRefNonceWRONG_cmd_auth_execute_with_epoch",
        nonce(SESSION_KEY, CMD, DOMAINS["cmd_auth_execute"].ctl, REQ_ID, 0, SRC_HASH, EPOCH))
    # response sequence endpoints
    for s in (0, 255):
        d = Dom(f"RESP OUTPUT slot3 seq{s}", RESP, 0x0, SLOT, authed=True,
                base_key=False, epoch_nonce=False, seq=s, variable=True)
        add(f"kRefBody_resp_output_auth_seq{s}", d.body(b"line one\n"))
    # tag-only empty ciphertext through a VARIABLE-body layout
    add("kRefBody_cmd_auth_execute_empty", DOMAINS["cmd_auth_execute"].body(b""))
    # terminal with bounded detail: scheduled + 4-byte LE delay 299999
    add("kRefBody_resp_terminal_auth_scheduled",
        DOMAINS["resp_terminal_auth"].body(bytes([0x01]) + le(299999, 4)))
    # every invalid authenticated PROTOCOL_ERROR result byte still AUTHENTICATES
    # (so a rejection proves the code-domain check, not a broken tag)
    for rb in (0x01, 0x07, 0x08, 0xFF):
        add(f"kRefBody_resp_protocol_error_auth_code{rb:02x}",
            DOMAINS["resp_protocol_error_auth"].body(bytes([rb])))
    # terminal 0x08 / 0xFF authenticate but are outside the terminal namespace
    for rb in (0x08, 0xFF):
        add(f"kRefBody_resp_terminal_auth_code{rb:02x}",
            DOMAINS["resp_terminal_auth"].body(bytes([rb])))
    # every legal terminal code, authenticated
    for rb in range(0, 8):
        add(f"kRefBody_resp_terminal_auth_t{rb}",
            DOMAINS["resp_terminal_auth"].body(bytes([rb])))
    # an OVERSIZE but perfectly valid sealed AUTH_EXECUTE body: 208 command
    # bytes -> 233 total, one over the 232 same-layer admission cap
    add("kRefBody_cmd_auth_execute_oversize",
        DOMAINS["cmd_auth_execute"].body(bytes((i * 7 + 11) & 0xFF for i in range(208))))
    # ...and the largest ADMITTED one: 207 command bytes -> 232 total
    add("kRefBody_cmd_auth_execute_atcap",
        DOMAINS["cmd_auth_execute"].body(bytes((i * 7 + 11) & 0xFF for i in range(207))))


def report_nonce_matrix() -> None:
    print("---- pairwise nonce inequality, same request_id/source, authenticated domains ----")
    ns = {k: DOMAINS[k].nonce() for k in AUTH_DOMAINS}
    seen = {}
    for k, v in ns.items():
        if v in seen:
            raise SystemExit(f"NONCE COLLISION between {seen[v]} and {k}")
        seen[v] = k
    n = len(ns)
    print(f"{n} authenticated domains, {n*(n-1)//2} pairs, all distinct")
    for k in AUTH_DOMAINS:
        print(f"  {k:28s} ctl=0x{DOMAINS[k].ctl:02x} outer=0x{DOMAINS[k].outer:02x} "
              f"key={'base' if DOMAINS[k].base_key else 'session'} "
              f"epoch_in_nonce={DOMAINS[k].epoch_nonce} nonce={ns[k].hex()}")
    print()


def birthday() -> None:
    n = 1 << 16
    p = n * (n - 1) / (2 * (2 ** 64))
    import math
    print("---- request-id birthday bound (design §9) ----")
    print(f"n = 2^16 = {n};  p ~= n(n-1)/2^65 = {p!r} = 2^{math.log2(p):.4f}")
    print()


LIT_RE = re.compile(
    r"const\s+uint8_t\s+(kRef[A-Za-z0-9_]+)\s*\[\s*(\d+)\s*\]\s*=\s*\{(.*?)\};",
    re.S)


def compare(path: str) -> int:
    src = open(path, "r", encoding="utf-8").read()
    found = {}
    for m in LIT_RE.finditer(src):
        name, declared, blob = m.group(1), int(m.group(2)), m.group(3)
        vals = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", blob)]
        if len(vals) != declared:
            print(f"  MISMATCH {name}: declared [{declared}] but {len(vals)} initialisers")
            return 1
        found[name] = bytes(vals)
    bad = 0
    for name, want in VECTORS.items():
        if name not in found:
            print(f"  MISSING  {name} — not present in {path}")
            bad += 1
            continue
        if found[name] != want:
            print(f"  MISMATCH {name}\n     reference {want.hex()}\n     frozen    {found[name].hex()}")
            bad += 1
    extra = sorted(set(found) - set(VECTORS))
    for name in extra:
        print(f"  EXTRA    {name} — frozen literal with no reference vector")
        bad += 1
    print(f"---- comparison: {len(VECTORS)} reference vectors vs {len(found)} frozen literals in {path} ----")
    if bad:
        print(f"  FAILED: {bad} discrepancies")
        return 1
    print("  OK: every reference vector matches its frozen native literal byte-for-byte")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--compare", metavar="TU")
    ap.add_argument("--quiet-vectors", action="store_true")
    a = ap.parse_args()
    run_anchors()
    build()
    report_nonce_matrix()
    birthday()
    if not a.quiet_vectors:
        print("---- C++ literals ----")
        for name, data in VECTORS.items():
            print(cpp(name, data))
        print()
    if a.compare:
        return compare(a.compare)
    print(f"{len(VECTORS)} vectors emitted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

### 3.8 The reference's own derived figures

```
---- pairwise nonce inequality, same request_id/source, authenticated domains ----
10 authenticated domains, 45 pairs, all distinct
  cmd_auth_execute             ctl=0x03 outer=0xa0 key=session epoch_in_nonce=False nonce=8ef2a105652e8e91f945088e0670c238c9f28112593d9189
  cmd_bootstrap                ctl=0x2f outer=0xa0 key=base epoch_in_nonce=False nonce=f3b81fab83e225c0cfdda47f64217a3e36d27ee08f6874a0
  cmd_response_ack             ctl=0x33 outer=0xa0 key=session epoch_in_nonce=False nonce=6b3c952f6d1b779c0306a44567a2f6d6746302892b36de1a
  cmd_safe_rollover            ctl=0x43 outer=0xa0 key=session epoch_in_nonce=False nonce=bf780af02f904230885e10f25a81d913e79d68626e6bd311
  cmd_force_rollover           ctl=0x53 outer=0xa0 key=session epoch_in_nonce=False nonce=b497b47903bed1c2eb31c894ba6f24c90a6dea0e49daacf3
  resp_output_auth             ctl=0x03 outer=0xa1 key=session epoch_in_nonce=False nonce=cc8175a34361c80f0744009890b44dc455bb508ccc97fe75
  resp_terminal_auth           ctl=0x13 outer=0xa1 key=session epoch_in_nonce=False nonce=94f6c3d9bebaff786a5c96a23a923530e4efa25b63c35e51
  resp_bootstrap               ctl=0x23 outer=0xa1 key=base epoch_in_nonce=True nonce=9af77ceb80a62baaca897655e0e2336f5fae180e2732cc3d
  resp_rollover_result         ctl=0x33 outer=0xa1 key=base epoch_in_nonce=True nonce=d90d087a5434593bea8aee21ea765afd37b668396ab9fe1a
  resp_protocol_error_auth     ctl=0x43 outer=0xa1 key=session epoch_in_nonce=False nonce=501a6c5d69e2c85ce0859bd0ad0084374669de26c9730d6b

---- request-id birthday bound (design §9) ----
n = 2^16 = 65536;  p ~= n(n-1)/2^65 = 1.1641354547009541e-10 = 2^-33.0000

87 vectors emitted
```

---

## §4 — Literal wire, result and negative tables (what the native suite asserts)

Every row below is the byte length the independent reference produced and the native suite reproduces through the
REAL `remote_body_encode`, then re-reads through the REAL `remote_body_decode`.

| domain | ctl | header | AAD | complete body | plaintext |
|---|---|---|---|---|---|
| CMD AUTH_EXECUTE slot 3 | `0x03` | 9 | 14 | **31** | `"routes"` (6) |
| CMD OPEN_EXECUTE slot F | `0x1F` | 9 | — | **15** | `"status"` (6), CLEAR |
| CMD BOOTSTRAP slot F | `0x2F` | 41 | 46 | **57** | — (tag over empty) |
| CMD RESPONSE_ACK slot 3 | `0x33` | 9 | 14 | **25** | — |
| CMD SAFE_ROLLOVER slot 3 | `0x43` | 9 | 14 | **25** | — |
| CMD FORCE_ROLLOVER slot 3 | `0x53` | 9 | 14 | **25** | — |
| RESP OUTPUT slot 3 | `0x03` | 10 | 15 | **35** | `"line one\n"` (9) |
| RESP OUTPUT slot F | `0x0F` | 10 | — | **19** | same, CLEAR |
| RESP TERMINAL slot 3 | `0x13` | 10 | 15 | **27** | `00` |
| RESP TERMINAL slot F | `0x1F` | 10 | — | **11** | `00`, CLEAR |
| RESP BOOTSTRAP slot 3 | `0x23` | 17 | 22 | **33** | — |
| RESP ROLLOVER_RESULT slot 3 | `0x33` | 18 | 23 | **34** | — |
| RESP PROTOCOL_ERROR slot 3 | `0x43` | 10 | 15 | **27** | `00` |
| RESP PROTOCOL_ERROR slot F | `0x4F` | 10 | — | **11** | `00`, CLEAR, **no result domain** |

★ Two ctl bytes deliberately COLLIDE across directions — `0x03` is both CMD AUTH_EXECUTE slot 3 and RESP OUTPUT
slot 3, and `0x33` is both CMD RESPONSE_ACK slot 3 and RESP ROLLOVER_RESULT slot 3 — and their nonces differ,
because the direction byte is a nonce and AAD input.

**Pairwise nonce inequality:** the 10 authenticated domains at ONE request id, one source and one slot give
**45 distinct pairs**, asserted individually with the colliding name printed on failure. The two OPEN domains are
deliberately EXCLUDED (they have no nonce; `remote_nonce` refuses them with `bad_pairing`), so no unauthenticated
member props up the claim. Separately measured: all ten slots give ten distinct nonces; seq 0/1/255 differ;
two controllers on ONE session key with different `SOURCE_HASH` differ (`0xDEADBEEF` vs `0x00C0FFEE`).

**Negative cases, by verdict:**

| case | verdict |
|---|---|
| every (direction × ctl) pair, 512 combinations | exhaustive table: `ok` with the exact domain/overhead, or `bad_slot` / `bad_opcode` / `bad_pairing` |
| 9 foreign outer DATA types (0x00, 0x01, MOBILE_SEND, SEALED_RELAY, E2E_ACK, CUSTODY_FAILURE, 0x9F, 0xA2, 0xFF) | `bad_outer_type`, layout `invalid` |
| reserved slots A..E × all 16 opcodes × both directions (160) | `bad_slot` |
| every short prefix of six FIXED bodies (0 … len−1) | never `ok`, sentinel intact, no OOB |
| one trailing byte on a FIXED body | `bad_length` |
| one trailing byte on an OPEN VARIABLE body | `ok` — legitimate payload (7 bytes), still `authenticated == false` |
| 24-byte AUTH_EXECUTE (one short of the envelope) | `bad_length` |
| plaintext span smaller than N; output span smaller than the body | `bad_buffer` |
| fixed layout given a payload; bootstrap without / others with a controller key; 31-byte key | `bad_argument` / `bad_key` |
| every byte of 4 sealed bodies flipped in turn (31/57/35/34 = 157 mutants) | byte 0 → refusal or an explicitly UNAUTHENTICATED open decode; **every other byte → `auth_failed`**, sentinel intact |
| all 256 control bytes over one sealed body | exactly **1 authenticated ok** (the original) + **1 open ok** (`0x1F`, a separately valid OPEN envelope) + **254 refusals** |
| a different / absent source hash; a wrong session key | `auth_failed` / `bad_argument` / `auth_failed` |
| authenticated TERMINAL `08`, `FF` (independently sealed) | `bad_result_code` |
| authenticated PROTOCOL_ERROR `01`, `07`, `08`, `FF` (independently sealed) | `bad_result_code` — the code domain, not the tag |
| an OPEN TERMINAL with code `08` | `bad_result_code` |
| an empty TERMINAL body | `bad_length` (never a defaulted `completed`) |
| a real `admin_cmd_seal` legacy frame, under BOTH outer types | never `ok`; sentinel intact; the OLD decoder still opens it |
| a valid, independently sealed 233-byte AUTH_EXECUTE on a 232-cap carrier | `bad_body_cap` — refused, not truncated, not partly returned |
| entropy: absent provider / `false` / `false` after a 3-byte partial fill | `entropy_failed`, `out_id` preserved exactly, ONE call (no retry) |

**The B313 failure-output contract, measured rather than assumed.** `dm_open`'s comment claims the primitive wipes
the caller's plaintext on a bad tag; it does not — `crypto_aead_read` verifies first and writes `plain_text` only on
a match (`lib/monocypher/src/monocypher.c:2919-2924`). The codec's contract is therefore stated truthfully in the
header ("publishes no decoded result and writes nothing into `plaintext_out` on failure; it does NOT scrub caller
storage") and the test fills a fresh 64-byte buffer with `0xC7`, forces an authentication failure and asserts all
64 bytes survive. The existing `dm_crypto` comment is left alone — that correction is outside this slice.

### 4.1 Codec call-site wiring — what proves which claim

There is **NO runtime consumer**: no RX router, transmitter, console verb, ACL/session store or timer calls this
module, and none was faked to earn a "wired" label. What the suite does prove is that the SEAL/OPEN/ADMISSION path
itself reaches every tested authority:

| claim | public entry point exercised | in-module call site a mutation must reach | falsifier |
|---|---|---|---|
| the KDF inputs/order/labels/truncation | `remote_kdf_base` / `remote_kdf_session` | the two `crypto_blake2b` sites | R01-R06, R18 |
| the degenerate-point refusal | `remote_kdf_base`, `remote_ecdh_shared` | `all_zero32` guards | R07, R08 |
| the derived nonce reaches the SEAL | **`remote_body_encode`** (whose literals are the sealed bodies) | `remote_nonce(...)` call inside encode/decode | R09-R18 |
| the AAD reaches the SEAL | **`remote_body_encode`** | `remote_aad(...)` call inside encode/decode | R19-R21 |
| the key SELECTOR reaches the seal | **`remote_body_encode` / `remote_body_decode`** | the two `L.uses_base_key ? keys.base : keys.session` sites | R53, R54 |
| admission reaches encode AND decode | **`remote_body_encode` / `remote_body_decode`** | the two `remote_body_cap` calls | R37-R39 |
| the tag gates publication | **`remote_body_decode`** | `if (!okk) return auth_failed;` | R55, R56 |
| the typed result domain | **`remote_body_decode`** | the `carries_result_code` block | R58-R62 |
| the entropy refusal | `remote_make_request_id` | the `fn(...)` status test | R63-R66 |

Every literal in §4 is reproduced through `remote_body_encode`/`remote_body_decode` — never through
`remote_nonce`/`remote_aad`/`remote_body_cap` alone — which is why R09-R21 and R37-R39 turn those cases red.

### 4.2 Entropy scope and the derived bound

The provider is **synthetic**, and this is the codec boundary only: it proves the codec refuses loudly, and says
nothing about whether the device HAL can detect a hardware RNG fault (it cannot report one) or about what a console
does before an RF send. **B312 remains open** for the first integration slice's real, status-bearing provider.

The birthday bound is DERIVED, in the reference and again in the native suite:

```
n = 2^16 = 65536;  p = n(n-1) / (2 * 2^64) = 1.1641354547009541e-10 = 2^-33.0000
```

`2^-33 = 1.16415321826934814453125e-10`; the ratio is within 0.01 %, which is what the native case asserts. `2^16`
is an ANALYSIS envelope, not an enforced cap: the codec counts nothing and refuses no request for exceeding it.

---

## §5 — Gate 2 · native

```
$ pio test -e native
--------------------- native:* [PASSED] Took 5.93 seconds ---------------------
================== 0 test cases: 0 succeeded in 00:00:05.933 ==================     <- the wrapper's lie (D1)
$ ./.pio/build/native/program
[doctest] test cases:   2640 |   2640 passed | 0 failed | 0 skipped
[doctest] assertions: 115288 | 115288 passed | 0 failed |
[doctest] Status: SUCCESS!
```

**2615 / 111354 / 0  →  2640 / 115288 / 0** = **+25 cases, +3934 assertions, 0 failed.**

```
$ ./.pio/build/native/program -tc='§radmin-2/*'
[doctest] test cases:   25 |   25 passed | 0 failed | 2615 skipped
[doctest] assertions: 3934 | 3934 passed | 0 failed |
```

`2615 skipped` is exactly the base case count, so **no pre-existing case moved**. Per-case, each measured on its
own `-tc=` filter:

| filter | cases | assertions |
|---|---:|---:|
| `§radmin-2/kdf*` | 2 | 61 |
| `§radmin-2/layout*` | 2 | 1581 |
| `§radmin-2/wire*` | 2 | 396 |
| `§radmin-2/nonce*` | 2 | 158 |
| `§radmin-2/keys*` | 1 | 8 |
| `§radmin-2/length*` | 2 | 439 |
| `§radmin-2/corruption*` | 3 | 659 |
| `§radmin-2/result*` | 2 | 97 |
| `§radmin-2/slots*` | 1 | 238 |
| `§radmin-2/legacy*` | 1 | 13 |
| `§radmin-2/carrier*` | 5 | 260 |
| `§radmin-2/entropy*` | 2 | 24 |
| **total** | **25** | **3934** |

`61+1581+396+158+8+439+659+97+238+13+260+24 = 3934` ✓

**`PIN re-synced? YES — base `9ea4947` measured 2615 / 111354 / 0; +25 new §radmin-2 cases contributing +3934
assertions (each measured on its own `-tc=` filter, summing exactly to 3934); +0 moved or strengthened existing
cases (the filtered run reports 2615 skipped, i.e. the whole base); measured after = 2640 / 115288 / 0. PIN_CASES,
PIN_ASSERTS moved 2615, 111354 -> 2640, 115288 in `tools/probe_ui_model_mutations.py`.`**

Both mutation batteries independently DERIVED that same clean baseline in their own scratch trees
(`clean baseline 2640 / 115288 / 0`), and neither printed the stale-pin banner.

---

## §6 — Gate 4 · the paired simulator and the corpus

### 6.1 The one simulator edit

```
$ git -C /home/staszek/lora-universal-simulator diff --stat
 CMakeLists.txt | 1 +
```
One added line in `_meshroute_core_srcs`, beside `dm_crypto.cpp`, consumed at `:83` by BOTH variants. No
per-variant duplication, no wrapper/factory/flag and no simulator-behaviour edit.

### 6.2 The forced build — actions, objects, symbols, namespaces

```
$ cmake -S /home/staszek/lora-universal-simulator -B /home/staszek/mr-slice2-lus -DMESHROUTE_DIR=/home/staszek/mr-slice2
$ cmake --build /home/staszek/mr-slice2-lus -j8
[ 22%] Building CXX object CMakeFiles/meshroute_core_gw.dir/home/staszek/mr-slice2/lib/core/remote_codec.cpp.o
[ 23%] Building CXX object CMakeFiles/meshroute_core_normal.dir/home/staszek/mr-slice2/lib/core/remote_codec.cpp.o
[ 25%] Linking CXX static library libmeshroute_core_gw.a
[ 26%] Linking CXX static library libmeshroute_core_normal.a
[ 86%] Linking CXX executable lus
```

**Five real build actions**, not "nothing to do", and both source paths name the PAIRED worktree.

```
$ ar t libmeshroute_core_normal.a | wc -l      20  ->  21     (diff: + remote_codec.cpp.o)
$ ar t libmeshroute_core_gw.a     | wc -l      20  ->  21     (diff: + remote_codec.cpp.o)
```

Defined text symbols in each new member, demangled — **12 in each, and the namespaces are ODR-distinct**:

```
meshroute::remote_aad · remote_application_cap · remote_body_cap · remote_body_decode · remote_body_encode ·
remote_ctl · remote_ecdh_shared · remote_kdf_base · remote_kdf_session · remote_layout ·
remote_make_request_id · remote_nonce                       (12/12 prefixed `meshroute::`)
meshroute_gw::…  the same twelve                            (12/12 prefixed `meshroute_gw::`)
```

### 6.3 The executable, and why nothing could move

```
$ md5sum /home/staszek/mr-slice2-lus/orchestrator/lus
b1b1d92c541cc7f6f63864a2bcc6a355     BEFORE
b1b1d92c541cc7f6f63864a2bcc6a355     AFTER    <- BYTE-IDENTICAL
$ nm -C lus | grep -c 'remote_body_cap|remote_kdf_|remote_nonce|remote_body_encode|remote_body_decode|…'
0
```

The new archive members are unreferenced, so the linker pulled neither in: the executable is byte-identical and
carries **zero** codec symbols. There is no metadata-only difference to attribute — there is no difference at all.
⛔ No runtime call was forced to retain the object; the object proof is the archive member and its symbols.

### 6.4 The corpus

```
$ python3 tools/run_corpus.py --jobs=8 --require-anchors --out <final> --lus /home/staszek/mr-slice2-lus/orchestrator/lus
PASS: 36/36 streams produced and validated, 0 failures
  anchors: 36/36 rows reproduce simulation/BASELINE.md
  s18_meshroute  32afbf11  events=269517   (read from BASELINE.md's `### 36/36 corpus` block, never hardcoded)
$ python3 tools/run_corpus.py --compare <base> <final>
PASS: byte-identical corpora — both runs VALIDATED, 36/36 streams agree on md5, sha256, size, event count and assertion count
```

lus sha256 BEFORE 38228060d410244a… AFTER 38228060d410244a… equal=True

| scenario | md5 before | md5 after | events | anchor | assertion failures | moved |
|---|---|---|---|---|---|---|
| `s06_seattle_lifecycle` | `e8f862b0` | `e8f862b0` | 69039 -> 69039 | reproduced | 0/0 | no |
| `s07_seattle_mobile_meshroute` | `16cc0dd1` | `16cc0dd1` | 111681 -> 111681 | reproduced | 0/0 | no |
| `s09_two_layer_gateway` | `71120178` | `71120178` | 2266 -> 2266 | reproduced | 0/0 | no |
| `s09_two_layer_gateway_metal` | `0182f858` | `0182f858` | 2343 -> 2343 | reproduced | 0/0 | no |
| `s10_two_layer_separation` | `c44c0b39` | `c44c0b39` | 2266 -> 2266 | reproduced | 0/0 | no |
| `s15_three_layer` | `f95e2d60` | `f95e2d60` | 51794 -> 51794 | reproduced | 0/0 | no |
| `s15_three_layer_metal` | `3611a93b` | `3611a93b` | 52237 -> 52237 | reproduced | 0/0 | no |
| `s16_dense_gateway` | `5b30637c` | `5b30637c` | 23898 -> 23898 | reproduced | 0/0 | no |
| `s17_metro` | `aa960050` | `aa960050` | 1181178 -> 1181178 | reproduced | 0/0 | no |
| `s18_meshroute` | `32afbf11` | `32afbf11` | 269517 -> 269517 | reproduced | 0/0 | no |
| `s19_singlelayer_multihop_chain` | `c669b1ef` | `c669b1ef` | 1065 -> 1065 | reproduced | 0/0 | no |
| `s20_random_mesh` | `db240065` | `db240065` | 40566 -> 40566 | reproduced | 0/0 | no |
| `s21_leaf_config_divergence` | `d7db6a04` | `d7db6a04` | 390 -> 390 | reproduced | 0/0 | no |
| `s21_mobile_dm_milestone_meshroute` | `fc466e77` | `fc466e77` | 678 -> 678 | reproduced | 0/0 | no |
| `s22_leaf_config_join` | `baadfbed` | `baadfbed` | 215 -> 215 | reproduced | 0/0 | no |
| `s22_mobile_team_meshroute` | `c406fb6a` | `c406fb6a` | 1824 -> 1824 | reproduced | 0/0 | no |
| `s23_leaf_config_epoch_write` | `0cd16bd5` | `0cd16bd5` | 219 -> 219 | reproduced | 0/0 | no |
| `s23_mobile_team_multihop_meshroute` | `568c684f` | `568c684f` | 924 -> 924 | reproduced | 0/0 | no |
| `s24_static_and_team_multihop_meshroute` | `d06536f4` | `d06536f4` | 1576 -> 1576 | reproduced | 0/0 | no |
| `s25_two_team_separation_meshroute` | `f87360c7` | `f87360c7` | 786 -> 786 | reproduced | 0/0 | no |
| `s26_team_reroute_meshroute` | `73a68a35` | `73a68a35` | 1037 -> 1037 | reproduced | 0/0 | no |
| `s27_cross_layer_mobiles_meshroute` | `662c6158` | `662c6158` | 9433 -> 9433 | reproduced | 0/0 | no |
| `s28_mixed_team_channels_meshroute` | `525756e2` | `525756e2` | 3861 -> 3861 | reproduced | 0/0 | no |
| `s29_mixed_leaf_team_meshroute` | `bb534a88` | `bb534a88` | 2025 -> 2025 | reproduced | 0/0 | no |
| `s30_team_dad_mediation_meshroute` | `4a1de37d` | `4a1de37d` | 1034 -> 1034 | reproduced | 0/0 | no |
| `s31_dual_carrier_gateway` | `4eafb125` | `4eafb125` | 2300 -> 2300 | reproduced | 0/0 | no |
| `s32_dual_cr_gateway` | `9574f5dd` | `9574f5dd` | 2266 -> 2266 | reproduced | 0/0 | no |
| `s33_mixed_cr_channel_overhear` | `814ef421` | `814ef421` | 2845 -> 2845 | reproduced | 0/0 | no |
| `s34_team_switch_clears_plane` | `0c724c05` | `0c724c05` | 919 -> 919 | reproduced | 0/0 | no |
| `s35a_cochannel_isolation_meshroute` | `bda1713b` | `bda1713b` | 2356 -> 2356 | reproduced | 0/0 | no |
| `s35b_cochannel_isolation_control_meshroute` | `7dbc19ae` | `7dbc19ae` | 1063 -> 1063 | reproduced | 0/0 | no |
| `s36_reprovision_purges_carriers` | `76d02e58` | `76d02e58` | 472 -> 472 | reproduced | 0/0 | no |
| `s37_team_homed_origin_meshroute` | `db535d42` | `db535d42` | 748 -> 748 | reproduced | 0/0 | no |
| `s38_team_origin_learn_meshroute` | `52be507e` | `52be507e` | 522 -> 522 | reproduced | 0/0 | no |
| `sim_9node_base` | `e7a1c3d6` | `e7a1c3d6` | 4945 -> 4945 | reproduced | 0/0 | no |
| `twin_9node_dm` | `dd28f145` | `dd28f145` | 14552 -> 14552 | reproduced | 0/0 | no |

moved rows: 0 of 36 · anchors reproduced: 36/36 · total assertion failures: 0

**0 rows moved, 36/36 anchors reproduced, 0 assertion failures, no re-anchor, `simulation/BASELINE.md` NOT edited.**
The zero-remote-event claim is structural rather than a census of this run: the module has no producer or consumer
anywhere in the build graph, and the executable that produced these streams is byte-identical to the base one.

---

## §7 — Gate 5 · boards and ABI

`measure_board.py pair --jobs=2` (the ruled pair only — gateway + heltec_mobile), base and final at **identical
fixed-identity paths** (`.pio-measure/env/<env>/build/<env>`), `SOURCE_DATE_EPOCH=946684800`,
`MESHROUTE_GIT_REV_OVERRIDE=b206b206b206`, so B254/B262's debug/path dependence cannot enter the comparison.

| env | RAM base → final | live flash base → final | objects | symbol_count | symbols_sha256 | loadable sections | ELF sha256 | payload sha256 |
|---|---|---|---|---|---|---|---|---|
| `gateway` (ARM) | 195844 → **195844 (Δ0)** | 512092 → **512092 (Δ0)** | 283 → **284 (+1)** | 6160 → 6160 | identical | identical | **identical** | **identical** |
| `heltec_mobile` (Xtensa) | 205684 → **205684 (Δ0)** | 1355292 → **1355292 (Δ0)** | 327 → **328 (+1)** | 13084 → 13084 | identical | identical | **identical** | **identical** |

The `+1` object is the compilation proof and nothing more:

```
$ find .pio-measure/env/gateway/build/gateway       -name remote_codec.cpp.o
.pio-measure/env/gateway/build/gateway/lib1ed/core/remote_codec.cpp.o        (284 objects total)
$ find .pio-measure/env/heltec_mobile/build/heltec_mobile -name remote_codec.cpp.o
.pio-measure/env/heltec_mobile/build/heltec_mobile/lib1ed/core/remote_codec.cpp.o   (328 objects total)
$ nm -C .pio-measure/capture-final/<env>/firmware.elf | grep -c '<any remote_* codec symbol>'
0        on BOTH boards
```

★ The ELF sha256 is UNCHANGED on both boards, so there is no metadata-only movement to attribute separately: the
new TU compiles, contributes no reachable symbol, and the linked image is the same file. No flash tolerance was
invented; nothing needed one.

**ABI, base vs final:** the full 20-row struct table diffs **EMPTY**. `sizeof(Node)/alignof` stays
native **222072/8**, heltec_mobile **117912/8**, gateway **148680/8**. No size assertion was repinned.

```
$ python3 tools/probe_board_abi.py      PASS: board ABI (191 checks, 9/9 controls RED, 0 unusable)      [base and final]
$ python3 tools/probe_b278_row_abi.py   PASS: B278 production correlation-row ABI mirror (42 measurements, 6/6 controls RED)
```

---

## §8 — Admission vs physical packing, measured separately (three verdicts)

**(1) ADMISSION** — the real `remote_body_encode` / `remote_body_decode` with the caller's buffer deliberately the
full 241-byte storage span, so a refusal is never `bad_buffer` masquerading as a cap:

| carrier | cap | body AT cap | body at cap+1 |
|---|---:|---|---|
| same-layer by hash | 232 | `ok`, `out_len == 232` | **`bad_body_cap`** |
| same-layer hash-less | 232 | `ok` | **`bad_body_cap`** |
| typed same-layer wrapper | 231 | `ok` | **`bad_body_cap`** |
| cross-layer depth 1 / depth 4 | 229 / 226 | `ok` | **`bad_body_cap`** |
| typed cross-layer wrapper depth 3 | 226 | `ok` | **`bad_body_cap`** |
| DECODE, a valid independently sealed 233-byte body on the 232-cap carrier | 232 | its 232-byte sibling decodes (207-byte plaintext) | **`bad_body_cap`**, nothing published |

Application limits subtract each domain's own overhead: `remote_application_cap` returns `cap − 25` for
AUTH_EXECUTE, `cap − 9` for OPEN_EXECUTE, and **0** for every fixed layout — and the fixed bootstrap/control bodies
(57 / 25 / 25 / 25) were encoded through the TIGHTEST live carrier (the depth-3 wrapper, cap 226) rather than
assumed small.

**(2) PHYSICAL PACKING** — the 0e method, `pack_unicast_inner` into the real 241-byte `TxItem.inner[]` and
`pack_data` into the real 255-byte frame, with the refusing authority NAMED:

* at the admitted cap, **all 13 live shapes physically fit** (`Bound::none`);
* ★ **THE COUNTEREXAMPLE, EXECUTED.** On a same-layer leg with NO `DST_HASH`, raw **233 CAN pack**
  (`Bound::none`, inner = 238 = origin 1 + source 4 + 233), and the raw ceiling for that shape is 236
  (237 → `Bound::storage`) — while ADMISSION refuses 233 on the very same carrier and admits 232. That is R-RA-28's
  reservation showing up as a real difference between two authorities, not a packer refusal, and it is measured
  rather than argued (B308);
* on fully hash-populated rows cap+1 DOES physically refuse, and it is **STORAGE**: same-layer 233 → `storage`,
  cross-layer depth 4 227 → `storage`, wrapper depth 3 227 → `storage`;
* ★ an **AIR-governed** case so the two bounds can never be conflated: outer `CRYPTED` on a hash-present leg gives
  `data_inner_cap` 238 < storage 241, so the cap is **229**; 229 packs, **230 → `Bound::air`** while its inner
  (239 ≤ 241) packed fine — storage did not bind. ⛔ A packing/arithmetic control only: no live v2 send path sets
  outer CRYPTED, and RPC AEAD and the outer DATA CRYPTED flag are different layers;
* the STRUCTURAL refusal: outer CRYPTED with no DST_HASH → `pack_data` returns 0 (`Bound::structural`), and the
  descriptor is `bad_carrier` rather than a lower cap.

**(3) SHAPE / AUTHORITY SENSITIVITY** — one term varied at a time: 232 (same layer) → 231 (+ enclosed type) →
229/228 (+ 2 + depth) → 228 (both); the DST_HASH term is never reclaimed at any depth; both RPC directions cost the
same; invalid full depth 0/5, cursor ≥ depth, wrapper destination depth 0 and 4 all refuse, while the RAW full
inner path at depth 4 remains perfectly legal at 226 — different questions, different answers. The derivation's own
terms are read from their named authorities in the test (`max_payload_bytes_hard_cap == 241`,
`origin+source+dst == 9`, `data_inner_cap(...) == 242` plaintext and `== 238` CRYPTED, `gw_env_max_hops == 4`).

**STOP 3 does not fire**: no legal carrier's allowance contradicts R-RA-28, no invalid wrapper depth is admitted,
and admission and physical packing are proved separately without altering any existing packer.

---

## §9 — Gate 3 · the mutation union (both selectors, in full)

### 9.1 The two selectors, derived

* **Changed-source selector → `radmin2codec` (NEW).** The slice's only production files are new
  (`lib/core/remote_codec.{h,cpp}`), so no existing per-file battery covers them. **No existing production file was
  edited at all.** The new header's dependency reach is accounted for: it declares types/constants only and carries
  no executable decision, so it gets no second per-file target (and no entry attacks it). The comment-only 0e hunk
  is likewise not a semantic source change — §11 proves token identity.
* **Historical/dependency selector → `b20codec` (FULL).** `remote_body_cap` derives its governing bound from
  `frame_codec.h`'s `data_inner_cap` / `data_frame_len`, so that unchanged file is a genuine acceptance dependency
  and its whole battery was run. No further dependency qualifies: no RX, custody, routing or timer battery is
  included merely for sharing an include or an arc name. **Stated rather than papered over:** the existing DM-crypto
  and identity KATs run inside the same native binary, but `TARGET_SRC` has **no** mutation battery for
  `dm_crypto.cpp` or `identity.cpp` — I did not invent one and do not claim their coverage.

**Baseline union = new `radmin2codec` (66) + full `b20codec` (5) = 71 entries.** Both were run to completion,
sequentially, at `--workers=2`.

### 9.2 Resource staging (B286), measured

1b measured ~5.1 GB per worker against 5.8 GB free because the shared checkout's `.pio-measure/` was 3.9 GB. Here:

```
free before:            5.3 GB
delivery tree inputs:    31 MB   (excluding .git / .pio / .pio-measure)
this worktree's .pio-measure: 309 MB      .pio: 279 MB
```

The harness rsyncs `ROOT` excluding only `.git` and `.pio`, so `.pio-measure` (a measurement OUTPUT, not a build,
test or tool input) would have been copied into every worker. The sanctioned workaround was used: an isolated
staging copy at `/home/staszek/mr-slice2-stage` excluding exactly `.git`, `.pio`, `.pio-measure`, from which the
harness was run. Nothing in the owner's tree was mutated, cleaned or built by the batteries.

```
BEFORE: delivery-tree inputs 866 entries vs staging 865 — the ONE difference is the worktree's `.git` FILE
        (a `gitdir:` pointer, the deliberate exclusion); all 865 real inputs byte-identical (md5 per file).
AFTER : delivery 865 / staging 865, only-in-delivery [], only-in-staging [], differing []  -> SOURCE RESTORATION VERIFIED
```

The full per-file manifest is `stage_manifest_before.txt` (866 lines) in the run's scratch. The harness's own
restoration proof agrees: `real tree untouched: all 42 target files byte-identical to launch (md5); no build ran in
/home/staszek/mr-slice2-stage`. Free space never dropped below 4.7 GB; **no worker was lost**.

### 9.3 Classification, declared before the controls ran

A native mutation must **compile, execute and fail the intended named assertion**. Compiler/linker failure, crash,
timeout, a missing worker, unreadable output, a wrong match count or a green mutant is **UNUSABLE / gate failure,
never RED**. This slice adds no inverted compile-error control class; the `static_assert`s in `remote_codec.cpp`
remain build invariants and no mutant is scored on a compiler rejecting one (each entry was pre-checked to keep
every `static_assert` satisfiable — e.g. the label controls preserve length exactly).

### 9.4 `radmin2codec` — 66/66 RED, 0 unusable

```
$ cd /home/staszek/mr-slice2-stage && python3 tools/probe_ui_model_mutations.py --target=radmin2codec --workers=2
  [w0] ok clean baseline 2640 / 115288 / 0   (DERIVED from this tree)
  [w1] ok clean baseline 2640 / 115288 / 0   (DERIVED from this tree)
  …
  worker 0: 33/33 entries, 33 RED / 0 worthless, wall 123.4s, rc 0, source restored: md5 281acaaa… (MATCHES)
  worker 1: 33/33 entries, 33 RED / 0 worthless, wall 123.2s, rc 0, source restored: md5 281acaaa… (MATCHES)
  real tree untouched: all 42 target files byte-identical to launch (md5)
  mutations: 66 RED / 0 unusable
```

| id | the tempting wrong fix | assertions failed | match count | verdict |
|---|---|---:|---:|---|
| **R01** | ★★★ the base KDF binds the two full public keys in the WRONG ROLE ORDER, so a controller and its target derive the same key from either end — the DM sorted-hash habit imported into a protocol whose two endpoints are NOT interchangeable | 11 | 1 | RED |
| **R02** | ★★ the base KDF label loses its version tag (same length, so every static_assert still holds) — remote-admin v1 and v2 would derive the same key from the same inputs | 11 | 1 | RED |
| **R03** | ★★ the base KDF drops the SHARED POINT and derives a key from two public keys alone | 12 | 1 | RED |
| **R04** | ★★★ the session KDF drops the epoch, so a rollover does not change the session key — the whole point of the epoch | 10 | 1 | RED |
| **R05** | ★★ the session KDF writes the epoch BIG-endian — the wrong-endian defect at a key boundary, invisible to any round-trip | 9 | 1 | RED |
| **R06** | ★★ the base key is truncated from the WRONG END of the 64-byte digest — still 32 bytes, still self-consistent, and not the spec's [:32] | 11 | 1 | RED |
| **R07** | ★★★ the ALL-ZERO shared point is accepted and turned into a key — exactly what the existing void `ecdh_shared` would hand over unchecked | 33 | 1 | RED |
| **R08** | ★★★ the ECDH boundary stops rejecting a degenerate result, so every low-order peer point yields a key both sides agree on and an attacker knows | 3 | 1 | RED |
| **R09** | ★★★ the nonce drops the DIRECTION byte, so a request and a response with the same opcode nibble and request id collide under one key | 8 | 1 | RED |
| **R10** | ★★★ the nonce drops the CONTROL BYTE, so every opcode and every ACL slot share one nonce domain at a given request id | 7 | 1 | RED |
| **R11** | ★★ the nonce drops the RESPONSE SEQUENCE, so two frames of one transcript reuse a nonce under the same session key | 8 | 1 | RED |
| **R12** | ★★★ the nonce drops the stable controller SOURCE_HASH — two controllers deliberately sharing one credential lose their pre-encryption nonce separation (design §9) | 7 | 1 | RED |
| **R13** | ★★★ EVERY domain gets the epoch in its nonce — including the bootstrap REQUEST, whose controller does not yet know one (§8.1) | 7 | 1 | RED |
| **R14** | ★★ NO domain gets the epoch in its nonce, so a replayed bootstrap after a rollover reuses the old response nonce | 21 | 1 | RED |
| **R15** | ★★ the nonce is truncated from the wrong end of the digest | 7 | 1 | RED |
| **R16** | ★★★ an OPEN domain is handed a nonce, so an unauthenticated envelope could join an authenticated inequality claim as a fake zero-nonce member | 4 | 1 | RED |
| **R17** | ★★ the ABSENT-SOURCE fallback comes back: a message with no captured controller source is derived against whatever number the field happens to hold | 1 | 1 | RED |
| **R18** | ★★ one byte of the nonce LABEL changes (same length, so every static_assert still holds) — the derivation drifts out of its frozen domain without any structural sign | 7 | 1 | RED |
| **R19** | ★★★ the AAD drops the DIRECTION byte, so a sealed request body would authenticate as a response body | 8 | 1 | RED |
| **R20** | ★★★ the AAD drops the controller SOURCE_HASH, so the return identity is no longer bound and a relay could rewrite it without breaking the tag | 7 | 1 | RED |
| **R21** | ★★ the AAD length stops covering the source hash — the bytes are written and then not authenticated, which is the shape that looks right in a hex dump | 8 | 1 | RED |
| **R22** | ★★★ the request id is written BIG-endian in the clear header — wrong on the wire and wrong in the AAD, and a round-trip never notices | 9 | 1 | RED |
| **R23** | ★★ the clear header puts the request id BEFORE the control byte | 9 | 1 | RED |
| **R24** | ★★ the rollover result writes its abandoned count BEFORE the epoch — the field order of §8.6 reversed | 13 | 1 | RED |
| **R25** | ★★ a bootstrap request may be built without its 32-byte controller key, or any other layout with one — the header/AAD field set stops being checked | 2 | 1 | RED |
| **R26** | ★★★ the outer DATA type is not validated, so a FOREIGN typed frame's first body byte is read as a remote `ctl` | 18 | 1 | RED |
| **R27** | ★★★ the RESERVED slot nibbles A..E stop being refused as a class | 335 | 1 | RED |
| **R28** | ★★ a RESERVED REMOTE_CMD opcode (0x6..0xF) is admitted instead of refused | 110 | 1 | RED |
| **R29** | ★★ a RESERVED REMOTE_RESP opcode (0x5..0xF) is admitted instead of refused | 121 | 1 | RED |
| **R30** | ★★★ an AUTHENTICATED EXECUTE is accepted on the sentinel slot — open becomes a fake ACL slot, which §8.1 forbids by name | 2 | 1 | RED |
| **R31** | ★★★ a BOOTSTRAP REQUEST is accepted on an established slot, i.e. on a row the controller cannot yet know | 20 | 1 | RED |
| **R32** | ★★ a BOOTSTRAP RESPONSE is accepted on the sentinel, so it stops carrying the actual matched slot | 2 | 1 | RED |
| **R33** | ★★★ the OPEN protocol error acquires the AUTHENTICATED `already_acknowledged` result namespace — an unauthenticated envelope gaining an authenticated meaning | 1 | 1 | RED |
| **R34** | ★★★ a FIXED layout accepts a trailing byte (the exact-length rule becomes a minimum) | 6 | 1 | RED |
| **R35** | ★★ a VARIABLE layout stops checking that its fixed fields are even present | 1 | 1 | RED |
| **R36** | ★★ a FIXED layout is allowed to carry an application payload | 1 | 1 | RED |
| **R37** | ★★★ ENCODE admission is skipped: an oversize body is built and handed on rather than refused | 7 | 1 | RED |
| **R38** | ★★ ENCODE admission is off by one: a body EXACTLY at the carrier's cap is refused | 14 | 1 | RED |
| **R39** | ★★★ DECODE admission is skipped, so an oversize body's size check hides behind a tag that happens to verify | 2 | 1 | RED |
| **R40** | ★★★ [[R-RA-28]] REVERSED: the four DST_HASH bytes are reclaimed on a leg that does not transmit them — the optional-for-size decision the ruling exists to forbid | 11 | 1 | RED |
| **R41** | ★★ the mandatory SOURCE_HASH reservation is dropped from the capacity derivation | 45 | 1 | RED |
| **R42** | ★★ the origin byte is forgotten in the capacity derivation | 45 | 1 | RED |
| **R43** | ★★★ the AIR-fit bound is ignored and STORAGE always governs — [[B20]]'s conflation at a new authority (the outer-CRYPTED shape then reads 232 instead of 229) | 2 | 1 | RED |
| **R44** | ★★★ the STORAGE bound is ignored and AIR always governs, so a plaintext carrier is told it may fill 233 inner bytes the 241-byte TxItem buffer cannot hold alongside its fields | 44 | 1 | RED |
| **R45** | ★★ the typed wrapper's enclosed-TYPE byte is free | 15 | 1 | RED |
| **R46** | ★★ the cross-layer path forgets its two count bytes and charges only the layer ids | 26 | 1 | RED |
| **R47** | ★★★ [[B309]]: the typed wrapper is given the FULL path's depth limit, so destination depth 4 is admitted as a 225-byte carrier the home could never re-originate | 4 | 1 | RED |
| **R48** | ★★ the path cursor stops being validated against the depth (`pack_unicast_inner` refuses what this would admit) | 2 | 1 | RED |
| **R49** | ★★ [[R-RA-13]] reversed: a carrier without SOURCE_HASH is admitted | 1 | 1 | RED |
| **R50** | ★★ `pack_data`'s structural rule is dropped: outer CRYPTED with no DST_HASH is reported as a carrier with a lower cap instead of no carrier at all | 1 | 1 | RED |
| **R51** | ★★ a same-layer descriptor is allowed to carry a path block that its flags would never emit | 1 | 1 | RED |
| **R52** | ★★ `addr_len` beyond the mobile last mile is admitted (`pack_data` refuses it) | 1 | 1 | RED |
| **R53** | ★★★ the ENCODE key selector collapses to the session key, so a bootstrap is sealed under a key the target cannot have yet | 6 | 1 | RED |
| **R54** | ★★★ the DECODE key selector collapses to the session key | 5 | 1 | RED |
| **R55** | ★★★ the AEAD verdict is IGNORED: a body with a broken tag publishes its plaintext | 321 | 1 | RED |
| **R56** | ★★★ a FAILED authenticated open FALLS BACK to the open decoder — the exact behaviour §8.1 forbids by name | 321 | 1 | RED |
| **R57** | ★★ an OPEN body is published as AUTHENTICATED — a success that isn't | 10 | 1 | RED |
| **R58** | ★★★ the two result namespaces COLLAPSE: the authenticated protocol error accepts the whole terminal namespace, so 0x01..0x07 stop rejecting | 4 | 1 | RED |
| **R59** | ★★★ the typed DOMAIN is lost: a protocol-error body decodes as a TERMINAL result, so 0x00 reads as `completed` instead of `already_acknowledged` | 2 | 1 | RED |
| **R60** | ★★ an UNALLOCATED terminal code (0x08..0xFF) is decoded as a known meaning | 6 | 1 | RED |
| **R61** | ★★ a TERMINAL body with NO result code defaults to `completed` instead of refusing | 2 | 1 | RED |
| **R62** | ★★ the bounded terminal DETAIL bytes are dropped rather than preserved exactly | 1 | 1 | RED |
| **R63** | ★★★ [[B312]] VERBATIM: the entropy provider's status is IGNORED, so a failed or partial draw becomes a usable request id | 4 | 1 | RED |
| **R64** | ★★★ an ABSENT provider silently yields the reserved id zero instead of refusing | 2 | 1 | RED |
| **R65** | ★★ the request id is composed BIG-endian from the drawn bytes | 3 | 1 | RED |
| **R66** | ★★★ the request id is silently NARROWED to 32 bits — R-RA-5's rejected width, and the birthday bound moves from 2^-33 to about 0.5 | 4 | 1 | RED |

66 entries, all RED, every match count exactly 1.

### 9.5 `b20codec` — 5/5 RED, 0 unusable (the full existing battery, nothing selected away)

```
  ok clean baseline 2640 / 115288 / 0   (DERIVED per worker tree; 2 tree(s) agree)
  ok C01 [[B20]]'s ROOT CAUSE at the authority (CRYPTED inner cap reads 243)          -> RED (132 failed, match 1)
  ok C02 the TYPE byte is forgotten in the inner cap                                   -> RED ( 76 failed, match 1)
  ok C03 the cap becomes the WHOLE FRAME BUDGET                                        -> RED (150 failed, match 1)
  ok C04 data_frame_len forgets the TYPE byte                                          -> RED ( 16 failed, match 1)
  ok C05 data_frame_len's trailer collapses to the 4-B MAC                             -> RED ( 16 failed, match 1)
  worker 0: 3/3 entries, 3 RED / 0 worthless, source restored: md5 70aff75d… (MATCHES)
  worker 1: 2/2 entries, 2 RED / 0 worthless, source restored: md5 70aff75d… (MATCHES)
  mutations: 5 RED / 0 unusable
```

**Union total: 71 RED / 0 unusable.** Every existing control was preserved; no entry was deleted or weakened, and
no production edit was made to rescue a control. **STOP 7 does not fire.**

---

## §10 — Gates 6, 7 and 8 · probes, tools, inventory, warnings

### 10.1 The six probes (default invocations, base → final)

| probe | result | pins |
|---|---|---|
| `tools/probe_console_sink/run.sh` | PASS: probe + structural + controls all green | structural 29/29 |
| `tools/probe_inbox_verbs/run.sh` | PASS | probe 91 checks (pin 91), controls 22 verified / 0 unusable (pin 22) |
| `tools/probe_firmware_ui/run.sh` | PASS | checks per arm l2 404 · v3 839; controls 223 verified / 0 unusable |
| `tools/probe_custody_usb/run.sh` | PASS | probe 27 checks (pin 27), controls 10 verified / 0 unusable (pin 10) |
| `tools/probe_ble_line/run.sh` | PASS | probe 40 checks (pin 40), controls 8 verified / 0 unusable (pin 8) |
| `tools/probe_features/run.sh` | PASS | matrix 9 cells (pin 9), **114 checks (pin 114), 0 failed**, controls 38 verified / 0 unusable (pin 38), ownership controls 19 verified / 0 unusable |
| `tools/probe_features/run.sh --no-neg` | **PROBE-ONLY — NOT A GATE** (as required, not a PASS) | matrix 9 cells, 114 checks, 0 failed |

**The full base-vs-final diff of the combined probe transcript is THREE lines**, all the same fact:

```
1028c1028
<   ok   O13 source integrity: 181 scanned files, sha256(tree) 9bec22a2caea9cfe… before and after
>   ok   O13 source integrity: 182 scanned files, sha256(tree) e7209be41be2576f… before and after
1209c1209   (the Y5 control's copy of the same hash)
1279c1279   (the --no-neg run's copy of the same line)
```

The 1b **ownership contract is untouched**: O1..O12 all pass with the same text, `O10 no file under test/ names the
capability pair in code` still holds (the new test TU names no `MR_FEAT_RADMIN_*`), and the approved three-file
site census is unchanged. Only the SCANNED-FILE COUNT moved, exactly as predicted — see §12 for the honest
accounting of why the base capture says 181 rather than 179.

### 10.2 Tools, inventory, warnings, matrices

```
$ python3 -m unittest discover -s tools -p 'test_*.py'
Ran 312 tests in 284.642s
OK
$ python3 tools/gen_command_inventory.py
gen_command_inventory: PASS: …/2026-09-04-radmin-command-inventory.md matches fresh generation byte-for-byte (177 command rows)
$ python3 tools/gen_command_inventory.py --check
gen_command_inventory: PASS: … matches fresh generation byte-for-byte (177 command rows)
```

No `--write`, no regeneration: `git status --porcelain` shows the inventory document unmodified. No firmware command
anchor moved.

```
$ tools/warning_census.sh
env                   objs      warn    expect  -Wswitch        RAM      Flash  verdict
gateway_heltec         328       173       173         0     230956    1306096  ok
gateway_heltec_v4      329       178       178         0     231228    1304100  ok
heltec_mobile          328       177       177         0     205684    1355292  ok
heltec_v3              328       177       177         0     206164    1360380  ok
heltec_v4              329       182       182         0     206436    1358440  ok
heltec_v4_mobile       329       182       182         0     205956    1353356  ok
PASS — 6 OLED env(s) match their pinned warning baseline
```

**Zero new warnings** at the census's own pinned environment set (173/178/177/177/182/182 — the values BASELINE.md
records), **zero `-Wswitch`**, and nothing was re-pinned. The object count is +1 per env, which is the new TU
compiling on every one of them; `heltec_mobile`'s RAM/flash reproduce the deterministic capture exactly.

```
$ python3 tools/check_a0_matrix.py
PASS — 21 enum members each have a matrix row, the CURRENT NAMESPACE table states every current value, and all 6 named special rows are present.
$ python3 tools/check_data_type_literals.py
PASS — 182 active source file(s) scanned; no numeric DataType comparison, argument literal or switch-case label survives.
```

The literal check scans the new files too and finds no numeric `DataType` comparison in them: the codec names
`DATA_TYPE_REMOTE_CMD` / `DATA_TYPE_REMOTE_RESP` / `DATA_TYPE_MOBILE_SEND`, never `0xA0` / `0xA1` / `0x02`.
(The two `CHECK(DATA_TYPE_REMOTE_CMD == 0xA0)` assertions in the test are the allocation pins the checker permits
in that form — it passed with them present.)

`sizeof(Node)` compiles and asserts on every native build (each mutation worker's clean baseline rebuilds it), and
`-Wreorder` is clean: no `node.h` field was touched.

⚠ **One grep hit, declared so it surprises nobody.** `lib/core/remote_codec.h` mentions the string
`MR_FEAT_RADMIN_*` **inside a comment**, in the sentence that explains why the codec is a shared module rather than
another capability consumer. `tools/probe_features/ownership.py` normalizes by REMOVING COMMENTS before it censuses
sites, so this contributes nothing to the contract — proven by the probe PASSing with the file present, with the
approved three-file census (`mr_features.h`, `node.h`, `node_mac_rx.cpp`) unchanged. There is **no preprocessor
reference** to either capability macro in any of the three new files:
`grep -c MR_FEAT_RADMIN` → `remote_codec.h:1` (that comment), `remote_codec.cpp:0`, `test_remote_codec.cpp:0`.

**The native suite was re-run on the FINAL delivered tree**, after the comment-only 0e edit and the harness/PIN
edit, and is green at the same figures: `pio test -e native` PASSED, `./.pio/build/native/program` →
`2640 | 2640 passed | 0 failed`, `115288 | 115288 passed | 0 failed`. The two mutation batteries independently
derived that same baseline from a staging copy taken after those edits.

```
$ git -C /home/staszek/mr-slice2 diff --check                       (clean)
$ git -C /home/staszek/lora-universal-simulator diff --check        (clean)
```

---

## §11 — The 0e correction is COMMENT-ONLY, proved twice

Three comment blocks were added to `test/test_radmin_characterization_0e.cpp`, each keeping the old claim visible:

1. the historical no-hash allowances are marked as **raw-packer measurements superseded for ADMISSION by R-RA-28**,
   with the live caps named and B308/B309 cited, and the production authority pointed at `remote_body_cap`;
2. the hosted-mobile last-mile ROW is marked as the **hash-present fixture**, distinct from the current producer,
   which passes `override_dst_hash = 0` (`node_hashlocate.cpp:1820`, R-RA-25's narrow addendum, B310 parked);
3. the claim that the RPC type byte is a placeholder "without pretending a codepoint has been allocated" is
   **WITHDRAWN in place** — `0xA0`/`0xA1` are allocated at `frame_codec.h:802-803` and live on the legacy path.
   The constant's NAME and every measurement are deliberately left unchanged.

**Proof 1 — every changed line is a comment.**
```
$ git diff -U0 test/test_radmin_characterization_0e.cpp | grep '^[+-]' | grep -v '^\(+++\|---\)' \
    | sed 's/^[+-]//' | sed 's/^[[:space:]]*//' | grep -v '^//' | wc -l
0
```

**Proof 2 — token identity in isolation.** With comments stripped (string- and char-literal aware) and all
whitespace removed, the before and after files are IDENTICAL:
```
before tokens: 17596   after tokens: 17596   TOKEN-IDENTICAL: True
```
No executable token, fixture row, expected value or 0e measurement changed. `git diff --stat` shows
`28 ++++` insertions and **zero deletions of executable text**.

---

## §12 — STOP audit, and the ONE ordering deviation

| STOP | verdict |
|---|---|
| **1** base missing/different, a measured repo starting dirty, the paired simulator pointing at the wrong tree, unexplained concurrent edits | **NO.** Both measured trees clean at their exact pins; every paired compile line names `/home/staszek/mr-slice2`; the shared checkout's single Author edit is identified and irrelevant to every measured input (a Markdown brief no build reads), and it was never touched. |
| **2** a new opcode/result allocation, owner policy, wire-version/re-anchor, carrier/hash/routing change, consumer, or Node/HAL/NV/timer/state change | **NO.** The frozen allocations are the ones the design already carries; `wire_version` untouched; no consumer, no Node/HAL/NV/timer edit; no `src/` file touched. |
| **3** an allowance contradicting R-RA-28, an invalid wrapper depth admitted, admission and packing not separable | **NO.** §8. |
| **4** reference unavailable/disagreeing, only round-trips passing, the real paths bypassing the tested authority, the typed domain lost | **NO.** §3 and §4.1. |
| **5** RNG failure hidden by a void-to-success adapter, a fallback ID, an unreported partial draw, or a claim that synthetic entropy proves device behaviour | **NO.** §2.2 and §4.2; B312 explicitly stays open and the header says so at the declaration. |
| **6** any corpus row/anchor/semantic change, a variant not compiling the TU, RAM/ABI movement, unattributed board/executable movement | **NO.** §6 and §7 — and the `lus` executable and both board ELFs are byte-identical, so there is nothing to attribute. |
| **7** a green/vacuous/multi-matched/unusable/misclassified control, a lost worker, an unreproducible comparison, a failed restoration, an underived pin move | **NO.** §5 and §9: 71/71 RED, every match count 1, both restorations md5-verified, the PIN derived from measured per-case additions. |
| **8** a warning, a failed standing gate, an unaccounted modified/untracked file, an inventory regeneration need, an unexplained input change | **NO.** §10 and §14. |

### 12.1 Deviation, reported rather than smoothed over

**The PROBE-family base capture was taken slightly late.** The base probe sweep finished at 11:24:53 UTC, by which
time `lib/core/remote_codec.{h,cpp}` already existed (in an earlier revision — before the `remote_body_encode`
rename), so `probe_features`' O13 line reads **181 scanned files** instead of the 179 a pristine base has. Every
other base capture (native, corpus, both ABI probes, the deterministic board pair) was taken before the first
production edit, and their values equal 1b's published pins.

It is closed by reconstruction rather than left as a loose end. Running the same census against three trees:

| tree | scanned files | tree sha256 |
|---|---:|---|
| HEAD `9ea4947`, no slice files (reconstructed) | **179** | `824fb5608f2f97d9…` |
| the base probe capture (HEAD + the two production files, pre-rename) | **181** | `9bec22a2caea9cfe…` |
| the final delivery tree | **182** | `e7209be41be2576f…` |

`git` confirms the set difference exactly: `live − HEAD = {lib/core/remote_codec.cpp, lib/core/remote_codec.h,
test/test_remote_codec.cpp}` and `HEAD − live = {}`. So the whole scanned-file delta is the three new files, the
ownership CONTRACT and every check/control pin are unchanged, and the probe's conclusion is unaffected. The
shared checkout gives 179 / `824fb560…` as well, confirming the pristine figure independently.

---

## §13 — Findings, as proposed register rows (M1)

None of these was written into the register by me; they are proposed with their measurement.

> **[[B314]] (proposed) — two functions named `remote_encode` would have shared one codebase.**
> `src/firmware_remote.cpp:24` defines a file-static `remote_encode(const char* v, uint8_t* tlv, size_t cap)` on
> the LEGACY remote path (deleted in Slice 9). Slice 2's codec originally exposed a public `remote_encode` in
> `MESHROUTE_NS`. No link-time collision was possible (the legacy one is `static` and its TU includes no
> `remote_codec.h`), but a reader — or a future TU that includes both — would have had two unrelated functions of
> one name. **Measurement:** `grep -rn '\bremote_encode\b' lib src` before the rename returned the legacy
> definition at `:24`, its call at `:152`, and two comments naming it. **Action taken in-slice:** the new entry
> points are `remote_body_encode` / `remote_body_decode`, which also match the name `src/device_ble.h:168` already
> anticipates (`remote_body_cap`). **Residual:** none for Slice 2; Slice 9 should delete the legacy static rather
> than rename it. Severity: low (naming/readability).

> **[[B315]] (proposed) — `measure_board.py --output` silently requires a path under the repo's `.pio-measure/`.**
> `--output` is documented as "new/empty directory", but `measurement_environment()` derives
> `MESHROUTE_MEASURE_COMPILER_STATE` from it and the build then aborts with
> `ValueError: MESHROUTE_MEASURE_COMPILER_STATE must be below the repository .pio-measure/` — after PlatformIO has
> already started, so the failure surfaces as `build gateway failed with exit 1` with the real cause 14 lines into
> a nested traceback. **Measurement:** `python3 tools/measure_board.py pair --jobs=2 --output <scratchpad>/x.json`
> → `1 failed, 0 succeeded in 00:00:19.383`; the same command with `--output .pio-measure/capture-base` → PASS in
> 31.2 s. **Proposed fix:** validate `--output` against that constraint in the argument parser and say so in the
> `--help` text. Severity: low (instrument ergonomics); no measurement was taken from the failed attempt.

> **[[B316]] (proposed) — the mutation harness copies `.pio-measure/` into every worker tree.**
> `probe_ui_model_mutations.py:9851` rsyncs `ROOT` with `--exclude=.git --exclude=.pio` only. `.pio-measure/` is a
> measurement OUTPUT directory, not a build/test/tool input, and it reached 3.9 GB in the shared checkout — which
> is the concrete shape of the 1b headroom incident. **Measurement:** in this worktree `.pio-measure` was 309 MB
> against 31 MB of real inputs, i.e. the copy would have been 10× the tree, per worker. **Workaround used here:**
> the sanctioned isolated staging copy (§9.2), byte-verified before and after. **Proposed fix:** add
> `--exclude=.pio-measure` to that one rsync. Severity: low, but it removes the standing need for the workaround.
> Related to the existing B286 debt; not repaired in this slice (fence).

Existing debts left as they are: **B286** and **B311** are separate instrument work (B311's requirement was met
here by giving every byte comparison a printable hex diagnostic and every status comparison a named-status
diagnostic); **B310** stays parked; **B313**'s correction to the existing `dm_crypto` comment stays outside this
slice — the new codec simply does not rely on that comment and says so at its own declaration.

**Metal residue: NONE (M2).** No runtime consumer and no hardware path changed, so
`docs/2026-07-31-bench-test-script.md` gains nothing. This does not close any older bench debt and does not revive
the suspended static/gateway legacy `rcmd` round trip.

---

## §14 — Complete delivered state (both repositories)

### MeshRoute — `/home/staszek/mr-slice2` at `9ea4947`, UNCOMMITTED

```
$ git status --porcelain
 M test/test_radmin_characterization_0e.cpp        <- COMMENT-ONLY (§11)
 M tools/probe_ui_model_mutations.py               <- new target + battery + dispatch entry + measured PIN
?? lib/core/remote_codec.cpp                       <- NEW production
?? lib/core/remote_codec.h                         <- NEW production
?? test/test_remote_codec.cpp                      <- NEW native TU
$ git diff --stat
 test/test_radmin_characterization_0e.cpp |  28 ++++
 tools/probe_ui_model_mutations.py        | 265 ++++++++++++++++++++++++++++++-
 2 files changed, 292 insertions(+), 1 deletion(-)
```

Plus this evidence file, `docs/superpowers/evidence/2026-09-06-radmin-slice2.md` (also untracked).

Gitignored build artefacts present in the worktree, listed for completeness and **not deliverables**:
`.pio/`, `.pio-measure/`, `firmware.map`, `tools/__pycache__/`, `tools/lab/__pycache__/`,
`tools/probe_console_sink/__pycache__/`.

⛔ Not touched: any `src/` file, `lib/core/node.{h,cpp}`, the HAL or any entropy provider, `dm_crypto`, `identity`,
`frame_codec`, `protocol_constants`, `admin_auth`, `mr_features.h`, `platformio.ini`, variants, the command
inventory, NV, `wire_version`, timers/capacities, `simulation/` scenarios or `BASELINE.md`, the companion contract,
the QA ledger, the rulings/register/bench/design documents, `MEMORY.md` or `tracker.md`.

### Simulator — `/home/staszek/lora-universal-simulator` at `fd3295d`, UNCOMMITTED

```
$ git status --porcelain
 M CMakeLists.txt
$ git diff
+        ${MESHROUTE_DIR}/lib/core/remote_codec.cpp  # remote-admin v2 Slice 2: the remote RPC codec …
 1 file changed, 1 insertion(+)
```

### The shared checkout — untouched

```
$ git -C /home/staszek/MeshRoute status --porcelain
 M docs/superpowers/plans/2026-09-06-radmin-slice2-remote-codec.md      <- the Author's, exactly as at dispatch
```

**No `git commit`, `stash`, `checkout --` or `reset` was run in either repository. The owner commits both.**

---

## §15 — Author handoff

After QA PASS, the Author lands: design §19/§19.1 Slice 2 **software-complete** with the exact frozen codec
allocations (the §8.1 nibbles, the §8.11 overheads, the §8.9 terminal numbering and the separate authenticated
protocol-error namespace — all now implemented and independently pinned); **B308/B309 closures from the actual
admission/packer/wrapper evidence in §8** (B308: the admission-vs-raw-packing difference is measured, with the
233-byte counterexample executed; B309: the wrapper's destination depth 4 is refused as an invalid carrier, not
admitted as 225); the **codec half of B312** with real entropy integration still open; and MEMORY/tracker plus the
three proposed rows in §13. Record both owner commit hashes when they exist; I have not invented either.
