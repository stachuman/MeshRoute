<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 1b — capability-owned pre-tail receive · coder evidence · 2026-09-06

Brief (authority): `docs/superpowers/plans/2026-09-06-radmin-slice1b-capability-owned-receive.md`
(Quality-Agent PASS 2026-09-06). Rulings: R-RA-8 / R-RA-17 / R-RA-19 / R-RA-26 / **R-RA-27** in
`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`. Pre-check ledger:
`docs/superpowers/plans/2026-09-05-radmin-slice1b-precheck.md` (its §2/§4.1 legacy-widening recommendation is
SUPERSEDED by R-RA-27 and kept only as history). Pass-2 seam notes: `2026-09-05-fable-review-pass2.md` §S3.

**Result: all gates green. 36/36 corpus streams byte-identical (predicted before the first production edit).
RAM delta ZERO on both ruled boards. `sizeof(Node)` unmoved on all three ABIs. No STOP fired. Uncommitted (D4).**

---

## §0 — Base check and the pre-existing dirty state

```
$ git rev-parse HEAD
0da4d56d3c29d06dc8e16f592003f4f83c744824          # == the brief's pinned base ("0b prep")   ✓
$ git status --porcelain                          # at the START of this slice
 M docs/superpowers/plans/2026-09-06-radmin-slice1b-capability-owned-receive.md
```

The **sole** pre-existing edit is the Author's status/base-pin edit of the brief itself — recorded, never touched,
staged or reverted by me. Nothing else was dirty. **STOP 1 does not fire.**

**Concurrent QA document (recorded, not a STOP).** Part-way through the run an untracked Markdown file appeared:
`docs/superpowers/plans/2026-09-06-radmin-slice2-precheck.md`. Per the brief this is a concurrent Quality-Agent
document (Markdown, read by no build); it is recorded in the final inventory (§13) and is not mine.

---

## §1 — The BEFORE census and the PREDICTION (both recorded before the first production edit)

### 1.1 base native, real binary (the wrapper lies; the binary is the measurement — D1)

```
$ pio test -e native
--------------------- native:* [PASSED] Took 1.51 seconds ---------------------
================== 0 test cases: 0 succeeded in 00:00:01.508 ==================     <- the wrapper's lie
$ ./.pio/build/native/program
[doctest] test cases:   2610 |   2610 passed | 0 failed | 0 skipped
[doctest] assertions: 110269 | 110269 passed | 0 failed |
```

Base = **2610 / 110269 / 0**. `tools/probe_ui_model_mutations.py` carried `PIN_CASES, PIN_ASSERTS = 2610, 110269`
— the pin was IN SYNC at the base.

### 1.2 base simulator and base corpus

```
$ md5sum /home/staszek/lora-universal-simulator/build/orchestrator/lus
0b018a5e445b6d294848950f4603a5af
$ python3 tools/run_corpus.py --jobs=8 --require-anchors --out <scratch>/before --lus <lus>
PASS: 36/36 streams produced and validated, 0 failures
  anchors: 36/36 rows reproduce simulation/BASELINE.md
  s18_meshroute  32afbf11  events=269517  (0 assertion failures)
```

### 1.3 the throwaway REMOTE_CMD/RESP census, in an ISOLATED source copy

Construction (no byte of the repository was ever instrumented):

* `lib/core` + `lib/console` + `lib/monocypher` copied to `<scratch>/census_tree`. The copy hashed **identically**
  to the main tree before instrumenting — `sha256(sorted per-file sha256) = 901383216daeb9a4…` on both.
* A second simulator was configured against that copy ALONE:
  `cmake -S /home/staszek/lora-universal-simulator -B <scratch>/lus_census -DMESHROUTE_DIR=<census_tree>`.
* **Four throwaway `MR_EMIT` probes** were added to the copy: `census1b_data_rx` (every parsed DATA whose type is
  `REMOTE_CMD`/`REMOTE_RESP`, in `handle_data` — i.e. arrival, transit included), `census1b_postack` (the same
  types entering `do_post_ack`), `census1b_arm` (INSIDE the legacy staging arm, recording `full`/`parsed`), and
  `census1b_guard` (the fail-closed internal guard).
* All 36 streams were re-run with that binary: `PASS: 36/36 … 0 failures` — and, notably, **`anchors: 36/36 rows
  reproduce BASELINE.md`**, which is itself the first hint of the answer: the probes never fired.

```
$ strings <lus_census> | grep census1b        # the probes ARE in the binary that produced the streams
census1b_data_rx   census1b_postack   census1b_arm   census1b_guard
$ grep -h -o '"census1b_..."' <census_out>/streams/*.ndjson | sort | uniq -c
(nothing)
```

| census probe | occurrences over all 36 streams |
| --- | --- |
| `census1b_data_rx` — a `REMOTE_CMD`/`REMOTE_RESP` DATA parsed anywhere | **0** |
| `census1b_postack` — either type reaching `do_post_ack` | **0** |
| `census1b_arm` — either type reaching the legacy staging arm | **0** |
| `census1b_guard` — anything reaching the fail-closed internal guard | **0** |

**⛔ A ZERO FROM AN INSTRUMENT NOBODY PROVED CAN FIRE IS NOT A MEASUREMENT.** The liveness control: the census copy
was re-instrumented with `census1b_live`, which fires for **every** parsed DATA at the *same statement position*
as `census1b_data_rx` — the only difference is the type predicate — and the 36 streams re-run.

```
census1b_live events over all 36 streams: 4340
DATA type histogram (from that probe):
  0:3069  128:988  137:90  3:39  1:36  129:30  144:26  145:20  2:16  147:13  146:12  139:1   (sum 4340 ✓)
frames with type 160 (0xA0 REMOTE_CMD) or 161 (0xA1 REMOTE_RESP): 0
```

Corroborating pre-existing counters in the same 36 streams: `delivered` **753**, `data_rx` **4235**,
`unsupported_internal` **0**, `remote_inbound_drop_full` **0**.

⇒ **The corpus contains ZERO remote-admin traffic.** 4340 DATA frames pass the very line the census probe sits on;
not one carries `0xA0`/`0xA1`. This is a measured reachability result, not missing evidence.

### 1.4 the instrumentation removed, the copy proven byte-identical again

```
$ diff -rq lib/core <census_tree>/lib/core        # while instrumented: node.h + node_mac_rx.cpp differ
$ # restored from `git archive HEAD lib/core`, re-hashed:
  901383216daeb9a43fb5df21c996fb49d2a2406f8128ce5dcfbce8e051db7feb   <- census copy, restored
  901383216daeb9a43fb5df21c996fb49d2a2406f8128ce5dcfbce8e051db7feb   <- reference taken before instrumenting
```

### ★ PREDICTION (written here BEFORE the first production edit and before any AFTER run)

> **All 36 corpus streams will be BYTE-IDENTICAL, and the s18 keystone will remain `32afbf11` / 269517 / 0.**
>
> Two independent reasons, either of which suffices: (a) the corpus contains **zero** `REMOTE_CMD`/`REMOTE_RESP`
> frames (§1.3), so the arm this slice changes is never reached; and (b) both simulator variants are HOST builds
> (`{CLIENT 1, ACCEPT 1}`, R-RA-17/R-RA-26 — measured by the feature matrix, cell `host_lus_normal` and
> `host_lus_gateway`), so **both owners exist** there and the owned staging bodies are byte-identical to today's.
> The `lus` BINARY hash will move (a `lib/core` dependency changed); stream identity may not.

**AFTER (§6): 36/36 byte-identical, 0 movers. The prediction held.**

---

## §2 — What changed in production

### 2.1 the shape (R-RA-27 items 1-3)

`lib/core/node.h` (declarations only — no member data, no layout, no `RemoteInbound` or API rewrite):

* `enum class Node::RadminRxOwner : uint8_t { none, command_accept, response_client }` and
  `static RadminRxOwner radmin_rx_owner(uint8_t type, bool client_on, bool accept_on);` — **public**, so the native
  tests call the *production* decision, **and deliberately UNGATED** (only its consumers are capability-gated);
