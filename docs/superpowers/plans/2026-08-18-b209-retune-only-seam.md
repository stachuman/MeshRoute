<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# §B209 — a retune-only provisioning PHY seam · dispatch brief · 2026-08-18

**Status: DISPATCHED 2026-08-18. Slice 1 of 4 in the QG-ruled [[B207]] follow-up package**
(1 = this · 2 = [[B211]] preserve+report `sf_list` · 3 = [[B210]] truthful DAD reporting · 4 = [[B212]] command-specific
validation). ⛔ **Each micro-slice is INDEPENDENTLY GATED. Build slice 1 only.**
★ Role split: the QA-gate wrote this brief and verifies your claims at the code; **the OWNER runs QG and rules.**

⛔⛔ **HARDWARE: do NOT flash, upload, open a serial port or touch a device.** Native tests, host probes and `pio run`
builds only.

## Normative sources — read in this order
1. `CLAUDE.md` + `docs/CODE_GUIDELINES.md` (U1-U3, V1-V2, C1-C4, D1-D4, M1-M3).
2. ★ The **B209** row of `docs/2026-07-30-open-bug-register.md` — the registered defect, metal-confirmed, with the
   four required pins. **It is the authority on scope.**
3. `docs/superpowers/specs/2026-08-17-team-provisioning-transaction-design.md` **§1.3** — records how this defect
   entered (the spec's own prose conflated the call). ⓘ **That correction is already made; ⛔ do not redo it.**
   ⚠ The spec carries `CORRECTED v2/v3/v4` markers and collapsed `<details>` blocks of **withdrawn** wording —
   ⛔ never implement anything withdrawn.

---
## 1 — The defect (verified)
`DeviceProvLive::apply_phy` (`src/firmware_config.cpp:1099-1108`) ends in `g_node.mobile_register_phy(phy)`, whose
contract is **three actions** (`lib/core/node.h:648`):
```cpp
void mobile_register_phy(const LayerConfig& phy) {
    adopt_mobile_phy(phy);                          // 1. retune            ← the only one provisioning wants
    mobile_request_home_service();                  // 2. AUTHORISE static-home attachment
    (void)_hal.after(0, kMobileDiscoverTimerId);    // 3. immediate DISCOVER
}
```
⇒ on a node with `mobile_autoregister=false`, a **team** PHY tail silently authorises **home** attachment:
`home_desired:true`, `attachment:"seeking"`, and repeated outbound J DISCOVERs. **Metal-confirmed on both bench
nodes.** ★ The framing that matters is the register's: this **authorises**, it does not merely "start seeking".

---
## 2 — ★★ TWO FACTS THAT SHAPE THE FIX (both verified — re-verify per V1)

**(a) `adopt_mobile_phy` is PRIVATE.** `lib/core/node.h:1748`, inside the `private:` section that opens at `:1621`.
⇒ **you cannot simply swap the call — a new PUBLIC retune-only seam on `Node` is required.**

**(b) ⛔⛔ THIS SLICE TOUCHES `lib/core`, WHICH THE LAST THREE DID NOT.** B207 was `src/`-only and therefore
**corpus-inert by construction**; ⇒ that argument **DOES NOT APPLY HERE**. `git diff -- lib/` will be non-empty, so
**D2 is fully live**: the s18 keystone must actually reproduce, `sizeof(Node)` must be re-asserted, and per-board
RAM/flash must be attributed. **Prove inertness, never assume it.**

---
## 3 — What to build
1. **A new PUBLIC retune-only method on `Node`**, beside the three existing wrappers (`node.h:647-649`), delegating to
   `adopt_mobile_phy(phy)` **and nothing else**. Name it so it cannot be confused with registration
   (e.g. `mobile_retune_phy`) — ★ **the naming is half the fix**; `mobile_register_phy` reads like a retune and is not.
   ⛔ It must **NOT** call `mobile_request_home_service()`, **NOT** arm `kMobileDiscoverTimerId`, and **NOT** touch
   `_mobile_attach_state`.
   ⚠ **CORRECTED (QG): put it beside the neighbours INSIDE the `#if MR_FEAT_MOBILE` block that opens at `node.h:635`,
   and ⛔ do NOT invent a non-mobile stub.** This brief previously claimed *"the neighbours have `#else` no-op stubs"* —
   **they do not.** The `#else` at `:616` closes at `:632` and belongs to the *accessor* block (that is where
   `mobile_home_desired()`'s `return false` stub lives); `mobile_register_current/phy/scan` (`:647-649`) exist **only**
   under `#if MR_FEAT_MOBILE`. Match that.
2. **Point `DeviceProvLive::apply_phy` at it** (`src/firmware_config.cpp:1108`). That is the whole call-site change.
3. ⛔ **Leave `mobile_register_phy()` EXACTLY as it is** — it is the correct primitive for the explicit console/app
   verb (`handle_mobile`, `src/firmware_config.cpp:1429`), which must keep authorising and DISCOVERing.
4. ⛔ **`fire_dad` stays the separate, last, team-plane airtime operation.** Do not fold anything into it.

---
## 4 — Traps
- ⛔ **The discover timer is DUAL-PURPOSE.** `lib/core/node.cpp:588` arms `kMobileDiscoverTimerId` when
  `is_mobile && (mobile_autoregister || team_id != 0)` — partly *so team-DAD runs* (`test/test_node_join.cpp:2749-2753`).
  ★ **The transaction already fires DAD explicitly via `fire_dad` (step 6d), so the retune seam must not arm it** —
  but **pin 1 exists to prove team-DAD still succeeds.** Removing the arming from the wrong place breaks the team plane.
- ★ **An ALREADY-authorised session must survive untouched** (pin 3): `home_desired`, attachment state and the
  registration timers are **preserved, not cleared**. This is not "make provisioning clear the flag" — it is
  "make provisioning not touch it".
- ⛔ No wire, NV or `kVersion` change. **`sizeof(Node)` stays 221880**, `kCap` 91, no timer id allocated.
- ⛔ Do not edit `simulation/BASELINE.md`'s `^### 36/36 corpus` anchor table; read the keystone from it.

---
## 5 — The four pins (from the B209 row — normative)
1. **auto-OFF + team create/join WITH a PHY tail ⇒ team-DAD / local id succeeds, `home_desired == false`,
   attachment `dormant`, and ZERO mobile DISCOVERs.**
2. **explicit `mobile register freq=…` still enters `seeking` and DISCOVERs** (unchanged).
3. **an already-authorised manual/auto session remains authorised across a team PHY apply** — no second hidden
   authorisation, no reset.
   ⛔⛔ **AND IT MUST COUNT TIMER ARMS, OR IT PASSES VACUOUSLY (QG).** `Node::mobile_request_home_service()`
   (`lib/core/node_mobile.cpp:555-559`) is **idempotent** for a mobile that is already authorised/attached — it only
   sets `_mobile_home_desired = true` and promotes `dormant → seeking`. ⇒ **asserting `home_desired` and the attachment
   state alone passes BOTH before and after the fix and proves nothing.**
   ★ **Required: record the `kMobileDiscoverTimerId` ARM COUNT before and after the provisioning apply.** The fixed
   retune must add **zero** arms; routing back through `mobile_register_phy()` must add **one** and turn the test RED.
4. ★★ **a mutation routing the adapter back through `mobile_register_phy()` turns the regression RED.**

ⓘ **Prior art — useful, but ⛔ NOT SUFFICIENT (QG).** `test/test_node_join.cpp:1054` pins *"§autoregister — OFF team
member: team-DAD runs (ungated), but ZERO DISCOVERs to any host"*, which is the proof that **the FSM kick is not
load-bearing for team-DAD** and the reason this fix is safe. ⛔ **But it exercises BOOT-TIME behaviour: it never calls
`DeviceProvLive::apply_phy`, never processes a PHY tail, and cannot touch the new seam.** Extend the family (U1), and
add **BOTH** of the following:
- ★ **a native CORE test** — the retune-only seam leaves authorisation, attachment state and the timer-arm count
  unchanged, while an explicit `team_dad_fire()` still assigns the team id with **zero DISCOVERs**;
- ★ **a structural/integration probe** proving `DeviceProvLive::apply_phy()` calls **the new seam** — and mutating it
  back to `mobile_register_phy()` goes **RED**. (`tools/probe_prov_tx` is the natural home; it already reads the
  adapter.)

⛔⛔ **CONTROL RULE — CORRECTED (QG). The previous wording was IMPOSSIBLE and is withdrawn:** it said *"every check
needs a control that goes RED against the current tree"*, which **pin 2 cannot satisfy** — pin 2 is an *unchanged
positive control* and must already pass. ⇒ **the correct rule:**
- ★ **every DEFECT-SPECIFIC regression must FAIL** either against the current implementation or under a controlled
  mutation;
- ★ **unchanged positive controls (pin 2) must stay GREEN** — a positive control that goes red is a regression, not a
  proof.
ⓘ This project has recorded **ten** instruments that were green against the defect they were written to catch, which is
why the first half matters; over-generalising it into "everything must go red" is how the rule became incoherent here.
Ask of each check: *could it have come out otherwise?* Print match counts; restore sources and md5-verify.

### 5.1 — Two scope clarifications (QG)
- ⓘ **Pin 3 proves provisioning leaves the ATTACHMENT MACHINERY untouched. It does NOT prove that an existing home
  remains physically valid after a real PHY change.** ⛔ Any immediate invalidation / re-home policy is a **separate
  question and out of scope for B209** — do not design one here, and do not let a test imply one.
- ⛔ **Do not pin [[B211]]'s current `sf_list` collapse in these tests.** The seam's job is simply to **preserve the
  supplied `LayerConfig` fields**; the collapse is slice 2's subject and a test that encodes today's behaviour would
  have to be undone next slice.

---
## 6 — Gate (baselines measured on this tree)
native **1713 / 83937 / 0** · `probe_prov_tx` **12/12 + 16 controls RED** · `probe_board_ui` 120/120 + 14/14 + 52/52 +
**153** RED · `probe_firmware_ui` 229/229 · census **174 / 178 / 178**, `-Wswitch` 0, 326 objs/env · `lus` `b77cfd3d` ·
s18 keystone **`9868cad3` / 269905** · `sizeof(Node)` **221880** · ARM `handle_team` frame **888 B**.

1. `pio test -e native`, **then RUN `./.pio/build/native/program`** (the wrapper falsely prints "0 test cases").
2. Both UI probes + `probe_prov_tx`, with their control sets.
3. `warning_census.sh` at its pins.
4. ★★ **Four-step simulator proof — and here it is a MEASUREMENT, not a formality (§2b).** If any corpus row moves,
   ⛔ **STOP AND REPORT**; do not re-anchor.
5. **Six board envs**, RAM/flash attributed, classified by the `board =`/`extends =` chain. ⚠ **[[B206]]:** delete both
   `__DATE__` objects or clean-build, confirm each image holds exactly ONE build-timestamp string, and equalise the
   `GIT_REV` string **length** between arms.
6. **D2 explicitly:** `git diff -- lib/` is expected NON-empty here — report `sizeof(Node)`, `kCap`, and the per-board
   consequence.

**Report:** the new seam with `file:line` and why its name cannot be confused with registration · the adapter change ·
proof `mobile_register_phy` is untouched · the four pins each with its control and match count · native baseline →
after · probes · census · **the four-step corpus result** · per-board RAM/flash · the D2 answer · exact final
`git status --short` confirming nothing was committed · and the M1/M2 text you owe.

⛔ **Anything you cannot establish, say so plainly.** Report failures with output (D3). **Stop and report** rather than
improvising if: team-DAD stops firing on the auto-OFF path · a corpus row moves · `sizeof(Node)` moves · or the fix
cannot be made without changing `mobile_register_phy`.

⛔ **Do not touch** `docs/2026-07-30-open-bug-register.md`, `docs/2026-07-31-bench-test-script.md`, the B207 spec, or
`tracker.md` — report owed M1/M2 text instead.
