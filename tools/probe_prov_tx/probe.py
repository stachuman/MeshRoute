#!/usr/bin/env python3
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §PROV-TX STRUCTURAL PROBE — the checks the native suite structurally CANNOT make, plus a negative control for
# every one of them.  Spec: docs/superpowers/specs/2026-08-17-team-provisioning-transaction-design.md §5; [[B207]].
#
# ★★ WHY GREP AND NOT A TEST, STATED PLAINLY. Two of §5's requirements are about SOURCE SHAPE rather than behaviour:
#      · ⛔ "NO FALLIBLE KEY CALL AFTER THE SAVE" — `team_channel_key_adopt` / `adopt_priv` / `mint` must not appear
#        in the post-save block at all. A behavioural test cannot see a call the fake seam does not expose; the type
#        system already makes it inexpressible THROUGH `IProvLive`, and this is the check that the seam was not
#        bypassed and that the console's own copies of those calls are gone.
#      · the six live mutations must be ABSENT from `handle_team`, which lives in a TU no automated gate compiles
#        (`src/firmware_config.cpp`: `test_build_src = no`, and the simulator builds `lib/core` + `lib/console` only).
#    These are WEAKER than the native cases and are labelled as such — but each carries a NEGATIVE CONTROL that
#    reinstates the defect in an in-memory COPY and proves the check turns RED. ⛔ A grep-shaped check is this arc's
#    easiest vacuous instrument, which is exactly why no check here ships without its control.
#
# ★ Every match count is PRINTED, never merely compared, and nothing is ever written: the sources are opened
#   read-only and every mutation is applied to a string in memory.
#
# USAGE:  tools/probe_prov_tx/run.sh          # checks + the negative controls (the controls run BY DEFAULT)
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SVC = os.path.join(ROOT, 'src', 'firmware_provisioning_service.h')
CFG = os.path.join(ROOT, 'src', 'firmware_config.cpp')

FALLIBLE = ('team_channel_key_adopt_priv', 'team_channel_key_adopt', 'team_channel_key_mint')


def neutral(txt):
    """A same-LENGTH copy with comments blanked (offsets preserved, so indices address the raw text).

    ★ THIS IS LOAD-BEARING, not hygiene: every one of the three fallible primitive names appears MANY times in the
    prose of both files (they are what the design withdrew), so a raw grep would report calls that do not exist —
    the probe would then be enforcing a rule it cannot see, or refusing a correct tree. String literals are blanked
    too: the console's own error text names them.
    """
    out = list(txt)
    i, n = 0, len(txt)
    while i < n:
        c = txt[i]
        if c == '/' and i + 1 < n and txt[i + 1] == '/':
            while i < n and txt[i] != '\n':
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and txt[i + 1] == '*':
            while i < n and not (txt[i] == '*' and i + 1 < n and txt[i + 1] == '/'):
                if txt[i] != '\n':
                    out[i] = ' '
                i += 1
            j = min(i + 1, n - 1)
            out[i] = out[j] = ' '
            i += 2
        elif c == '"':
            i += 1
            while i < n and txt[i] != '"':
                if txt[i] == '\\':
                    out[i] = ' '
                    i += 1
                if i < n and txt[i] != '\n':
                    out[i] = ' '
                i += 1
            i += 1
        else:
            i += 1
    return ''.join(out)


def body(txt, signature):
    """The brace-balanced body of a function located by its SIGNATURE — never by line number."""
    i = txt.index(signature)
    i = txt.index('{', i)
    depth, j = 1, i + 1
    while j < len(txt) and depth:
        if txt[j] == '{':
            depth += 1
        elif txt[j] == '}':
            depth -= 1
        j += 1
    return txt[i:j]


# ---------------------------------------------------------------------------------------------------------------
# The checks. Each returns (ok, detail). They take the NEUTRALISED sources so prose can never satisfy or break one —
# ⚠ PLUS the RAW ones, because S10 is a check about a STRING LITERAL and `neutral()` blanks every literal by design.
def s1_no_fallible_in_service(svc, cfg, rsvc, rcfg):
    """S1 — the three FALLIBLE key primitives appear NOWHERE in the transaction header (§3.6 step 6)."""
    hits = {name: svc.count(name) for name in FALLIBLE}
    return (sum(hits.values()) == 0), 'counts=%s' % hits


def s2_install_key_is_void(svc, cfg, rsvc, rcfg):
    """S2 — `IProvLive::install_key` returns `void` (§3.6 step 5 / the v3 correction)."""
    m = re.search(r'virtual\s+(\w+)\s+install_key\s*\(', svc)
    if not m:
        return False, 'install_key declaration not found'
    return (m.group(1) == 'void'), 'return type=%r' % m.group(1)


