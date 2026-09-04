<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 0f — Quality-Agent pre-check ledger (2026-09-04): BLE line capacity (R-RA-24′ bound 2)

Authority: R-RA-24′ (`docs/superpowers/plans/2026-09-03-remote-admin-v2-rulings.md`); design §12/§19 after the
Author lands the ruling. Verified at `HEAD 66eb4cf`. A `src/`-only CAPACITY change (behaviour: longer BLE lines are
accepted) ⇒ corpus-inert, RAM-visible, BLE-observable ⇒ bench part owed. Its own slice (C1): not a refactor.

## 0. What 0f is, in one line

Grow the BLE inbound line buffer from 160 to a DERIVED 275 bytes (⚠ 273 until the 0f coder's STOP-2 census; owner ruled policy B 2026-09-04, see the ruling ledger) so every syntactically legal local line (the longest
legal `send`, and the longest legal `remote` line) reaches the common dispatcher byte-exact over BLE, with the
existing loud overflow refusal kept at the new edge, and the RAM cost measured on the nRF52 pair member.

## 1. Source facts

| fact | anchor |
| --- | --- |
| the buffer: `char g_line[160]`, `g_pos`, `g_overflow`; nRF52-only (`MRBLE_NRF52`), inert stubs elsewhere | `src/device_ble.h:79-81`, `:24-35` (gate), `:43-46` (stubs) |
| intake: `service_rx()` drains `g_bleuart` byte by byte, drops `\r`, dispatches on `\n`, stores while `g_pos < sizeof(g_line) - 1`, else sets `g_overflow` and keeps eating until `\n` — so ATT chunk boundaries are invisible by construction | `:175-184` |
| overflow refusal: `dispatch_current_line()` writes `{"err":"line_too_long"}\n` and drops the line; never truncates | `:91-96` |
| the seam: `DispatchFn = size_t(*)(const char* line, size_t len, char* out, size_t cap)`; `g_out` is the reply buffer (256 B, chunked on TX by MTU); fw_main supplies `ble_dispatch_line` | `:33`, `:69`, `:76`; `src/fw_main.cpp` (`ble_dispatch_line`) |
| USB parity: `line[1024]`, overflow refused loudly as `> err: line too long (>1023) — rejected …` | `src/fw_main.cpp:1071`, `:1079` |
| the longest syntactically legal local line: the by-hash `send` tail accepts `-a -e -t -K -l` (five flags; `-g` is `send_channel`-only) ⇒ `send 0xffffffff "<239>" -a -e -t -K -l` = 5+10+1+1+239+1+5×3 = **272** (my first derivation, 269, missed `-K`); ⚠ AND `send` is NOT the longest producer: `send_layer 0xffffffff 255,255,255 "<226>" -a -e -K -l` = 11+10+1+11+1+1+226+1+4×3 = **274** (226 = the depth-4 cross-layer-by-hash body cap, 0e table; deeper = +4 chars −1 body byte, so depth 4 wins) — found by the 0f coder (evidence §2), missed by me twice ⇒ **policy B, 274 + NUL = 275** (owner-confirmed SYNTACTIC definition: canonical parser-accepted spelling, each option once, largest carrier-admitted body — the 274 form itself refuses by name: `-l` unsupported cross-layer, `-e` sealed overhead; the 268-byte plaintext `-a -K` form is the one that queues; the 226 term = transitional mirror of `pack_unicast_inner`'s sizing, frame_codec.cpp:1084-1091); `dm_max_body_bytes` 239 | `lib/console/console_parse.cpp:119-124` (flag set), `:352-353` (`send` call: allow_a, allow_e=by_hash, team, no_intro, loc), `protocol_constants.h:1062` |
| the longest legal `remote` line: `remote ` 7 + selector ≤ 32 (⚠ corrected at the brief gate: the management-target label has NO named source authority yet — `/mrtargets` is unspecified byte-wise and `device_nv.h:149` is the IdBlob node NAME, a different record — so 32 is a transitional DESIGN pin the brief must label as such; `keyN` = `key0`..`key9`, design :267) or 10 (`0x` hash) + ` -e` 3 + ` using=key9` 11 + ` -a` 3 + ` -- ` 4 = 60, + tail 201 (R-RA-24′ bound 3) = 261, + NUL = 262 ≤ 275 | R-RA-18 grammar, 0e cap table |
| the semantic refusal for a 239-byte SEALED body: `SealOutcome::too_large` (loud, named) — the line must be ACCEPTED by BLE so this refusal, not a transport drop, is what the operator sees | `lib/core/node_hashlocate.cpp:824-825` |
| RAM today: `gateway` 195724 B (83.1 % of 235520); +115 B ⇒ ≈195839 (83.2 %); `heltec_mobile` unaffected (no BLE transport compiled) | S5/0d gate pairs |
| no host instrument compiles `device_ble.h` today (the console-sink probe fakes `Arduino.h` only; no Bluefruit fake) | `tools/probe_console_sink/fakes/` |
| the companion contract does not state the BLE line capacity (no `160`/`159`/MTU line) — an Author landing after 0f | `ios-companion/INBOX_SYNC_CONTRACT.md` |

## 2. Shape the brief must pin

- ONE derived constant (e.g. `ble_line_max_bytes = <longest legal local line> + 1`) with its derivation in source
  (the send grammar's maximum + the remote wrapper maximum, `static_assert`ed ≥ both), replacing the literal 160.
  ⛔ No bare 275; the number is the derivation's output = max over the named producers (`send` 272, `send_layer` 274, `remote` 261) + NUL, each term traceable (my own passes got 269/270 then 272/273 by pricing `send` alone — exactly the failure a literal would freeze).
- The overflow refusal stays at the new edge, same JSON, same drop semantics; USB unchanged.
- A host probe for the intake (`tools/probe_ble_line/` in the console-sink idiom): a minimal `g_bleuart` fake with
  `available()/read()/write()` that feeds a scripted byte stream in chosen chunk sizes (1, 20, 244 = typical ATT
  MTU payloads), compiled against the real `src/device_ble.h` intake (`service_rx`/`dispatch_current_line`) — this
  needs the header's nRF52-only gate to be satisfiable on host (a fake `bluefruit.h`, or the intake factored so the
  byte machine is includable without Bluefruit — the brief chooses; a factoring is a refactor and must be its own
  commit inside the slice, C1). Checks: the 274-byte line reaches the dispatch fn byte-exact under every chunking;
  275 bytes ⇒ `line_too_long` once, nothing dispatched, state reset; `\r` dropped; an empty line ignored; a
  1-byte line works; the reply path untouched.
- Native (lib/console): `parse_command` accepts the 272-byte `send` line (239-byte body, five flags) and the 274-byte `send_layer` line
  (226-byte body, three hops, four flags); 240 bytes of body refuse (`bad_args`/too long by the parser's own rule, named); the plaintext form
  `send 0xffffffff "<239>" -a -t` queues through `Node::on_command`; the sealed form refuses `too_large` by name.
- Controls: restoring 160 makes the 274-byte chunked case RED; removing the overflow branch makes the 275 case
  RED (silent acceptance); changing the derivation to a literal is a structural RED.

## 3. Gate and fence

- Fence: `src/device_ble.h` (the constant + derivation; optionally the intake factoring as its own commit), the new
  probe under `tools/` + its auto-discovered `test_*.py`, native cases in the console-parse test TU, evidence.
  ⛔ No `lib/core`, no `fw_main.cpp` behaviour, no companion contract edit (Author), no BASELINE.
- Corpus 36/36 by construction (no `lib/core`); lus rebuild 0 actions (say so). Boards: `gateway` RAM **+115 B
  expected, attributed to `g_line`** (`.bss` symbol diff), flash ±0; `heltec_mobile` byte-identical. ABI probe (Node
  unmoved). Census 6/6 (no new warning: the constant is used). Probes (console-sink, inbox-verbs, custody-USB) at
  their pins. Sweep +N for the new test. Checkers. `git diff --check`.
- Bench: **Part 61 (BLE, nRF52)**: from the companion (or a BLE terminal), send the 272-byte `send` line (plaintext form `send 0x<hash> "<239 bytes>" -a -t -K -l`, or the sealed form
  expecting `too_large`) and, on a two-layer bench, the 274-byte `send_layer` line with a 226-byte body; expect `queued ctr=N` in the reply (or the named refusal if no route), never
  `line_too_long`; send a 275-byte line ⇒ `{"err":"line_too_long"}`; send the same 274 bytes with 20-byte writes
  and with one 244-byte write ⇒ identical result.
- Held Author landings: the companion contract's BLE line capacity sentence (275, with the derivation), design
  §12 (bound 2 landed), §19 Slice 0f marked landed.

## 4. Owner decision

None — R-RA-24′ rules it. One brief-level choice: fake `bluefruit.h` vs factoring the intake; QA has no
preference as long as the probe executes the REAL intake bytes and the C1 split is kept.
