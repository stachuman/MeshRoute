<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Serial / command-interface cleanup

**Status:** decisions LOCKED (author 2026-06-21). §1 DONE; §2/§3 for the coder; §4 is a standing design constraint. I (quality-gate) verify; author commits.

## 1. Help accuracy — DONE (committed in this session's working tree)
`fw_main.cpp` `dump_help()`: added a `[help] provision:` line for `join`/`create`/`leave` (the live verbs) and corrected the stale `[help] leaf:` line (was "reboot to apply"; now flags `leaf create`/`leaf name` as the low-level/reboot path with `create` as the live front-door). Builds (xiao_sx1262 ✓).

## 2. Send-verb sprawl → 3 verbs + flags + QUOTED text (hard switch)

The 9 send verbs are sugar over 3 orthogonal dims (the parse already computes `by_hash`/`e2e_ack`/`crypt` → a uniform `Command`). Collapse to:

```
send <id|hash> "<text>" [-a] [-e]          # id (≤254 decimal) vs hash (8-hex) AUTO-detected; -a=ack, -e=encrypt
send_channel <ch> "<text>"                 # channel gossip (no ack/enc)
send_layer <hash> <l1,l2,…> "<text>" [-a]  # explicit cross-layer path
```

**Locked decisions:**
- **Text is QUOTED `"…"`** (author) — delimits the free-form body, so flags are unambiguous (before or after the quotes). Cleans the parse; same convention as `create "<name>"`. Apply to all three verbs.
- **Flags `-a` (ack) / `-e` (encrypt)** (author) — `-e` valid only on `send` (hash target); `-e` on an id/channel/layer target = error. `-a` valid on `send`/`send_layer`.
- **id|hash auto-detect:** ≤254 decimal ⇒ id; exactly 8 hex chars ⇒ key_hash32; else `bad_args`. (Unambiguous — an id is ≤3 digits.)
- **HARD SWITCH (author):** REMOVE the old verbs (`send_ack`/`sendhash`/`sendhash_ack`/`sendhashx`/`sendhashx_ack`/`send_layer_ack`) — no aliases. Crypt default still from `cfg set e2e_dm` when `-e` absent (was `sendhash`=force-plain / `sendhashx`=force-crypt → now: `-e`=crypt, absent=e2e_dm default; the old force-plain `sendhash` semantic is dropped — `cfg set e2e_dm off` + no `-e` = plain).
- **⚠ COMPANION CONTRACT BREAKS:** `MeshRouteWire` parses the old verbs (`sendhashx`/`sendhash_ack`/…). **Mark `ios-companion/INBOX_SYNC_CONTRACT.md` "UPDATE REQUIRED — send verbs changed"** (the §Per-message-crypt block names `sendhashx`/`sendhashx_ack`; the send acks). The iOS coder migrates `Command.swift`/`InboxSync` to the new `send … -e`/`-a` quoted form in lock-step.

Firmware: this is mostly the `console_parse.cpp` send block (lines ~115–202) — replace the 7 verb stems with `send`/`send_channel`/`send_layer`, add the quoted-body tokenizer + `-a`/`-e` flag parse + id/hash auto-detect. The emitted `Command` (kind/dst_id/dst_hash/flags/crypt) is unchanged.

## 3. `debug off` by default (author)
The console debug-trace defaults to OFF at boot (today it may default on / persist). Find the debug-enable default (the `g_mr_trace_on` / `debug` state) and make boot default = off; `debug on` turns it on for a session. Keep the `debug [on|off]` command.

## 4. STANDING DESIGN CONSTRAINT — keep the command interface UNIVERSAL (remote-admin-ready)

A node admin will want to **log in remotely and issue commands** (not just local serial/BLE). So the command path must stay **transport-agnostic + auth-ready**:
- The parser (`console_parse.cpp` → `Command`) is already transport-neutral (serial + BLE share it). **Preserve that** — never bake serial/BLE assumptions into a command handler.
- A future **remote-admin** transport (commands over the mesh, or over IP/BLE-relayed) will need: a command-carrying frame + **authentication** (you must not let any node command any node — sign/authorize the admin). That's a separate future feature; this cleanup just must not foreclose it. Keep `Command` the single chokepoint; keep handlers from doing `Serial.*` for *logic* (they may for human echo, but the structured result must go through the result/Push path that the companion already consumes).

## 5. Tests + gate
- Native parse units: `send 5 "hi" -a` ⇒ id 5, ack, body "hi"; `send 1a2b3c4d "hi" -e` ⇒ hash, crypt; `send 12345678 "x"` ⇒ hash (8-hex, not id); flags before/after the quoted body both parse; `-e` on a non-hash target ⇒ error; unquoted body ⇒ error; the removed verbs ⇒ `unknown_verb`.
- `debug` boot default = off (a boot-state unit / inspection).
- Full native suite green; 1 board builds; `help` shows the new send + provision lines.
- Docs: update `help`, `INBOX_SYNC_CONTRACT.md` (the breaking-change banner + new verb syntax), and any console-design doc.

## 6. Build order
1. (DONE) §1 help.
2. §2 `console_parse.cpp` send-block rewrite (3 verbs, quoted body, `-a`/`-e`, id/hash auto-detect, drop old verbs) + the `help` send line + native parse tests.
3. §3 `debug` boot default off.
4. Mark `INBOX_SYNC_CONTRACT.md` UPDATE-REQUIRED (send verbs) so the iOS side migrates.
5. Hand back green-shaped + uncommitted → gate (native + 1 board + help/contract doc check).