def s3_post_save_order(svc, cfg, rsvc, rcfg):
    """S3 — inside `apply_team`: exactly ONE save, and set_team -> install_key -> apply_phy -> fire_dad after it."""
    b = body(svc, 'ProvResult apply_team(')
    saves = b.count('_store.save(')
    idx = {k: b.find('_live.%s(' % k) for k in ('set_team', 'install_key', 'apply_phy', 'fire_dad')}
    save_at = b.find('_store.save(')
    ok = (saves == 1 and all(v > save_at for v in idx.values())
          and idx['set_team'] < idx['install_key'] < idx['apply_phy'] < idx['fire_dad'])
    return ok, 'saves=%d save_at=%d order=%s' % (saves, save_at, idx)


def s4_no_live_call_before_save(svc, cfg, rsvc, rcfg):
    """S4 — ⛔ NO `_live.` call precedes the durable write. (S3 checks the four names; this catches a fifth.)"""
    b = body(svc, 'ProvResult apply_team(')
    save_at = b.find('_store.save(')
    before = [m.start() for m in re.finditer(r'_live\.', b) if m.start() < save_at]
    return (not before), 'live calls before the save=%d' % len(before)


def s5_console_has_no_mutations(svc, cfg, rsvc, rcfg):
    """S5 — ★★ ALL SIX of [[B207]]'s live mutations are GONE from `handle_team` (and so is its own `mrnv::save`)."""
    b = body(cfg, 'void handle_team(')
    banned = ('team_channel_key_adopt_priv', 'team_channel_key_adopt', 'team_channel_key_mint',
              'set_team_id', 'team_dad_fire', 'mobile_register_phy', 'mrnv::save')
    hits = {name: b.count(name) for name in banned}
    return (sum(hits.values()) == 0), 'counts=%s' % hits


def s6_blob_helper_delegates(svc, cfg, rsvc, rcfg):
    """S6 — the node-reading blob helper DELEGATES to the one explicit-material helper (U2, ruled v4 §4)."""
    b = body(cfg, 'static void blob_take_team_channel_key(')
    return (b.count('blob_put_team_channel_key(') == 1 and 'team_ch_pub[' not in b), \
        'delegations=%d open-coded field writes=%d' % (b.count('blob_put_team_channel_key('),
                                                      b.count('team_ch_pub['))


# ---------------------------------------------------------------------------------------------------------------
# QG-HOLD CORRECTIONS 2026-08-17. Each of the four below pins a property no native case can reach — either because it
# is about the SHAPE of `apply_team` (S8/S9) or because it lives in `src/firmware_config.cpp`, a TU neither the native
# suite nor the simulator compiles (S7/S10). ⛔ Each carries its own negative control below.
def s7_adapter_syncs_persist_tracker(svc, cfg, rsvc, rcfg):
    """S7 - QG DEFECT (2): `handle_team` syncs `g_persist_team_local_id` to the value the transaction SAVED.

    Without it a newly assigned `team_local_id` can never reach NV when team-DAD happens to re-pick the OLD team's
    number (`src/fw_main.cpp:1030`'s `team_changed` stays false). Unreachable natively: the tracker is a `fw_main`
    global and this TU is compiled by no automated build. Two clauses, so neither half can drift:
      (a) the assignment exists exactly once and reads THE SERVICE'S REPORT - not a re-derivation from
          `membership_changed`, which would be a second copy of the composition rule;
      (b) it sits AFTER the `verdict != applied` guard, so a refusal or a `no_change` can never reach it.
    """
    b = body(cfg, 'void handle_team(')
    n = b.count('g_persist_team_local_id = res.persisted_team_local_id;')
    guard = b.find('if (res.verdict != mrfw::ProvVerdict::applied)')
    at = b.find('g_persist_team_local_id')
    return (n == 1 and guard >= 0 and at > guard), 'assignments=%d guard_at=%d sync_at=%d' % (n, guard, at)


def s8_projection_precedes_plan_and_blob(svc, cfg, rsvc, rcfg):
    """S8 - QG DEFECT (3): the `NodeConfig` projection is COMPLETE before the plan and the Blob exist.

    The withdrawn comment claimed the copy could not coexist with `TeamPlan` + `mrnv::Blob`; it did, because both were
    declared above the call. This check pins the order that makes the comment true.
    WHAT IT DOES NOT CLAIM: any stack benefit. Measured with `-fstack-usage`, `handle_team`'s ARM frame is 856 B with
    the projection first, 856 B with the plan and the Blob first, and 856 B with the copy deleted outright -- at
    `-Ofast` the copy is scalarised away. The order is pinned because the comment asserts it, not because it saves RAM.
    """
    b = body(svc, 'ProvResult apply_team(')
    proj = b.find('project_team(')
    plan = b.find('TeamPlan plan{}')
    blob = b.find('mrnv::Blob cand{}')
    ok = proj >= 0 and plan > proj and blob > proj
    return ok, 'project_at=%d plan_at=%d blob_at=%d' % (proj, plan, blob)


