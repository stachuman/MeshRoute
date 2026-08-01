<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Bench test script — 2026-07-31 work

*Short manual pass for real hardware. **Every check here is one that NO automated gate can reach** — either the
platform differs (native is 64-bit), or the file is compiled by neither the native suite nor the simulator, or the
effect is flash wear that only metal exhibits. The 36-scenario corpus and 1046 native cases already cover everything
else, so this is deliberately the residue, not a re-test.*

**UPDATED 2026-08-01 — everything below is now COMMITTED and flashable** through `1918904`:
`§loc-per-send` (B0) · `§team-target-range` (B17) · `§nv1` (B26) · `§team-id-cfg-removal` (B27) · `§role-model` (B28) ·
`§ab1` · `§ab2` · `§ab3` · `§ab4` · `§err-reason` (B32+B33) · `§b22` · `§cl1` · `§o3-key-lifetime` · `§cl2a` · `§cl2b`.
⇒ **the address book is complete and the channel-crypt arc is complete. Part 5 and Part 6 are both live — no ⏳ left.**

**Setup:** one node is enough for Parts 0–2 and 4. Parts 3 and 5 need two nodes that can hear each other.
⚠ **Flash the `xiao_sx1262` (or another 32-bit board) for Part 1** — that is the whole point of check 1.2.

---

## Part 0 — first boot after flashing (2 minutes)

**Two NV bumps landed together, so the first boot resets two things independently.**

| # | do | expect | why it matters |
|---|---|---|---|
| 0.1 | flash, open the console, **watch the boot lines** | `peers = 0 restored (0 pinned, 0 authoritative)` | ★ the `kPeersVersion` 1→2 bump **rejects the old store exactly once**. This line exists so the loss is **observable, not silent** |
| 0.2 | `status` | the node is **unprovisioned** | separate event: `kVersion` 22→23 (`§loc-per-send`). Config + identity are a *different* store from peers |
| 0.3 | reboot again, watch the boot lines | `peers = 0 restored …` again (nothing was pinned yet), **no** `⚠ n REJECTED` | proves 0.1 was the version bump, not a corrupt-blob path |

---

## Part 1 — the parse/role refusals · **metal-only by construction** (5 minutes)

★ **`src/firmware_config*.cpp` is compiled by NEITHER the native suite NOR the simulator.** These refusals have no
automated detector anywhere. Check 1.2 additionally **cannot** be reproduced on the host at all: native
`unsigned long` is 64-bit, so the 32-bit saturation does not exist there.

| # | type | expect | 
|---|---|---|
| 1.1 | `team 0x88A672BA` | **accepted** — joins that team (regression control: the legitimate hex form still works) |
| 1.2 | ★★ `team 4294967296` | **REFUSED**: `> team err: bad target … a team id must be a WHOLE numeric token that FITS IN 32 BITS.` ⇒ **before B17 this joined garbage team `0xFFFFFFFF` on a 32-bit board.** The one check on this page that no test on any host could ever catch |
| 1.3 | `team 88A672BA` (no `0x`) | **REFUSED** — pre-B17 this silently joined **team 88** |
| 1.4 | `team 0xFFFFFFFF` | **accepted** — an explicit in-range value must still work (it is a *range* rule, not a value ban) |
| 1.5 | `cfg set team_id 5` | **`unknown_key`** — the key was **removed** (B27). ⚠ If this *works*, the wrong firmware is flashed |
| 1.6 | `cfg` | the dump contains **no `loc_dm=`** and **still shows `team_id=0x…`** | ★ the *write* was removed, the *read* was kept — the app depends on reading it |
| 1.7 | with a team set, `cfg set mobile 0` | **REFUSED**: `role_refused in_a_team — … Say \`team 0\` … or \`leave\` … FIRST.` |
| 1.8 | `team 0`, then `cfg set mobile 0` | **accepted** — proves the refusal is conditional, not a blanket ban |
| 1.9 | `team 0x1234` on a node that was static, then `status` | it is **now `is_mobile`**, and a `> role -> MOBILE …` line printed **before** the team line | R2: team ⇒ mobile, auto-set |
| 1.10 | on a **gateway** build: `cfg set mobile 1` | **REFUSED**: `role_refused gateway_is_static …` |
| 1.11 | on a node **hosting** ≥1 mobile: `cfg set mobile 1` | **REFUSED**: `role_refused hosting_mobiles n=<N>` — the guests keep their home |

---

## Part 2 — NV write-coalescing · ★★ **the ONLY detector is this bench** (3 minutes)

