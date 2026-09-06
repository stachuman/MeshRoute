<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 3 — Quality-Agent pre-check ledger (2026-09-06): target identity, ACL and USB provisioning

Authority: design §19 item 3 (`docs/superpowers/specs/2026-08-23-remote-admin-independent-rpc-design.md:1850-1853`),
§19.1 row 3 ("target identity/ACL storage and USB provisioning owners | zero remote events, 36/36 unchanged; ruled
pair | Bench Part 55a"), §6.3 (:370-395), §6.4 (:397-417), §6.5 (:419-448), §6.6 (:450-465), §12.1 physical-only
authority (:1210-1222), §17 (:1573-1621), §19.2 (the store/regen/USB-only controls); rulings R-RA-6 (the four stores in
the keyring idiom, ledger :73-83), R-RA-8 (accept = static + gateway, :86-101), R-RA-21 (the `physical` authority
class, :296-318). Verified at `HEAD 231e1be` ("slice 2"). Hypotheses, not authority.

## 0. What Slice 3 is, in one line

On ACCEPT builds, land the two target-side stores — `/mradmid` (one 32-byte administration seed) and the fixed
ten-slot `/mracl` (controller public key + role per slot) — in the `/mrteams` keyring idiom, with the local USB-only
verbs that create the first owner, list/add/set/remove ACL rows, show/generate/rotate the administration identity and
recover a corrupt store; prove `regen`/`leave` preserve both and `factory_reset` erases both. NO remote execution, NO
session, NO codec consumer ⇒ zero remote events, 36/36 by construction, no `wire_version` change, no Node member.

## 1. Source facts — the persistence idiom Slice 3 must copy (the frozen template, R-RA-6)

| element | the `/mrteams` authority | anchor |
| --- | --- | --- |
| record | `struct TeamKeyBlob { magic; version; count; TeamKeyRecord rec[4]; }`, own magic `'MRK1'`, version **equality** (a bump REJECTS the old record ⇒ keyless, never wrongly keyed), NAMED `reserved[]` padding so a whole-record `memcmp` is deterministic, `static_assert`s on `sizeof`/`alignof` that EVERY board compiles (= the per-ABI pin; the ABI probe deliberately does not duplicate them) | `src/device_nv.h:295-320`; `tools/probe_board_abi.py:167-175` |
| slot | `inline constexpr Slot kSlotTeams { "/mrteams", "mr", "teams" }` — the `"mr"` namespace / a plain InternalFS file ⇒ `factory_erase()` (whole-namespace `clear()` on ESP32, full `InternalFS.format()` on nRF52) erases it with ⛔ zero new code; `regen` (writes `/mrid` only) and `leave` (rebuilds `/mrcfg` only) cannot touch it | `device_nv.h:395-427`, `:867-876`; `src/firmware_commands.cpp:672-683` (`do_regen`); `src/firmware_config.cpp:2397-2409` (`handle_leave` `b = mrnv::Blob{}`) |
| four-state read | `TeamKeyRead { ok, absent, invalid, io_failed }` from `SlotIo{backend_failed, oversize}` + `kSlotAbsent = -1` + `blob_valid_exact`; `invalid` and `io_failed` are BOTH "unreadable" for a write (`team_key_read_unreadable`), and a non-ok read may leave a PARTIAL record in `out` ⇒ every caller re-inits | `device_nv.h:448-497`, `:623-640`, `:1074-1083`; `src/firmware_team_keyring.h:441-443` |
| store seam + service | `ITeamKeyStore{load, save}` (host-fakeable, write-COUNTING tests) + `TeamKeyringService`: refuse → load → refuse unreadable → seed an absent store in RAM → compose ONE candidate → byte compare (identical ⇒ ZERO writes) → AT MOST ONE save → verdict; a FULL store refuses loudly and evicts NOTHING; `SecretWipeGuard<T>` (`crypto_wipe`, scope-guarded, hoisted so a test can observe it) on every transient that carries a secret | `firmware_team_keyring.h:96-135`, `:461-465`, `:507-560`; `test/test_firmware_team_keyring.cpp:1-70` |
| boot restore | the service's verdict GOVERNS the live state: every non-installing arm CLEARS (never "declines to act"); a half-committed transaction is detected by a committed WITNESS and boots keyless | `firmware_team_keyring.h:563-610` |
| what the fake cannot see | `write_slot` on nRF52 is `remove()` THEN `open/write` (`:846-855`) — ⛔ NOT atomic: a power cut between the two leaves the record ABSENT (not corrupt); "a save that reports failure may have written PARTIALLY" ⇒ no console text may say "nothing was written"; power-cut behaviour is METAL-ONLY (B193 closed for the keyring on Part 20.5, the ACL owes its own line — Part 55a) | `device_nv.h:846-855`; `firmware_team_keyring.h` header §"THE LIMIT OF EVERY CLAIM"; register B193 |
| corruption self-heal | `mount_or_repair()` probes a FIXED file list `kFiles[] = {/mrcfg,/mrid,/mrpeers,/mri_dm,/mri_ch,/mrfault}` and, on ANY corrupt CTZ, `format()`s the WHOLE FS — `/mrjoin`, `/mrteams`, `/mrui` are deliberately NOT in that list (a corrupt record of their own reads as `io_failed`/`invalid` and refuses; it does not trigger a reformat). ⚠ A reformat triggered by one of the six listed files WIPES `/mradmid` and `/mracl` too — the file's own comment already flags "identity-preservation across a corrupt-format is a flagged later refinement" | `device_nv.h:877-905` (`kFiles` :892), `:230`, `:411`, `:424` |
| host arm | native has NO backend: `read_slot` = absent, `write_slot` = false, `factory_erase` = true; `mrrng::fill` on the host writes ZEROS ("degenerate-on-purpose") | `device_nv.h:986-999`; `src/device_rng.h:44-80` |

## 2. Source facts — what exists to build on, and what must NOT be touched

| fact | anchor |
| --- | --- |
| `Identity{seed[32], ed_pub, ed_secret[64], x_secret, x_pub, key_hash32}` (196 B) from `identity_from_seed(out, seed)`; the seed is the ONE secret to persist (design §6.2/§6.3, R-RA-6) — never a hand-maintained public/private pair that can disagree | `lib/core/identity.h:34-45` |
| an all-zero seed/scalar refusal EXISTS for the team key (`team_channel_key_derive`: `all_zero32` → refuse, "dead RNG") — but `do_regen` has NO such guard and the host RNG returns zeros ⇒ a `/mradmid generate` on host would mint the all-zero seed unless the verb refuses it (C2) | `lib/core/identity.cpp:52-66`; `firmware_commands.cpp:672-683` |
| the LEGACY single-admin state stays UNTOUCHED in Slice 3 (Slice 10 removes it, its own NV-version slice — design §6.5 last paragraph, §17): `Blob::admin_pubkey/admin_counter_floor/admin_provisioned` (v20 fields of `/mrcfg`), the `Node` mirrors + accessors under `MR_FEAT_REMOTE_MGMT`, `g_admin_id` (fw_context.h, the ISSUER side), `handle_password/unlock/lock` and their `dispatch` arms | `src/device_nv.h:110-112`; `lib/core/node.h:103-110`; `src/fw_context.h:117-121`; `src/firmware_config.cpp:2411-2437`; `firmware_commands.cpp:1204-1208`; `fw_main.cpp:829` |
| boot order today: `mount_or_repair` (:638) → `load_id` + `identity_from_seed` (:814-822) → `admin_load` (:829) → … `peer_store_restore` (:933) → `preset_boot_restore_console` (:944). A `/mradmid`/`/mracl` boot step has a natural slot beside `:829`; fw_main.cpp is compiled by NO automated gate (only the console-sink probe's structural pins S21-S23 and the BLE-guard extraction read it) | `src/fw_main.cpp`; `tools/probe_console_sink/structural.py:237-301` |
| the ten codec session slots are `0..9` (`kRemoteSlotSessionMax = 0x09`) — the ACL's ten slots ARE those wire handles (design §6.5 "slot numbers are stable wire handles"); the two tens must agree by a `static_assert`, not by coincidence | `lib/core/remote_codec.h:52-55` |
| `parse_hex_exact`-style decoder ("EXACTLY 2*n hex chars into out[0..n)") already serves `peerkey <hex64>` — reuse it for `acl add … <hex64>` (U1) | `lib/console/console_parse.cpp:75`, `:170` |
| NO console verb prints this node's FULL 64-hex `ed_pub` today (`whoami` prints id/hash/name only; the full key reaches the companion only through the status JSON writer); the USB first-owner exchange (design §6.4 step 2/5) needs the target's full administration public key and the controller's full `self` key on a console | `firmware_commands.cpp:959-966`; `lib/console/console_json.cpp:582` |
| native compiles NO `src/*.cpp` (`test_build_src = no`) but has `-I src` ⇒ pure `src/*.h` units are testable with fake stores; the console-sink probe host-compiles the REAL `dispatch()` router against fakes (720 checks) and extracts the BLE guard from `fw_main.cpp`; `tools/probe_inbox_verbs` drives `dispatch()` directly | `platformio.ini:78, :89`; `tools/probe_console_sink/run.sh:63, :279-287` |

## 3. Source facts — the transport seams, and the ONE established shape for a USB-only verb family

| fact | anchor |
| --- | --- |
| `dispatch(line, len, Print&)` has NO transport parameter; `exec_console_line(line, len, LineFormat fmt, …)` knows `text` (USB) vs `json` (BLE) but ⛔ "OWNS NO COMMAND-NAME SPECIAL CASE" — the transport-specific NAMED refusals "stay in their transports" | `src/firmware_commands.h:117-160`; `firmware_commands.cpp:1232` |
| the precedent for a console-only family is `help`: BLE refuses every spelling in `ble_dispatch_line` BEFORE the seam with the named envelope `write_err(out, cap, "help", "console_only")` (`peers all` likewise); the refusal is EXECUTED by `ble_guard.py`, which extracts the guard's condition from the real `fw_main.cpp` — anchored on that UNIQUE call text (two matches = refusal) — and runs it against the same lines the router owns; the inventory records `help` as its OWN top-level surface with `transports = "serial"`, proven by `reached_from` | `src/fw_main.cpp:573-574`; `tools/probe_console_sink/ble_guard.py:33-40`; `tools/gen_command_inventory.py:110-127` |
| the same file carries the standing note "*reboot/regen/ota/factory_reset are reachable here [BLE] too … flag for review if the console should stay USB-only*" — Slice 3 does not resolve that note (C1); it adds ONE new family to the BLE refusal set | `fw_main.cpp:550-552` |
| R-RA-21 `physical` = "first-owner/recovery/root-identity operations on a managed target … local USB-only in v2"; design §12.1 "internal service seams may be transport-neutral, but a future BLE caller is not authorized until it supplies a separately reviewed physical-presence signal"; §19.2 "merely adding a secured BLE caller without a physical-presence authority reddens the structural gate"; §6.4 "a secured bond/static PIN alone is not physical presence" | rulings :296-318; design :1210-1222, :2009-2010, :414-417 |
| the inventory's `authority` column is EMPTY by construction (the generator REFUSES a filled row); the owner's one-shot classification is Slice 6's gate ⇒ Slice 3's new rows land with an empty column, the inventory is REGENERATED (`--write` is legitimate here: anchors move by design) and the row count moves 177 → 177 + N, predicted first | `tools/gen_command_inventory.py:1-40`, `:832-840` |

## 4. Source facts — gates the brief must name explicitly

| gate | what Slice 3 changes in it | anchor |
| --- | --- | --- |
| capability ownership census | `ownership.py` compares the MULTISET of every `MR_FEAT_RADMIN_*` site per file against `APPROVED_SITES` (today exactly three files: `mr_features.h`, `node.h`, `node_mac_rx.cpp`); a new `#if MR_FEAT_RADMIN_ACCEPT` arm in `firmware_commands.cpp` / `fw_main.cpp` / a header = RED until the census is extended ⇒ the brief PRE-AUTHORISES the exact new site strings (and the pure headers stay UNGATED per the B255 idiom: only the INSTANTIATION and the console surfacing are compiled out) | `tools/probe_features/ownership.py:62-90`; `src/firmware_commands.h:24-40` |
| feature matrix | unchanged (no new macro); the ACCEPT cells are `board_gateway_*` + `board_static_*` + both host cells; CLIENT-only cells (`board_mobile_*`) must compile the verbs OUT | `tools/probe_features/envmap.py:30-40` |
| board pair | `gateway` is the ruled ACCEPT board (`heltec_mobile` = CLIENT): the new records' `sizeof` and the verbs' flash land on `gateway`, `heltec_mobile` must be RAM/flash-identical except the compiled-in-but-unreferenced pure headers (predict: heltec_mobile ±0 both) | `tools/measure_board.py` pair |
| ABI | new NV records get UNGUARDED `static_assert`s in `device_nv.h` (the per-ABI pin, the `TeamKeyRecord` idiom) — NOT a `PINNED` entry (the probe's own note says why); `sizeof(Node)` unchanged (no Node member: the administration identity is `src/` state) | `device_nv.h:318-320`; `probe_board_abi.py:167-175` |
| mutation | per-file targets exist for `device_nv.h` (`devicenv`) and `firmware_team_keyring.h` (`teamkeyring`); NONE for `firmware_commands.cpp`/`fw_main.cpp` (compiled by no gate). New pure headers need NEW targets; the changed-source selector adds `devicenv` (records + typed wrappers) in full; the dependency selector: `teamkeyring` ONLY if a shared helper is touched (it must not be — U1 says reuse `SlotIo`/`blob_valid_exact`/`SecretWipeGuard`, not edit them) | `tools/probe_ui_model_mutations.py:113-212` |
| console-sink probe | new `dispatch` arms are exercised by its 720-check corpus on the six profiles + ownership (router vs parser intersection EMPTY) + the BLE-guard rows: the physical family's refusal must become EXECUTED rows, which means `ble_guard.py` learns a SECOND extraction anchor (today it refuses on anything but the one help call) — a tools change the brief pre-authorises, with the pins re-derived (structural 29, BLE-guard 212, controls 58 all move) | `tools/probe_console_sink/run.sh:95-103`, `ble_guard.py:33` |
| inventory | +N rows (new surface(s) with `transports="serial"` for the physical family, `"serial,ble"` for anything BLE may run — see §6.4), `--write` then `--check`; the 0g help oracle (`primary_names`) gains the new primary verb name(s) on accept profiles ⇒ `src/firmware_help.h`'s bare index moves too (49 → 49 + k on full builds; the probe's per-profile counts move; predicted first) | `gen_command_inventory.py:110-170`; `src/firmware_help.h` |
| corpus / simulator | inert by construction: `src/` is compiled by neither the simulator nor native; NO simulator edit; 36/36 byte-identical, `lus` identical, 0 build actions expected on a forced rebuild (the recompile control) | design §19.1 row 3 |
| bench | **Part 55a** (reserved by §19.1; the script's last part is 62): target-side first-owner USB exchange + local recovery on real flash — power-cut across the two-step `write_slot`, the `factory_reset confirm` erasure of BOTH records, `regen`/`leave` preservation across a reboot, a corrupt-record boot (`io_failed`/`invalid` reported, no reformat), and the BLE refusal envelope over real NUS | `docs/2026-07-31-bench-test-script.md` (Part 59 is the template shape) |

## 5. Shape the brief must pin

- **Records** (`src/device_nv.h`, the `/mrteams` idiom): `AdminIdBlob { magic 'MRA?' ; version = 1 (EQUALITY); seed[32]; reserved }` and
  `AclBlob { magic; version = 1 (EQUALITY); AclRow rec[10] }` with `AclRow { ed_pub[32]; role u8 (0 = empty, 1 = operator,
  2 = owner, anything else = the record is INVALID); reserved[3] }` ⇒ 36-byte rows, 8-byte headers, `static_assert`s per
  ABI; `kSlotAdmid { "/mradmid", "mr", "admid" }`, `kSlotAcl { "/mracl", "mr", "acl" }`; two more four-state typed
  wrapper pairs (`load_admin_id/save_admin_id`, `load_acl/save_acl`) with NO read-before-write coalescing (the stated
  asymmetry); `static_assert(kAclSlots == kRemoteSlotSessionMax + 1)` binds the ten ACL slots to the codec's ten.
- **Services** (new PURE headers under `src/`, one per store, `IAdminIdStore`/`IAclStore` seams, `SecretWipeGuard` on
  every seed-bearing transient): the ACL transaction = load → classify → candidate copy → FULL validation (role domain,
  no duplicate key, no all-zero key, count/holes as stored — "empty entries are not compacted") → durable save → verdict;
  duplicate key REFUSED; full ACL REFUSED loudly (10 rows, never evicts); the LAST owner cannot be removed/demoted and a
  slot cannot remove/demote ITSELF — both refusals belong to the service now, even though the "own slot" of a LOCAL
  USB call is "none" (the check takes an optional acting slot so Slice 6's remote caller reuses it, never re-implements
  it); `absent` / `invalid` / `io_failed` are three verdicts; an unreadable (`invalid`/`io_failed`) store costs ZERO
  writes on every ordinary verb; ONLY the explicit physical recovery verb may re-initialise an `invalid` store, and
  NOTHING may write over `io_failed` (nothing is known). The admin-identity service: generate (a status-returning
  entropy source, the B312 idiom — REFUSE an all-zero draw, never mint from it), rotate (explicit `confirm`, same
  path), show (public material only), and the boot restore (`ok` → derive the `Identity`, else NO identity and a loud
  status line; the verdict governs — no stale RAM identity survives a failed read).
- **Verbs** (accept builds only, `#if MR_FEAT_RADMIN_ACCEPT` around the INSTANTIATION/console surfacing; names the
  Author's): the design's `acl list | acl add <operator|owner> <hex64> | acl set <slot> <operator|owner> | acl remove
  <slot> confirm` (§6.6), plus the administration identity family (show / generate / rotate confirm) and the
  physical recovery (`… reset confirm`); first-owner = `acl add owner …` on an ABSENT record (physical). `acl list` and
  the identity `show` print slot, role and the FULL 64-hex public key on USB (the exchange needs it; §6.4) — the
  fingerprint line is §6.1. ⛔ No remote dispatch, no `CommandContext`, no authority check (Slice 6), no session
  invalidation (Slice 5): each is MARKED IN CODE as missing-and-why.
- **Transport**: the whole family is refused over BLE in `ble_dispatch_line` before the seam with a named
  `console_only` envelope (the `help` shape), executed by the generalized BLE-guard rows; the router arm itself stays
  transport-blind; the inventory carries the family as its own `serial` surface with `reached_from` proof.
- **Boot**: one restore step beside `fw_main.cpp:829` (the verdict printed on `mrcon`); `status`/`version` may surface
  "administration identity: present/absent/INVALID/io_failed" and "ACL: n owners, m operators / absent / INVALID /
  io_failed" — no key bytes, ever.
- **Tests** (native, fake stores, WRITE-COUNTED): every verdict × every read state; identical material ⇒ 0 writes;
  full ⇒ 0 writes, rows byte-identical; duplicate/all-zero/invalid-role refusals; last-owner + self-slot refusals;
  recovery only on `invalid`, never on `io_failed`; seed generate refuses all-zero; `identity_from_seed` of a stored
  seed reproduces the shown public key; `SecretWipeGuard` observed on a carrier that outlives it; the record layouts
  pinned by `sizeof`/`offsetof`; the ten-slot ↔ codec-slot assert. `regen`/`leave`/`factory_reset` preservation is
  proved STRUCTURALLY (the write sets of `do_regen` = `{/mrid}`, `handle_leave` = `{/mrcfg}` are console-sink-probe
  facts; the erasure is the namespace/format fact) plus the bench line — the brief must not claim a host test proves
  flash.
- **Prediction (before editing)**: native +cases; corpus 36/36 identical, `lus` identical, 0 build actions; `sizeof(Node)`
  unmoved on all three ABIs; `heltec_mobile` RAM/flash ±0 (attributed if not); `gateway` RAM = exactly the resident
  state the brief chooses (§6.2) + any static I/O buffer, flash = the verbs + services, both attributed; inventory
  177 → 177 + N; help index 49 → 49 + k; ownership census 3 files → the named set; probe pins re-derived.

## 6. Open points — 6.1 and 6.4 RULED (R-RA-29, 2026-09-06); the rest are Author decisions with QA's recommendation

- **6.1 ✅ RULED 2026-09-06 (R-RA-29: `fp = BLAKE2b-512(ed_pub)[:8]`, 16 lowercase hex, full key beside it on USB; pinned by an independent reference vector) — the question as put:** the fingerprint. §6.4 says both nodes "show full-key fingerprints"; §6.5 "reports slot, role, and a
  fingerprint"; §6.3 makes "the stable administration-key fingerprint" a Slice 4 TRUST SELECTOR; §6.6 defers the format
  "with the dispatcher slice". The only existing idiom is the 4-byte `fp` of `password` (the first four key bytes —
  grindable, and `key_hash32` is documented "NOT a security anchor"). A selector shared by Slices 3/4/6 must be frozen
  ONCE. **Recommendation:** freeze now, append-only: `fp = BLAKE2b-512(ed_pub)[:8]`, printed as 16 lowercase hex, and
  the FULL 64-hex key printed beside it on USB listings; a 32-bit routing hash is never a fingerprint.
- **6.2 (Author, with the RAM consequence stated) residency of the administration identity.** Slice 5 needs ECDH per
  authenticated request; a per-request `identity_from_seed` is an Ed25519 keygen on the RX path, a resident `Identity`
  is 196 B of secret in `.bss`. **Recommendation:** Slice 3 keeps NOTHING resident — every verb loads, derives, prints
  public material and wipes — so its `gateway` RAM delta is the I/O buffers only; residency is Slice 5's decision with
  its own attribution (C1). The boot restore then only VALIDATES and reports.
- **6.3 (Author) recovery is an explicit verb.** A corrupt (`invalid`) ACL is recovered ONLY by a confirm-gated physical
  verb that re-initialises the record; `acl add` on an `invalid` store REFUSES (never an implicit re-seed); `io_failed`
  refuses everything (the keyring rule: an unreadable store costs zero writes). Loss of every owner cannot happen
  remotely (last-owner rule) and locally is the same recovery verb.
- **6.4 ✅ RULED 2026-09-06 (R-RA-29: the WHOLE target family is refused over BLE in Slice 3; no listing verb widened) — the question as put:** exactly which verbs are `physical` vs `owner`-class local. R-RA-21: first-owner, recovery and
  root-identity ops are USB-only. Listing (`acl list`, identity `show`) exposes public material only — §6.2's
  controller twin allows `list/show` over secured BLE. **Recommendation:** in Slice 3 refuse the WHOLE family over
  BLE (one guard, one envelope, one inventory surface); widening `list/show` to BLE is a later one-line decision that
  needs the physical-presence discussion nobody has had. State it in the brief so the BLE-guard rows are the whole family.
- **6.5 (Author, tools) the BLE-guard generalization + the new inventory surface(s)** are pre-authorised tool edits
  with re-derived pins (structural, BLE-guard, controls, inventory rows, help counts) — all predicted before editing.
- **6.6 (Author, mark in code) the self-heal limit.** A `mount_or_repair` reformat triggered by one of the six probed
  files wipes `/mradmid`; §6.3 promises erasure by `factory_reset` only. Not Slice 3's to fix (the file already flags
  it as a later refinement) — the new store's header states it, and the register gets a row if none exists.
- **6.7 (Author, brief text) the non-atomic `write_slot`.** "Save failure leaves the old ACL active" is true for the
  LIVE (RAM) view and for ESP32 NVS; on nRF52 a failed second step leaves the record ABSENT after reboot ⇒ physical
  recovery, "never an invented owner" (§6.4) — the brief says so, the console never says "nothing was written", and
  Part 55a measures the power-cut case.
- **6.8 (numbering)** the design reserves **Part 55a** although the script has no Part 55; the Author adds it under that
  name. Register rows: none proposed yet; B312's real entropy provider is NOT closed by 6's synthetic seam (it stays
  open for the first RF integration).

Starting pins = the Slice 2 closure's (native 2640/115288/0; s18 `32afbf11`/269517/0; `lus` `b1b1d92c`; Node
222072/117912/148680; gateway 195844/512092/284; heltec_mobile 205684/1355292/328; tools 312; inventory 177; help 49;
console-sink 6/720/29/212/58; features 9/114/38 + ownership 19; inbox 91/22; fw-ui 223; custody 27/10; ble 40/8) —
re-record at brief time from `231e1be`. Bench: Part 55a (target side only; the controller half is Slice 4's Part 55b).