def s9_phase2_holds_no_nodeconfig(svc, cfg, rsvc, rcfg):
    """S9 - the same property from the other side: PHASE 2 names no `NodeConfig` at all.

    S8 pins the ORDER; this pins that the split was not undone by giving phase 2 its own copy. Comments are blanked
    before this runs, so the many mentions of `NodeConfig` in the prose cannot fail it. As with S8, this is a
    STRUCTURAL property (the comment must describe the code) and NOT a claim about frame size.
    """
    b = body(svc, 'inline ProvErr stage_team_candidate(')
    return (b.count('NodeConfig') == 0), 'NodeConfig mentions in phase 2=%d' % b.count('NodeConfig')


def s10_no_flash_claim(svc, cfg, rsvc, rcfg):
    """S10 - QG DEFECT (4): NOTHING claims *"no flash was changed"* on the save-failure arm.

    Not established, and not establishable here: a backend can fail AFTER a partial physical write ([[B193]]). THIS
    CHECK READS THE RAW SOURCES on purpose - the claim lives in an `F("...")` string literal and `neutral()` blanks
    every literal, so the neutralised text can never see it. The console must not contain the phrase at all, in code
    OR prose: it is the file that speaks to the operator. The header may name it ONCE, in the prohibition itself.
    """
    hits = {'svc': rsvc.count('no flash was changed'), 'cfg': rcfg.count('no flash was changed')}
    return (hits['cfg'] == 0 and hits['svc'] <= 1), 'occurrences=%s' % hits


# ---------------------------------------------------------------------------------------------------------------
# QG-HOLD ROUND 3 (2026-08-17). The behavioural half of this correction is pinned by the native suite (the two
# `QG3` cases + 12 mutation controls). What NO automated build can see is that the ADAPTER actually FILLS the live
# half of `ProvSnapshot` — `src/firmware_config.cpp` is compiled by neither the native suite nor the simulator, so a
# snapshot left at its defaults would make every live comparison read "freq 0.0 / no key", i.e. it would report
# `applied` for every keyed or PHY-carrying request and NEVER `no_change`. ⇒ S11 pins the six reads, and S12 pins the
# purity that forces them to live in the adapter at all.
def s11_adapter_fills_live_snapshot(svc, cfg, rsvc, rcfg):
    """S11 - `handle_team` fills all SIX live snapshot fields from the EXISTING core accessors, before `apply_team`.

    ⛔ Not "a live field is set somewhere": each field must be assigned EXACTLY ONCE, from the specific accessor the
    correction is built on, and BEFORE the transaction runs. A field left unassigned is the silent half of this defect
    -- the service would then compare the request against a zeroed snapshot and could never report `no_change`.
    """
    b = body(cfg, 'void handle_team(')
    want = (('snap.live_freq_mhz',          'g_node.active_freq_mhz()'),
            ('snap.live_bw_hz',             'g_node.active_bw_hz()'),
            ('snap.live_routing_sf',        'c.layers[0].routing_sf'),
            ('snap.live_allowed_sf_bitmap', 'c.layers[0].allowed_sf_bitmap'),
            ('snap.live_key_pub',           'g_node.team_channel_pub()'),
            ('snap.live_key_priv',          'g_node.team_channel_priv()'))
    at = b.find('apply_team(')
    counts, ok = {}, at >= 0
    for field, src in want:
        hits = [m.start() for m in re.finditer(re.escape(field) + r'\s*=\s*' + re.escape(src), b)]
        counts[field.replace('snap.live_', '')] = len(hits)
        if len(hits) != 1 or hits[0] > at:
            ok = False
    return ok, 'assignments=%s apply_team_at=%d' % (counts, at)


def s12_service_reads_no_globals(svc, cfg, rsvc, rcfg):
    """S12 - the transaction header touches NO firmware global, which is WHY the live facts arrive as a snapshot.

    The header is compiled by the native suite (no `fw_main`, no board), so a `g_node` read here would not merely be
    impure -- it would not link. Pinning it makes the split a checked property instead of an intention: comments are
    blanked before this runs, and the prose names `g_node` many times.
    """
    hits = {name: svc.count(name) for name in ('g_node', 'g_hal', 'g_persist_team_local_id', 'LORA_FREQ', 'LORA_BW')}
    return (sum(hits.values()) == 0), 'counts=%s' % hits



