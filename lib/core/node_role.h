// MeshRoute — lib/core/node_role.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// ★ THE NODE ROLE MODEL — the ONE home for "when is a node MOBILE and when is it STATIC".
// Spec: docs/superpowers/specs/2026-07-31-node-role-model-design.md (rules R1-R5); ruling: open-bug-register B28
// (*"is_mobile should be automatically set when team is in use — unless it is impossible (firmware without teams
// handling)"* + *"we keep `cfg set mobile` — but we make it consistent"*).
//
// WHY A HEADER OF ITS OWN rather than a rule pasted per call site: the role has FIVE write paths that used to
// disagree (spec §1.2), and the invariant below has to hold on paths in BOTH repos halves — `lib/core`
// (Node::set_team_id, the live team switch) and `src/` (the NV boot restore + the `cfg set mobile` / `team` verbs).
// A header in `lib/core` is the only place both can include, so the rule is stated ONCE (U1) and is natively
// testable even though the console paths that consume it are in `src/`, which neither the native suite
// (`test_build_src = no`) nor the simulator (its CMake lists lib/core + lib/console only) compiles. The truth
// tables are pinned in `test/test_dual_layer.cpp`, beside the §clean-team `set_team_id` cases they belong with.
//
// ★ WHERE THE INVARIANT IS ENFORCED — three points, because there are three ways into a non-zero `team_id`:
//   1. `Node::set_team_id` (node.cpp)      — the single LIVE switch (`team new` AND `team <id>` both route here).
//   2. the NV boot restore (src/fw_main.cpp) — NV persists `team_id` and `is_mobile` INDEPENDENTLY, so a provisioned
//      blob can reproduce the outlawed pair with no console involved; without this a power cycle bypasses point 1.
//   3. the `cfg set mobile` refusals (src/firmware_config.cpp) — a DEMOTION while in a team is refused (O1) rather
//      than cascading the team away, and a PROMOTION is refused while hosting mobiles (O2) or on a gateway (R4).
//      The team-implied promotion in `handle_team` refuses through the same table, so `team <id>` is not a back door.
//
// ✖ MISSING, DELIBERATELY — `Node::on_init` is NOT a fourth enforcement point. It accepts a NodeConfig verbatim, so
//   the SIMULATOR (which builds NodeConfig directly, per scenario JSON) and the native suite can still CONSTRUCT
//   `team_id != 0 && !is_mobile`. Two reasons it stays that way, and neither is an oversight:
//     · scope (C1) — the spec names three points; on_init is the one path the 36-scenario corpus actually runs, so
//       normalising there would fold a corpus-visible change into the invariant slice. It is also unnecessary today:
//       all 12 team scenarios / 48 team-bearing nodes are ALREADY is_mobile (QA-measured 2026-07-31), i.e. the
//       corpus already satisfies R2 — which is what makes this slice's byte-identity a real prediction.
//     · on_init already REFUSES an invalid layer config loudly; adding a silent role rewrite next to that would mix
//       "refuse" and "normalise" semantics in one function.
//   ⇒ On the DEVICE every path is covered (points 1-3 above; the boot restore runs before on_init). If a future
//   slice wants the sim held to the invariant too, the honest shape is a REFUSAL in on_init, not a silent fix — and
//   it must be its own slice, because it can move team scenarios.
//
// ★ The organising principle (spec §2.1) — the role is about HOW REACHABILITY IS OBTAINED, not about hardware
// or movement:
//   • STATIC — reachable at its own DAD-assigned node_id on a leaf, and CARRIES the static plane for others.
//   • MOBILE — reachable THROUGH SOMEONE ELSE: a HOME (static host) and/or by `team_local_id` on the team plane.
// ⇒ a team member is a mobile BY CONSTRUCTION, because `team_local_id` IS a through-someone-else identity. That
// is what makes R2 a consequence of the model rather than an extra rule — and it is already asserted, as a
// BUILD dependency, by mr_features.h's `#error "MR_FEAT_TEAM requires MR_FEAT_MOBILE (a team member is
// is_mobile; the team plane reuses the mobile link-layer)"`. This header is that same statement at RUNTIME.
#pragma once
#include <cstdint>
#include "mr_features.h"     // MR_FEAT_MOBILE — is there a mobile plane in this build at all?
#include "node_carriers.h"   // NodeConfig (is_mobile / team_id / is_gateway live there)

