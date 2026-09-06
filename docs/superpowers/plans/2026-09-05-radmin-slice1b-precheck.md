<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 1b — Quality-Agent pre-check ledger (2026-09-05): capability-owned pre-tail remote handlers

Authority: R-RA-19 (design ruling 47, `docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:1949`),
design §19 item 1b (:1661-1677 incl. the CORRECTED staging plan and the pass-2 seam obligations), §19.1 row 1b
("capability-owned pre-tail handlers, `lib/core/node_mac_rx.cpp` | prediction-first 36/36 identity; four role-by-type
native arms; ruled pair | none"), R-RA-8/R-RA-17/R-RA-26 (the pair Slice 1 lands), pass-2 ledger S3
(`docs/superpowers/plans/2026-09-05-fable-review-pass2.md`). Verified at `HEAD c1c342b` ("prep") — 1b's base is the
owner's commit AFTER Slice 1 closes (the pair must exist). Anchors move; re-verify (V2). Hypotheses, not authority.

## 0. What 1b is, in one line

A BEHAVIOUR-NEUTRAL refactor (C1) of the ONE shared staging arm for `DATA_TYPE_REMOTE_CMD`/`_RESP` into two
capability-owned entry points — `REMOTE_CMD` owned by ACCEPT, `REMOTE_RESP` owned by CLIENT — whose bodies stay the
legacy staging byte-for-byte; a role that is compiled OUT lets its type fall through to the existing fail-closed
`unsupported_internal` guard. No v2 body, no wire change, prediction-first 36/36.

## 1. Source facts

| fact | anchor |
| --- | --- |
| the arm: `if (pa.type == DATA_TYPE_REMOTE_CMD \|\| pa.type == DATA_TYPE_REMOTE_RESP) { if (_remote_inbound.active) { emit remote_inbound_drop_full } else { stage: src = ui ? ui->body : inner+1; n clamped to inbox_max_body (241, a no-op today); active/is_response/from=pa.origin/len/body } become_free; return; }` — sits BEFORE the open steps (SEALED_RELAY, CRYPTED trial) and reads the CLEARTEXT body; ⛔ NOT gated by any `MR_FEAT_*` (a mobile with REMOTE_MGMT 0 still stages; fw_main's `remote_exec` stub is what makes it inert there) | `lib/core/node_mac_rx.cpp:2220-2238` |
| both types are INTERNAL (`DataTypeTraits{known, internal, !app, !generic, !persistent}`), 0xA0/0xA1 in the administration block ⇒ an UNHANDLED one reaches the fail-closed guard and is dropped with one `unsupported_internal` (never delivered as a DM) — the property R-RA-19's "falls through to the guard" relies on | `lib/core/frame_codec.h:802-803`, `:913-916`; guard `node_mac_rx.cpp:≈2428` |
| the slot: `struct RemoteInbound { active, is_response, from (8-bit origin), len, body[inbox_max_body=241] }`; `_remote_inbound` UNCONDITIONAL (`node.h:2905`, ~246 B on EVERY profile incl. client-only mobiles); drained by `take_remote_inbound()` (`node_mac.cpp:874-879`) | `lib/core/node.h:154-163` |
| the consumer (fw_main main loop): `is_response` → print `[rcmd <from>] …` (sealed-open + floor-resync + binary-TLV arms under `MR_FEAT_REMOTE_MGMT`, plain print otherwise) = the legacy ISSUER (client-like) side; else `remote_exec(from, body, len)` = the legacy ACCEPTOR side (inert stub when REMOTE_MGMT 0, `firmware_remote.cpp:159`) | `src/fw_main.cpp:1670-1695`, `src/firmware_remote.cpp:91/159` |
| ⚠ THE ROLE TENSION 1b MUST RESOLVE: today a STATIC node (REMOTE_MGMT 1) is BOTH issuer and acceptor (USB `rcmd` → REMOTE_CMD out; REMOTE_RESP back → printed). Under R-RA-8 a static/gateway build is ACCEPT-only (CLIENT 0). If 1b gates REMOTE_RESP staging on `MR_FEAT_RADMIN_CLIENT` alone, every static node's legacy `rcmd` loses its responses (they fall to the guard) — a behaviour change on metal (bench rcmd steps) that contradicts "bodies remain behaviour-identical". ⇒ the brief must gate the LEGACY bodies as `CLIENT \|\| MR_FEAT_REMOTE_MGMT` (RESP) and `ACCEPT \|\| MR_FEAT_REMOTE_MGMT` (CMD) for as long as the legacy switch lives (Slice 9 deletes it), while the ENTRY POINTS are role-named now. Owner/Author decision §4.1 | derived from the two rows above |
| native compiles `{CLIENT 1, ACCEPT 1, REMOTE_MGMT 1}` ⇒ a compile-time `#if` per role cannot produce the "role disabled" arms inside the one native binary. "Four role-by-type combinations" needs a routing decision that takes the two capabilities as VALUES: e.g. a pure `constexpr`/inline `radmin_rx_owner(type, client_on, accept_on)` (header-visible, no state) called by the RX arm with the macro values; the native test calls it with all four `{client,accept}` combinations × {CMD, RESP}; production behaviour under the real values is proven by the corpus. ⛔ Not a runtime gate/flag in production paths, not a test-only macro override | design question §4.2 |
| existing native coverage of the arm: `test/test_node_r3.cpp:6405` "rcmd: a REMOTE_CMD DM STAGES into the inbound slot … take drains it; a 2nd-while-pending drops" (+ :3322/:3351 typed sends), `test_custody_relay_f.cpp:1296` (RESP as an internal type in a loop), `test_dual_layer.cpp:4768` (custody reportability of REMOTE_CMD), `test_data_type_{namespace,audit_a0}.cpp` (type table) — these must stay GREEN unchanged (behaviour-neutral) | those TUs |
| corpus: the sim compiles `lib/core` as host `{1,1,1}` ⇒ both owners present ⇒ every REMOTE_CMD/RESP flight stages exactly as today ⇒ 36/36 predicted; a throwaway census (0h idiom) counts REMOTE_CMD/RESP arrivals per stream BEFORE (expected: few; 0e's characterization used them) | `simulation/`, my 0h census idiom |
| mutation batteries owning the receiver file: `b161rx b251rx b159rx a0rx sliceBrx sliceGrx` (`tools/probe_ui_model_mutations.py` TARGET_SRC) — `a0rx` (the addressed if-chain's missing default arm) and `sliceBrx` (the guard's predicate + placement) own exactly this region; 1b adds its own controls into them (no new cross-file target); all six run in full (0h precedent) | that harness |
| board effect: gateway (ACCEPT) + heltec_mobile (CLIENT): with the legacy-widened gates (§4.1) both bodies still compile on both boards ⇒ flash ±small, RAM ±0; ⛔ removing `_remote_inbound` from client-only builds is NOT 1b (design: the accept-side staging RAM leaves client builds in Slice 5) | design §19 1b |

## 2. Shape the brief must pin

- Two entry points (names the coder's): `rx_remote_cmd_accept(pa, ui)` / `rx_remote_resp_client(pa, ui)` — each contains the
  legacy staging body byte-for-byte (shared helper for the common staging is fine as long as the drop-full emit,
  the clamp shape, `from = pa.origin`, `is_response` and `become_free` are unchanged); the RX chain selects the
  owner by the pure routing decision (§1 row 6); an un-owned type takes NO arm and reaches the fail-closed guard.
- Gates during the legacy period (§4.1): CMD body compiled iff `MR_FEAT_RADMIN_ACCEPT || MR_FEAT_REMOTE_MGMT`, RESP
  body iff `MR_FEAT_RADMIN_CLIENT || MR_FEAT_REMOTE_MGMT` — on every REAL build today both are true (static/gateway:
  ACCEPT 1 + REMOTE_MGMT 1; mobile: CLIENT 1; host: all 1) EXCEPT a mobile's CMD (ACCEPT 0, REMOTE_MGMT 0) → the
  ONE behaviour delta: a REMOTE_CMD addressed to a mobile is dropped at the guard (one scalar emit) instead of staged
  and fed to the inert `remote_exec` stub. Externally identical (no response either way); the brief states it and the
  evidence measures it on `heltec_mobile` (flash/RAM) — it is not corpus-visible (host {1,1}).
- The four native arms: `radmin_rx_owner` × {CMD, RESP} × {client,accept} ∈ {00,01,10,11} → owner/none; plus the
  production RX drive: CMD staged, RESP staged (native), the guard drop for an unowned type via the pure decision.
- Prediction-first: BEFORE census of REMOTE_CMD/RESP arrivals per stream; predicted 36/36 identical; AFTER compare.
- Comments: the arm's header comment (":OTA remote diagnostics: STAGE for the main loop") gains the ownership
  statement + the pass-2 S3 obligations for later slices (SOURCE_HASH requirement, 32-bit key, refuse-not-clamp,
  client-build RAM) — as "MISSING/deferred, by design" in-source (mark done-vs-missing IN CODE).

## 3. Gate and fence

- Fence: `lib/core/node_mac_rx.cpp` (the arm → two entry points + the routing decision), `lib/core/node.h` ONLY if the
  entry points need declarations (no state, no layout change — `sizeof(Node)` unchanged on all three ABIs), the
  native test TU(s) for the four arms, the six rx batteries' new controls, evidence. ⛔ No `src/` (fw_main's consumer
  unchanged), no frame/codec, no `mr_features.h` (Slice 1 owns it), no `RemoteInbound` change, no NV.
- Gate: native (+N cases, PIN re-sync); lus REBUILDS → 36/36 byte-identical (prediction written first); ABI unchanged;
  boards: gateway ±0 RAM, flash small attributed; heltec_mobile ±0 RAM, flash small (the CMD body compiled out);
  census; all probes at pins; inventory unchanged (no `src/`); the six rx batteries + any historical/dependency
  battery (state both selectors); checkers; `git diff --check`. Bench: NONE (R-RA-19 row = none) — but the legacy
  `rcmd` USB flow on a static node must still round-trip: cite the existing bench rcmd Part as the standing metal
  check that 1b must not break (M2: no new part unless the brief adds a metal-only behaviour).

## 4. Owner decisions — RULED 2026-09-06 (R-RA-27): 4.1 NO legacy widening (strict R-RA-8 owners; static nodes' legacy `rcmd` responses become unowned guard drops until Slice 9 — accepted, not deployed); 4.2 pure routing decision — adopted; 4.3 mobiles ignore remote commands — adopted. ⚠ §2's "Gates during the legacy period" paragraph is SUPERSEDED by 4.1 (kept visible).

- **4.1 Legacy widening of the role gates** (`ACCEPT || REMOTE_MGMT` / `CLIENT || REMOTE_MGMT`) until Slice 9 — the only
  reading that keeps the static node's legacy `rcmd` issuer working AND names the owners now. Recommendation: adopt.
- **4.2 The pure routing decision** as the mechanism for the four native arms (no runtime gate, no test macro).
  Recommendation: adopt; the brief names the function shape.
- **4.3 The mobile CMD delta** (guard-drop instead of stage-to-inert-stub): accept as the ruled fail-closed
  behaviour, stated in the brief and measured on `heltec_mobile`.

Starting pins = the Slice 1 closure commit's (native, ABI, boards, s18, lus, tools, probes) — record at brief time.