# ---------------------------------------------------------------------------------------------------------------
# [[B209]] (2026-08-18). The behavioural half is pinned by the native suite (the three §B209 cases + their seam
# mutation). What NO automated build can see is which CORE SEAM the ADAPTER reaches for: `src/firmware_config.cpp`
# is compiled by neither the native suite nor the simulator, and the defect was ONE identifier — `apply_phy` calling
# `mobile_register_phy` (retune + AUTHORISE static-home attachment + immediate DISCOVER) instead of the retune-only
# `mobile_retune_phy`. ⇒ S13 pins the identifier at the one site that matters.
def s13_apply_phy_is_retune_only(svc, cfg, rsvc, rcfg):
    """S13 - [[B209]]: `DeviceProvLive::apply_phy` calls the RETUNE-ONLY seam, and never the registration verb.

    Two clauses, because either alone is weak:
      (a) `mobile_retune_phy` is called EXACTLY ONCE in the adapter body -- the retune must still happen;
      (b) `mobile_register_phy` appears ZERO times in that body -- it is the authorising verb, and a team PHY tail
          must not decide that this device wants a static home.
    ⛔ SCOPED TO THE FUNCTION BODY ON PURPOSE, not to the file: `handle_mobile` (the explicit console verb
    `mobile register [freq=...]`) legitimately calls `mobile_register_phy`, and a file-wide count would either fail
    on that correct call or have to whitelist it. Comments are blanked before this runs, so the prose above the call
    -- which names the withdrawn call in order to record the defect -- can neither satisfy nor break the check.
    """
    b = body(cfg, 'void apply_phy(')
    retune, reg = b.count('mobile_retune_phy('), b.count('mobile_register_phy(')
    return (retune == 1 and reg == 0), 'mobile_retune_phy=%d mobile_register_phy=%d' % (retune, reg)


# ---------------------------------------------------------------------------------------------------------------
# [[B211]] (2026-08-18). The behavioural half — resolve the unspecified `sf_list` from the PERSISTED RECORD before
# both comparisons — is pinned by the native suite (§B211 pins 1/1b/2/5 + four source mutations). What NO automated
# build can see lives entirely in `src/firmware_config.cpp`, which neither the native suite nor the simulator
# compiles: that the console STOPS SENDING the collapsed bitmap (S14), that it REPORTS the resolved one through the
# EXISTING formatter and its sink (S15), and ★ that `mobile register` was left ALONE (S16 — a POSITIVE control).
def s14_team_request_sends_no_sf_list(svc, cfg, rsvc, rcfg):
    """S14 - [[B211]]: `handle_team` sends the "not specified" sentinel, never the parser's collapsed bitmap.

    `parse_phy_tail` sets `allowed_sf_bitmap = 1u << pa.sf` from the ROUTING sf, and the console used to copy that
    into the request -- which silently replaced the DATA SF set and PERSISTED it (metal-confirmed: `6,7` -> `7`).
    Two clauses:
      (a) the collapsed value is copied ZERO times;
      (b) the sentinel `rq.phy.allowed_sf_bitmap = 0` is assigned EXACTLY ONCE -- deleting the field entirely would
          leave the default 0 and pass clause (a), so the assignment is required to be there and readable.
    """
    b = body(cfg, 'void handle_team(')
    collapsed = b.count('rq.phy.allowed_sf_bitmap = phy.allowed_sf_bitmap')
    sentinel = len(re.findall(r'rq\.phy\.allowed_sf_bitmap\s*=\s*0\s*;', b))
    return (collapsed == 0 and sentinel == 1), 'collapsed copies=%d sentinel assignments=%d' % (collapsed, sentinel)


def s15_team_phy_line_reports_the_resolved_sf_list(svc, cfg, rsvc, rcfg):
    """S15 - [[B211]]: the `> team PHY:` line reports the RESOLVED sf_list, through THE existing formatter, to `out`.

    Three clauses, and each is a defect this project has actually shipped:
      (a) `print_sf_list` is called EXACTLY ONCE in `handle_team`, on `res.phy.allowed_sf_bitmap` -- the value the
          TRANSACTION resolved and applied, ⛔ not the request's (which is the sentinel 0);
      (b) it is passed `out` -- ★ its own comment (`firmware_commands.cpp:247-249`) records the B95 defect where a
          formatter wrote to the global `mrcon` and half a line went to a different sink;
      (c) THE EXACT OPERATOR-VISIBLE LINE, read from the RAW source because `neutral()` blanks every literal: the
          echo ends ` kHz sf_list=` -> `> team PHY: freq=869.463 sf=7 bw=125.00 kHz sf_list=6,7`.
    """
    b = body(cfg, 'void handle_team(')
    calls = b.count('print_sf_list(')
    sunk = b.count('print_sf_list(out, res.phy.allowed_sf_bitmap)')
    anchor = rcfg.find('out.print(F("> team PHY: freq="))')
    literal = rcfg.count('F(" kHz sf_list=")') if anchor >= 0 else 0
    inline = (anchor >= 0 and ' kHz sf_list=' in rcfg[anchor:anchor + 400])
    return (calls == 1 and sunk == 1 and literal == 1 and inline), \
        'print_sf_list calls=%d sunk-on-out=%d literal=%d in-echo=%s' % (calls, sunk, literal, inline)


