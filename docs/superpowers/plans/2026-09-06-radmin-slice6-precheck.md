<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 6 — Quality-Agent pre-check ledger (2026-09-06): the common dispatcher's validator, context, result and authority table

Authority: design §19 item 6 (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:2022-2027` —
"This slice cannot start until the complete generated authority table has received its separate owner
classification; missing or duplicate rows refuse the brief"), §19.1 row 6 ("common dispatcher/context/authority-table
consumers | zero remote events; all pre-existing local behaviour attributed; ruled pair | none", :2088), §12 + §12.1
(:1204-1363), §13 (the disruptive set, :1363), §17 (the legacy verbs Slice 9 deletes, :1708); rulings R-RA-1 (the table is
GENERATED, the classification is the owner's one-shot ruling), R-RA-7 (one validator: NUL/CR/LF + a length),
R-RA-14 (one validator, no legacy cap), R-RA-21 (the policy: open / operator / owner / physical; controller-local
never target-dispatchable; ambiguous refuses closed), R-RA-24′ (three named bounds: USB 1024, BLE 275, remote tail
201), R-RA-29/30 (the target and controller local families). Written at `HEAD a0ff994` while Slice 4 runs on the
main tree ⇒ the inventory in the tree ALREADY carries Slice 4's uncommitted rows; the ruled table is taken from the
Slice 4 closure's regenerated inventory, never from this snapshot. Hypotheses, not authority.

## 0. What Slice 6 is, in one line

On the transport-neutral seam, land (a) the ONE command-byte validator (embedded NUL/CR/LF refused; a per-surface
bound: USB 1024, BLE 275, remote tail 201), (b) the `CommandContext` / `DispatchResult` shape (transport, authority,
physical presence, request id; matched / unmatched / refused / scheduled / internal-failure as a TYPED completion),
and (c) the authority table — every generated inventory row carrying exactly ONE owner-ruled minimum authority,
consumed by the dispatcher as metadata — with NO remote request reaching it yet and local USB/BLE behaviour
unchanged except where the validator's refusal was separately ruled. **It is gated on your classification** (§6.1).

## 1. Source facts — the seam as it stands

| fact | anchor |
| --- | --- |
| the ONE seam: `LineExec exec_console_line(line, len, LineFormat fmt, Print& stream, char* reply, size_t cap)` — router-first (`dispatch`), then `parse_command` + `handle_peerkey`/`handle_peername`/`Node::on_command`; its header says in as many words: "⛔ NOT the future `DispatchResult`. `State` is a narrow, local completion … Remote authority, `CommandContext` and feature policy are Slice 6's and are deliberately absent here"; and "IT OWNS NO COMMAND-NAME SPECIAL CASE" | `src/firmware_commands.h:117-160`; `src/firmware_commands.cpp:1232` |
| a THIRD consumer exists: `ExecResult exec_command(line, len)` (UI-7, the OLED panel's typed path: parser → `Node::on_command`, no router, no `Print`) — the validator must sit where all three meet, or the panel bypasses it | `src/firmware_commands.h:245-250`; `src/firmware_commands.cpp:1677-1687` |
| `dispatch(line, len, Print&)` has NO context parameter; `bool` = matched only. The design's target shape is `DispatchResult dispatch(line, len, out, const CommandContext&)` with `{transport, authority, physical_presence, request_id}`; "the exact type layout is an implementation decision" | design :1206-1224; `src/firmware_commands.h:64` |
| TODAY'S INTAKE is per transport and NOT a validator: USB `static char line[1024]` — `\r` skipped, `\n` terminates, over-long line REFUSED loudly ("line too long (>1023)"), ⛔ an embedded NUL is NOT refused (the line is NUL-terminated for the debug cmds; `strncmp`-based arms stop at it silently); BLE `g_line[275]` — the same `\r`/`\n`/overflow idiom (`g_overflow` → loud), the same NUL blindness | `src/fw_main.cpp:1146-1175`; `src/device_ble.h:199, :296-303` |
| R-RA-24′'s three bounds: USB 1024 incl. NUL (unchanged), BLE **275** incl. NUL (`device_ble.h` derives it symbolically: `max(send 265, send_layer 274, remote 261) + 1`), remote tail **201** = the smallest authenticated carrier (depth-4 XL, 226) − 25, to be DERIVED from `remote_body_cap` + `kRemoteOverheadAuthExecute` (both exist since Slice 2), never a literal | rulings :421-452; `lib/core/remote_codec.h:159, :298` |
| the validator's caller set: `exec_console_line` (USB + BLE), `exec_command` (panel), and — later — the Slice 7b remote consumer with bound 201; ⛔ "three named bounds do not justify three validators" | design :1226-1229 |
| the generated inventory is the ONLY completeness authority (R-RA-1): `tools/gen_command_inventory.py` → `docs/superpowers/evidence/2026-09-04-radmin-command-inventory.md`, `authority` column EMPTY by construction (the generator REFUSES a filled row: `verify_rows`); rows are per SURFACE (`dispatch` / `help_command` / sub-verb handlers / `parse_command` / the two caller arms `ble_dispatch_line` + `service_console` / the legacy `remote_encode` + `remote_exec`) — so one SEMANTIC command appears on several surfaces (e.g. `status` on `dispatch`, `ble_dispatch_line`, `remote_encode`) | `tools/gen_command_inventory.py:1-40, :110-170`; the inventory doc |
| ⚠ THE DUPLICATE R-RA-21 NAMES IS STILL THERE: `peers` is emitted TWICE on `dispatch` (`:1389` `len == 5 && "peers"` and `:1390` `len > 5 && "peers "` — two arms, one verb) and twice on `ble_dispatch_line` (`peers` streams; `peers <args>` refuses `console_only`), beside a separate `peers \| all` sub-verb row ⇒ "its correction must merge a true duplicate or expose the semantic discriminator" BEFORE classification lands — a generator fix that is a PREREQUISITE of the brief, not part of the classification | `src/firmware_commands.cpp:1389-1390`; `src/fw_main.cpp:541-542`; design :1310-1314 |
| the classification granularity is the SEMANTIC command/sub-command, transport arms inherit: the `ble_dispatch_line` rows are JSON twins of `dispatch` verbs, `service_console`'s `peerkey` row is the USB envelope of the same parser command; `help` is local-only by construction (R-RA-29 shape); the legacy `remote_encode`/`remote_exec` rows and `rcmd`/`password`/`unlock`/`lock`/`password rotate` are Slice 9's deletions (§17) — classified "legacy, not target-applicable", never given a remote authority | the inventory; design :1708-1730 |
| §13's disruptive set is a SECOND attribute of a row (reboot / prep-restart / factory_reset / regen / OTA / crashtest / any retune-or-detach network/radio change → `scheduled` terminal, 7b) — the table is the natural place to carry it ONCE so 7b consumes it rather than re-deciding by name | design :1363-1380 |
| the physical class (R-RA-21) is already implemented as USB-only families: target `acl`/`admin-id` (R-RA-29, whole family) and controller `admin-key generate/import/export/remove` + every `admin-target` mutation (R-RA-30); their rows classify `physical` (target) / `controller-local` (never target-dispatchable) — the table must not contradict the shipped guards | `src/fw_main.cpp` (the two guards); rulings R-RA-29/30 |
| the a0-matrix idiom is the precedent for "a ruled table checked against code": `docs/superpowers/evidence/2026-08-29-custody-a0-matrix.md` + `tools/check_a0_matrix.py` (every enum member has a row; the current namespace table states every value) — the authority table can be checked the same way: ruled Markdown ↔ the firmware's `constexpr` policy table ↔ the generated inventory, all three agreeing row for row, with missing / duplicate / unclassified = refusal | `tools/check_a0_matrix.py`; `tools/check_data_type_literals.py` |

## 2. What the validator changes locally (the "separately ruled" behaviour delta)

| line | today | after Slice 6 | ruled by |
| --- | --- | --- | --- |
| a line with an embedded NUL byte (USB or BLE) | silently truncated at the NUL by the `strncmp` arms; the tail is never seen | REFUSED loudly by the validator before the router/parser (`> err …` on USB, `{"err":…}` on BLE) | R-RA-7 |
| an embedded CR/LF | cannot occur today (both intakes strip `\r` and split on `\n`) | the validator refuses them anyway (the remote path has no line assembly to strip them) — zero local delta | R-RA-7 |
| USB over 1023 chars / BLE over 274 | refused loudly at intake | UNCHANGED (the intake stays; the validator's USB/BLE bounds equal the intake's, so cap+1 can never reach it locally — the validator's bound is EXECUTED by the remote consumer and by native tests, and is PINNED equal to the intake's) | R-RA-24′ |
| the OLED panel's `exec_command` | parser only | validator first (the panel cannot produce NUL/CR/LF, so zero behavioural delta — the proof is the executed row) | R-RA-7 |

Everything else local is byte-identical: the router, the parser, every handler, every envelope. The console-sink,
inbox-verbs and firmware-ui probes are the executed proof, with their pins unchanged except the new validator rows.

## 3. Shape the brief must pin

- **Validator**: ONE pure function in a `lib/console` or `src/` pure header (`command_line_valid(line, len, bound)` →
  a typed refusal reason: `embedded_nul` / `embedded_cr` / `embedded_lf` / `too_long`), bound supplied per surface as
  a NAMED constant (`console_line_max_bytes_usb = 1024`, `_ble = 275` bound to `device_ble.h`'s symbolic derivation by
  `static_assert`, `remote_command_max_bytes = remote_body_cap(depth-4 XL) − kRemoteOverheadAuthExecute = 201` derived
  at compile/test time); called at the head of `exec_console_line` (both formats) and `exec_command`; cap+1 and every
  byte class executed natively; mutation-pinned.
- **Context/result**: `CommandContext{transport: usb|ble|remote; authority: local|remote_open|remote_operator|
  remote_owner; physical_presence; request_id}` and `DispatchResult{completed|unmatched|refused|scheduled|internal}`
  threaded through the seam WITHOUT changing any local output byte: local callers pass `{usb|ble, local,
  physical_presence = (transport == usb), 0}`; the router's `bool` becomes the typed result at the seam boundary
  (the existing `LineExec::State` maps 1:1 onto four of the five values; `refused` is new and unreachable locally
  until the table is consulted for a REMOTE context — Slice 7b/8).
- **Authority table**: a `constexpr` table in firmware keyed by the SEMANTIC (verb, sub-verb) with `{authority,
  disruptive}`; ONE ruled Markdown source (§6.1's document, owner-signed) → the generator fills the inventory's
  `authority` column from it (no guessing from names) → a checker (`tools/check_command_authority.py`, the a0-matrix
  idiom) proves the three agree: every target-applicable semantic row exactly once, exactly one authority, no
  unclassified / duplicate / orphan row, controller-local and legacy rows explicitly NON-dispatchable; the generator's
  `peers` duplicate merged or discriminated FIRST. The table has NO consumer that changes a local decision in Slice 6
  (local authority is `local`, which every row admits); its first remote consumer is 7b.
- **Fence**: `src/firmware_commands.{h,cpp}` (the seam), the validator header, the table header, `tools/gen_command_inventory.py`
  (+ its unit test; the `peers` merge; the `authority` column from the ruling), the new checker, `tools/probe_console_sink`
  (validator rows on every profile), `tools/probe_inbox_verbs` (the real seam with the validator), `tools/probe_firmware_ui`
  (the panel's `exec_command` path), the mutation batteries; evidence. NO handler edit, NO remote consumer, NO Node,
  NO wire, NO simulator edit (`src/` + `tools/` only ⇒ corpus inert, `lus` untouched).
- **Prediction**: native +cases; six probes' pins move by the validator rows only; inventory row count UNCHANGED
  except the `peers` merge (predicted exactly); help unchanged; boards: gateway + heltec_mobile flash + (the validator
  + the table), RAM ±0 (a `constexpr` table is flash); Node unmoved; 36/36 identical, `lus` byte-identical (no core edit).

## 4. Gates the brief must name explicitly

| gate | what Slice 6 changes in it |
| --- | --- |
| inventory | the `peers` duplicate merged/discriminated (row count −1 or the discriminator column, predicted); the `authority` column FILLED from the ruling (the generator's `verify_rows` inverts: an EMPTY authority on a target-applicable row now REFUSES); `--write` then bare + `--check` |
| new checker | `tools/check_command_authority.py` in the a0 idiom, in the tools unit sweep, with controls (a missing row, a duplicate semantic row, an unclassified row, a table/ruling disagreement each RED) — R-RA-21's "mutation controls for a missing row, a duplicate semantic row, and an unclassified row" |
| probes | console-sink: the validator's refusal envelopes on USB and BLE per profile + the byte-identity of every other line; inbox-verbs: the real seam with NUL/CR/LF/cap+1 lines on both arms; firmware-ui: `exec_command` refuses a NUL line |
| mutation | new per-file batteries for the validator and the table header; the seam's existing controls (`probe_inbox_verbs` C18-C21 shape) extended; dependency: none in core |
| boards | flash attributed on both; RAM ±0 predicted on both (no resident state) |
| corpus/sim | inert; no simulator edit; `lus` byte-identical with 0 build actions (the checked no-op) |
| metal | none (§19.1 row 6) |

## 5. Numbers that move (to be predicted from the Slice 4 closure)

Inventory rows: the closure's count (188 + Slice 4's rows) → −1 for the `peers` merge (or +0 with a discriminator);
help unchanged; console-sink structural/controls + the validator rows; inbox-verbs checks/controls + the validator
rows on both arms; tools sweep + the checker's tests; native + the validator/table cases. All pinned at brief time.

## 6. Open points — the ONE ruling this slice cannot start without, and the Author decisions

- **6.1 (OWNER — the gate of the slice) the one-shot authority classification.** R-RA-1/R-RA-21 make it your ruling,
  taken ONCE over the complete generated list. To make it a single pass, the companion document
  `docs/superpowers/plans/2026-09-06-radmin-authority-classification-proposal.md` lists EVERY semantic command and
  sub-command of the current inventory with a PROPOSED class (`open` / `operator` / `owner` / `physical` /
  `controller-local` / `legacy`) and a proposed `disruptive` flag, derived mechanically from R-RA-21's policy and §13,
  with the judgment calls marked ⚖. **Recommendation:** rule by exception — confirm the proposal as a whole and name
  the rows you change; the Author then lands the signed table and the generator consumes it. Rows Slice 4 adds are
  classified in the proposal from its brief (`admin-key`/`admin-target` = controller-local) and re-verified at closure.
- **6.2 (Author, prerequisite) the `peers` duplicate** — merge the two `dispatch` arms into one semantic row with the
  `all` sub-verb (and the same on `ble_dispatch_line`), or expose the discriminator; a generator fix with its unit
  test, before the classification is applied.
- **6.3 (Author) the validator's home and the bound constants** — a pure header both `src/` seams and the future
  core remote consumer can include (⇒ `lib/console/`, beside `console_parse.h`, is the layer that already serves both);
  `remote_command_max_bytes` DERIVED from Slice 2's authority, never typed.
- **6.4 (Author) the table's shape** — `constexpr` `{verb, sub_verb, authority, disruptive}` rows in a pure header,
  looked up by the seam (no consumer decision in Slice 6), checked by the new tool against the ruled Markdown AND the
  generated inventory; controller-local and legacy rows present with an explicit non-dispatchable class so a future
  remote consumer refuses them by TABLE, not by name.
- **6.5 (Author) the `disruptive` attribute** — carried in the same ruling so 7b's scheduling reads metadata (design
  §12 "policy is metadata … not a copied remote allowlist").
- **6.6 (Author) local refusal envelopes for the validator** — USB `> err bad_line <reason>` text, BLE
  `{"err":"line","msg":"<reason>"}` in the companion idiom; exact bytes pinned by the console-sink probe.
- **6.7 (numbering)** proposed rows follow Slice 4's and 5's allocations at closure.

Starting pins = the Slice 5 CLOSURE's if Slice 6 follows 5, or Slice 4's if you re-order (Slice 6 touches no core
file, so it can be coded in parallel with Slice 5 on disjoint files — `src/firmware_commands.*` is shared with
Slice 5's live-activation edits, so the merge gate would re-run the console-sink and inbox-verbs probes once).
Bench: none.