★★ **A poison probe proved native cannot see this at all**: deleting the change-detection left **1046/1046 green**.
Native cannot observe a write; the corpus cannot compile `src/`. **If it regresses, only metal will tell you.**

| # | do | expect |
|---|---|---|
| 2.1 | `cfg set beacon_ms 900000` (**the value it already has**) ×5 | each replies `ok`, and **no flash write happens** — the H3 guard skips an identical blob |
| 2.2 | `cfg set beacon_ms 600000`, then again ×4 | the **first** writes, the next four do not |
| 2.3 | reboot | `600000` survived | the skip did not skip a *real* change |

⚠ **What you are protecting:** a companion slider bound to `cfg set` would otherwise rewrite the whole blob per tick —
flash wear **and** a wider reset-during-write corruption window, in a tree that has been **NV-bricked once**.

---

## Part 3 — `send -l` · two nodes (5 minutes)

| # | do | expect |
|---|---|---|
| 3.1 | `cfg set lat 0` + `cfg set lon 0`, then `send <peer> "hi" -l` | **REFUSED `no_location`** — you asked for a position and there is none. ⚠ **Not** an encryption error; do not prompt for keys |
| 3.2 | set a real lat/lon, `e2e_dm` **off**, no peer key: `send <peer> "hi" -l` | **REFUSED `unsealable`** — ★ this is B0: **before today the DM flew with your coordinates in the CLEAR** |
| 3.3 | same, plain `send <peer> "hi"` (no `-l`) | **sends normally** — an ordinary DM is untouched |
| 3.4 | acquire the peer key (`reqpubkey` / QR), then `send <peer> "hi" -l -e` | **sends**; the peer's `msg_recv` carries the position |
| 3.5 | `send_layer … -l` | **REFUSED** `err_unsupported` — cross-layer cannot carry a position (the SEALED_RELAY body has no flags word) |
| 3.6 | `send_channel <ch> "x" -l` (no `-t`) | ❌ **`unsealable`** — ⓘ **UPDATED: `-l` on `send_channel` is now REAL** (`§cl2b`). Without `-t` there is no team ⇒ no content key ⇒ it cannot be sealed. **See Part 6 for the working form.** |

---

## Part 4 — the peer store survives a reboot (4 minutes)

★ Before AB1 the store held **pinned keys only, nameless** — so a reboot lost every on-air key and every label.

| # | do | expect |
|---|---|---|
| 4.1 | acquire a peer key **on air** (`reqpubkey 0x<hash>`) | `KEY CACHED hash=0x… conf=authoritative nv=<put>` ⓘ pre-AB2 firmware prints `(on-air, unpinned)` instead |
| 4.2 | **reboot** | boot line shows `peers = 1 restored (0 pinned, 1 authoritative)` — ★ **the on-air key survived, at its true confidence** |
| 4.3 | `send <peer> "x" -e` immediately after that reboot | **works with no `reqpubkey` first** — the capability now persists |
| 4.4 | QR-import a peer (`peerkey <hex64>`), reboot | that one restores as **`1 pinned`** — provenance is preserved, **never widened** |
| 4.5 | `peername 0x<hash> "Alice"`, reboot, `nameof 0x<hash>` | the name **survives** |

⚠ **Two things to watch rather than assert** (both are new steady-state costs, not bugs):
1. **ESP32 NVS churn** — NVS is copy-on-write, the blob doubled (584→1160 B), and a write now happens on **every
   on-air key-learn**. The `unchanged` guard bounds it; confirm a busy mesh does not visibly cycle NVS pages.
2. **Post-boot RX cost** — restoring up to 16 `authoritative` keys means trial-decryption may try up to 16
   candidates per sealed frame right after boot instead of ~0–2.

---

## Part 5 — the address-book verbs (`§ab2` / `§ab3`) — ✅ now live

| # | do | expect |
|---|---|---|
| 5.1 | `peername 0x<hash> "Alice"` | `{"ev":"peer_name_set","hash":…,"name":"Alice"}` |
| 5.2 | `peername 0x<unknown> "X"` | `{"ev":"peer_name_err","reason":"unknown_hash"}` — remedy is `reqpubkey` first |
| 5.3 | a 40-char name | `too_long` — **refused, never truncated** |
| 5.4 | `peername` on a **QR-pinned** peer | **succeeds**, and its `conf` stays `pinned` |
| 5.5 | ★★ **THE BUG YOU HIT ON METAL — the one to try first:** on an off-grid team node, `reqpubkey <team-id>` then `hashof <that id>` | **resolves** (it used to answer `unknown`) |
| 5.6 | `peers` / `peers all` | the book (≤16 rows) vs the full known-node list |