namespace MESHROUTE_NS {

// ★ The owner's "unless it is impossible" clause, as a value. MR_FEAT_MOBILE 0 compiles the whole
// roaming-endpoint plane out (node_mobile.cpp, the registration FSM, the mobile accessors), so on such a build
// `is_mobile = true` would name a plane that does not exist. Today MR_FEAT_MOBILE 0 arrives only with
// MR_PROFILE_GATEWAY (which also sets MR_FEAT_TEAM 0), but the two axes are independent by construction, so the
// rule is written against the axis that actually decides it — the MOBILE plane, not the TEAM one.
static constexpr bool kRoleHasMobilePlane = (MR_FEAT_MOBILE != 0);

// What role_enforce() had to correct. NOT a status code — the callers REPORT it (B28 constraint 3: an operator
// whose role changed must be told, C2 spirit), so it names the correction rather than merely flagging one.
enum class RoleFix : uint8_t {
    none = 0,        // already consistent — the overwhelmingly common case (every static node, every lone mobile)
    forced_mobile,   // R2: team_id != 0 on a !is_mobile node -> is_mobile forced ON
    dropped_team,    // "impossible" arm: no mobile plane in this build -> the outlawed team_id dropped to 0 instead
};

// ★★ R2 — `team_id != 0` ⇒ `is_mobile`. Idempotent; safe to call on any NodeConfig at any time.
//
// ⚠ R3 — THE IMPLICATION IS ONE-DIRECTIONAL, and that is load-bearing, not a nicety: `team_id == 0` returns
// `none` and NEVER clears `is_mobile`, because BOTH of these are legitimate configurations that must stay
// reachable — a HOMED MOBILE WITH NO TEAM (an ordinary roaming endpoint on a static network) and an OFF-GRID
// TEAM MEMBER WITH NO HOME (the hiking group, routing on the team plane alone). ⇒ `team 0` (leave a team)
// leaves a mobile a mobile. Do NOT "simplify" this into a symmetric assignment: that would silently un-mobile
// every departing team member.
//
// ⚠ It also never touches `is_gateway` (R4 is a REFUSAL, see role_set_refusal — a gateway is derived from
// n_layers==2 in on_init and is not something a normalisation may re-decide).
static inline RoleFix role_enforce(NodeConfig& cfg) {
    if (cfg.team_id == 0) return RoleFix::none;                 // R3: no team -> nothing to imply, role untouched
    if (!kRoleHasMobilePlane) { cfg.team_id = 0; return RoleFix::dropped_team; }   // no plane to enter -> drop the team, never set the flag
    if (cfg.is_mobile) return RoleFix::none;                    // already consistent
    cfg.is_mobile = true;                                       // R2
    return RoleFix::forced_mobile;
}

// Why an operator's requested role change was REFUSED. ★ Every one of these is a REFUSAL and not a cascade, per
// the owner's O1/O2 rulings: a verb that never mentioned teams must not destroy the team channel key (which is
// UNRECOVERABLE — no seed derives it), the team-DAD id or the team routes, and a promotion must not silently
// orphan hosted mobiles. Only `join` / `create` / `leave` are sanctioned to wipe planes (§clean-team).
enum class RoleSetRefusal : uint8_t {
    none = 0,          // allowed (includes "no transition requested")
    no_mobile_plane,   // promote on a build with MR_FEAT_MOBILE 0 — the plane the flag names is compiled out
    gateway_static,    // R4: is_gateway ⇒ STATIC exclusively — two-layer infrastructure cannot be reached through someone else
    hosting_mobiles,   // O2: promoting a HOST orphans its guests (they lose their home + reverse-ack path, unnotified)
    in_a_team,         // O1: demote while team_id != 0 would break R2 — the operator must say `team 0` / `leave` first
};

// ★ The whole role-transition policy as ONE truth table (R4 + O1 + O2 + the build clause). Pure: no Node, no
// console, no globals — so `test/test_node_role.cpp` pins it natively even though its two consumers
// (`cfg set mobile` and the team-implied promotion in handle_team) live in `src/`, which native does not build.
//
// `now_mobile` is what makes this a TRANSITION rule (R5) rather than a state check: re-asserting the role you
// already hold is not a change and is never refused. The promotion clauses are ordered
// build → derived-role → runtime-state so the refusal an operator cannot fix by shedding guests is reported first.
static inline RoleSetRefusal role_set_refusal(bool want_mobile, bool now_mobile, bool is_gateway,
                                              uint32_t team_id, uint8_t hosted_mobiles) {
    if (want_mobile == now_mobile) return RoleSetRefusal::none;   // no transition -> nothing to rule on
    if (want_mobile) {                                            // STATIC -> MOBILE
        if (!kRoleHasMobilePlane) return RoleSetRefusal::no_mobile_plane;
        if (is_gateway)           return RoleSetRefusal::gateway_static;
        if (hosted_mobiles != 0)  return RoleSetRefusal::hosting_mobiles;
        return RoleSetRefusal::none;
    }
    if (team_id != 0) return RoleSetRefusal::in_a_team;            // MOBILE -> STATIC (O1)
    return RoleSetRefusal::none;
}

}  // namespace MESHROUTE_NS
