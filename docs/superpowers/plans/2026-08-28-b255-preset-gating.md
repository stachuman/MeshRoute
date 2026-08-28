<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B255 — gate the preset catalog on `MR_FEAT_OLED` · dispatch brief · 2026-08-28

**Status: DISPATCHED — ★ owner-ruled (b) 2026-08-28.** Authority: the register row **[[B255]]**
(`docs/2026-07-30-open-bug-register.md` — read it in full: QG's ELF evidence is `preset_catalog()::cat` in
gateway `.bss` at exactly 0x46c = 1132 B; the ruling: *"gate the catalog, boot restore, status/help and the
`ui preset` verb on `MR_FEAT_OLED` — a headless gateway cannot select these presets, and a uniform-but-
unusable command surface is not worth 1.1 KB on the tightest board; `gateway_heltec*` retains the feature
(OLED enabled)"*). Context: the UI-10/11 arc's landed P1/P2/P3 (spec
`docs/superpowers/specs/2026-08-25-ui10-11-preset-catalog-spec.md`). ⛔ NO DEVICE CONTACT.

## Scope (the ruling, made concrete)
- Gate on `MR_FEAT_OLED`: the live `PresetCatalog` instance + its device bindings
  (`firmware_commands.cpp:160` area) · `preset_boot_restore_console()` and its `fw_main` call · the `ui`
  dispatch arm / `handle_ui` · the `cfg`/`status` presets surfacing · the hierarchical help entries. Follow
  the tree's established `MR_FEAT_*` stub idiom (grep `MR_FEAT_OLED` in `firmware_commands.h` — the
  `ui_emergency_active()` inline-false stub is the local precedent): a gated-out verb answers the ESTABLISHED
  loud-unsupported shape, ⛔ never silence, ⛔ never a new lexeme (find the existing unsupported-verb answer
  and reuse it; if none fits, STOP and report).
- ⛔ The PURE code (`firmware_ui_presets.h`, `firmware_ui_preset_verbs.h`, the NV record in `device_nv.h`)
  stays ungated — native tests and OLED builds compile it unchanged; only the INSTANTIATION + surfacing gate.
- ⛔ No behaviour change on any OLED env (`heltec_v3/v4`, `*_mobile`, `gateway_heltec*` — all keep the
  feature). The change is ONLY that `MR_FEAT_OLED=0` envs (`gateway`, `xiao_*` non-OLED, `production`?) drop
  the catalog instance and the verb surface. ⚠ Check `/mrui` interplay: a `MR_FEAT_OLED=0` node never creates
  the record; factory reset's namespace erase already covers any stale one — verify, state, don't add code.
- W49's wiring pin (the `ui` dispatch arm) will need its expectation reconciled with the new guard — the
  probe must still pin the arm on OLED-shaped source while not failing the honest gated form; re-anchor
  deliberately, controls stay RED.

## Gate
- The reclaim proven the ruled way: the **B246 probe's T column flips to `-` on gateway** for the preset set
  (re-pin with the derivation written — the T pins move deliberately), AND the per-board RAM diff — ⚠ SCOPED
  board exception: `gateway` before/after with the landed deterministic runner (compute only, no flashing) —
  expected ≈ **−1132 B `.bss` + the guards**, attributed; `heltec_mobile` byte-identical (measure it).
- Native (RUN the binary) — the suite compiles the pure code unchanged; counts stay unless a gating case is
  added (a case pinning the gated-out shape is WANTED — the unsupported answer, zero instance) with the
  derivation written. Touched-target batteries per `TARGET_SRC` (full pass each). Both probes green (W49
  re-anchor included; `probe_board_abi` re-pinned T column, its controls still RED). `git diff --check`
  clean. `git diff -- lib/` EMPTY ⇒ s18-inert by construction (state it).
- ⛔ NEVER `git commit`/`git add`/`git checkout --`. ⛔ No docs (register/spec/BASELINE = supervisor's on
  PASS), no `tracker.md`, no `platformio.ini` (the gate is source-side `#if`, not env flags — if you find it
  genuinely needs an env flag, STOP and report). No pollers; never pipe the runner. Metal residue: one line
  if anything is host-unreachable (likely none — a compile-out) — say so.

## Report
The gated sites · the loud-unsupported shape reused (named) · the gated-out native case · the T-column
re-pins with derivation · the gateway RAM reclaim (runner manifests, attributed) + heltec_mobile
byte-identical proof · W49's re-anchor · battery/probe/native results · exact final `git status --short`.