* `#if MR_FEAT_RADMIN_ACCEPT  void rx_remote_cmd_accept (const PostAck&, const data_unicast_inner*); #endif`
* `#if MR_FEAT_RADMIN_CLIENT  void rx_remote_resp_client(const PostAck&, const data_unicast_inner*); #endif`
* `#if MR_FEAT_RADMIN_ACCEPT || MR_FEAT_RADMIN_CLIENT  void remote_inbound_stage(…, bool is_response); #endif`
  — the shared staging body, guarded by "at least one owner exists". ⛔ Not an owner gate, and stated as such in
  the source.

`lib/core/node_mac_rx.cpp`:

```cpp
Node::RadminRxOwner Node::radmin_rx_owner(uint8_t type, bool client_on, bool accept_on)
{
    if (type == DATA_TYPE_REMOTE_CMD)  return accept_on ? RadminRxOwner::command_accept  : RadminRxOwner::none;
    if (type == DATA_TYPE_REMOTE_RESP) return client_on ? RadminRxOwner::response_client : RadminRxOwner::none;
    return RadminRxOwner::none;   // every other type belongs to another handler — this decision never steals one
}
```

and, at the exact position the legacy arm occupied (before SEALED_RELAY/CRYPTED, after the H-answer arms):

```cpp
        const RadminRxOwner radmin_owner = radmin_rx_owner(pa.type, MR_FEAT_RADMIN_CLIENT, MR_FEAT_RADMIN_ACCEPT);
#if MR_FEAT_RADMIN_ACCEPT
        if (radmin_owner == RadminRxOwner::command_accept) {
            rx_remote_cmd_accept(pa, ui ? &*ui : nullptr);
            become_free();
            return;
        }
#endif
#if MR_FEAT_RADMIN_CLIENT
        if (radmin_owner == RadminRxOwner::response_client) { … become_free(); return; }
#endif
        // ⛔ NO `none` ARM: it consumes nothing and reaches the EXISTING fail-closed internal guard.
        (void)radmin_owner;   // [[B169]] hygiene only (a config compiling NEITHER arm is an mr_features.h #error)
```

### 2.2 the owned staging body — byte comparison against the legacy arm

The legacy arm's statements were moved VERBATIM into `Node::remote_inbound_stage`. The ONE textual change is that
`is_response` arrives as a parameter instead of being re-derived from `pa.type`; the owner is selected by that same
type, so the stored value is identical.

| legacy arm (`node_mac_rx.cpp:2223-2235` at the base) | `remote_inbound_stage` now | identical? |
| --- | --- | --- |
| `if (_remote_inbound.active) { MR_EMIT("remote_inbound_drop_full", EF_I("from", pa.origin)); }` | same, then `return;` (early-out instead of `else`) | **yes** (same emit, same field, same name) |
| `const uint8_t* src = ui ? ui->body.data() : ((pa.inner_len > 1) ? pa.inner + 1 : nullptr);` | identical | **yes** |
| `uint8_t n = ui ? …ui->body.size() : ((pa.inner_len > 1) ? …pa.inner_len - 1 : 0);` | identical | **yes** |
| `if (n > protocol::inbox_max_body) n = protocol::inbox_max_body;` | identical | **yes** |
| `_remote_inbound.active = true;` | identical | **yes** |
| `_remote_inbound.is_response = (pa.type == DATA_TYPE_REMOTE_RESP);` | `= is_response` (`false` from the accept owner, `true` from the client owner) | **equivalent by construction** |
| `_remote_inbound.from = pa.origin;` | identical | **yes** |
| `_remote_inbound.len = n;` | identical | **yes** |
| `for (uint8_t i = 0; i < n; ++i) _remote_inbound.body[i] = src ? src[i] : 0;` | identical | **yes** |
| `become_free(); return;` | kept at the CALL SITE (the caller owns it — `custody_failure_receive`'s convention) | **yes** |

⛔ No v2 body is authenticated or opened; the clamp is not tightened; the slot is not partitioned; the arm did not
move relative to SEALED_RELAY / CRYPTED / the forwarding roles / the fail-closed guard.

### 2.3 the deferred pass-2 obligations, MARKED IN SOURCE (not in this document)

`node_mac_rx.cpp`, in the block immediately ahead of `do_post_ack`, records four items as **MISSING/deferred by
design**, each naming the slice that owes it: mandatory `ui->has_source_hash` (R-RA-13, Slices 5/7b) · 32-bit source
identity instead of the 8-bit `pa.origin` (5/7b) · **refuse, don't clamp** (C2; the clamp is a documented no-op
today) · role-owned storage, i.e. `_remote_inbound` still costs a client-only mobile its ~246 B until R-RA-22's
partitioned admission lands in Slice 5 (R-RA-27 explicitly keeps the slot unconditional here).

### 2.4 the two intentional behaviour changes (R-RA-27), stated in source at the seam

* a **mobile** (`{CLIENT 1, ACCEPT 0}`) now IGNORES an incoming `REMOTE_CMD`: unowned ⇒ the fail-closed guard ⇒ one
  scalar `unsupported_internal`, no staging into the inert `remote_exec` stub. Externally identical (neither shape
  ever answered).
* a **static/gateway** build (`{0,1}`) no longer receives `REMOTE_RESP`: the legacy USB `rcmd` issuer on those nodes
  stops seeing replies until Slice 9 deletes it. ⛔ Not restored by `|| MR_FEAT_REMOTE_MGMT`, a fallback owner or any
  firmware edit — the design's "bodies remain behaviour-identical" sentence is SUPERSEDED for exactly this case.

### 2.5 comment-only edits, proven comment-only

| file | claim withdrawn (old text kept visible) | proof |
| --- | --- | --- |
| `lib/core/mr_features.h:53` | *"SCAFFOLD ONLY (slice 1): … No consumer exists yet."* | comment-stripped token streams identical **before == after**, 1638 tokens each (`git show HEAD:…` vs the file) |
| `tools/probe_features/probe_main.cpp` | *"Slice 1 adds … and NO consumer"* | comment-stripped token streams identical before == after |
| `lib/core/node_mac_rx.cpp:2417` **([[B307]])** | *"…on all ten board envs…"* → "on EVERY board env", with the withdrawn number and the derived **thirteen** (4 mobile + 4 gateway + 5 static) beside it | the edit is inside the guard's rationale comment; the guard, its predicate, its telemetry fields and the matrix are untouched (mutation controls B04-B08 all still RED, §5) |

---

## §3 — Wiring proof 1+2: native production routing vs the synthetic decision matrix

⛔ **THE DISTINCTION IS LOAD-BEARING AND IS STATED IN THE TEST FILE ITSELF.** The native binary is compiled
`{CLIENT 1, ACCEPT 1}` (a host is not a product configuration, R-RA-17), so **both** owners exist in it. A `none`
result from the synthetic matrix is a routing verdict, **NOT an executed remote RX drop**.

### 3.1 production routing — real RTS → DATA → post-ACK, five new cases in `test/test_node_r3.cpp`

| case | what it drives and asserts |
| --- | --- |
| `§radmin-1b/2` | a real `REMOTE_CMD` flight: staged with `is_response=false`, `from == pa.origin`, `len`, exact body `"status"`; `delivered == 0`, **no inbox record**, **no `msg_recv` push**, `remote_inbound_drop_full == 0`, **`unsupported_internal == 0`** (owned here ⇒ it never reaches the guard); the drain clears the slot |
| `§radmin-1b/3` | a real `REMOTE_RESP` flight: `is_response=true` (the one field that differs), `from`, `len`, `memcmp` of the whole body; same three "not delivered" surfaces; `unsupported_internal == 0` |
| `§radmin-1b/4` | staging BOUNDARIES: an EMPTY body (`len == 0`, slot still active, drain clears) and a **200-byte** body staged byte-for-byte (`memcmp`), i.e. NOT truncated — which is what keeps the 241 clamp a documented no-op |
| `§radmin-1b/5` | ONE slot, BOTH owners: a `REMOTE_RESP` arriving while a `REMOTE_CMD` is pending ⇒ exactly one `remote_inbound_drop_full`, the command survives intact, and the dropped frame was not delivered instead |
| `§radmin-1b/1` | the SYNTHETIC decision matrix (below) |

