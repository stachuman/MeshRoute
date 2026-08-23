// MeshRoute — src/firmware_provisioning_service.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §PROV-TX — ONE TYPED TEAM-PROVISIONING TRANSACTION. Spec:
// docs/superpowers/specs/2026-08-17-team-provisioning-transaction-design.md (v4, owner-ruled); defect [[B207]].
//
// ★★★ THE DEFECT THIS FILE EXISTS TO REMOVE, stated as the property it replaces: `handle_team` used to perform SIX
//     LIVE MUTATIONS — including `team_dad_fire()`, which SPENDS AIRTIME — BEFORE its `mrnv::save`, and its own
//     failure line said so: *"team is LIVE but NOT persisted — will revert on reboot"*. On an off-grid safety device a
//     `team new` whose flash write failed PRESENTED AS SUCCESS (the node beacons, teammates hear it) and then lost
//     membership at the next power cycle.
// ★★★ THE OWNER'S RULING (2026-08-17, reported form): VALIDATE AND STAGE WITH NO MUTATION → PERSIST A COMPLETE
//     CANDIDATE (with `team_local_id = 0` meaning DAD-pending) → ONLY THEN APPLY LIVE AND START DAD. A save failure
//     must leave live PHY, role, team, keys and NV unchanged AND SPEND NO AIRTIME. All three forms — `team new`,
//     `team <id>`, `team 0` — route through here.
// ⛔⛔ AND THE PRECISE FORM OF THAT GUARANTEE, because the loose form is a claim this code cannot make (spec §5.1):
//     **EXACTLY ONE WRITE IS ATTEMPTED, AND NOTHING IS APPLIED WHEN IT REPORTS FAILURE** — so no live domain moves and
//     no airtime is spent. ⛔ It is NOT a guarantee that a FAILED PHYSICAL WRITE LEFT THE STORED RECORD BYTE-INTACT: a
//     backend can fail *after* a partial write ([[B193]]'s open question), and neither a fake store nor any host test
//     can establish otherwise. ⇒ nothing in this file or its console voice may say *"no flash was changed"*.
//
// ★★ WHY A SIBLING OF `ConfigService` AND NOT A BRANCH INSIDE IT: `firmware_config_service.h:54` disclaims
//    provisioning explicitly ("provisioning is explicitly NOT a draft field"). What IS reused, unchanged, is
//    `ICfgStore` (U1) — one durable seam, one whole-record write, and the same "false = THE WRITE FAILED (nothing may
//    be applied live)" contract this transaction is built on.
//
// ★★ WHY THE DECISION LOGIC IS HERE AND NOT IN `handle_team`: `src/firmware_config.cpp` is compiled by NEITHER the
//    native suite (`test_build_src = no`) NOR the simulator (which compiles `lib/core` + `lib/console` only), and no
//    scenario ever runs a console verb. ⇒ logic left in that function is unreachable by EVERY automated gate. This
//    header is pure (no `Print`, no Arduino, no globals) so `test/test_firmware_provisioning_service.cpp` reaches all
//    of it, and `handle_team` shrinks to parse → request → render.
//
// ★★★ THE INVARIANT THAT MAKES THE GUARANTEE REAL RATHER THAN NOMINAL (spec §3.2):
//     **EVERY POST-SAVE OPERATION IS INFALLIBLE GIVEN A VALIDATED PLAN.** ⛔ No fallible key derivation, no
//     cross-check and no range test may happen after `store.save` returns. That is enforced STRUCTURALLY by the
//     `IProvLive` seam below, which exposes only a `void` key install — the three fallible core primitives
//     (`team_channel_key_adopt`, `team_channel_key_adopt_priv`, `team_channel_key_mint`) are not expressible through
//     it at all. ⓘ That was a real trap, not a hypothetical: v2 of the design named `adopt_priv` as the post-save
//     install on the strength of a COMMENT (*"adopt_priv cannot fail here"*, a scoped claim about an
//     already-canonical stored key) and `lib/core/node.h:229` declares it `bool`. V1 — verify the declaration.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>                     // memcpy/memcmp — the staged keypair into the plan, and the change tracker
#include "device_nv.h"                 // mrnv::Blob — THE durable carrier (never rebuilt field-by-field, U2)
#include "firmware_config_service.h"   // ICfgStore — REUSED UNCHANGED (U1); this file adds no second durable seam
#include "firmware_team_keyring.h"     // §UI-16 K2: mrfw::TeamKeyringService — the `/mrteams` durable key store
#include "firmware_config_parse.h"     // mrfw::team_fnv1a32 — the `team new` id derivation, already pure + tested
#include "identity.h"                  // meshroute::team_channel_key_derive — the PURE crypto (no Node, no install)
#include "node_role.h"                 // meshroute::role_enforce / role_set_refusal / NodeConfig (pure truth tables)
#include "monocypher.h"                // crypto_wipe — the SAME primitive Node::team_channel_key_* wipes with (§3.10)

namespace mrfw {

// ---- the typed REQUEST -----------------------------------------------------------------------------------------
// ★ TYPED, not textual, and NOT a `Print&`: the console PARSES (`mrfw::split_team_key_tail`, `parse_team_target`,
// `parse_phy_tail`) and hands VALUES over; the future OLED path will fill the same struct from a screen. ⛔ The
// service never formats — it returns a typed verdict the caller renders (§3.8).
//
// `mint` + `team_id` mirror the console's own two entry shapes exactly, so there is no third state to keep
// consistent: `team new` -> mint (the id is GENERATED here, §3.5) · `team <id>` -> !mint, id != 0 · `team 0` ->
// !mint, id == 0 = LEAVE.
struct ProvPhy {
    bool     present            = false;   // false = no PHY tail was typed -> the current PHY is CARRIED THROUGH
    double   freq_mhz           = 0.0;
    uint8_t  routing_sf         = 0;
    uint32_t bw_hz              = 0;
    // ★★ [[B211]] — `0` MEANS "THE OPERATOR NAMED NO sf_list", NOT "an empty set". A PHY tail names freq / routing SF
    //    / bw only; `stage_team_candidate` RESOLVES a 0 from the PERSISTED RECORD before either comparison runs, and
    //    the resolved value comes back out on `ProvResult::phy` so the caller can report WHAT LANDED.
    //    ⓘ Why `0` is a safe sentinel and needs no companion flag: an empty DATA SF set blocks DATA entirely and is
    //    already refused (`ProvErr::sf_list_empty` below, [[B230]]'s own arm), and the [[data-sf-removed]] ruling makes an empty
    //    `sf_list` illegal ⇒ 0 can never be a legitimate REQUEST value. A caller that really means one SF sets that
    //    one bit explicitly.
    uint16_t allowed_sf_bitmap  = 0;
    double   bw_khz             = 0.0;     // the RAW operator value, kept only for the echo line: re-deriving it
                                           // from bw_hz would round a second time (parse_phy_tail's own reason)
};
// The BUILD defaults a `0` in the persisted record resolves to. ⚠ PASSED IN rather than read here so this header
// stays board-free (`LORA_FREQ` / `LORA_BW` are board macros) AND so the incomplete-PHY rule is natively drivable.
// ⓘ On device a fresh chip never reaches these: `ICfgStore::load` is the §nv-ritual, which SEEDS from the live
// config when nothing is persisted — so a 0 here means the record genuinely holds 0.
struct ProvPhyFloor {
    double   freq_mhz = 0.0;
    uint32_t bw_hz    = 0;
};
struct TeamRequest {
    bool     mint         = false;
    uint32_t team_id      = 0;
    bool     key_supplied = false;         // `tkpub=`/`tkpriv=` were BOTH given and are syntactically valid
    uint8_t  key_pub[32]  = {};            // ⚠ SECRET-BEARING: `apply_team` wipes both halves on EVERY exit (§3.10)
    uint8_t  key_priv[32] = {};
    ProvPhy  phy{};
    ProvPhyFloor floor{};
    // ⛔ THE SECRET'S ONE UN-DOER. `apply_team` calls it on every exit, and the CONSOLE guards its own parse-failure
    // returns with it too — the caller must not depend on reaching the service for its buffer to be cleared.
    void wipe() { crypto_wipe(key_priv, sizeof key_priv); crypto_wipe(key_pub, sizeof key_pub); }
};
// The LIVE reads the transaction needs that are NOT in the `NodeConfig` the caller already passes. Every field is a
// pure const read; nothing here mutates the node, and the ADAPTER fills them — ⛔ this header never sees `g_node`.
struct ProvSnapshot {
    uint8_t  mobile_reg_count = 0;   // O2: promoting a HOST orphans its guests -> role_set_refusal needs the count
    uint32_t key_hash32       = 0;   // §mobile 6.1: team_id = FNV-1a(key_hash32 ‖ nonce)

