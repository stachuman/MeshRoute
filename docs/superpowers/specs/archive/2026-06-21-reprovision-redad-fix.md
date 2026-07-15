<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Fix: `join`/`create` on an already-joined node doesn't re-DAD (no J frame, node_id stays 0)

**Status:** BUG FIX, READY FOR CODER. Precise root cause + an existing in-codebase pattern to reuse. I quality-gate; author commits.

## The bug (two symptoms, ONE cause)
1. **`create`** on a node that was already running → `node_id` stays **0** (never claims an id).
2. **`join`** on a node that's been running a while → **no J frame fires** (no DAD).

**Root cause:** `provision_apply_live()` (fw_main — the live path for `join`/`create`/`leave`) drops the id with `set_identity(0, …)`, but **`set_identity` does NOT clear `_joined`** (node.cpp:38). The re-DAD it then issues — `CmdKind::join` — is *idempotent once joined* (node.cpp:746: `if (_joined) return CmdResult{queued,…};`). So on an already-`_joined` node the join command no-ops: no `join_start_claim`, no claim, no J, and `node_id` never leaves 0. A FRESH node works (it's not `_joined`), which is why this only bites a node that already DAD-adopted an id.

The DAD picker is already correct/random (`join_choose_candidate_id` → `free_list[_hal.rand_range(...)]`, 17..254) — the problem is purely that the DAD never *runs* on a reprovision.

## The fix — reset the join FSM on reprovision (reuse the `forced_rejoin` pattern)
`forced_rejoin()` (node_join.cpp:260) already does the exact reset for a re-DAD: `_joined=false`, `_join_claim.active=false`, cancel `kJoinClaimGuardTimerId`, `join_deny_id(prior)`, drop our own `(prior,_key_hash32)` id_bind entry, `set_identity(0,…)`, then re-claim. But it early-returns `if (!_joined) return;`, so it can't serve a fresh node.

**Extract the reset into a reusable Node method** and call it from the reprovision path:

```cpp
// node.h / node_join.cpp — the join-FSM reset shared by forced_rejoin + the console reprovision verbs.
void Node::reset_join_for_reprovision() {
    if (_joined) {                                   // joined-only cleanup (a fresh node skips it)
        const uint8_t prior = _node_id;
        join_deny_id(prior);                         // so the re-DAD picks a FRESH id, not the same one
        drop our own (prior, _key_hash32) id_bind binding;   // (the loop from forced_rejoin)
    }
    _joined = false;
    _join_claim.active = false;
    _hal.cancel(kJoinClaimGuardTimerId);
    set_identity(protocol::unjoined_node_id, _key_hash32);   // 0 = unprovisioned
}
```
- `forced_rejoin()` becomes: `if (!_joined) return; … reset_join_for_reprovision(); join_start_claim(reason);` (DRY — same behaviour as today).
- **`provision_apply_live()` (fw_main):** replace the bare `g_node.set_identity(0,…); lc.layers[0].node_id = 0;` with `g_node.reset_join_for_reprovision(); lc.layers[0].node_id = 0;` Then the existing `if (do_dad) { CmdKind::join }` actually fires (since `_joined` is now false) → claim-after-listen → J → a random free id.

### Notes
- **The J is not instant — by design.** `CmdKind::join` arms `kJoinListenTimerId` (`join_listen_ms`) and fires `join_start_claim` *after* the listen window (claim-after-listen, so the picker sees existing ids first). So expect the J ~`join_listen_ms` after the verb, not immediately. The bug is that today it fires *never* on a joined node.
- **Issue 1 "random in range":** falls out — with the old id denied + binding dropped, `join_choose_candidate_id` returns a uniform-random free 17..254 (gateways excluded). No separate "set to random" code needed.
- **`leave`** (do_dad=false): `reset_join_for_reprovision()` leaves the node unprovisioned + idle (no claim) — same intended end state as today, now also correctly clearing `_joined`.
- Alternative considered + rejected: make `set_identity(0)` itself clear `_joined`. Works, but it wouldn't drop the stale `(prior,hash)` binding or deny the old id, so the picker could re-prefer the old id (issue 1 wants fresh). The explicit reset is clearer and matches `forced_rejoin`.

## Tests + gate
- **Native unit:** a node that has joined (DAD-adopted an id, `_joined==true`) → `reset_join_for_reprovision()` + `on_command(CmdKind::join)` ⇒ `_joined==false`, a claim becomes active / a J is emitted (drive the listen timer), and the adopted id is in 17..254 and ≠ the prior id (deny worked). A FRESH node (`_joined==false`) → same path still DADs. `leave` path → unprovisioned, no claim.
- `forced_rejoin` behaviour unchanged (its existing addr-conflict test stays green).
- Full native suite green; the t91/t92/t93 DAD gates + s18 etc. unchanged (this only adds a reset hook; the picker/claim wire is untouched); 1 board builds.

## Build order
1. Extract `reset_join_for_reprovision()` (node.h decl + node_join.cpp) from `forced_rejoin`; refactor `forced_rejoin` to call it.
2. `provision_apply_live()` (fw_main) → call `reset_join_for_reprovision()` instead of the bare `set_identity(0,…)`.
3. Native unit (above) + run the suite + 1 board.
4. Hand back green-shaped + uncommitted → I gate.