def s16_mobile_register_is_unchanged(svc, cfg, rsvc, rcfg):
    """S16 - ★ THE POSITIVE CONTROL (pin 4): `mobile register` and the SHARED parser are UNTOUCHED.

    ⛔ THE ONE FACT THAT SHAPED [[B211]]'s FIX: `parse_phy_tail` is SHARED -- `handle_team` calls it and so does
    `handle_mobile`. The owner's ruling changes the TEAM path only, so the parser must still collapse the bitmap for
    the mobile verb and `handle_mobile` must still hand the parser's own `phy` straight to `mobile_register_phy`.
    ⚠ A POSITIVE control fails only if this slice broke something: ⛔ do not try to make it RED. Its own negative
    controls below prove it is not vacuous.
    """
    p = body(cfg, 'static PhyTail parse_phy_tail(')
    m = body(cfg, 'void handle_mobile(')
    collapse = p.count('phy.allowed_sf_bitmap = (uint16_t)(1u << pa.sf)')
    reg = m.count('mobile_register_phy(phy)')
    touched = m.count('allowed_sf_bitmap')
    return (collapse == 1 and reg == 1 and touched == 0), \
        'parser collapse=%d mobile_register_phy(phy)=%d bitmap writes in handle_mobile=%d' % (collapse, reg, touched)


# ---------------------------------------------------------------------------------------------------------------
# [[B210]] (2026-08-18). The line `  team-DAD: local_id=N` used to print on EVERY applied team command that left the
# node a mobile in a team — a display line ASSERTING an airtime event that may not have occurred (the mirror of this
# project's standing rule that a display-shaped field must never MAKE an airtime decision). The truth was already
# computed and returned: `ProvResult::dad_fired`, set only where `_live.fire_dad()` is called. The native suite pins
# `dad_fired` itself; ⛔ what it CANNOT see is the print site, because `src/firmware_config.cpp` is compiled by
# neither the native suite nor the simulator. ⇒ S17 pins the gate, the id SOURCE, and the operator-visible literal.
def s17_team_dad_line_is_gated_on_dad_fired(svc, cfg, rsvc, rcfg):
    """S17 - [[B210]]: the `team-DAD` line prints IF AND ONLY IF the transaction actually fired DAD.

    Five clauses, because every weaker shape of this check is one this arc has already shipped green:
      (a) the gate is `res.dad_fired`, present EXACTLY ONCE in `handle_team`;
      (b) ⛔ the OLD gate (`res.team_id != 0 && g_node.config().is_mobile`) appears ZERO times -- it is true of every
          applied team command on a mobile, which IS the defect;
      (c) ★ the id is still read LIVE (`g_node.team_local_id()`) inside the gated statement. `res.persisted_team_local_id`
          carries the CANDIDATE's value, which is 0 on exactly the membership-change case this line prints (design v2:
          `team_local_id = 0` means DAD-pending) -- swapping to it would print `local_id=0` every time, a silent defect
          no build and no native case would catch. So the candidate field must NOT appear inside the statement;
      (d) pin 4, CONFIRMED rather than assumed: the site sits AFTER the `verdict != applied` guard, so `no_change`
          and every refusal return before reaching it;
      (e) ★ THE COUNTED DISCRIMINATOR the brief asks for -- the operator-visible literal occurs EXACTLY ONCE in the
          raw source. Read RAW because `neutral()` blanks every string literal by design. Presence/absence of this
          literal is the whole behaviour under test: deleting the line would silence a GENUINE DAD.
    """
    b = body(cfg, 'void handle_team(')
    gate = len(re.findall(r'if\s*\(\s*res\.dad_fired\s*\)', b))
    old = b.count('res.team_id != 0 && g_node.config().is_mobile')
    at = b.find('if (res.dad_fired)')
    stmt = b[at:at + 200] if at >= 0 else ''
    live = stmt.count('out.println(g_node.team_local_id())')
    cand = stmt.count('res.persisted_team_local_id')
    guard = b.find('if (res.verdict != mrfw::ProvVerdict::applied)')
    literal = rcfg.count('F("  team-DAD: local_id=")')
    ok = (gate == 1 and old == 0 and live == 1 and cand == 0
          and guard >= 0 and at > guard and literal == 1)
    return ok, ('dad_fired gates=%d old-gate=%d live-id reads=%d candidate-id reads=%d '
                'guard_at=%d site_at=%d literal=%d' % (gate, old, live, cand, guard, at, literal))