    // ★★★ THE LIVE PHY AND THE LIVE KEY — [[B207]] QG ROUND 3's FIX, and they are here rather than derived because
    //     THE RECORD IS NOT THE RADIO. `no_change` used to compare the candidate against the PERSISTED RECORD ONLY, so
    //     an explicitly requested change was SILENTLY DISCARDED whenever NV already held the requested value while the
    //     live node did not: `mobile register freq=…` retunes the radio and mutates `_cfg.layers[0]` WITHOUT persisting
    //     (`Node::adopt_mobile_phy`, `lib/core/node.cpp:869-890`), so a later `team <current> freq=<the record's own
    //     value>` produced a candidate byte-identical to NV ⇒ `no_change` ⇒ the radio stayed on the divergent PHY and
    //     the operator was told "nothing was written and nothing was applied", which was true and useless.
    // ⇒ THE RULE (see `stage_team_candidate`): no_change = the candidate equals the record AND membership already
    //   matches AND every EXPLICITLY REQUESTED live domain already matches.
    // ⓘ WHICH ACCESSORS THE ADAPTER USES, verified at the declaration (V1) — ⛔ no new core accessor was invented (U1):
    //     freq -> `Node::active_freq_mhz()` (`lib/core/node.h:388`) · bw -> `Node::active_bw_hz()` (`:375`); both
    //     resolve the per-layer override OR the global, i.e. the EFFECTIVE carrier the radio actually flies on
    //     · routing_sf / sf_list -> `cfg.layers[0].{routing_sf,allowed_sf_bitmap}` (`lib/core/node_carriers.h:33-34`),
    //     the same two fields `adopt_mobile_phy` writes; layers[0] is the mobile's only leaf (`MR_N_LAYERS < 2`) and
    //     mirrors the legacy active-layer scalars (`node_carriers.h:269`), so the two reads cannot disagree here.
    double   live_freq_mhz          = 0.0;
    uint32_t live_bw_hz             = 0;
    uint8_t  live_routing_sf        = 0;
    uint16_t live_allowed_sf_bitmap = 0;
    // ★★ THE LIVE KEY AS **POINTERS**, and that is a deliberate choice with two reasons, not a shortcut:
    //    (1) ⛔ NO SECOND COPY OF A PRIVATE KEY. A `uint8_t[32]` pair here would put 32 bytes of SECRET into the
    //        console's frame in a carrier nobody wipes — the exact hygiene §3.10 removed from the old `tk_priv`
    //        scratch buffer. These point AT the node's own storage; the transaction only compares.
    //    (2) `Node::team_channel_pub()` / `team_channel_priv()` (`lib/core/node.h:225-226`) ALREADY return
    //        **nullptr when no key is held** ("never a zero buffer a caller could mistake for one"), so the
    //        absent-key case needs no companion flag and cannot be misread as an all-zero key. Both are null or
    //        both are non-null; the predicate below requires both.
    const uint8_t* live_key_pub  = nullptr;
    const uint8_t* live_key_priv = nullptr;
};

// ---- the KEY APPLICATION, as an EXPLICIT DECISION (§3.6.1) -----------------------------------------------------
// ★★ THE CANDIDATE AND THE LIVE APPLY USE THE SAME VALUE. v2 of the design said "load the staged key" unconditionally
//    — which would CHANGE OR CLEAR the key during a same-team PHY-only update, i.e. destroy an UNRECOVERABLE secret
//    (no seed derives a team content key) for a request that never mentioned it.
//      preserve — ⛔ NO key call at all, and the candidate's key bytes are left EXACTLY as loaded
//      install  — `team_channel_key_load(staged_pub, staged_priv, true)`, the INFALLIBLE `void` primitive
//      clear    — happens via `set_team` (`set_team_id` destroys the pair by design, §o3-key-lifetime)
// ⓘ §3.6.1's table shows `no_change` in this column for the "nothing changed" row. That row's `no_change` is the
//   TRANSACTION's verdict (`ProvVerdict::no_change` below), not a fourth key action: nothing is applied at all there,
//   so the honest key action is `preserve`. The tests assert BOTH — verdict `no_change` AND `preserve` with zero key
//   calls — which is stronger than either reading of the table alone.
enum class KeyAction : uint8_t { preserve, install, clear };
inline const char* key_action_name(KeyAction a) {
    switch (a) {
        case KeyAction::preserve: return "preserve";
        case KeyAction::install:  return "install";
        case KeyAction::clear:    return "clear";
    }
    return "?";     // total function; `-Werror=switch` fires before this can be reached for a valid enumerator
}

// ---- explicit outcomes -----------------------------------------------------------------------------------------
enum class ProvVerdict : uint8_t {
    applied,     // EXACTLY ONE durable write, then the live apply in the §3.2 order
    no_change,   // ★ ZERO saves and ZERO live applies — a HARD REQUIREMENT, not an optimisation (§3.7). ⛔ Reachable
                 //   ONLY when the RECORD and every EXPLICITLY REQUESTED **LIVE** domain already match (QG round 3)
    refused,     // a staging refusal: ZERO saves, ZERO live applies, ZERO airtime
    nv_failed,   // the ONE save attempt failed: ZERO live applies, ZERO airtime, NV and live state untouched
};
enum class ProvErr : uint8_t {
    none,
    role_refused,      // the O1/O2/R4 truth table refused the implied promotion (see `ProvResult::role_refusal`)
    no_mobile_plane,   // this build has no roaming-endpoint plane for a team member to live on
    key_on_leave,      // `team 0 tkpub=…` — leaving a team takes no key
    phy_on_leave,      // ★ v4 RULING: `team 0` with ANY PHY argument is REFUSED LOUDLY, before the save
    key_degenerate,    // the supplied/minted scalar is all-zero or derives a degenerate point (derive refused)
    key_mismatch,      // ★ `tkpub=` is not `tkpriv=`'s public key — the cross-check the SYNTAX parser cannot do
    keygen_failed,     // `team new`: the entropy seam produced material the derivation refuses (a dead CSPRNG)
    incomplete_phy,    // the STAGED effective PHY lacks freq / routing_sf(5..12) / bw — the TAIL-SETTABLE fields
    // ★★★ [[B230]] — THE STAGED `sf_list` IS EMPTY, AND IT IS A **SEPARATE ARM** BECAUSE ITS REMEDY IS A DIFFERENT
    //     COMMAND. freq / routing_sf / bw ride the `team new` / `team <id>` PHY tail; the DATA SF set does ⛔ NOT
    //     ([[B211]]'s deliberate omission — the tail PRESERVES the node's own list), so the ONLY way to fill it is
    //     `cfg set sf_list …`. Folded into `incomplete_phy`, the console could only offer the inline tail — i.e. a
    //     command that FAILS THE SAME CHECK, which is exactly the dead end the operator measured on metal.
    // ⛔ THE REFUSAL ITSELF IS UNCHANGED (C2 / [[data-sf-removed]]: an empty set blocks DATA entirely and must never
    //    silently default). Same inputs, same verdict, same zero writes — only the CLASSIFICATION is finer.
    sf_list_empty,
    id_unavailable,    // `team new` could not draw an id that is neither 0 nor the CURRENT team (§3.5)
    nv_load_failed,    // the record could not be read, so the non-provisioning fields cannot be preserved
    nv_save_failed,    // the single write failed
    // ★★★ §UI-16 K2 — THE FOUR KEYRING ARMS. The transaction now makes the key DURABLE (`/mrteams`) BEFORE it
    //     persists the `/mrcfg` candidate that marks it ACTIVE, so the keyring's refusals are the transaction's
    //     refusals: ⛔ zero `/mrcfg` writes, ⛔ zero live applies, ⛔ zero airtime, and the keyring itself untouched.
    //     ⓘ They are FOUR and not one because each takes a different action from the operator, exactly as
    //     `/mrjoin`'s `store_invalid` / `store_io_failed` split does — and each is ≤15 characters, the width
    //     `prov_err_name`'s widest existing arm (`no_mobile_plane`) already budgets on the 19-column panel.
    keyring_full,      // ★ P-15: four teams are stored and this is a fifth ⇒ REFUSED LOUDLY, ⛔ nothing evicted
    keyring_invalid,   // the `/mrteams` record is present but unreadable — a teammate must re-grant the key
    keyring_io_fail,   // the `/mrteams` store would not open at all — nothing is known; ⛔ never blind-rewritten
    keyring_failed,    // the ONE keyring save attempt failed
};
inline const char* prov_err_name(ProvErr e) {
    switch (e) {
        case ProvErr::none:            return "none";
        case ProvErr::role_refused:    return "role_refused";
        case ProvErr::no_mobile_plane: return "no_mobile_plane";
        case ProvErr::key_on_leave:    return "key_on_leave";
        case ProvErr::phy_on_leave:    return "phy_on_leave";
        case ProvErr::key_degenerate:  return "key_degenerate";
        case ProvErr::key_mismatch:    return "key_mismatch";
        case ProvErr::keygen_failed:   return "keygen_failed";
        case ProvErr::incomplete_phy:  return "incomplete_phy";
        // ⓘ [[B230]] — 13 characters, DELIBERATELY: this token is rendered VERBATIM on the OLED's result-detail row
        //   (`firmware_ui_prov.h:130` -> `mrui::prov_result_detail`), whose §7.3 audit budgets 19 columns and names
        //   `no_mobile_plane` (15) as the widest `prov_err_name`. A longer spelling would have moved that audit.
        case ProvErr::sf_list_empty:   return "sf_list_empty";
        case ProvErr::id_unavailable:  return "id_unavailable";
        case ProvErr::nv_load_failed:  return "nv_load_failed";
        case ProvErr::nv_save_failed:  return "nv_save_failed";
        // §UI-16 K2 — 12/15/15/14 characters; see the enum's note on the 19-column budget.
        case ProvErr::keyring_full:    return "keyring_full";
        case ProvErr::keyring_invalid: return "keyring_invalid";
        case ProvErr::keyring_io_fail: return "keyring_io_fail";
        case ProvErr::keyring_failed:  return "keyring_failed";
    }
    return "?";
}
// §UI-16 K2 — the 1:1 relabel of a keyring refusal into this transaction's vocabulary. ⛔ ONE mapping, `default`-less
// so a new `KeyringErr` arm fails the build here rather than silently becoming somebody's nearest neighbour.
// ⓘ `none` is unreachable on a refusal path and is mapped to the keyring's own generic failure rather than to
//   `ProvErr::none`, which would report a refusal as a success (C2).
inline ProvErr prov_err_of_keyring(KeyringErr e) {
    switch (e) {
        case KeyringErr::keyring_full:    return ProvErr::keyring_full;
        case KeyringErr::store_invalid:   return ProvErr::keyring_invalid;
        case KeyringErr::store_io_failed: return ProvErr::keyring_io_fail;
        case KeyringErr::nv_save_failed:  return ProvErr::keyring_failed;
        // `zero_team` cannot arise here — `key_action == install` implies a non-zero team (a key on `team 0` is
        // refused by `key_on_leave` in phase 1, and a mint always stages a non-zero id). Mapped to the keyring's
        // generic failure rather than dropped, so an unreachable arm cannot become a silent success.
        case KeyringErr::zero_team:
        case KeyringErr::none:            return ProvErr::keyring_failed;
    }
    return ProvErr::keyring_failed;
}

// ---- PHASE 1's OUTPUT: the ROLE + ID PROJECTION -----------------------------------------------------------------
// ★★★ WHY THIS 8-BYTE STRUCT EXISTS. §3.3 forbids hand-deriving the role, so the projection needs a REAL
//     `role_enforce` pass over a `NodeConfig` COPY (256 B on ARM, measured). The first version of this file scoped that
//     copy to a block INSIDE the candidate builder and its comment claimed the copy *"dies at the closing brace, never
//     alongside the Blob AND the plan AND a keypair"* — ⛔ THAT WAS FALSE: `apply_team` held the `TeamPlan` and the
//     `mrnv::Blob` live ACROSS that block, so all three DID coexist. ⇒ the projection is now its OWN PHASE, and the
//     copy really is destroyed before the plan and the candidate are declared. The comment now describes the code.
//
// ⛔⛔ AND WHAT THAT SPLIT DOES **NOT** BUY — MEASURED, BECAUSE THE FIRST VERSION'S MISTAKE WAS ASSERTING INSTEAD OF
//     MEASURING, AND REPEATING IT HERE WOULD BE THE SAME DEFECT WITH THE OPPOSITE SIGN:
//       · `handle_team`'s ARM frame is **IDENTICAL** with the projection before or after the plan/Blob declarations
//         (856 B either way, `-fstack-usage`, `xiao_sx1262` flags), and
//       · **IDENTICAL AGAIN with the `NodeConfig` copy deleted outright** (856 B).
//     ⇒ at `-Ofast` the copy is SCALARISED AWAY: `role_enforce` reads only `team_id` and writes only `is_mobile`
//     (`node_role.h:79-85`), so GCC never materialises 256 bytes at all. **The copy was never the cost.** The growth
//     over the pre-slice frame is the TYPED CARRIERS — `TeamRequest` 136 B + `TeamPlan` 128 B + `ProvResult` 64 B on
//     ARM, against the 64 bytes of `tk_pub`/`tk_priv` the old console frame held. That is an OPEN OWNER CALL, ⛔ not
//     something this split addressed and ⛔ not something to claim it did.
// ⓘ What the split DOES buy, beyond making the comment true: the role and `team 0` refusals no longer sit behind an
//   `ICfgStore::load`, so a role-refused request reports the ROLE refusal even when the record is unreadable (the
//   actionable one). ⚠ Its price is stated where it is visible — the 4-byte team-id nonce is now drawn before the load,
//   so a `nv_load_failed` refusal has spent four bytes of CSPRNG. It still spends no write, no apply and no airtime.
struct TeamProjection {
    uint32_t team_id             = 0;
    bool     membership_changed  = false;   // vs the LIVE config's team_id — the §3.7 discriminator
    bool     projected_is_mobile = false;
    bool     role_promoted       = false;   // RoleFix::forced_mobile — OWED as a report (B28 constraint 3)
};

// ---- the PLAN (the staged transaction; ⛔ nothing here is live) -------------------------------------------------
// ⚠ SECRET-BEARING (`key_priv`). `wipe()` is called on EVERY exit of `apply_team`, success and failure alike, by a
// scope guard rather than by hand at each `return` — a hand-written wipe per exit is exactly the shape that gets one
// path added later and missed.
// ⛔⛔ THE WIPE APPLIES TO THIS TRANSIENT PLAN ONLY. It must NEVER touch the persisted candidate or the installed
//    live key: the team key is REQUIRED to survive in NV (`src/device_nv.h:116-118`) and in the node, or the node
//    cannot read team traffic. Wiping the candidate after a successful save would DESTROY THE TEAM.
struct TeamPlan {
    uint32_t  team_id             = 0;
    bool      membership_changed  = false;
    bool      projected_is_mobile = false;
    bool      role_promoted       = false;   // RoleFix::forced_mobile — OWED as a report (B28 constraint 3)
    KeyAction key_action          = KeyAction::preserve;
    uint8_t   key_pub[32]         = {};      // meaningful iff key_action == install
    uint8_t   key_priv[32]        = {};
    bool      key_minted          = false;   // MINTED (generated here) vs ADOPTED (operator-supplied)
    ProvPhy   phy{};
    bool      fire_dad            = false;   // ★ THE AIRTIME OPERATION, and the one a fake counts
    // ★★ DERIVED FROM THE CANDIDATE'S ACTUAL DIFFERENCES against the loaded record — ⛔ NOT from the request's shape.
    //    See the change tracker in `stage_team_candidate`.
    bool      no_change           = false;
    void wipe() { crypto_wipe(key_priv, sizeof key_priv); crypto_wipe(key_pub, sizeof key_pub); }
};

// The typed OUTCOME (§3.8) — so the console and the future OLED render the SAME verdict, and ⛔ so `ADOPTED` /
// `MINTED` / `> team PHY:` / `> team ->` / `mr_ui_on_config_saved()` are emitted ONLY after the transaction
// completed. §1.2.4: today all three of those lines print BEFORE the save.
struct ProvResult {
    ProvVerdict verdict = ProvVerdict::refused;
    ProvErr     err     = ProvErr::none;
    meshroute::RoleSetRefusal role_refusal = meshroute::RoleSetRefusal::none;   // rendered through the ONE message set
    uint32_t    team_id       = 0;
    bool        membership_changed = false;
    KeyAction   key_action    = KeyAction::preserve;
    bool        key_minted    = false;
    bool        role_promoted = false;
    ProvPhy     phy{};          // .present -> the `> team PHY:` echo
    bool        dad_fired     = false;
    // ★★★ WHAT THE RECORD NOW HOLDS IN `team_local_id`, reported so THE CALLER CAN SYNC ITS OWN PERSIST TRACKER.
    //     ⛔ MEANINGFUL ONLY WHEN `verdict == applied` (nothing was written otherwise).
    // ⓘ WHY THE SERVICE REPORTS IT INSTEAD OF DOING IT: `fw_main`'s `g_persist_team_local_id` is the change-detector
    //   `persist_cfg_if_needed()` compares the LIVE team-DAD id against (`src/fw_main.cpp:1030`), and it is updated in
    //   only two places — the boot restore (`:797`) and that function's own successful save (`:1042`). An
    //   `ICfgStore::save` from here does NOT touch it. So after this transaction persists `team_local_id = 0` the
    //   tracker still held the OLD team's id, and if team-DAD then happened to pick THE SAME NUMERIC ID the old team
    //   used, `team_changed` was FALSE, nothing was persisted, NV STAYED 0, and the next boot needlessly re-DAD'd —
    //   the exact outcome design v2 corrected. ⛔ The service cannot fix that itself: it must not see `fw_main`'s
    //   globals (it is compiled by the native suite, which has no `fw_main`). ⇒ it reports the saved value and the
    //   ADAPTER assigns it.
    uint8_t     persisted_team_local_id = 0;
};

// ---- the seams -------------------------------------------------------------------------------------------------
// ★★ THE ENTROPY SEAM. Key material must be minted AT STAGE TIME, BEFORE the save (it has to be inside the
//    candidate), so the generator cannot be `Node::team_channel_key_mint()` — which draws AND INSTALLS, and is
//    fallible. Injecting it also makes two behaviours natively drivable that are otherwise unreachable: a DEAD
//    CSPRNG (all-zero draw -> `keygen_failed`) and ★ `team new` regenerating the CURRENT team id (§3.5), which
//    cannot be produced against a live CSPRNG at all.
struct IEntropy {
    virtual ~IEntropy() = default;
    virtual void fill(uint8_t* out, size_t n) = 0;
};
// ★★ THE LIVE SEAM, and its SHAPE is the guarantee. Four operations, in the §3.2 order, and every one of them is
//    INFALLIBLE given a validated plan:
//      set_team    -> Node::set_team_id (drops the old team's plane / key cache / DAD id; may PROMOTE the role)
//      install_key -> Node::team_channel_key_load(pub, priv, true)  ⛔ `void` BY REQUIREMENT (§3.6 step 5)
//      apply_phy   -> Node::mobile_retune_phy  ★ RETUNE ONLY — ⛔ NOT `mobile_register_phy` ([[B209]]: that one also
//                     AUTHORISES static-home attachment and DISCOVERs, which a team PHY tail must never do)
//      fire_dad    -> Node::team_dad_fire  ★ THE AIRTIME OPERATION
// ⛔ `key_mint` and `key_adopt` are DELIBERATELY ABSENT (they were on v1's version of this seam): the key belongs in
//    the candidate before persistence, and all three core key primitives that could appear here are fallible.
// ⓘ `set_team` is `void` although `Node::set_team_id` returns `bool`, and the discard is deliberate rather than
//   sloppy: the plan already decided `membership_changed` at STAGE time from `NodeConfig::team_id`, so the live
//   return carries nothing new — and its one false-WITH-work case (a build with no mobile plane) is refused at
//   staging by `role_set_refusal` / `role_enforce` and is unreachable on every profile that compiles `handle_team`.
struct IProvLive {
    virtual ~IProvLive() = default;
    virtual void set_team(uint32_t team_id) = 0;
    virtual void install_key(const uint8_t pub[32], const uint8_t priv[32]) = 0;
    virtual void apply_phy(const ProvPhy& phy) = 0;
    virtual void fire_dad() = 0;
};

// ---- the ONE conversion of key material into the durable carrier (U2, owner-ruled v4 §4) -----------------------
// ★★ RULED: ONE EXPLICIT-MATERIAL HELPER, and the node-reading `blob_take_team_channel_key()` DELEGATES to it —
//    ⛔ not an overload pair, because an overload pair is exactly how the S1/L9 field-drop rot starts. Candidate
//    composition calls THIS with STAGED material; the live-collecting caller calls it with the accessors' output.
// `pub`/`priv` null (either one) writes `present = 0` + all-zero, which is what `load` restores as "no key" — so the
// CLEAR case needs no second function.
inline void blob_put_team_channel_key(mrnv::Blob& b, const uint8_t* pub, const uint8_t* priv) {
    b.team_ch_key_present = (pub && priv) ? 1 : 0;
    for (uint8_t i = 0; i < 32; ++i) {
        b.team_ch_pub[i]  = pub  ? pub[i]  : 0;
        b.team_ch_priv[i] = priv ? priv[i] : 0;
    }
}

// ---- THE TWO "IS THE LIVE DOMAIN ALREADY THERE?" PREDICATES ([[B207]] QG round 3) ------------------------------
// ★★★ WHY THEY EXIST AND WHAT THEY ARE FOR: `no_change` may only be reported when NOTHING IS OWED, and something IS
//     owed whenever a domain THE REQUEST EXPLICITLY NAMES does not already match LIVE — regardless of what NV holds.
//     ⛔ The reference is the REQUEST, not the candidate: a domain the operator did not mention is carried through and
//     can never make a request "not a no-change" (that is what keeps `team <current>` on a node whose PHY was retuned
//     out-of-band by `mobile register` from re-writing NV on every invocation).
// ⚠ EXACT `double` COMPARISON IS DELIBERATE and is the SAME comparison the record tracker already makes on
//   `cand.freq_mhz`: both sides originate from one `parse_phy_tail` decimal parse, so an equal request produces an
//   equal double. An epsilon would be the WRONG direction of error anyway — a false "matches" DISCARDS the request,
//   while a false "differs" merely re-applies the value the node already has (one coalesced write, one idempotent
//   retune). ⇒ when in doubt this pair says "differs", never "matches".
// ⛔ [[B211]]: `phy` MUST be the RESOLVED `plan.phy`, never the raw `req.phy` — an unresolved `allowed_sf_bitmap == 0`
//   can match no live node, so this would answer "differs" for every PHY tail and `no_change` would never be reachable.
//   The single call site passes `plan.phy` after `stage_team_candidate` has resolved it.
inline bool live_phy_matches(const ProvPhy& phy, const ProvSnapshot& snap) {
    if (!phy.present) return true;                       // nothing requested -> nothing owed
    return snap.live_freq_mhz          == phy.freq_mhz
        && snap.live_bw_hz             == phy.bw_hz
        && snap.live_routing_sf        == phy.routing_sf
        && snap.live_allowed_sf_bitmap == phy.allowed_sf_bitmap;
}
// ⓘ `preserve` and `clear` name no key material, so nothing is owed: `preserve` is reached only when the operator
//   supplied none, and `clear` only with `membership_changed` (which already forbids `no_change`).
// ★ BOTH HALVES ARE COMPARED, and the private half is the load-bearing one rather than a belt-and-braces extra: the
//   live pair is installed VERBATIM at boot (`team_channel_key_load` — "no re-derivation"), so a record whose two
//   halves disagree ([[B193]]'s partial-write question is exactly how that happens) yields a live node whose PUBLIC
//   half matches the request while its PRIVATE half — the one that decrypts team traffic — does not. Comparing `pub`
//   alone would call that a no-change and leave the node unable to read its own team.
inline bool live_key_matches(KeyAction act, const uint8_t pub[32], const uint8_t priv[32], const ProvSnapshot& snap) {
    if (act != KeyAction::install) return true;
    if (!snap.live_key_pub || !snap.live_key_priv) return false;   // no live key at all -> the explicit key IS owed
    return memcmp(snap.live_key_pub, pub, 32) == 0 && memcmp(snap.live_key_priv, priv, 32) == 0;
}

// ---- THE ONE SHARED PURE BUILDER (§3.9), IN TWO PHASES -----------------------------------------------------------
// Validates, PROJECTS the role, generates/validates key material and COMPOSES THE CANDIDATE — ⛔ with ZERO live
// mutation and ZERO persistence. ⛔ Without one shared builder the console and the future OLED would duplicate
// creation logic — the exact rot U1/U2 exist to prevent.
// ★★ IT IS TWO FUNCTIONS AND NOT ONE (QG defect ③): phase 1 holds the `NodeConfig` copy, phase 2 holds the `TeamPlan`
//    and the `mrnv::Blob`, and the earlier single function let those two working sets OVERLAP while its comment denied
//    it. The 8-byte `TeamProjection` is the whole handover. ⛔ MEASURED, so it is not oversold: the split does NOT
//    reduce `handle_team`'s frame by one byte — see `TeamProjection`'s block for the three figures and for what the
//    growth actually is.
//
// ★ Bounded resample count for `team new`. The id must satisfy `t != 0 && t != current_team_id`: checking only
// non-zero leaves a rare case where `team new` REGENERATES THE CURRENT ID and therefore becomes a same-team re-key
// instead of creating a new team — the verb silently doing something other than what was asked, which is the
// `team_fnv1a32`-returns-0 defect in a second guise. Excluding both costs nothing (the same loop), and exhaustion is
// a LOUD refusal rather than a fallback (C2).
constexpr uint8_t kTeamIdMintTries = 8;

// ---- PHASE 1 — VALIDATE, STAGE THE ID, PROJECT THE ROLE ---------------------------------------------------------
// ★★ THIS IS THE ONLY FUNCTION IN THE TRANSACTION THAT HOLDS A `NodeConfig` COPY, and it holds NEITHER the plan nor
//    the candidate: that separation IS the stack correction (see `TeamProjection`). It touches no store and no live
//    seam — pure decision, so every refusal below costs zero writes, zero applies and zero airtime.
inline ProvErr project_team(const TeamRequest& req, const meshroute::NodeConfig& live,
                           const ProvSnapshot& snap, IEntropy& ent,
                           TeamProjection& proj, meshroute::RoleSetRefusal& role_refusal) {
    role_refusal = meshroute::RoleSetRefusal::none;
    const bool is_leave = !req.mint && req.team_id == 0;

    // (1) ★★ THE TWO `team 0` REFUSALS, both BEFORE anything else — a leave takes neither a key nor a PHY.
    //     `key_on_leave` is the PRE-EXISTING refusal (firmware_config.cpp's *"tkpub=/tkpriv= make no sense on
    //     `team 0` (leave)"*); `phy_on_leave` is the v4 ruling, in the SAME shape and for the same reason.
    // ✅ RULED 2026-08-17 (owner, reported form): ON LEAVE THE PHY IS PRESERVED. Two consequences, and v4 made them
    //    consistent where v3's pair contradicted itself:
    //      · `team 0` NEVER resets, clears or re-derives the PHY — frequency, routing SF, sf_list and bandwidth carry
    //        through untouched, as an INVARIANT this transaction upholds rather than an accident of an early `if`;
    //      · `team 0` WITH ANY PHY ARGUMENT IS REFUSED LOUDLY, BEFORE THE SAVE. ⛔ Not honoured, ⛔ not ignored,
    //        ⛔ not partially parsed. Zero save, zero live application, zero airtime.
    if (req.key_supplied && is_leave) return ProvErr::key_on_leave;
    if (req.phy.present  && is_leave) return ProvErr::phy_on_leave;

    // (2) THE ROLE TRUTH TABLE (O1/O2/R4), before ANY entropy is drawn. ⚠ Evaluated on `!is_leave` rather than on a
    //     non-zero id, deliberately: by R3 `team 0` does NOT demote (a mobile with no team is legitimate), so passing
    //     "wants static" would make O1 fire and REFUSE EVERY `team 0`, trapping members in their team. The team verb
    //     is a one-way role gate by design. U1: the DECISION is `role_set_refusal`, the VOICE stays in the console.
    if (!is_leave) {
        role_refusal = meshroute::role_set_refusal(/*want_mobile=*/true, live.is_mobile, live.is_gateway,
                                                   live.team_id, snap.mobile_reg_count);
        if (role_refusal != meshroute::RoleSetRefusal::none) return ProvErr::role_refused;
    }

    // (3) STAGE A USABLE TEAM ID. ⛔ `team_fnv1a32` has NO zero guard (`firmware_config_parse.h:419-425` returns the
    //     raw hash), and the old code assigned it straight to `t` — so a 0 made `team new` mint a key and then execute
    //     `team 0` = LEAVE, skipping every `t != 0` guard and doing the OPPOSITE of what was asked. ~1 in 2^32,
    //     structurally reachable, and free to exclude.
    if (req.mint) {
        bool got = false;
        for (uint8_t i = 0; i < kTeamIdMintTries && !got; ++i) {
            uint32_t nonce = 0;
            ent.fill(reinterpret_cast<uint8_t*>(&nonce), sizeof nonce);
            const uint32_t t = team_fnv1a32(snap.key_hash32, nonce);
            if (t != 0 && t != live.team_id) { proj.team_id = t; got = true; }
        }
        if (!got) return ProvErr::id_unavailable;
    } else {
        proj.team_id = req.team_id;
    }

    // (4) ★ THE PROJECTED ROLE — ⛔ do NOT hand-derive it. `set_team_id`'s promotion is a general `role_enforce(_cfg)`
    //     pass whose `RoleFix` it discards (`lib/core/node.cpp:701`); `role_enforce` is a free `static inline` over a
    //     plain struct, so the honest projection is: COPY the config, set `team_id` on the copy, run THE REAL
    //     `role_enforce`, read the result. Zero mutation, zero duplication, natively reachable.
    // ⚠ §3.10 / ⛔ QG DEFECT ③: the copy is 256 B as a type (measured) and it is scoped to this block — and, unlike the
    //   first version of this file, THAT SCOPING IS NOW TRUE OF THE WHOLE TRANSACTION: this function holds no
    //   `TeamPlan` and no `mrnv::Blob`, so the copy cannot coexist with them. The earlier comment here CLAIMED that
    //   property while `apply_team` held both live across the block.
    // ⛔ AND THE HONEST FRAME FACT: deleting this copy entirely changes `handle_team`'s ARM frame by ZERO bytes
    //   (measured with `-fstack-usage`), because `-Ofast` scalarises it. ⇒ it is written for §3.3's correctness, not as
    //   a stack cost, and it must not be described as one.
    {
        meshroute::NodeConfig probe = live;
        probe.team_id = proj.team_id;
        const meshroute::RoleFix fix = meshroute::role_enforce(probe);
        proj.projected_is_mobile = probe.is_mobile;
        proj.role_promoted       = (fix == meshroute::RoleFix::forced_mobile);
        // `dropped_team`: this build has no mobile plane, so `role_enforce` DROPPED the team rather than setting the
        // flag. A live verb can refuse before any state moves — and refusing beats adopting-then-stripping.
        if (fix == meshroute::RoleFix::dropped_team) return ProvErr::no_mobile_plane;
    }
    // ★ THE UNIFORM PROJECTED-ROLE GATE ON THE PHY TAIL, which is §1.2.1's fix. The old code gated the PHY parse on
    //   the OLD role (`if (phy_args && *phy_args && c.is_mobile)`) ⇒ on a STATIC node
    //   `team new freq=869 sf=7 bw=125` SILENTLY DISCARDED the arguments and then ran the incomplete-PHY check
    //   against live values. Silently ignoring supplied arguments violates C2; here the tail is honoured under the
    //   role the request PROJECTS. ⓘ Defence-in-depth as written: a node holding a non-zero `team_id` has necessarily
    //   been promoted, so this cannot fire once `dropped_team` above has been excluded — the point is to gate on the
    //   projected role UNIFORMLY rather than to rely on that coincidence.
    if (req.phy.present && !proj.projected_is_mobile) return ProvErr::no_mobile_plane;

    // (5) ★★ MEMBERSHIP CHANGE IS THE DISCRIMINATOR — not "same id". A same-team request is NOT necessarily a no-op:
    //     `team <current> tkpub=…` REPLACES the key (`set_team_id` returns early for the same team at
    //     `lib/core/node.cpp:667`, so it never clears) and `team <current> freq=…` CHANGES the PHY.
    proj.membership_changed = (live.team_id != proj.team_id);
    return ProvErr::none;
}

// ---- PHASE 2 — COMPOSE THE CANDIDATE, STAGE THE KEY, AND DECIDE WHETHER ANYTHING ACTUALLY CHANGES ----------------
// ⛔ NO `NodeConfig` ANYWHERE IN HERE (that is the point — see `TeamProjection`): every live fact this phase needs
//    arrived as the 12-byte projection. `cand` comes in holding the FRESHLY LOADED record and leaves holding the
//    complete candidate; on any refusal it is meaningless and is never written.
inline ProvErr stage_team_candidate(const TeamRequest& req, const TeamProjection& proj, const ProvSnapshot& snap,
                                   IEntropy& ent, TeamPlan& plan, mrnv::Blob& cand) {
    plan.team_id             = proj.team_id;
    plan.membership_changed  = proj.membership_changed;
    plan.projected_is_mobile = proj.projected_is_mobile;
    plan.role_promoted       = proj.role_promoted;

    // ★★★ THE CHANGE TRACKER, and it is QG DEFECT ①'s FIX. `no_change` used to be computed from THE REQUEST'S SHAPE
    //     (`!membership_changed && key_action == preserve && !phy.present`), which was wrong in three measurable ways:
    //       (a) re-supplying the ALREADY-STORED PHY reported `applied`, ran the live retune and notified the UI;
    //       (b) supplying the IDENTICAL existing key reported `applied` and REINSTALLED it;
    //       (c) ⛔ the dangerous one — a ROLE-PROJECTION-ONLY correction (same team, no key, no PHY, but the record's
    //           `is_mobile` disagrees with the projection) was classified `no_change` and SILENTLY DISCARDED, so a
    //           needed `is_mobile` fix never reached NV.
    // ⇒ the verdict is now derived from THE CANDIDATE'S ACTUAL DIFFERENCES against the loaded record, accumulated
    //   field-by-field AS THE FIELDS ARE STAGED. ⛔ Deliberately NOT by keeping a second `mrnv::Blob` and comparing:
    //   the console frame is exactly what QG defect ③ was about, and a second Blob would undo that fix to fix this one.
    // ⚠ AND ITS BOUNDARY IS NOW CLOSED RATHER THAN DOCUMENTED — [[B207]] QG ROUND 3. This tracker's reference is THE
    //   STORED RECORD, so it is STRUCTURALLY BLIND to a live-vs-record divergence, and that blindness DISCARDED
    //   EXPLICITLY REQUESTED WORK: `mobile register freq=…` retunes the radio and mutates `_cfg.layers[0]` without
    //   persisting (`Node::adopt_mobile_phy`, `lib/core/node.cpp:869`), so `team <current> <the record's own PHY>`
    //   produced a candidate identical to NV and was reported `no_change` while the radio sat elsewhere.
    // ⇒ `differs` is KEPT EXACTLY AS IS (it answers "does the RECORD move?") and the LIVE question is answered
    //   SEPARATELY by `live_phy_matches` / `live_key_matches` above, both conjuncts landing in `plan.no_change` at the
    //   end of this function. ⛔ The earlier note claiming this needed an owner call because *"`NodeConfig` carries no
    //   `freq_mhz`"* was WRONG — that is true of the top-level struct only; `Node::active_freq_mhz()` /
    //   `active_bw_hz()` / `layers[0]` / `team_channel_p*()` all already existed and the adapter now reads them.
    bool differs = false;
    if (cand.team_id != plan.team_id) differs = true;
    cand.team_id = plan.team_id;
    // NV carries team_id and is_mobile INDEPENDENTLY — which is precisely why (c) above was reachable.
    const uint8_t projected_mobile = plan.projected_is_mobile ? 1 : 0;
    if (cand.is_mobile != projected_mobile) differs = true;
    cand.is_mobile = projected_mobile;
    // ★ `team_local_id` may be zeroed ONLY when membership actually changes; otherwise it and `node_id` are PRESERVED,
    //   or the node needlessly re-DADs after reboot and loses a stable, defended local id.
    // ⓘ ⚠ THIS CLAUSE CAN NEVER BE THE DECIDING ONE, AND THAT IS RECORDED RATHER THAN LEFT TO LOOK LIKE COVERAGE: it
    //   only runs when `membership_changed` is true, which already forces `no_change` false below. It is kept as
    //   defence-in-depth for a future arm that zeroes the id without a membership change — and ⛔ a mutation control
    //   that deletes it stays GREEN by construction, so it is NOT claimed as tested. (The same is true of the `clear`
    //   arm of the key comparison further down; both are marked, neither is counted.)
    if (plan.membership_changed) {
        if (cand.team_local_id != 0) differs = true;
        cand.team_local_id = 0;                              // ★ 0 = DAD PENDING (lib/core/node.h:353's own sentinel)
    }
    // ⛔ `cand.node_id` IS DELIBERATELY NOT ASSIGNED, on either arm. Membership unchanged -> preserve the loaded
    //    value; membership changed -> also the loaded value, because `persist_cfg_if_needed()` already writes a moved
    //    id back (`src/fw_main.cpp:1029` `join_changed`). That is what makes the old post-DAD read at `:1241`
    //    unnecessary, and it is why nothing here has to run AFTER `fire_dad`.
    plan.phy = req.phy;
    // ★★★ [[B211]] — RESOLVE AN UNSPECIFIED `sf_list` FROM THE **PERSISTED RECORD**, AND DO IT HERE, WHICH IS BEFORE
    //     BOTH COMPARISONS. A PHY tail names freq / routing SF / bw; it does NOT name the DATA SF set, and the console
    //     therefore sends `allowed_sf_bitmap = 0` = "not specified" (`firmware_config.cpp:1358`). Collapsing the set to
    //     `1 << routing_sf` was the metal-confirmed defect: a node booted `data sf = 6,7` came back `data sf = 7`, and
    //     it PERSISTED. ⓘ `lib/core/node.h:302-305` defines team-PHY compatibility as freq/bw/routing_sf/cr and states
    //     **NOT sf_list — F-SF-1 keeps that across registration** ⇒ a team never needed a common DATA SF set.
    // ⛔⛔ THE POSITION IS THE REQUIREMENT, NOT A STYLE CHOICE. `cand` still holds the FRESHLY LOADED record at this
    //     point (nothing above touches `allowed_sf_bitmap`), and the resolved value must be in `plan.phy` before
    //     **(1)** the `differs` tracker just below and **(2)** `live_phy_matches` at the end of this function. Resolve
    //     later and a raw `0` reaches both: the record comparison sees a change that is not one and the live predicate
    //     can never match ⇒ a same-PHY re-apply reports `applied` FOREVER, which is a direct regression of bench
    //     27.8/27.9 (already passed on metal). The `no_change` row of §3.6.1 depends on this line's position.
    // ★ THE SOURCE IS `cand`, THE PERSISTED RECORD — ⛔ NEVER `snap.live_allowed_sf_bitmap`. The two genuinely
    //   diverge on this device: `mobile register sf=…` collapses the LIVE bitmap without persisting
    //   (`Node::adopt_mobile_phy`), which is the [[B207]] condition bench 27.8 exercises. Resolving from live would
    //   LAUNDER that collapse into NV — the very defect this slice removes, one layer down.
    // ⚠ A record that genuinely holds NO DATA SF resolves to 0 and is still REFUSED by the check below — since
    //   [[B230]] under its OWN arm, `ProvErr::sf_list_empty`: resolution must not turn a real "no DATA SF"
    //   configuration into a silent pass (C2), and the operator must be told WHICH part is missing.
    if (plan.phy.present && plan.phy.allowed_sf_bitmap == 0) plan.phy.allowed_sf_bitmap = cand.allowed_sf_bitmap;
    if (plan.phy.present) {
        if (cand.freq_mhz          != plan.phy.freq_mhz
            || cand.routing_sf     != plan.phy.routing_sf
            || cand.bw_hz          != plan.phy.bw_hz
            || cand.allowed_sf_bitmap != plan.phy.allowed_sf_bitmap) differs = true;
        cand.freq_mhz          = plan.phy.freq_mhz;
        cand.routing_sf        = plan.phy.routing_sf;
        cand.bw_hz             = plan.phy.bw_hz;
        cand.allowed_sf_bitmap = plan.phy.allowed_sf_bitmap;
    }

    // (6) ★ THE INCOMPLETE-PHY REFUSAL, AGAINST THE STAGED CANDIDATE — ⛔ never against live state. A team is a
    //     SHARED-PHY overlay: members hear each other only on a COMMON freq/routing_sf/bw.
    // ⛔⛔ CORRECTED (§B211): this comment used to list `sf_list` among the fields a team must hold in COMMON. THAT IS
    //     WRONG, and it is the belief the sf_list-collapse defect rested on. `lib/core/node.h:302-305` defines team-PHY
    //     compatibility EXPLICITLY EXCLUDING it — "freq/bw/routing_sf/cr; NOT layer_id ... NOT sf_list — F-SF-1 keeps
    //     that across registration". ⇒ `sf_list` is NODE-LOCAL and is PRESERVED from the persisted record (step 5b
    //     above); it is checked here only for NON-EMPTINESS, because an empty set blocks DATA entirely
    //     ([[data-sf-removed]]) — not for agreement with anyone else. ★ [[B230]]: being NODE-LOCAL is also why its
    //     emptiness is a DIFFERENT refusal from the shared triplet's — a different field, set by a different verb.
    //     ⓘ The old check read LIVE values that the retune above it had ALREADY MOVED, under a comment claiming
    //     nothing had changed. Leave (id 0) is exempt.
    // ★★★ [[B230]] — THE EMPTY `sf_list` IS ITS OWN ARM AND IT IS TESTED **FIRST**, and the ORDER IS THE FIX rather
    //     than a preference. The generic arm's remedy is the INLINE tail (`team new freq=… sf=… bw=…`); that tail
    //     carries no `sf_list=` key ([[B211]]), so while the staged set is empty the suggested command CANNOT succeed
    //     — it fails this very check. Reporting the tail-settable complaint first would therefore hand the operator a
    //     failing command a second time, which IS the measured defect. ⇒ whenever the inline remedy would be a lie,
    //     the sf_list arm speaks; the generic arm is reachable only once the inline remedy is actually usable.
    // ⛔ NO OUTCOME MOVES: both arms are the SAME refusal — zero saves, zero live applies, zero airtime. The split
    //    changes WHICH SENTENCE the caller renders, nothing else.
    if (plan.team_id != 0) {
        const double   eff_freq = (cand.freq_mhz > 0.0) ? cand.freq_mhz : req.floor.freq_mhz;
        const uint32_t eff_bw   = cand.bw_hz ? cand.bw_hz : req.floor.bw_hz;
        if (cand.allowed_sf_bitmap == 0) return ProvErr::sf_list_empty;
        if (eff_freq <= 0.0 || cand.routing_sf < 5 || cand.routing_sf > 12
            || eff_bw == 0) return ProvErr::incomplete_phy;
    }

    // (7) ★★ KEY STAGING — ALL FALLIBLE KEY WORK HAPPENS HERE, BEFORE THE SAVE.
    //     ⚠ `split_team_key_tail` validates SYNTAX ONLY: it parses hex and NEVER proves the public half belongs to
    //     the private one. That cross-check lived in `Node::team_channel_key_adopt` (`node.h:228`), which this design
    //     may no longer call post-save ⇒ the transaction does it ITSELF, at stage time, where refusing is free.
    //     ⓘ Derive-and-compare is the house pattern, not an invention: `lib/core/frame_codec.h:735` records that the
    //     T-K3 grant deliberately omits `tkpub` from the wire so the receiver RE-DERIVES it.
    if (req.key_supplied) {
        uint8_t derived_pub[32], canon_priv[32];
        if (!meshroute::team_channel_key_derive(derived_pub, canon_priv, req.key_priv)) {
            crypto_wipe(canon_priv, sizeof canon_priv);
            return ProvErr::key_degenerate;
        }
        uint8_t diff = 0;
        for (int i = 0; i < 32; ++i) diff = static_cast<uint8_t>(diff | (derived_pub[i] ^ req.key_pub[i]));
        if (diff != 0) { crypto_wipe(canon_priv, sizeof canon_priv); return ProvErr::key_mismatch; }
        memcpy(plan.key_pub,  derived_pub, 32);   // ★ the CANONICAL pair: what is persisted is exactly what is installed
        memcpy(plan.key_priv, canon_priv,  32);
        crypto_wipe(canon_priv, sizeof canon_priv);
        plan.key_action = KeyAction::install;
    } else if (req.mint) {
        // `team new` with no tail -> draw 32 B from the SEAM and derive the canonical pair. ⛔ NOT
        // `team_channel_key_mint()`, which draws AND INSTALLS (and is fallible). ★ The owner's ruling that a CREATOR
        // ALWAYS ENDS UP HOLDING A KEYPAIR still holds — now guaranteed by the candidate plus a `void` install
        // rather than by ordering luck, so v1's stash-and-re-apply dance around `set_team_id` disappears entirely.
        uint8_t scalar[32];
        ent.fill(scalar, sizeof scalar);
        uint8_t pub[32], priv[32];
        const bool ok = meshroute::team_channel_key_derive(pub, priv, scalar);
        crypto_wipe(scalar, sizeof scalar);
        if (!ok) { crypto_wipe(priv, sizeof priv); return ProvErr::keygen_failed; }
        memcpy(plan.key_pub, pub, 32);
        memcpy(plan.key_priv, priv, 32);
        crypto_wipe(priv, sizeof priv);
        plan.key_action = KeyAction::install;
        plan.key_minted = true;
    } else {
        // A BARE `team <id>` generates NOTHING: a joiner is meant to RECEIVE the key (a T-K3 grant / the T-K4 QR),
        // and minting an unrelated second key here would silently split the team's readership in two. Membership
        // changed -> the pair is CLEARED via `set_team`; unchanged -> ⛔ PRESERVED, with no key call at all.
        plan.key_action = plan.membership_changed ? KeyAction::clear : KeyAction::preserve;
    }
    // The candidate carries the SAME `KeyAction` the live apply will use — and the tracker asks whether that action
    // would actually MOVE anything in the record (defect ①(b): an identical re-supplied key is not a change).
    if (plan.key_action == KeyAction::install) {
        if (!cand.team_ch_key_present
            || memcmp(cand.team_ch_pub,  plan.key_pub,  32) != 0
            || memcmp(cand.team_ch_priv, plan.key_priv, 32) != 0) differs = true;
        blob_put_team_channel_key(cand, plan.key_pub, plan.key_priv);
    } else if (plan.key_action == KeyAction::clear) {
        // ⓘ ⚠ LIKE THE `team_local_id` CLAUSE, THIS ONE CAN NEVER DECIDE THE VERDICT and is not claimed as tested:
        //   `clear` is reached only with `membership_changed` true (see the `else` above), which already forces
        //   `no_change` false. Kept for completeness; ⛔ a mutation deleting it stays GREEN by construction.
        if (cand.team_ch_key_present) differs = true;
        blob_put_team_channel_key(cand, nullptr, nullptr);
    }
    // preserve: ⛔ the loaded record's key bytes are left untouched — that IS the preservation, and it is by
    // construction not a difference.

    // (7b) ★★★ §UI-16 K2 — THE ACTIVE BINDING (`/mrcfg` v24), COMPOSED FROM THE **SAME** `KeyAction` THE LIVE APPLY
    //      WILL USE, so the record can never claim a key the node does not hold.
    //        install  -> this team's `/mrteams` record is ACTIVE  (the keyring write happens BEFORE the save below)
    //        clear    -> ★ the binding is CLEARED and the KEYRING RECORD IS **RETAINED**. That pair IS the ruling:
    //                    `team 0` (and a switch) destroys the LIVE key via `set_team_id` and forgets the ACTIVATION,
    //                    while the stored key survives — and ⛔ mere knowledge of the public team id can never bring
    //                    it back, because the restore requires this binding and not `team_id` (P-2b).
    //        preserve -> ⛔ untouched: a same-team PHY-only update must not re-arm or drop an activation it never
    //                    mentioned, exactly as it must not touch the key bytes.
    // ⛔ IT IS NOT DERIVED FROM `cand.team_id`, and that is the point of carrying a second id at all — see the field's
    //    own note in `src/device_nv.h`.
    if (plan.key_action == KeyAction::install) {
        if (cand.team_key_team_id != plan.team_id || cand.team_key_active != 1) differs = true;
        cand.team_key_team_id = plan.team_id;
        cand.team_key_active  = 1;
    } else if (plan.key_action == KeyAction::clear) {
        // ⓘ ⚠ LIKE THE `team_local_id` AND KEY-`clear` CLAUSES ABOVE, THIS ONE CAN NEVER DECIDE THE VERDICT and is
        //   not claimed as tested: `clear` is reached only with `membership_changed` true, which already forces
        //   `no_change` false. The ASSIGNMENT is load-bearing (it is what `team 0` persists); only the `differs`
        //   term is defence in depth.
        if (cand.team_key_team_id != 0 || cand.team_key_active != 0) differs = true;
        cand.team_key_team_id = 0;
        cand.team_key_active  = 0;
    }

    // (8) ★ DAD LAST, AND ONLY FOR A CHANGED, NON-ZERO TEAM. A same-team re-key must NOT spend airtime.
    plan.fire_dad = plan.membership_changed && plan.team_id != 0 && plan.projected_is_mobile;
    // ★★ THE `no_change` ROW IS A HARD REQUIREMENT, NOT AN OPTIMISATION: a same-team request that changes nothing
    //    performs ZERO saves and ZERO live applies and reports `no_change`. ⓘ `mrnv::save` already coalesces
    //    byte-identical writes (`src/device_nv.h:484-501`), so the flash cost is avoided anyway — but the
    //    TRANSACTION must decide this explicitly rather than inherit it from a lower layer, because the OLED renders
    //    the outcome and `mr_ui_on_config_saved()` must NOT fire for a save that never happened.
    // ★★★ THE VERDICT, AND ALL FOUR CONJUNCTS ARE LOAD-BEARING — each one is a defect this file has actually shipped:
    //     · `!membership_changed`  — the ORIGINAL rule, and the ① defect when used alone;
    //     · `!differs`             — ①'s fix: does the RECORD move? (a role-projection-only repair must reach NV);
    //     · `live_phy_matches`     — ★ QG ROUND 3: does the RADIO already fly the PHY the operator NAMED?
    //     · `live_key_matches`     — ★ QG ROUND 3: does the NODE already hold the key the operator SUPPLIED?
    // ⛔ AND NEITHER PAIR SUBSUMES THE OTHER, in both directions:
    //     · `differs` alone would be wrong — a record that already named the requested team while the LIVE node is in
    //       another shows no record difference, yet the switch and the DAD are owed (`membership_changed` catches it);
    //     · the RECORD conjuncts alone were wrong — NV holding the requested value proves nothing about the radio or
    //       the installed key, and reporting `no_change` there DISCARDS AN EXPLICIT COMMAND. **An explicit request is
    //       applied whenever the live domain it names does not already match, even when NV already does.**
    // ⓘ WHAT `applied` THEN COSTS IN THAT NEW CASE, stated rather than glossed: one `store.save` of a candidate that
    //   may be byte-identical to the record — which `mrnv::save` COALESCES (`src/device_nv.h:484-501`), so no flash is
    //   spent — plus the one live application that was owed. ⛔ It never adds airtime: `fire_dad` still requires
    //   `membership_changed`.
    plan.no_change = !plan.membership_changed && !differs
                     && live_phy_matches(plan.phy, snap)
                     && live_key_matches(plan.key_action, plan.key_pub, plan.key_priv, snap);
    return ProvErr::none;
}

// ---- the service -----------------------------------------------------------------------------------------------
// RAM: three references and nothing else — no cached candidate, no cached plan, no draft. The transaction is a
// single call, so there is no state between calls to go stale (and no secret to sit in a member).
class ProvisioningService {
  public:
    // ★★ §UI-16 K2 — THE KEYRING IS A **REQUIRED** SEAM, ⛔ NOT AN OPTIONAL/NULLABLE ONE. A binding that could be
    //    left unwired would make a `team new` on that build persist NOTHING durable and report success — which is
    //    [[B240]] itself, arriving through the constructor. Every construction site passes one (C2).
    ProvisioningService(ICfgStore& store, IProvLive& live, IEntropy& entropy, TeamKeyringService& keyring)
        : _store(store), _live(live), _entropy(entropy), _keyring(keyring) {}