"Not delivered" is asserted on **all three** consumer surfaces (telemetry `delivered`, the durable inbox via
`inbox().pull`, the live `PushKind::msg_recv` ring), because a leaked remote frame would show up on exactly one.

### 3.2 the synthetic pure-decision matrix (`§radmin-1b/1`) — the production function, four capability combinations

| `{client, accept}` | CMD | RESP | reachability |
| --- | --- | --- | --- |
| `{0,0}` | `none` | `none` | synthetic only; a board deriving it is an `mr_features.h` `#error` |
| `{0,1}` | `command_accept` | **`none`** | static/gateway product — the ruled R-RA-27 loss |
| `{1,0}` | **`none`** | `response_client` | mobile product — R-RA-27 item 3 |
| `{1,1}` | `command_accept` | `response_client` | native + both lus variants |

Plus: an **exhaustive 256-value sweep under all four combinations** (1024 assertions) proving the decision never
steals another handler's type; ten NAMED non-remote controls (`0`, `E2E_ACK`, `CUSTODY_FAILURE`, `H_ANSWER`,
`AUTHORITATIVE_H_ANSWER`, `MOBILE_KEY_FORWARD`, `TEAM_KEY_GRANT`, `SEALED_RELAY`, `INTRO`, `0xFF`); and two
non-interchangeability assertions (accept never owns RESP, client never owns CMD).

### 3.3 the SEPARATE executed proof of the fail-closed drop (not this slice's, deliberately)

The scalar-only `unsupported_internal` drop — after every forwarding role, no staging/inbox/push — is proven by the
PRE-EXISTING generic guard fixtures this slice does not touch and which the gate executes:
`test/test_custody_internal_b.cpp` (§CUSTODY-B/1 the bound, the STATIC receiver's fail-closed drop at `:230`, the
relay passing no verdict at `:249`, the home not eating its mobile's frame at `:271`, the ruled field set at
`:411-412`), `test/test_data_type_audit_a0.cpp:561/565`, `test/test_dual_layer.cpp:8799-8863`. All green, unchanged.

### 3.4 the derived native figures and the PIN

```
$ pio test -e native   &&   ./.pio/build/native/program
[doctest] test cases:   2615 |   2615 passed | 0 failed | 0 skipped
[doctest] assertions: 111354 | 111354 passed | 0 failed |
$ ./.pio/build/native/program -tc='§radmin-1b/*'
[doctest] test cases:    5 |    5 passed | 0 failed | 2610 skipped
[doctest] assertions: 1085 | 1085 passed | 0 failed |
```

per-case, each on its own filter: `/1` **1044** · `/2` **11** · `/3` **10** · `/4` **10** · `/5` **10**
→ 1044+11+10+10+10 = **1085** over **5** cases. 2610 + 5 = **2615**; 110269 + 1085 = **111354**. ✓
No existing case moved or was strengthened (+0/+0).

**PIN re-synced? YES — `PIN_CASES, PIN_ASSERTS` 2610, 110269 → 2615, 111354; derivation = base 2610/110269/0
measured on the clean tree, plus five NEW `§radmin-1b` cases measured on their own `-tc=` filters
(1044 + 11 + 10 + 10 + 10 = 1085), plus zero moved/strengthened cases, = 2615/111354/0 measured after.**
The mutation harness's own clean-tree baseline independently derived `2615 / 111354 / 0` in every worker, so the
pin and the measurement agree without a stale-pin banner.

---

## §4 — Wiring proof 3: the product ownership is COMPILED, per ruled board

Flags come from each env's OWN `pio run -t idedata` (the flag authority `tools/probe_board_abi.py` uses), and the
env's own pinned toolchain preprocesses the real receiver TU.

| | `gateway` (ARM, nRF52840) | `heltec_mobile` (Xtensa, ESP32-S3) |
| --- | --- | --- |
| compiler | `…/toolchain-gccarmnoneeabi/bin/arm-none-eabi-g++` | `…/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-g++` |
| env defines | `ARDUINO=10804`, `MR_PROFILE_GATEWAY` | `ARDUINO=10812`, `MR_PROFILE_MOBILE`, `MR_FEAT_OLED=1` |
| resolved `MR_FEAT_RADMIN_CLIENT` | **0** | **1** |
| resolved `MR_FEAT_RADMIN_ACCEPT` | **1** | **0** |
| `Node::rx_remote_cmd_accept` definition | **PRESENT** | **ABSENT** |
| `Node::rx_remote_resp_client` definition | **ABSENT** | **PRESENT** |
| `void rx_remote_cmd_accept` class-body declaration | **PRESENT** | **ABSENT** |
| `void rx_remote_resp_client` class-body declaration | **ABSENT** | **PRESENT** |
| the consuming call `rx_remote_cmd_accept(pa, ui …)` | **PRESENT** | **ABSENT** |
| the consuming call `rx_remote_resp_client(pa, ui …)` | **ABSENT** | **PRESENT** |
| `Node::radmin_rx_owner` definition | PRESENT (capability-agnostic) | PRESENT |
| the call `radmin_rx_owner(pa.type, …)` | PRESENT ×1 | PRESENT ×1 |
| `Node::remote_inbound_stage` | PRESENT | PRESENT |

(`arm-none-eabi-g++ -E -dD` / `xtensa-esp32s3-elf-g++ -E -dD` on `lib/core/node_mac_rx.cpp` with the env's own
resolved flag set; the `#define MR_FEAT_RADMIN_*` lines above are read back out of the preprocessed output, not
assumed.) ⇒ **the opposite owner is compiled out, declaration, definition and call site, on each ruled product.**

**Object-level corroboration (the strongest form, before any linker decision):**

```
$ arm-none-eabi-nm -C <gateway build>/lib72c/core/node_mac_rx.cpp.o | grep -Ei 'radmin|remote_inbound_stage|rx_remote'
T meshroute::Node::radmin_rx_owner(unsigned char, bool, bool)
T meshroute::Node::remote_inbound_stage(meshroute::PostAck const&, meshroute::data_unicast_inner const*, bool)
T meshroute::Node::rx_remote_cmd_accept(meshroute::PostAck const&, meshroute::data_unicast_inner const*)
                                                    # ⛔ rx_remote_resp_client is NOT in the gateway object at all
```

