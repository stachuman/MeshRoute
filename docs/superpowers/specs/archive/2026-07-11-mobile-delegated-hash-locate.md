<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile-delegated hash-locate (send-by-hash via the home) — DIRECTLY IMPLEMENTABLE

**Status:** coder spec (2026-07-11). Fixes the bench-confirmed RREQ storm + node crashes: a REGISTERED mobile that does its OWN `send_by_hash` floods an `H` query with `origin = _node_id` (its LOCAL id, e.g. 17). The answer then has to route back to a mobile local id, which is **invisible to the static plane** (our whole separation design) → the answerer floods `F RREQ dst=17` forever (never resolves) → airtime storm + the deep `handle_h → RREQ` path that overflowed the 4 KB loop stack (see the 8 KB-task fix, separate). A mobile must NEVER hash-locate on the static plane.

**Decision (user, 2026-07-11): DELEGATE the locate to the home, modelled as a DM + an APP-type code.** The mobile hands the send to its home; the **home** resolves the target hash + sends it, re-using the EXISTING origination machinery (`send_by_hash` already parks-an-origination + floods `H` as a static node). The reply routes back to the mobile unchanged via `source_hash` (already the mobile's hash on a mobile DM). No new "park-a-forward" primitive. Consistent with "a mobile is reached/reaches only via `home_id + hash`" and reuses the `do_post_ack` + `_mobile_reg` fork end-to-end.

## Wire: one new DATA APP type (backward-compat; `team_id==0`/static-inert)
`DATA_TYPE_MOBILE_SEND = 14` (`frame_codec.h`, next free after 13). A registered mobile → its home: "send the enclosed payload to `dst_hash`." It is a normal unicast DM (`dst = home_id`, `APP` set, `TYPE = 14`, `DST_HASH = target_hash`, `SOURCE_HASH = mobile_hash`); the **inner body = the app payload** the user asked to send. Only a home (`_mobile_reg_n > 0`) ever consumes it → static/non-host is byte-identical.

## Part 1 — mobile side (`node_hashlocate.cpp` `send_by_hash`)
Before the `park_send + emit_hash_query` fallback (the storm path), add the delegate branch:
```cpp
// §mobile: a REGISTERED mobile is a leaf — it does NOT hash-locate on the static plane (origin=local id -> the answer can't
// route back -> RREQ storm). DELEGATE: send a MOBILE_SEND DM to the home carrying dst_hash=target; the home resolves + sends.
// The reply (E2E-ack) routes back to us via source_hash (=our hash, stamped on every mobile DM). Static/unregistered: skip.
if (_cfg.is_mobile && _my_mobile_reg.active) {
    return do_send(_my_mobile_reg.home_id, body, body_len,
                   flags, crypt,
                   /*override_dst_hash=*/key_hash32,          // the TARGET hash (home resolves it)
                   /*type=*/DATA_TYPE_MOBILE_SEND);           // thread the app type through do_send (add the param)
}
```
`do_send` already threads `override_dst_hash`; add an optional `uint8_t type = 0` param it stamps into the DATA TYPE byte (0 → today's behaviour). `stamp_origin` already sets `origin = home_id`, `source_hash = mobile_hash` for a registered mobile → the reply address is correct with no extra work.

## Part 2 — home side (`node_mac_rx.cpp` `do_post_ack`, the `!is_forward` block)
The MOBILE_SEND DM is addressed to the home (`dst = home_id` → `!is_forward`). Add a fork BEFORE the deliver, AFTER the existing hosted-mobile last-mile fork (grep `mobile_lastmile_fwd`):
```cpp
// §mobile delegate: a hosted mobile asked us to send its payload to dst_hash. Re-originate via send_by_hash (existing
// resolve/park-origination), but stamp SOURCE_HASH = the requesting mobile's hash (pa.source_hash) so the target's reply
// routes back to the MOBILE, not us. origin stays home_id (anti-spam bills the home relaying for its leaf). Gated on
// _mobile_reg_n>0 -> non-host inert. A malformed/keyless request drops.
if (pa.type == DATA_TYPE_MOBILE_SEND && _active->_mobile_reg_n > 0 && ui && ui->has_dst_hash) {
    // MULTI-MOBILE disambiguation: the reply address is the requesting mobile's HASH (pa.source_hash), NOT its local id —
    // the local id is home-scoped + §18-collision-prone and is only ever the last-mile `next`, always DERIVED from the hash
    // via _mobile_reg. VERIFY source_hash is one of OUR hosted mobiles (else the reply couldn't route back to us anyway);
    // reject a request whose source_hash we don't host.
    bool ours = false; for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
        if (_active->_mobile_reg[i].key_hash32 == pa.source_hash) { ours = true; break; }
    if (ours)
        send_by_hash_on_behalf(ui->dst_key_hash32, /*reply_to_hash=*/pa.source_hash,
                               <inner payload span>, pa.flags & ~APP-typing, crypt_of(pa.flags));
    become_free();
    return;
}
```
When target X's E2E-ACK comes back (`dst_hash = source_hash = mobile_hash`), the EXISTING hosted-mobile fork (`do_post_ack:640`, `_mobile_reg[i].key_hash32 == ui->dst_key_hash32`) matches the exact mobile by hash and last-miles to *its* local id — so a home hosting N mobiles routes every reply to the right one with no new lookup.
`send_by_hash_on_behalf(target_hash, reply_to_hash, body, flags, crypt)` = `send_by_hash` with a `source_hash` override:
- resolve `target_hash` (authoritative id_bind → send now; mobile_home_find → via that home; else park + `emit_hash_query` **as the home**, `origin = home_id`);
- when it flies, the `do_send` stamps `SOURCE_HASH = reply_to_hash` (the mobile's hash) instead of our own, so the E2E-ack's `dst_hash = source_hash = mobile_hash` → resolves to `home_id` via our own proxy (`handle_h` `_mobile_reg` scan) → we last-mile it to the mobile. Everything after this point is the EXISTING reach-a-mobile path.
- Add `reply_to_hash` to the parked-send record so a parked delegated send keeps the mobile's reply address.

## Part 3 — defense: never RREQ a bare mobile local id
Independent guard (belt-and-suspenders, in case any path still addresses a mobile by local id): before `emit_route_request(dst)` (`node_route_discovery.cpp`), skip if `dst` is one of our hosted mobiles (`_mobile_reg` local-id match) — a hosted mobile is reached by last-mile, never RREQ. And if `dst` is a known mobile local id with no host here, drop rather than flood. This stops the storm even for a stray local-id dst.

## What does NOT change
- The reverse leg (target → mobile): the E2E-ack routes by `source_hash = mobile_hash` → home proxy → last-mile. Already works (s21).
- Reaching a mobile FROM static: `dst_hash = mobile_hash` → home proxy → last-mile. Already works.
- Team-plane sends (local id within the team): unchanged — a team DM routes via `_rt_team`, not this path.

## Gate
- **native** green (+ tests: a registered mobile's `send_by_hash` emits a MOBILE_SEND DM to its home + NO `emit_hash_query`; a home consuming MOBILE_SEND re-originates with `source_hash = requester`; the RREQ-for-a-hosted-mobile guard).
- **s18 md5 == `3ac88d40e00d2605ff66659f696d52bf`** (all mobile-gated → static-inert).
- s07/s21 0 assertion failures; ★ a NEW/extended scenario: a registered mobile `send_hash <static-target>` delivers end-to-end with **NO `»tx H origin=<local id>` and NO `F RREQ dst=<local id>`** from the mobile (the storm is gone).
- 4 boards SUCCESS. Bench: the mobile's log shows the DM to its home (no self-H); the static nodes show no `RREQ dst=17`.