    // ★★★ THE TRANSACTION. The order of the seven steps IS the contract (§3.2):
    //   1-4. parse (the caller's) → validate + PROJECT → stage id + key → compose the candidate
    //   4b.  §UI-16 K2: when the plan INSTALLS a key, make it DURABLE in the `/mrteams` keyring FIRST. A refusal
    //        there refuses the whole transaction — ⛔ no `/mrcfg` write, no live apply, no airtime.
    //   5.   ONE `store.save`. On false: return `nv_failed`, apply NOTHING, ⛔ no `fire_dad`, so NO AIRTIME.
    //   6.   on success ONLY, in this order: (a) switch team if membership changed · (b) apply the plan's KeyAction
    //        — `install` calls the `void` load, `preserve` makes NO key call, `clear` happens via `set_team` ·
    //        (c) apply the staged PHY · (d) ★ start DAD LAST, and only for a changed non-zero team.
    //   7.   return the typed result; the CALLER renders it and notifies the UI.
    // ⛔ (a) BEFORE (b) IS LOAD-BEARING AND NOT COSMETIC: `set_team_id` CLEARS the team channel key by design
    //    (§o3-key-lifetime), so installing first and switching second would persist a key the node no longer holds.
    //    The native suite pins the order with a control that swaps the two and goes RED.
    //
    // ★ THE REQUEST IS TAKEN BY MUTABLE REFERENCE ON PURPOSE: it carries a PRIVATE KEY in `key_priv`, and this is
    //   the one place that can guarantee it is zeroed on EVERY exit (§3.10). ⓘ The old console path never wiped its
    //   `tk_priv` frame buffer at all.
    ProvResult apply_team(TeamRequest& req, const meshroute::NodeConfig& live, const ProvSnapshot& snap) {
        ProvResult r{};
        // ★ ONE GUARD PER SECRET CARRIER, EACH DECLARED WITH ITS CARRIER — a guard over the plan could not live up
        //   here, because the plan is now declared in phase 2 and a guard that outlived it would wipe dead storage.
        //   The request's guard covers the PHASE-1 refusals, which return before a plan exists at all.
        struct ReqGuard { TeamRequest& q; ~ReqGuard() { q.wipe(); } } rguard{req};

        // ---------- PHASE 1: validate + stage the id + PROJECT the role. The `NodeConfig` copy lives HERE and nowhere
        //            else, and it is gone before the plan and the candidate below exist (QG defect ③ — and see
        //            `TeamProjection` for the measurement showing that this ordering is stack-NEUTRAL).
        TeamProjection proj{};
        const ProvErr pe = project_team(req, live, snap, _entropy, proj, r.role_refusal);
        r.team_id            = proj.team_id;
        r.membership_changed = proj.membership_changed;
        if (pe != ProvErr::none) { r.err = pe; return r; }  // ⛔ REFUSED: 0 loads, 0 writes, 0 live calls, 0 airtime

        // ---------- PHASE 2: the plan + the candidate. Neither exists until the projection is complete.
        TeamPlan plan{};
        struct PlanGuard { TeamPlan& p; ~PlanGuard() { p.wipe(); } } pguard{plan};
        mrnv::Blob cand{};                               // value-initialised: a failed load may still have written
        if (!_store.load(cand)) {                        // into it (device_nv.h's §nv-ritual warning)
            r.err = ProvErr::nv_load_failed;
            return r;                                    // ⛔ 0 writes, 0 live calls
        }
        const ProvErr e = stage_team_candidate(req, proj, snap, _entropy, plan, cand);
        r.key_action         = plan.key_action;
        r.key_minted         = plan.key_minted;
        r.phy                = plan.phy;
        if (e != ProvErr::none) { r.err = e; return r; }   // ⛔ REFUSED: 0 writes, 0 live calls, 0 airtime
        if (plan.no_change) { r.verdict = ProvVerdict::no_change; return r; }   // ⛔ 0 writes, 0 live calls

        // ---------- §UI-16 K2 — THE KEY IS MADE DURABLE **FIRST**, AND THE ORDER IS THE CONTRACT. ----------
        // ★★★ PERSISTENCE BEFORE ACTIVATION, the same ruling K3 applies to a received grant (spec F-10/R-9): the
        //     `/mrcfg` candidate composed above CLAIMS an active binding, so writing it before the key is stored
        //     would let a reboot find an activation with no key behind it. ⇒ the keyring write happens here, and
        //     ⛔ a keyring refusal REFUSES THE WHOLE TRANSACTION: zero `/mrcfg` writes, zero live applies, zero
        //     airtime — the §3.2 guarantee, unchanged in shape.
        // ⓘ ONLY the `install` arm writes. `preserve` names no material, and `clear` RETAINS the stored record by
        //   ruling — ⛔ leaving a team must never delete the key it was granted (that is what K5's explicit
        //   `USE SAVED KEY` will later offer back).
        // ⚠ WHAT A `keyring_full` REFUSAL COSTS, stated rather than glossed: a `team new` has already drawn its id
        //   nonce and its key scalar from the CSPRNG. It still spends no write, no apply and no airtime — the same
        //   honest qualification `nv_load_failed` carries above.
        // ⓘ `unchanged` is a SUCCESS and the common one (a re-grant of identical material) — ★ ZERO flash writes.
        if (plan.key_action == KeyAction::install) {
            const KeyringResult kr = _keyring.put(plan.team_id, plan.key_pub, plan.key_priv);
            if (kr.verdict == KeyringVerdict::refused || kr.verdict == KeyringVerdict::nv_failed) {
                r.err     = prov_err_of_keyring(kr.err);
                r.verdict = (kr.verdict == KeyringVerdict::nv_failed) ? ProvVerdict::nv_failed : ProvVerdict::refused;
                return r;                                 // ⛔ 0 /mrcfg writes, 0 live calls, 0 airtime
            }
        }

        if (!_store.save(cand)) {                         // ★ EXACTLY ONE save ATTEMPT
            r.err     = ProvErr::nv_save_failed;
            r.verdict = ProvVerdict::nv_failed;
            return r;                                     // ⛔ 0 live calls — live PHY/role/team/keys untouched, 0 airtime
        }
        // ★★ WHAT THE RECORD NOW HOLDS — reported so the ADAPTER can sync `g_persist_team_local_id` (QG defect ②).
        //    Read off the candidate that was just written, ⛔ not re-derived from `membership_changed` by the caller:
        //    the composition rule lives in ONE place and the caller must not hold a second copy of it.
        r.persisted_team_local_id = cand.team_local_id;
        // ---------- POST-SAVE. Every operation below is INFALLIBLE given a validated plan. ----------
        if (plan.membership_changed) _live.set_team(plan.team_id);          // (a) — and it CLEARS the key
        switch (plan.key_action) {                                         // (b)
            case KeyAction::install:  _live.install_key(plan.key_pub, plan.key_priv); break;
            case KeyAction::preserve: break;                               //     ⛔ NO key call at all
            case KeyAction::clear:    break;                               //     already done by (a)'s set_team
        }
        if (plan.phy.present) _live.apply_phy(plan.phy);                    // (c)
        if (plan.fire_dad) { _live.fire_dad(); r.dad_fired = true; }        // (d) ★ LAST — the airtime operation
        r.role_promoted = plan.role_promoted;
        r.verdict       = ProvVerdict::applied;
        return r;
    }

  private:
    ICfgStore& _store;
    IProvLive& _live;
    IEntropy&  _entropy;
    TeamKeyringService& _keyring;   // §UI-16 K2 — the `/mrteams` durable key store (⛔ never null, see the ctor)
};

}  // namespace mrfw