**Symbols in the LINKED images** (from the deterministic runner's own normalized inventories):

* `heltec_mobile`: `Node::rx_remote_resp_client` **19 B** and `Node::remote_inbound_stage` **92 B** are present;
  `rx_remote_cmd_accept` is absent.
* `gateway`: **none** of the three appears in the ELF — they were inlined into `do_post_ack` and their out-of-line
  copies discarded, because the nRF52 build uses `-ffunction-sections` (derived from `idedata`) and the adafruit
  nRF52 platform links with `-Wl,--gc-sections` (`~/.platformio/platforms/nordicnrf52/builder/frameworks/…`).
  ⛔ **A MISSING SYMBOL IS THEREFORE NOT THE PROOF HERE** — the preprocessing table and the object-level `nm` above
  are, and they are what the compile-out claim rests on.

⛔ No runtime capability load exists anywhere: the two macros are read only at the single call site, textually, and
the ownership contract (§5.2 O7a) asserts their ORDER.

---

## §5 — Instruments

### 5.1 the feature-matrix gate, extended with the first-consumer ownership contract

```
$ tools/probe_features/run.sh
tree unchanged: the real sources' md5 is identical before and after (fb5d04b8e3c6a10640a5d237ad00c04e)
matrix: 9 configuration cells (pin 9), 114 checks against the REAL mr_features.h, 0 failed (pin 114)
controls: 38 verified / 0 unusable (pin 38)
PASS
$ tools/probe_features/run.sh --no-neg
matrix: 9 configuration cells (pin 9), 114 checks …, 0 failed (pin 114)
PROBE-ONLY — NOT A GATE (controls skipped; run without --no-neg to gate)      # ⛔ never a bare PASS
```

Preserved intact: the nine-cell matrix, the six old feature projections, `envmap.py`'s 13 environment checks, the
three board-only diagnostics, the up-front classification declaration, and **all 19 prior controls** (A1-A4, B1-B6,
C1-C5, X1-X4).

**Pin derivations (both moved, both derived, both reconciled by an executed wrapper test):**

* `PIN_CHECKS` 97 → **114**: S1..S2 (2, was S1..S3 = 3 — S3 is *retired by replacement*) + E1..E13 (13) +
  9 cells × 9 (81) + the ownership contract's **18** emitted check lines = 2 + 13 + 81 + 18 = 114.
* `PIN_CONTROLS` 19 → **38**: 4 + 6 + 5 + 4 (the 19 prior) + 13 W-controls + 6 Y-controls = 38.
* `tools/test_probe_features.py::test_the_two_pins_reconcile_with_what_the_contract_actually_emits` RUNS the
  contract and asserts `PIN_CHECKS − emitted == 96` and `PIN_CONTROLS − emitted == 19` — i.e. the pins are
  reconciled against what the instrument emits TODAY, never against a retyped figure.

### 5.2 `tools/probe_features/ownership.py` — the first-consumer contract (new file)

18 checks over the real `lib/`, `src/`, `test/` sources (179 files scanned, tree hashed before and after):

```
  ok   O1  the pair is named in CODE by exactly the 3 approved files: lib/core/mr_features.h, lib/core/node.h, lib/core/node_mac_rx.cpp
  ok   O2  mr_features.h is the sole definer, with 6 `#define`s (3 derivation arms x 2 capabilities)
  ok   O3  node.h names the pair only on 3 `#if` declaration guards, never in an expression
  ok   O4a mr_features.h:      all 9 naming sites match the approved census exactly
  ok   O4b node.h:             all 3 naming sites match the approved census exactly
  ok   O4c node_mac_rx.cpp:    all 6 naming sites match the approved census exactly
  ok   O5  no directive widens a RADMIN capability with MR_FEAT_REMOTE_MGMT (the one `!=` agreement pin is permitted BY NAME)
  ok   O6a all 3 occurrences of `rx_remote_cmd_accept`  compile under ACCEPT and never under CLIENT
  ok   O6b all 3 occurrences of `rx_remote_resp_client` compile under CLIENT and never under ACCEPT
  ok   O6c the shared staging helper is declared and defined under exactly `ACCEPT || CLIENT`
  ok   O7a the production router makes exactly ONE call to the pure decision, with (pa.type, CLIENT, ACCEPT) in the declared order
  ok   O7b both dispatch arms select on the DECISION'S RESULT, not on the raw type byte
  ok   O8  both owned arms CONSUME the frame (`become_free(); return;` immediately after the owner call)
  ok   O9  there is no `none` arm — an unowned remote type takes no branch here and reaches the guard
  ok   O10 no file under test/ names the capability pair in code — tests are not production owners
  ok   O11 the pure decision is declared `static` and compiled UNGATED on every build
  ok   O12 the pair has no `#ifndef` override surface in lib/, src/ or test/ (R-RA-26)
  ok   O13 source integrity: 179 scanned files, sha256(tree) 824fb5608f2f97d9… before and after
```

The **approved census is the reviewed contract, written down literally** (per file, as normalized text with
comments removed and string literals masked): 9 sites in `mr_features.h`, 3 in `node.h`, 6 in `node_mac_rx.cpp`,
including the production call verbatim. The checker reads the real sources; it holds no model of the derivation.

**19 controls, each an exact-one-match edit on an ISOLATED snapshot, each reporting the NAMED check that rejected it:**

| control | rejected by |
| --- | --- |
| Y0 the untouched snapshot passes all 17 checks (the baseline every rejection is measured against; 17 = the 18 emitted lines minus O13, which is the whole-run source-integrity line and is not part of a per-snapshot control) | — (the positive baseline) |
| W-UNKNOWN an unapproved production file (`src/firmware_remote.cpp`) starts naming the pair | O1 |
| W-EXTRA-SITE an extra capability guard **inside an allowed file** | O4c |
| W-NOCALL the production decision call deleted, replaced by a constant | O4c, O7a |
| W-BYPASS the decision is called but the router re-tests the raw type byte (a correctly tested helper the real router bypasses) | O7b |
| **W-SWAP** ★ the two macro arguments swapped at the call site — **invisible to the `{1,1}` native binary** | O4c, O7a |
| W-OWNER-CMD the command entry point compiled under CLIENT (the R-RA-8 inversion) | O4c, O6a |
| W-OWNER-RESP the response entry point compiled under ACCEPT | O4c, O6b |
| W-WIDEN-ACCEPT `#if MR_FEAT_RADMIN_ACCEPT \|\| MR_FEAT_REMOTE_MGMT` | O4c, O5, O6a, O6c |
| W-WIDEN-CLIENT `#if MR_FEAT_RADMIN_CLIENT \|\| MR_FEAT_REMOTE_MGMT` | O4c, O5, O6b, O6c |
| W-NONE-ARM an early consume/return for `none` — **the edit names no capability at all**, so only O9 can see it | O9 |
| W-TEST-OWNER a test file starts naming the pair | O1, O10 |
| W-OVERRIDE an `#ifndef` override surface re-opened | O12, O4a |
| W-DECISION-GATED the pure decision itself becomes capability-gated | O11, O4b |
| Y1 a benign comment edit must **NOT** be rejected | — (accepted, as required) |
| Y2 a multi-match find is refused as an INSTRUMENT ERROR, never scored | — |
| Y3 a vacuous find is refused as an INSTRUMENT ERROR, never scored | — |
| Y4 an unreadable/malformed source raises a GATE ERROR, never a silent pass | — |
| Y5 the shared checkout is byte-identical before and after | — |

`ownership controls: 19 verified / 0 unusable`. The classification for class W/Y is declared **before** any control
runs (the wrapper asserts the ordering): a nonzero exit alone is never a RED — the named check must do the rejecting.

★ **W-SWAP is the reason this instrument exists.** Swapping `MR_FEAT_RADMIN_CLIENT` and `MR_FEAT_RADMIN_ACCEPT` at
the call site cannot change one native assertion (both are 1 in a host build), so it is deliberately NOT a mutation
battery entry — a control that cannot fail is not a control. `tools/probe_ui_model_mutations.py`'s `a0rx` block says
so in place, and points here.

### 5.3 the retired zero-consumer pin — retired by REPLACEMENT, visibly

`run.sh`'s S3 (`owners=$(grep -rl 'MR_FEAT_RADMIN' lib src test)`, required `== $HDR`) and
`test_probe_features.py::test_the_pair_has_no_consumer` are both gone, replaced by the contract above. Slice 1's own
wrapper instructed exactly this: *"WHEN THE FIRST CONSUMER LANDS (a later slice), `test_the_pair_has_no_consumer` is
the assertion that must be DELIBERATELY updated — that is its job, not an obstacle to route around."* The old text
is quoted in place in both files, and `test_the_retired_zero_consumer_pin_is_gone_and_visibly_replaced` asserts both
halves (the old line absent, the replacement present) so a silent deletion cannot pass.

### 5.4 the wrapper sweep

```
$ python3 -m unittest tools.test_probe_features -v      →  Ran 33 tests … OK
```
26 → 33 (`TheSliceOneFence`'s 2 → `TheFirstConsumerFence`'s 8, +1 in `TheGateRuns`). All four pre-existing sabotage
arms still execute and still fail loudly: `CXX=/bin/false` (MATRIX BUILD FAILED), `MR_PROBE_DROP=cell`
(CELL COUNT MOVED), `=check` (CHECK COUNT MOVED), `=control` (CONTROL COUNT MOVED).

---

## §6 — Corpus: forced simulator rebuild, then 36/36 BYTE-IDENTICAL

```
$ md5sum <lus>                                        0b018a5e445b6d294848950f4603a5af      (BEFORE)
$ cmake --build /home/staszek/lora-universal-simulator/build
   40 build actions — meshroute_core_normal: node.cpp, node_beacon, node_routing, node_mac,
   node_mac_rx, node_route_discovery, node_query, node_channel, node_hashlocate, node_join,
   node_mobile, node_budget, node_cascade, NodeRuntimeWrapper  →  libmeshroute_core_normal.a
   meshroute_core_gw: the same 14  →  libmeshroute_core_gw.a
   meshroute_console: console_json, ConsoleNames  →  libmeshroute_console.a   →  Linking lus
$ md5sum <lus>                                        b1b1d92c541cc7f6f63864a2bcc6a355      (AFTER)
```

The dependency really rebuilt (both core variants recompiled because `node.h` changed), and the **binary hash moved**
— exactly what the prediction allowed.

```
$ python3 tools/run_corpus.py --jobs=8 --require-anchors --out <scratch>/after --lus <lus>
PASS: 36/36 streams produced and validated, 0 failures
  anchors: 36/36 rows reproduce simulation/BASELINE.md
  s18_meshroute  32afbf11  events=269517  (0 assertion failures)
```

Manifest-to-manifest comparison of BEFORE vs AFTER over `(output_sha256, events, assertion_failures, anchor_match)`:

```
streams: 36 before / 36 after; MOVERS: 0
anchor_match all: True    assertion failures: 0
s18: ('27ecfa988f062d3b…', 269517, 0)  ->  ('27ecfa988f062d3b…', 269517, 0)
total events: 1864840 -> 1864840
lus sha (recorded IN the manifests): 6d099c7f2c099d65… -> 38228060d410244a…
```

Full 36-row table (BEFORE md5 == AFTER md5 on every row):

| # | stream | md5 | events | anchor |
| --- | --- | --- | --- | --- |
| 1 | `s06_seattle_lifecycle` | `e8f862b0` | 69039 | match |
| 2 | `s07_seattle_mobile_meshroute` | `16cc0dd1` | 111681 | match |
| 3 | `s09_two_layer_gateway` | `71120178` | 2266 | match |
| 4 | `s09_two_layer_gateway_metal` | `0182f858` | 2343 | match |
| 5 | `s10_two_layer_separation` | `c44c0b39` | 2266 | match |
| 6 | `s15_three_layer` | `f95e2d60` | 51794 | match |
| 7 | `s15_three_layer_metal` | `3611a93b` | 52237 | match |
| 8 | `s16_dense_gateway` | `5b30637c` | 23898 | match |
| 9 | `s17_metro` | `aa960050` | 1181178 | match |
| 10 | **`s18_meshroute`** | **`32afbf11`** | **269517** | match |
| 11 | `s19_singlelayer_multihop_chain` | `c669b1ef` | 1065 | match |
| 12 | `s20_random_mesh` | `db240065` | 40566 | match |
| 13 | `s21_leaf_config_divergence` | `d7db6a04` | 390 | match |
| 14 | `s21_mobile_dm_milestone_meshroute` | `fc466e77` | 678 | match |
| 15 | `s22_leaf_config_join` | `baadfbed` | 215 | match |
| 16 | `s22_mobile_team_meshroute` | `c406fb6a` | 1824 | match |
| 17 | `s23_leaf_config_epoch_write` | `0cd16bd5` | 219 | match |
| 18 | `s23_mobile_team_multihop_meshroute` | `568c684f` | 924 | match |
| 19 | `s24_static_and_team_multihop_meshroute` | `d06536f4` | 1576 | match |
| 20 | `s25_two_team_separation_meshroute` | `f87360c7` | 786 | match |
| 21 | `s26_team_reroute_meshroute` | `73a68a35` | 1037 | match |
| 22 | `s27_cross_layer_mobiles_meshroute` | `662c6158` | 9433 | match |
| 23 | `s28_mixed_team_channels_meshroute` | `525756e2` | 3861 | match |
| 24 | `s29_mixed_leaf_team_meshroute` | `bb534a88` | 2025 | match |
| 25 | `s30_team_dad_mediation_meshroute` | `4a1de37d` | 1034 | match |
| 26 | `s31_dual_carrier_gateway` | `4eafb125` | 2300 | match |
| 27 | `s32_dual_cr_gateway` | `9574f5dd` | 2266 | match |
| 28 | `s33_mixed_cr_channel_overhear` | `814ef421` | 2845 | match |
| 29 | `s34_team_switch_clears_plane` | `0c724c05` | 919 | match |
| 30 | `s35a_cochannel_isolation_meshroute` | `bda1713b` | 2356 | match |
| 31 | `s35b_cochannel_isolation_control_meshroute` | `7dbc19ae` | 1063 | match |
| 32 | `s36_reprovision_purges_carriers` | `76d02e58` | 472 | match |
| 33 | `s37_team_homed_origin_meshroute` | `db535d42` | 748 | match |
| 34 | `s38_team_origin_learn_meshroute` | `52be507e` | 522 | match |
| 35 | `sim_9node_base` | `e7a1c3d6` | 4945 | match |
| 36 | `twin_9node_dm` | `dd28f145` | 14552 | match |

⛔ `simulation/BASELINE.md` was NOT read for values, edited or re-anchored. **STOP 4 does not fire.**

---

## §7 — ABI

```
$ python3 tools/probe_board_abi.py
  meshroute::Node        native 222072/8 T     heltec_mobile 117912/8 T     gateway 148680/8 T
PASS: board ABI (191 checks, 9/9 controls RED, 0 unusable)

$ python3 tools/probe_b278_row_abi.py
PASS: B278 production correlation-row ABI mirror (42 measurements, 6/6 controls RED)
```

`sizeof(Node)` is **222072 / 117912 / 148680** — identical to the pins on all three ABIs. `node.h`'s live
`static_assert(sizeof(Node) == 222072)` compiles, and `-Wreorder` is clean (the warning census, §10, reports zero
new warnings on six envs). Nothing was added to `Node`: this slice adds only member-function declarations.

---

## §8 — Boards: the deterministic pair, base vs final

Base capture: a **linked `git worktree` detached at the pinned base `0da4d56`** (so the base sources are pristine
and the working checkout is never reverted), measured with that tree's own `tools/measure_board.py`.
Final capture: the real checkout. A **third** capture builds the FINAL sources INSIDE the base worktree, so the
A/B is at an IDENTICAL build path — [[B262]]: `heltec_mobile`'s `payload_sha256` is path-dependent while RAM,
flash, objects, sections and symbols are not, and `gateway`/ARM is path-insensitive throughout.

```
$ python3 tools/measure_board.py pair --output <base-worktree>/.pio-measure/base  --jobs=2      # BASE
$ python3 tools/measure_board.py pair --output <base-worktree>/.pio-measure/final --jobs=2      # FINAL, same path
$ python3 tools/measure_board.py pair --output .pio-measure/s1b-final             --jobs=2      # FINAL, real checkout
```

| | `gateway` base | `gateway` final | Δ | `heltec_mobile` base | `heltec_mobile` final | Δ |
| --- | --- | --- | --- | --- | --- | --- |
| **RAM** | 195844 | **195844** | **0** | 205684 | **205684** | **0** |
| flash | 512076 | 512092 | **+16** | 1355300 | 1355292 | **−8** |
| objects | 283 | 283 | 0 | 327 | 327 | 0 |
| symbol count | 6159 | 6160 | +1 | 13082 | 13084 | +2 |
| symbol size total | 686817 | 686821 | +4 | 1454964 | 1454952 | −12 |

The base figures reproduce the Slice-1 pins EXACTLY (gateway 195844/512076/283, heltec 205684/1355300/327), which
is what calibrates the base capture despite its different build path. The real-checkout final agrees with the
same-path final on RAM, flash, objects and (on ARM) the payload hash — `gateway` payload
`4b3e9121…` in BOTH; `heltec_mobile` payload differs between the two paths (`2675b2cd…` vs `c5237f28…`) and is
identical in every other field, which is [[B262]]'s exact recorded signature.

**Sections (base → final, same path):**

* `gateway`: `.text` 511092 → **511108** (+16); `.data` 976 and `.ARM.exidx` 8 UNCHANGED.
  511108 + 976 + 8 = 512092 ✓
* `heltec_mobile`: `.flash.text` 1038702 → **1038694** (−8); `.dram0.data` 25260, `.flash.rodata` 212468,
  `.flash.appdesc` 256, `.iram0.text` 77843, `.iram0.vectors` 1027, `.rtc.text` 256, `.rtc.force_fast` 8 ALL
  UNCHANGED. ⇒ the whole movement is code, and none of it is RAM.

**Attribution, closing to the byte.**

`heltec_mobile` — every changed sized symbol, from the runner's own normalized inventory:

| symbol | base | final | Δ |
| --- | --- | --- | --- |
| `Node::do_post_ack()` | 3470 | 3350 | **−120** (the staging body left it) |
| `Node::handle_data(...)` | 3198 | 3202 | +4 |
| `Node::remote_inbound_stage(...)` | — | 92 | **+92** (NEW, out-of-line) |
| `Node::rx_remote_resp_client(...)` | — | 19 | **+19** (NEW — the CLIENT owner; the ACCEPT owner does not exist here) |
| `std::map<uint8_t,uint64_t>::operator[]` | 162 | 163 | +1 |
| `_Rb_tree<uint8_t,pair<const uint8_t,uint64_t>>::_M_get_insert_hint_unique_pos` | 151 | 147 | −4 |
| `_Rb_tree<uint64_t,pair<const uint64_t,uint8_t>>::_M_get_insert_hint_unique_pos` | 185 | 181 | −4 |
| | | **sum** | **−12** = the measured `symbol_size_total` Δ ✓ |

The three libstdc++ rows are **COMDAT/weak template instantiations emitted by more than one TU**, measured rather
than asserted: `nm -S` shows `_M_get_insert_hint_unique_pos<uint8_t,…>` emitted at **151** bytes by
`node_mac_rx.cpp.o` and at **147** bytes by `node_routing.cpp.o`; the linked image carried 151 at the base and 147
now, i.e. the linker's selected copy switched TU when `node_mac_rx.cpp`'s section layout changed. Same function,
no behaviour, no RAM. `.flash.text` moved −8 against a symbol total of −12 ⇒ **+4 of inter-section alignment**.

`gateway` — the only sized symbol that moved is `Node::do_post_ack()` **4852 → 4856 (+4)**; the extra inventory row
is one ARM `$t` mapping symbol (size 0, no flash). The remaining **+12** of `.text` is downstream re-alignment, and
it is LOCATED rather than assumed: comparing the addresses of all 1838 common `.text`/`.rodata` symbols, the shift
is 0 up to `Node::do_post_ack`, then **+4** from `Node::drain_xl_handoffs_for_leaf`, then **+8** from
`Node::send_c_config`, then **+16** from `memchr` — three boundaries, 4 + 4 + 8 = **16** ✓.

Why `gateway` grows while `heltec_mobile` shrinks, measured at the object level:
`node_mac_rx.cpp.o` (gateway) emits the three new out-of-line function sections
`.text…radmin_rx_ownerEhbb` **0x14 = 20**, `.text…remote_inbound_stage…` **0xd4 = 212**,
`.text…rx_remote_cmd_accept…` **0xd8 = 216** — 448 bytes — plus `do_post_ack`'s +4 = **+452**, which is exactly the
object's measured `.text` Δ. All three are then **inlined and garbage-collected at link** (`-ffunction-sections`
+ the nRF52 platform's `-Wl,--gc-sections`), leaving only the +4 of inlined code and 12 bytes of re-alignment.
Xtensa keeps two of them out-of-line instead, and pays −120 + 111 = −9 net on those, hence the −8.

**No unattributed byte, no RAM movement, no NV/wire/timer/capacity change. STOP 3 and STOP 4 do not fire.**

---

## §9 — Standing probes, tool sweep, command inventory

| instrument | result |
| --- | --- |
| `tools/probe_console_sink/run.sh` | **PASS** — `PINS profiles=6 checks=720 structural=29 ble_guard=212 ownership=6 ownership_controls=3 controls=58 unusable_controls=0` (all at their pins) |
| `tools/probe_inbox_verbs/run.sh` | **PASS** — 91 checks (pin 91), 22 controls / 0 unusable (pin 22) |
| `tools/probe_firmware_ui/run.sh` | **PASS** — 223 controls verified / 0 unusable |
| `tools/probe_custody_usb/run.sh` | **PASS** — 27 checks (pin 27), 10 controls / 0 unusable (pin 10) |
| `tools/probe_ble_line/run.sh` | **PASS** — 40 checks (pin 40), 8 controls / 0 unusable (pin 8) |
| `tools/probe_features/run.sh` | **PASS** — 9 cells (pin 9) · **114** checks (pin 114) · 0 failed · **38** controls / 0 unusable (pin 38) |
| `python3 -m unittest discover -s tools -p 'test_*.py'` | **OK — Ran 312 tests** (305 → 312: +7, all in `test_probe_features.py`; see §5.4) |
| `python3 tools/gen_command_inventory.py` (bare) | **PASS** — matches byte-for-byte, **177 command rows** |
| `python3 tools/gen_command_inventory.py --check` | **PASS** — 177 rows, no drift ⇒ **no `--write` was run or needed** |

No legacy command profile projection moved; no unrelated instrument debt was touched.

---

## §10 — Warning census and the two checkers

```
$ tools/warning_census.sh                                       # its own pinned 6-env set (the ruled exception)
env                   objs      warn    expect  -Wswitch        RAM      Flash  verdict
gateway_heltec         327       173       173         0     230956    1306096  ok
gateway_heltec_v4      328       178       178         0     231228    1304100  ok
heltec_mobile          327       177       177         0     205684    1355292  ok
heltec_v3              327       177       177         0     206164    1360380  ok
heltec_v4              328       182       182         0     206436    1358440  ok
heltec_v4_mobile       328       182       182         0     205956    1353356  ok
PASS — 6 OLED env(s) match their pinned warning baseline
```

Zero new warnings, `-Wswitch` **0** on every env, nothing re-pinned. The census's `heltec_mobile`
**205684 RAM / 1355292 flash** reproduces the deterministic runner's final figure independently (§8).

```
$ python3 tools/check_a0_matrix.py
PASS — 21 enum members each have a matrix row, the CURRENT NAMESPACE table states every current value,
       and all 6 named special rows are present.
$ python3 tools/check_data_type_literals.py
PASS — 179 active source file(s) scanned; no numeric DataType comparison, argument literal or switch-case
       label survives.
$ git diff --check
(no output)
```

---

## §11 — Mutation: both selectors, and the union run IN FULL

### 11.1 the two selectors, derived independently from the final diff

**(a) Changed-source.** The final production diff touches exactly two files that this harness can mutate or
depends on: `lib/core/node_mac_rx.cpp` (semantic) and `lib/core/node.h` (declarations only — a HEADER, not a
`TARGET_SRC` entry, so it maps to no battery of its own but is compiled by every battery that builds native).
`TARGET_SRC` maps `lib/core/node_mac_rx.cpp` to **six** targets: `b161rx`, `b251rx`, `b159rx`, `a0rx`,
`sliceBrx`, `sliceGrx`. Derived from the harness's own table, not quoted from the brief.

**(b) Historical / dependency.** `a0rx` (the addressed if-chain whose arm this slice replaces) and `sliceBrx`
(the fail-closed guard the un-owned type now falls to) own the acceptance surface directly; **`sliceAcodec`**
(`lib/core/frame_codec.h`) is the range predicate and the ONE trait authority on which the unowned drop depends —
`data_type_traits(REMOTE_CMD/RESP).internal` is what makes an un-owned remote type reach the guard rather than the
delivery tail. No further dependency was found: this slice adds no origination, no codec field, no cascade or
timer interaction, and no TX path. An unrelated TX battery is NOT included merely for sharing an arc name.

**Union gated = the six RX targets + `sliceAcodec` = 7 targets, run IN FULL (not just the new entries),
sequentially and exclusively, `--workers=2`.**

### 11.2 results

| target | source | entries | RED | unusable |
| --- | --- | --- | --- | --- |
| `a0rx` | `lib/core/node_mac_rx.cpp` | 7 | **7** | 0 |
| `sliceBrx` | `lib/core/node_mac_rx.cpp` | 16 | **16** | 0 |
| `b161rx` | `lib/core/node_mac_rx.cpp` | 8 | **8** | 0 |
| `b251rx` | `lib/core/node_mac_rx.cpp` | 19 | **19** | 0 |
| `b159rx` | `lib/core/node_mac_rx.cpp` | 3 | **3** | 0 |
| `sliceGrx` | `lib/core/node_mac_rx.cpp` | 42 | **42** | 0 |
| `sliceAcodec` | `lib/core/frame_codec.h` | 4 | **4** | 0 |
| **union** | | **99** | **99** | **0** |

Every worker in every target derived its own clean baseline as **2615 / 111354 / 0** (so the PIN is in sync and no
stale-pin banner printed), every worker reported `source restored: md5 … (MATCHES)`, and every target closed with
`real tree untouched: all 41 target files byte-identical to launch (md5)`.

### 11.3 the thirteen NEW controls (each exactly one match, each judged by a named §radmin-1b case)

Placed under the two existing targets that own the region — **no new cross-file target was created**.

**`a0rx` — the DISPATCH SITE inside the addressed if-chain:**

| control | verdict |
| --- | --- |
| `1b-01` the ACCEPT-owned call deleted (the CMD is consumed but never staged — swallowed silently) | RED, 15 assertions |
| `1b-02` the CLIENT-owned call deleted (the reply disappears with no drop and no print) | RED, 14 assertions |
| `1b-03` the ACCEPT arm stops CONSUMING (a handled type also reaches the guard and reports itself unsupported) | RED, 1 assertion |
| `1b-04` the two owned calls SWAPPED at the dispatch (a command stages as `is_response=true`) | RED, 4 assertions |

**`sliceBrx` — the PURE DECISION and the one staging body it reaches:**

| control | verdict |
| --- | --- |
| `1b-05` the `accept_on` term dropped — CMD owned on every configuration (R-RA-27 item 3 inverted) | RED, 5 |
| `1b-06` the `client_on` term dropped — the legacy widening R-RA-27 forbids, reached by another route | RED, 5 |
| `1b-07` the two capabilities SWAPPED **inside the decision** — the R-RA-8 inversion | RED, 10 |
| `1b-08` the decision's `none` tail answers `command_accept` — it steals every other handler's type | RED, 218 |
| `1b-09` the response marker hard-wired `false` in the shared staging body | RED, 3 |
| `1b-10` the drop-full guard deleted — a second frame overwrites the pending one, silently | RED, 8 |
| `1b-11` the staging clamp tightened — a long body silently truncates | RED, 2 |
| `1b-12` the staged origin becomes `pa.dst` — the reply would be addressed to ourselves | RED, 7 |
| `1b-13` the body copy stops copying — right length, wrong bytes (a length-shaped assertion cannot see it) | RED, 5 |

**Mapping of each new claim to a falsifier** — the pure decision's four terms/directions: 1b-05/06/07/08; each real
owner call: 1b-01/02 (+ 1b-04 for the pairing); the response marker: 1b-09 (+ 1b-04); staging preservation:
1b-10 (drop-full), 1b-11 (clamp), 1b-12 (`from`), 1b-13 (body bytes); the consuming return: 1b-03.
The pre-existing guard controls B04-B08 (deletion, weakened predicate, two wrong placements, the scalar-only
telemetry bound) and A04/A05/A08 were **reused unweakened** and are all still RED — which is what proves the
[[B307]] comment edit did not disturb the guard it sits beside.

⛔ **ONE WIRING DEFECT IS DELIBERATELY NOT A MUTATION ENTRY.** Swapping the two MACRO ARGUMENTS at the call site
(`radmin_rx_owner(pa.type, MR_FEAT_RADMIN_ACCEPT, MR_FEAT_RADMIN_CLIENT)`) cannot change a single assertion in a
`{1,1}` host binary. A control that cannot fail is not a control, so per the brief it lives in the ownership
instrument instead (§5.2, **W-SWAP**, rejected by O7a) and `a0rx` says so in place, with the reason.

### 11.4 an instrument finding measured on the way (proposed register row, §14)

The first form of `1b-13` wrote `0xFF` into the staged body; a doctest failure message then carried that raw byte,
and the harness's own `subprocess(..., text=True)` died with
`UnicodeDecodeError: 'utf-8' codec can't decode byte 0xff in position 464`. The worker never reported, so the entry
came back **MISSING** (`RUN INTEGRITY FAILURE`, correctly refusing to be read as a battery result) rather than as a
diagnosable failure. The entry was re-cut to write printable ASCII (`0x3F`), the reason recorded in-source beside
it, and the **whole union was then re-run from scratch** against the final tree — the table in §11.2 is that
complete second run, not a patched first one. The runner-robustness half is registered as a finding.

---

## §12 — STOP audit (all seven evaluated explicitly)

| # | condition | fired? |
| --- | --- | --- |
| 1 | base unpinned/missing/different, or unexplained starting edits | **NO** — `git rev-parse HEAD` == `0da4d56…`; the only dirty path at the start was the Author's own brief |
| 2 | legacy-widened owners, runtime role state, test-only role override, firmware/codec change, moved forwarding/guard order, v2 body/security/storage behaviour | **NO** — strict `MR_FEAT_RADMIN_*` gates only (ownership check O5 + controls W-WIDEN-ACCEPT/CLIENT); no runtime state; no `src/`, no codec; the seam sits exactly where the legacy arm did and the guard did not move (B06/B07 still RED); no v2 body opened, no clamp tightened, no slot partitioned |
| 3 | owned staging semantics beyond the ruled split, or a change to the slot / any Node layout-ABI pin / RAM / wire / NV / timer / capacity / feature definition or diagnostic | **NO** — §2.2's statement-by-statement comparison; `sizeof(Node)` 222072/117912/148680 unmoved; RAM Δ0 on both boards; `mr_features.h` comment-only (token-identical); the three `#error` diagnostics and all six `#define`s untouched (feature gate A1-A4, C1-C5 still RED) |
| 4 | any corpus stream/anchor change, an undemonstrable simulator rebuild, or an unattributable board movement | **NO** — 36/36 byte-identical with the anchors reproduced; 40 rebuild actions and a moved `lus` hash; every board byte attributed (§8), closing exactly on both ABIs |
| 5 | a synthetic pure decision claimed as an executed native remote RX drop, or ownership not tied to both the tested decision and the board-compiled arms | **NO** — §3 states the distinction in the test file itself; the executed guard proof is the separate pre-existing fixture set; the board-compiled arms are proven by preprocessing + object symbols (§4) |
| 6 | a green/vacuous/multi-matched/unusable control, a misclassified refusal, failed source integrity, an undreived pin drift, or a gate needing an out-of-fence edit | **NO** — 99/99 mutations RED with `match count 1` each and 0 unusable; 38/38 feature-gate controls verified; the one MISSING entry was re-cut and the union re-run in full (§11.4); every pin moved with a written derivation; no out-of-fence edit was made to rescue anything |
| 7 | an unexplained warning, concurrent change, unaccounted file, inventory regeneration or new owner choice | **NO** — zero new warnings on six envs; the three concurrent doc changes are identified and attributed (§13); inventory `--check` clean, no `--write`; no owner ruling is requested |

---

## §13 — Complete file inventory at the end of the slice

```
$ git status --porcelain
 M docs/2026-07-30-open-bug-register.md                                     <- NOT MINE (concurrent Author landing)
 M docs/superpowers/plans/2026-09-06-radmin-slice1b-capability-owned-receive.md   <- NOT MINE (Author base pin)
 M docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md <- NOT MINE (concurrent Author landing)
 M lib/core/mr_features.h                                                   <- mine (COMMENT ONLY)
 M lib/core/node.h                                                          <- mine
 M lib/core/node_mac_rx.cpp                                                 <- mine
 M test/test_node_r3.cpp                                                    <- mine
 M tools/probe_features/probe_main.cpp                                      <- mine (COMMENT ONLY)
 M tools/probe_features/run.sh                                              <- mine
 M tools/probe_ui_model_mutations.py                                        <- mine
 M tools/test_probe_features.py                                             <- mine
?? docs/superpowers/evidence/2026-09-06-radmin-slice1b.md                   <- mine (this file)
?? docs/superpowers/plans/2026-09-06-radmin-slice2-precheck.md              <- NOT MINE (concurrent QA document)
?? tools/probe_features/ownership.py                                        <- mine (new support instrument)
```

**Not mine, identified rather than ignored (STOP 7):** the register gained rows **B308/B309** (Slice 2 carrier
authority / carrier-KAT coverage) and the design gained a §8.9 `TERMINAL` result-byte allocation table for Slice 2
QA review; `…slice2-precheck.md` is a concurrent Quality-Agent plan. All three are Markdown, read by no build, and
by no gate this slice ran (`check_a0_matrix.py` reads the custody matrix, `gen_command_inventory.py` reads the
command-inventory evidence — neither of these files). None was written, staged or reverted by me.

```
$ git diff --stat -- lib src test tools
 lib/core/mr_features.h              |   7 +-
 lib/core/node.h                     |  39 ++++++++
 lib/core/node_mac_rx.cpp            | 129 +++++++++++++++++++++----
 test/test_node_r3.cpp               | 184 ++++++++++++++++++++++++++++++++++++
 tools/probe_features/probe_main.cpp |  12 ++-
 tools/probe_features/run.sh         |  97 ++++++++++++++-----
 tools/probe_ui_model_mutations.py   | 117 ++++++++++++++++++++++-
 tools/test_probe_features.py        | 139 +++++++++++++++++++++++----
 8 files changed, 661 insertions(+), 63 deletions(-)
```

⛔ **No `src/`, no other `lib/core` TU, no frame/codec/constants, no simulator source, no `platformio.ini`, no
variant, no NV/wire/timer/state/capacity, no `simulation/BASELINE.md`, no anchor table, no command inventory, no
companion, no register/bench/design/tracker/MEMORY edit.**

**The one new file** — `tools/probe_features/ownership.py` — carries the author header on line 2, is mode `0755`
(executable), is invoked by `tools/probe_features/run.sh` in BOTH modes by default, and is exercised by
`tools/test_probe_features.py` (which is in the standing `unittest discover` sweep). It is untracked and must be
committed with this slice.

**[[B307]] comment sub-diff proven token-inert in isolation.** Applying ONLY the B307 hunk to the base file
(`git show HEAD:lib/core/node_mac_rx.cpp`) gives comment-stripped token streams identical to the base —
**66041 tokens before and after**, +3 lines. The thirteen-env census the corrected comment states is derived
read-only from PlatformIO's own resolver, not typed: `pio project config --json-output` resolves **13** board
environments (**5** static `production/xiao_sx1262/heltec_v3/heltec_v4/xiao_esp32s3`, **4** gateway, **4** mobile)
and exactly one host env (`native`).

**Throwaway instrumentation, and where it lived.** The §1.3 census probes existed ONLY in `<scratch>/census_tree`
(a copy of `lib/core` + `lib/console` + `lib/monocypher`) compiled by a second simulator configured against that
copy. Nothing in the repository was ever instrumented; the copy was restored from `git archive HEAD lib/core` and
re-hashed to its pre-instrumentation value. The base board capture used a temporary **linked `git worktree`
detached at `0da4d56`**, removed at the end (`git worktree remove --force`; `git worktree list` no longer shows
it). ⛔ No `git commit`, `stash`, `checkout --` or `reset` was run in this repository at any point.

---

## §14 — Findings, with proposed register rows

**F1 — the mutation harness can lose a worker to non-UTF-8 test output (INSTRUMENT ROBUSTNESS).** Measured, not
theorised: an entry whose mutant causes a staged byte `0xFF` to reach a doctest failure message kills the worker
with `UnicodeDecodeError: 'utf-8' codec can't decode byte 0xff` inside
`subprocess.communicate(..., text=True)` (`tools/probe_ui_model_mutations.py`, `run_suite`). The parent correctly
refuses to read the run (`RUN INTEGRITY FAILURE … 1 selected entry came back with NO verdict`), so nothing is
laundered into a false PASS — but a legitimate mutation is unmeasurable, and the diagnosis costs a full re-run.
This is the same family as [[B273]] (a measured RED laundered into "did not measure"), arriving through the
*decoding* door rather than the *string-matching* door.
**Proposed row:** `| B310 | OPEN / LOW / INSTRUMENT ROBUSTNESS — found by Slice 1b 2026-09-06, measured and
reproduced | The mutation harness decodes worker output with subprocess text=True; a mutant whose test output
contains a non-UTF-8 byte kills the worker and its entries come back MISSING rather than RED/UNUSABLE. Measured on
Slice 1b's 1b-13 with a 0xFF staged-body mutant; worked around in-slice by re-cutting the mutant to printable
ASCII, recorded in-source beside the entry. CLOSE BY: read the child's output as bytes and decode with
errors="replace" (or capture to a file), with a control — an entry whose mutant deliberately emits an invalid byte
must still score RED. Same family as [[B273]].` |

**F2 — [[B286]] re-measured, and it now blocks `--workers=2` on this host.** The brief's `--workers=2` could not
be run against the checkout as it stands: the harness's `rsync` excludes only `.git` and `.pio`, so each worker
would copy `.pio-measure/` (**4.1 GB** today) and `.claude/` (**984 MB**) — about **5.1 GB per worker**, against
**5.8 GB** free on this host's single filesystem. Measured with `du`/`df` before starting. The union was therefore
run from a staging copy of the working tree created with those two directories excluded (`46 MB`), whose
`node_mac_rx.cpp` / `frame_codec.h` / `node.h` / `test_node_r3.cpp` / the harness itself were verified md5-identical
to the checkout before and after; every run printed `real tree untouched: all 41 target files byte-identical to
launch`. ⛔ The harness itself was NOT modified — B286's fix is out of this slice's fence.
**Proposed row:** append to the existing **[[B286]]**: *"Re-measured 2026-09-06 (Slice 1b): `.pio-measure/` is now
4.1 GB and `.claude/` 984 MB, i.e. ~5.1 GB per worker against 5.8 GB free — `--workers=2` is no longer merely
wasteful, it exhausts the disk. Worked around by running the battery from an excluded-directory staging copy, with
per-file md5 identity proven both ways. Raises the priority from MEDIUM."*

**F3 — the accept-side RAM is still unconditional, and now visibly so (no new defect; a marker).** `_remote_inbound`
remains `node.h:2905`, unconditional, so a client-only mobile still pays ~246 B for a slot it can no longer be
commanded through. That is R-RA-27's explicit boundary (Slice 5 owns the replacement), it is marked in source as
**MISSING/deferred by design**, and it is why RAM Δ is 0 in §8 rather than negative. No register row proposed —
the design already carries it.

**Metal residue: NONE**, as ruled by R-RA-27. Bench §9.9's static/gateway `rcmd` round-trip portion is already
marked suspended from 1b until Slice 9; its local supplied-sink checks are unaffected by this slice.

**Left UNCOMMITTED (D4).** The owner commits.