CHECKS = [
    ('S1 no fallible key primitive in the transaction header', s1_no_fallible_in_service),
    ('S2 IProvLive::install_key returns void',                 s2_install_key_is_void),
    ('S3 one save, then set_team -> key -> phy -> DAD',        s3_post_save_order),
    ('S4 no live call precedes the durable write',             s4_no_live_call_before_save),
    ('S5 handle_team holds none of B207 six mutations',        s5_console_has_no_mutations),
    ('S6 blob_take_team_channel_key delegates',                s6_blob_helper_delegates),
    ('S7 handle_team syncs the team_local_id persist tracker', s7_adapter_syncs_persist_tracker),
    ('S8 the role projection precedes the plan AND the Blob',  s8_projection_precedes_plan_and_blob),
    ('S9 phase 2 holds no NodeConfig copy',                    s9_phase2_holds_no_nodeconfig),
    ('S10 nothing claims "no flash was changed"',              s10_no_flash_claim),
    ('S11 handle_team fills the SIX live snapshot fields',      s11_adapter_fills_live_snapshot),
    ('S12 the service reads no firmware global',                s12_service_reads_no_globals),
    ('S13 apply_phy uses the RETUNE-ONLY core seam',            s13_apply_phy_is_retune_only),
    ('S14 the team request sends NO collapsed sf_list',          s14_team_request_sends_no_sf_list),
    ('S15 the team PHY line reports the RESOLVED sf_list',       s15_team_phy_line_reports_the_resolved_sf_list),
    ('S16 mobile register + the shared parser are UNCHANGED',    s16_mobile_register_is_unchanged),
    ('S17 the team-DAD line is gated on res.dad_fired',         s17_team_dad_line_is_gated_on_dad_fired),
]