---

## Part 6 — ★ CHANNEL CRYPT, the overnight arc (10 minutes, two nodes in one team)

⚠ **This whole plane is CORPUS-BLIND** — no simulator node can hold a team content key, so **your bench is the only
end-to-end validator.** Native covers the crypto (KATs, nonce-uniqueness, tamper, wrong-key); metal covers that it is
wired up at all.

**Setup** — on node A (a keyholder with a fix):
```
cfg set mobile 1
team new key=… freq=… sf=… bw=… sf_list=…      # mints the team CONTENT key
cfg set lat 521234567 ; cfg set lon -12345678
cfg                                            # expect team_ch_key=1  team_channel_crypt=1
team exportkey                                 # then grant it to node B (team grantkey / QR)
```

| # | do | expect |
|---|---|---|
| 6.1 | ★★ `send_channel 7 "at the col" -t -l -e` | **OK — THE TARGET.** B shows `CH 7 [enc] from=<team_local_id>: at the col`, and **`peers` on B shows A's row with `lat`/`lon`/`loc_age_s`/`loc_src:"team"`** |
| 6.2 | `send_channel 7 "at the col" -t -l` | **OK** — sealed by the `team_channel_crypt` default, no `-e` needed |
| 6.3 | `send_channel 7 "" -t -l -e` | **OK** — a position-only *"here I am"* post |
| 6.4 | `send_channel 7 "x" -l` (no `-t`) | ❌ `unsealable` — no team ⇒ no content key |
| 6.5 | `send_channel 7 "x" -t -g -l -e` | ❌ `unsealable` — the global copy would air **coordinates** in the clear |
| 6.6 | `send_channel 7 "" -t -e` | ❌ empty post (`flags==0`) — ⓘ **sync error only, no `FAILED` push, by design** |
| 6.7 | `cfg set team_channel_crypt 0` then `send_channel 7 "x" -t -l` | ❌ `unsealable` — opted out ⇒ it would go clear |
| 6.8 | …and `send_channel 7 "x" -t -l -e` | **OK** — explicit `-e` still seals |
| 6.9 | `cfg set team_channel_crypt 1`, `cfg set lat 0`, `cfg set lon 0`, then `-t -l` | ❌ **`no_location`** — *"asked for a position and this node has NO fix"*. ⚠ **Not** an encryption error |
| 6.10 | on a member with **no** key: `send_channel 7 "x" -t -e` | ❌ `no_key` → remedy is `team grantkey` from a teammate |
| 6.11 | same node, plain `send_channel 7 "x" -t` | **OK, plaintext** — a keyless member still posts, unchanged |
| 6.12 | ★ **O3:** on A, `team 0x<other>` then `cfg` | **`team_ch_key=0`** — the key is cleared on a team switch and must be re-granted |
| 6.13 | size limits | **173 B** text sealed OK / 174 refused; **167 B** with `-l` OK / 168 refused |

⚠⚠ **THREE things that are NOT bugs if you see them.** ★ **First, the one most likely to look broken: on a small
co-located team every channel post reports `NOT HEARD` / `relayed:false` even though everybody received it.** That is
**owner-ruled expected** (2026-08-01): with all members 1 hop away nobody re-broadcasts, so there is no relay to
overhear, and `relayed` means *"a relay was observed"* — never *"it was delivered"*. `§b38-b40` fixed the **multi-hop**
false negative (s28 confirmed at **2.4 s** instead of exhausting at **43.6 s**); the 1-hop reading is truthful. **To see
the fix work you need a member that is 2+ hops away.** And two more:

⚠ **Two things that are NOT bugs if you see them:** `team_channel_crypt` **does not survive a reboot** (live-only by
design — the default is the privacy-safe value, so a forgotten opt-out heals itself); and a **plaintext** channel post
is still accepted from a keyholder when the toggle is off — that is the documented opt-out, and it emits a loud
`channel_crypt_skipped` rather than downgrading silently.

★ **A refusal now names itself** (`§err-reason`): every one of the above prints `> <err_token> ctr=0 depth=0`, and the
token is the **same string the companion sees** in `{"ack":"…"}` — one regex serves both transports.

---

**If anything in Part 1 or 2 fails, stop and report before continuing** — those are the checks with no second line of
defence. Parts 3–5 have native coverage behind them, so a failure there is a narrower question.