# ---------------------------------------------------------------------------------------------------------------
# The NEGATIVE CONTROLS. Each reinstates a real defect in an in-memory copy and names the check it must redden.
# `(name, target, from, to, check_id)` — target 'svc' or 'cfg'. A control whose substitution does not apply is a
# FAILURE, not a skip: a control that silently changed nothing would be the vacuous instrument this file exists to
# avoid.
CONTROLS = [
    # the v3 defect: the post-save install becomes the FALLIBLE adopt_priv
    ('C1 post-save install becomes the fallible adopt_priv', 'svc',
     '_live.install_key(plan.key_pub, plan.key_priv)',
     '_live.team_channel_key_adopt_priv(plan.key_priv)', 'S1'),
    # install_key stops being void
    ('C2 install_key returns bool', 'svc',
     'virtual void install_key(', 'virtual bool install_key(', 'S2'),
    # the v1 defect: the key is installed BEFORE the switch that clears it
    ('C3 install_key before set_team', 'svc',
     'if (plan.membership_changed) _live.set_team(plan.team_id);',
     'if (plan.key_action == KeyAction::install) _live.install_key(plan.key_pub, plan.key_priv);', 'S3'),
    # the B207 defect: DAD fires BEFORE the save (the airtime clause)
    ('C4 fire_dad hoisted above the save', 'svc',
     'if (!_store.save(cand)) {', 'if (plan.fire_dad) _live.fire_dad();\n        if (!_store.save(cand)) {', 'S4'),
    # the B207 defect in the console: handle_team switches the team itself again
    ('C5 handle_team calls set_team_id again', 'cfg',
     'const mrfw::ProvResult res = prov_service().apply_team(rq, c, snap);',
     '(void)g_node.set_team_id(rq.team_id);\n    const mrfw::ProvResult res = prov_service().apply_team(rq, c, snap);',
     'S5'),
    # the field-drop rot: the blob helper open-codes the write again instead of delegating
    ('C6 blob helper open-codes the fields', 'cfg',
     'blob_put_team_channel_key(b, g_node.team_channel_pub(), g_node.team_channel_priv());',
     'b.team_ch_pub[0] = 0; b.team_ch_priv[0] = 0; b.team_ch_key_present = 0;', 'S6'),
    # QG DEFECT (2) REINSTATED: the adapter never syncs the tracker -- this WAS the shipped defect
    ('C7 the persist-tracker sync is deleted', 'cfg',
     '    g_persist_team_local_id = res.persisted_team_local_id;\n', '', 'S7'),
    # ...and the subtler half: the sync hoisted ABOVE the applied-verdict guard, so a refusal / no_change corrupts the
    # tracker with a value that was never written
    ('C8 the sync hoisted above the applied guard', 'cfg',
     '    const mrfw::ProvResult res = prov_service().apply_team(rq, c, snap);',
     '    const mrfw::ProvResult res = prov_service().apply_team(rq, c, snap);\n    g_persist_team_local_id = res.persisted_team_local_id;', 'S7'),
    # QG DEFECT (3) REINSTATED: the plan and the Blob are declared ABOVE the projection -- the coexistence the
    # withdrawn comment denied
    ('C9 the plan and the Blob declared above the projection', 'svc',
     '        TeamProjection proj{};\n        const ProvErr pe',
     '        TeamProjection proj{};\n        TeamPlan plan{}; mrnv::Blob cand{};\n        const ProvErr pe', 'S8'),
    # ...and phase 2 grows its own NodeConfig copy again, putting the 256 B back beside the plan and the candidate
    # ⚠ THE SIGNATURE IN THIS CONTROL WAS RE-PINNED for QG round 3 (phase 2 now also takes the `ProvSnapshot`, so the
    #   LIVE domains can be compared). The probe reported it UNUSABLE (match count 0) the moment the signature moved,
    #   which is the behaviour a control must have — it refused to pretend rather than silently testing nothing.
    ('C10 phase 2 takes its own NodeConfig copy', 'svc',
     'inline ProvErr stage_team_candidate(const TeamRequest& req, const TeamProjection& proj, const ProvSnapshot& snap,\n                                   IEntropy& ent, TeamPlan& plan, mrnv::Blob& cand) {',
     'inline ProvErr stage_team_candidate(const TeamRequest& req, const TeamProjection& proj, const ProvSnapshot& snap,\n                                   IEntropy& ent, TeamPlan& plan, mrnv::Blob& cand) {\n    meshroute::NodeConfig probe{}; (void)probe;', 'S9'),
    # QG DEFECT (4) REINSTATED: the console re-asserts what it cannot establish
    ('C11 the console claims no flash was changed', 'cfg',
     'persistence FAILED: NO live state was applied',
     'no flash was changed: NO live state was applied', 'S10'),
    # ---- QG ROUND 3's controls. Each reinstates a HALF-DONE adapter, which is the failure mode a native test cannot
    # see: the service would be correct and the device would still discard the request.
    ('C12 the live PHY reads are deleted from the adapter', 'cfg',
     '    snap.live_freq_mhz          = g_node.active_freq_mhz();\n', '', 'S11'),
    ('C13 the live KEY pointers are deleted from the adapter', 'cfg',
     '    snap.live_key_priv          = g_node.team_channel_priv();\n', '', 'S11'),
    ('C13b the live BANDWIDTH read is deleted from the adapter', 'cfg',
     '    snap.live_bw_hz             = g_node.active_bw_hz();\n', '', 'S11'),
    # ...and the subtler half, which is why S11 checks POSITION and not just presence: the transaction runs on a
    # snapshot that has not been filled yet, so every live comparison reads 0.0 / nullptr.
    ('C14 the transaction runs BEFORE the snapshot is filled', 'cfg',
     '    mrfw::ProvSnapshot snap{};\n',
     '    mrfw::ProvSnapshot snap{};\n    { const mrfw::ProvResult r0 = prov_service().apply_team(rq, c, snap); (void)r0; }\n', 'S11'),
    # the purity the split depends on: the header reaches for the node itself
    ('C15 the service reads g_node directly', 'svc',
     'inline bool live_phy_matches(const ProvPhy& phy, const ProvSnapshot& snap) {',
     'inline bool live_phy_matches(const ProvPhy& phy, const ProvSnapshot& snap) {\n    (void)g_node.active_freq_mhz();', 'S12'),
    # ---- [[B209]]'s control: THE DEFECT ITSELF, reinstated verbatim -- the adapter routed back through the
    # registration verb. This is pin 4 of the register row.
    ('C16 apply_phy routes back through mobile_register_phy', 'cfg',
     '        g_node.mobile_retune_phy(phy);',
     '        g_node.mobile_register_phy(phy);', 'S13'),
    # ...and the other half, which is why S13 counts the retune too: deleting the call outright would silently stop
    # the team PHY from ever being applied, and a check that only BANNED the registration verb would call that a pass.
    ('C17 the retune call is deleted outright', 'cfg',
     '        g_node.mobile_retune_phy(phy);\n', '', 'S13'),
    # ---- [[B211]]'s controls. C18 is THE DEFECT ITSELF, reinstated verbatim: the console copying the parser's
    # routing-sf-collapsed bitmap into the team request, which is what dropped SF6 on metal and persisted it.
    ('C18 the team request copies the collapsed bitmap again', 'cfg',
     '            rq.phy.allowed_sf_bitmap = 0;',
     '            rq.phy.allowed_sf_bitmap = phy.allowed_sf_bitmap;', 'S14'),
    # ...and the silent half: the field is simply dropped. The default IS 0, so the behaviour would be correct while
    # the intent became invisible -- which is why S14 requires the assignment to be WRITTEN, not merely defaulted.
    ('C19 the sentinel assignment is deleted (left to default)', 'cfg',
     '            rq.phy.allowed_sf_bitmap = 0;\n', '', 'S14'),
    # the report regresses to the three-field line that altered a fourth field it never mentioned
    ('C20 the sf_list is dropped from the team PHY echo', 'cfg',
     'out.print(F(" kHz sf_list="));\n        mrfw::print_sf_list(out, res.phy.allowed_sf_bitmap); out.println();',
     'out.println(F(" kHz"));', 'S15'),
    # ★ the B95 defect: the formatter reaches past the sink it was given
    ('C21 the formatter writes to the global instead of `out`', 'cfg',
     'mrfw::print_sf_list(out, res.phy.allowed_sf_bitmap)',
     'mrfw::print_sf_list(mrcon, res.phy.allowed_sf_bitmap)', 'S15'),
    # ...and the subtler one: it reports what was ASKED FOR (the sentinel 0 -> `-`) instead of what LANDED
    ('C22 the echo reports the REQUEST bitmap, not the resolved one', 'cfg',
     'mrfw::print_sf_list(out, res.phy.allowed_sf_bitmap)',
     'mrfw::print_sf_list(out, rq.phy.allowed_sf_bitmap)', 'S15'),
    # ⛔ THE WRONG TURN THE BRIEF NAMED FIRST: "fix" the SHARED parser, which silently changes `mobile register` too
    ('C23 the SHARED parser stops collapsing the bitmap', 'cfg',
     'phy.allowed_sf_bitmap = (uint16_t)(1u << pa.sf);', '', 'S16'),
    # ...and the other way of leaking the team ruling into the mobile verb: override the parser at the call site
    ('C24 handle_mobile overrides the parser bitmap', 'cfg',
     '            g_node.mobile_register_phy(phy);',
     '            phy.allowed_sf_bitmap = 0;\n            g_node.mobile_register_phy(phy);', 'S16'),
    # ---- [[B210]]'s controls. C25 is THE DEFECT ITSELF, reinstated verbatim: the gate that is true of every applied
    # team command on a mobile, so the line claims DAD on a same-team re-key that spent no airtime.
    ('C25 the old always-true gate is restored', 'cfg',
     'if (res.dad_fired) { out.print(F("  team-DAD: local_id="))',
     'if (res.team_id != 0 && g_node.config().is_mobile) { out.print(F("  team-DAD: local_id="))', 'S17'),
    # ...the blunter form: no gate at all, so the line prints on every applied team command whatsoever
    ('C26 the gate is deleted outright (always prints)', 'cfg',
     '    if (res.dad_fired) { out.print(F("  team-DAD: local_id="))',
     '    { out.print(F("  team-DAD: local_id="))', 'S17'),
    # ...and the half-fix: gated on the RESULT SHAPE rather than the airtime decision -- still true of every
    # same-team re-apply that leaves the node in a team
    ('C27 gated on res.team_id != 0 alone', 'cfg',
     'if (res.dad_fired) { out.print(F("  team-DAD: local_id="))',
     'if (res.team_id != 0) { out.print(F("  team-DAD: local_id="))', 'S17'),
    # ⛔⛔ THE SILENT DEFECT THE BRIEF NAMED: the id source swapped to the candidate's value, which is 0 on exactly
    # the membership-change case that now reaches this line -- `team-DAD: local_id=0` on every genuine DAD.
    ('C28 the id source swapped to the candidate value', 'cfg',
     'out.println(g_node.team_local_id());',
     'out.println(res.persisted_team_local_id);', 'S17'),
    # ...and the other direction, which is why the literal is COUNTED: the line removed altogether would silence a
    # GENUINE DAD, and a check that only banned the old gate would call that a pass.
    ('C29 the whole team-DAD line is deleted', 'cfg',
     '    if (res.dad_fired) { out.print(F("  team-DAD: local_id=")); out.println(g_node.team_local_id()); }\n',
     '', 'S17'),
]


def main():
    raw_svc = open(SVC, encoding='utf-8').read()
    raw_cfg = open(CFG, encoding='utf-8').read()
    rc = 0

    print('== §PROV-TX structural checks (%s, %s)' % (os.path.relpath(SVC, ROOT), os.path.relpath(CFG, ROOT)))
    nsvc, ncfg = neutral(raw_svc), neutral(raw_cfg)
    results = {}
    for i, (label, fn) in enumerate(CHECKS, 1):
        cid = 'S%d' % i
        ok, detail = fn(nsvc, ncfg, raw_svc, raw_cfg)
        results[cid] = ok
        print('   %-6s %-52s %s   [%s]' % (cid, label, 'PASS' if ok else 'FAIL', detail))
        if not ok:
            rc = 1

    print('== negative controls (each must turn its check RED)')
    for label, target, frm, to, cid in CONTROLS:
        src_svc, src_cfg = raw_svc, raw_cfg
        n = (src_svc if target == 'svc' else src_cfg).count(frm)
        if n != 1:
            print('   %-6s %-52s UNUSABLE  [match count=%d, expected 1]' % (cid, label, n))
            rc = 1
            continue
        if target == 'svc':
            src_svc = src_svc.replace(frm, to)
        else:
            src_cfg = src_cfg.replace(frm, to)
        fn = dict((('S%d' % i), f) for i, (_l, f) in enumerate(CHECKS, 1))[cid]
        ok, detail = fn(neutral(src_svc), neutral(src_cfg), src_svc, src_cfg)
        red = not ok
        print('   %-6s %-52s %s   [%s]' % (cid, label, 'RED' if red else 'GREEN=BAD', detail))
        if not red:
            rc = 1

    print('== %s' % ('OK' if rc == 0 else 'FAILURES ABOVE'))
    return rc


if __name__ == '__main__':
    sys.exit(main())
