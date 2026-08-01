<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# HANDOVER — 2026-08-01

*Written at 100 % context. **Supersedes `docs/2026-07-31-agent-handover.md`** (still useful for the team-routing/mobile
arc that preceded this one). ★ My role in this session was **QA-gate**: coding agents implement, I brief and gate, the
owner commits and rules. The owner granted a **temporary commit window** for the channel-crypt arc — **that arc is
complete, so treat the window as CLOSED unless renewed.** Default back to D4: leave green work uncommitted and report.*

---

## 1. State, verified

| | |
|---|---|
| MeshRoute HEAD | **`aaff9d2`** — clean |
| simulator HEAD | **`fd3295d`** — clean |
| native | **1094 cases / 71859 assertions / 0 failed** |
| corpus | **36/36**, 0 assertion failures |
| ★ s18 keystone | **`1cd21235` / 271629** — **never moved once in this entire session** |
| `sizeof(Node)` | **220976** |
| `lus` md5 | **`907c8524b83150b4f3577a44f93efe7a`** ⚠ re-measure before quoting; **ten of my anchors aged inside one session** |

**Current per-scenario anchors that moved this session:** s22 `808f7abf`/1804 · s28 `0116cafd`/4018 · s29 `a9987eaa`/1943 ·
s34 `59310b6b`/921 · s15 `266d32e2` · s15_metal `f5c6f2b1` · s17 `f930899d` · s33 `2e70c4f5` · sim_9node_base `94b6ad0a`.
⚠ **The authoritative anchor is always the NEWEST `BASELINE.md` note naming a scenario, or a fresh run — never an md5
quoted from a historical note.** Some 25j-era rows in that file are known-stale (s09 documents `92765cdb`, live is
`f171652c`).

**Corpus arithmetic:** `simulation/` holds **37** JSONs = **35** `s*` (including `sim_9node_base`) + `twin_9node_dm`
(a scenario) + `topo_9node` (**a topology fragment, not a scenario**) ⇒ **36 runnable.** A coder miscounted this as 34.

---

## 2. Where the durable records live — read these, not this file, for detail

- **`simulation/BASELINE.md`** — the **evidence store**. One note per slice, **newest at the top**, each carrying its
  gate numbers, its poison matrix, and every premise that was disproven. ⚠ **Newest note wins**; a scenario name greps
  to the oldest match first.
- **`docs/2026-07-30-open-bug-register.md`** — the **index**, now genuinely a register: **537 lines, all 42 bugs (B0–B41)
  exactly once**, numeric order. **Closed = one table row** pointing at the BASELINE tag. **Open = full detail**, because
  an open entry is what a coder is handed. **§0 is the dispatch contract — hand it over verbatim.**
- **`docs/2026-07-31-bench-test-script.md`** — **only what no automated gate can reach.** Six parts, ~30 min.
- **`ios-companion/INBOX_SYNC_CONTRACT.md`** — **QA-owned; a coder never edits it.** Opens with a PENDING box.
- Specs: `2026-07-29-peer-address-book-design.md` · `2026-07-30-channel-crypt-and-location-privacy-design.md` ·
  `2026-07-31-node-role-model-design.md`.

★ **CLAUDE.md now carries a `[MAINTAIN]` block**: **M1** the register is maintained · **M2** the bench script is
maintained · **M3 MeshRoute is NOT deployed, so wire changes are FREE** — never contort a design to dodge a bump; the
residual cost is **attribution**, so a bump gets its own slice (C4 was sharpened accordingly).

---

## 3. What landed this session

**The address book, complete** (owner: *"we finish address book fully before moving to channel message"*):
`§ab1` persists names + **authoritative** keys (`kPeersVersion` 1→2) · `§ab2` the `peername` verb + `conf` on
`peer_key_cached` · `§ab3` the generated `peers` view + `hashof`/`nameof` rewired onto it · `§ab4` the RAM-only
`_peer_loc` ring (320 B, 16 × 20 B).

**Channel crypt, complete** (owner ruled the order): `§b22` the sim's plain `send_channel` matches metal · `§cl1` `-e`
parse + the permanent refusals · `§o3-key-lifetime` the channel key lives as long as its `team_id` · `§cl2a` posts
**seal** · `§cl2b` **`send_channel -t -l -e` works** · `§cl2c` `source_hash` so a located post says **whose** position.

**Bugs closed:** B0 B1 B2 B3 B4 B17 B26 B27 B28 B29 B32 B33 B38 B39 B40 B41.

---

## 4. Open work, in the order I would take it

1. ★★ **Teach the sim to hold a team key** — one `team_ch_priv` (hex-64) config key in
   `orchestrator/runtime/NodeRuntimeWrapper.cpp` mapped onto `Node::team_channel_key_adopt_priv`, plus that key in
   s28/s29. **This is the highest-value work available.** The entire channel-crypto and channel-location plane is
   **corpus-blind by construction** today: no sim node can hold a content key, so nothing seals. ★ **Poison A has
   already measured the exact expected result twice: s22, s28, s29, s34 re-anchor and nothing else.** ⇒ own slice, so
   the re-anchor is attributable.
2. **The sim's `cmd_reply` never adopted `§err-reason`'s `CmdCode` naming** — metal says `err_no_binding`, the sim says
   `error`. ⓘ **This is also B39's route to corpus coverage**, which it has none of: the sim *does* distinguish `queued`
   from `error`, so a scenario could assert `OK queued ctr=0`.
3. **B34** — the sim drops every refusal *reason* at **7** command-reply sites. ⚠ Fixing it **moves P-T1's documented
   signature** (`OK error ctr=0 depth=0`), so it re-anchors *and* re-baselines a detector probe: its own slice, and §0's
   P-T1 row must change in the same commit. **Payoff: it makes the whole `err_*` family assertable.**
4. **B35** — `ingest_channel_m`'s self-skip is plane-blind ⇒ a teammate whose `team_local_id` equals our `node_id` has
   posts **silently swallowed**. Silent is the severity.
5. **B36** — a located DM's position reaches no per-message app surface; `send -l` is visible only via the peers row.
   Decide whether a position belongs on the message or only on the contact — a contract addition on **both** planes.
6. **B30** (`team_id_of_key` silently first-matches an aliased hash on the live send-by-hash path; `§ab3`'s
   `team_id_of_key_freshest` is the reference fix) · **B31** · **B37** (downgraded — see §6).
7. **Parked with reasons** (do not pick up without a ruling): B5–B8, B9–B16, B18–B21, B23–B25. ★ **The dedup entries have
   no pressure behind them: `gateway` flash 56 %, RAM 81.5 % — RAM is the constraint, not flash.**

**Owner decisions still open:** **O4** — `team exportkey` prints the team **private** key on **any** transport including
BLE, which has no auth gate; live since T-K1b, and under the export ruling it is *the only* control protecting that key.
**O2** — I ruled it config-only (`cfg set team_channel_crypt 0`, no per-send plaintext flag); reversible. Plus the OLED
spec's own items.

---

## 5. Rulings from this session that must not be re-litigated

- ★★ **`relayed` on a team post means FIRST RELAY ONLY** — not coverage. **And on a fully-1-hop team it reads `false` at
  100 % delivery**, which the owner ruled is **accepted behaviour, not a defect**: with all members 1 hop away nobody
  re-broadcasts, so there is no relay to overhear. ⇒ **the OLED emergency will spend its full budget and show
  `NOT HEARD` on a small co-located team.** Anyone proposing to "make `relayed` true when delivery succeeded" is
  reversing this. A real delivery signal needs a per-member ack, which was **declined**.
- **Coordinates live on the live push + the RAM peers ring, NEVER the durable inbox** — putting them in the inbox would
  reverse AB4 §2.7's second reason (a captured node must not yield a position history).
- **Only an AUTHENTICATED location is retained.** A plaintext one is pushed and dropped, with `peer_location_unauth`.
- **The team channel key lives exactly as long as its `team_id`** (cleared on any change, including `team 0`);
  `create`/`join` still preserve it.
- **`team_id != 0` ⇒ `is_mobile`**, three enforcement points, one-directional, reported not silent.
- **No auto-`reqpubkey`, ever.** A CRYPTED send to an unresolved hash fails loud.
- **`team_channel_crypt` is live-only, no NV** — the default is the privacy-safe value, so a forgotten opt-out heals on
  reboot. Overrulable (that is a `kVersion` bump and its own slice).

---

## 6. Method that earned its place — the part worth carrying forward

**On briefs.** ★★ **Every premise in a brief is a hypothesis. Twenty slices in this session each returned at least one
correction that changed the work.** Say so in the brief and ask for disproofs; it is where most of the value came from.
⇒ **When a brief and the spec disagree, the SPEC wins and the disagreement is the report's headline.** A coder who
silently followed my brief on `§ab4` would have shipped a security regression **through a green gate**.

**On probes.**
- ★ **A 0/N poison result means "the corpus cannot reach it", NEVER "it is inert."** Pair it with a **positive control**
  that proves the site executes.
- ★★ **Before reporting any probe, ask whether the instrument was CAPABLE of moving anything.** `§o3` discarded two
  probes that returned a clean-looking 0/36 and could not have fired.
- ★★ **The two-probe pair, when the corpus cannot reach the feature at all:** poison the **precondition** (does the site
  run?), then poison the **change under that poison** (is my code the delta?). `§cl2b`'s A/A′ is the model.
- **A same-site control is the line immediately above your change** — with no intervening branch, that proves reach.

**On counting.** ★★ **Four of my counts were wrong, all from pattern-matching instead of evaluating.** The rule that
survived: **evaluate the PREDICATE *and* match the VERB EXACTLY.** `startswith("send_channel")` silently swallows
`send_channel_g`/`_b`; a `grep -c` for an event name matches its **prose** in a `_desc`; counting a shared substring
conflates the two surfaces a brief was written to distinguish.

**On builds.** ⚠⚠ **Restoring a probed file is not enough — prove the rebuild happened, and the two build systems need
OPPOSITE fixes: ninja keys on mtime ⇒ `touch` after restoring; PlatformIO keys on a content signature ⇒ delete the
`.o`.** In both cases the control is **the binary md5 returning to its clean value.** Three incidents.

**On measurement.**
- **The corpus validates BEHAVIOUR, never FORMAT** — one `lus` binary drives both ends of every link, so every
  "byte-identical" claim is byte-identical *against ourselves*. **The KATs are the only real format check**, and
  `§cl2a`'s is the pattern: recompute key/nonce/AAD **from the spec wording** and open with the raw primitive.
- **Diff flash-bearing SECTIONS, not object size** — an enlarged header inflates DWARF with 0 flash bytes.
- **RAM is the trustworthy number**; flash has a ±32 B `__DATE__`/`__TIME__` floor, and **a UB loop can be CHEAPER than
  the correct one** (`§idbind-loop` measured +16/+32/+16 for *fixing* it).
- **A ledger entry that defers its numbers to another document silently ages the entry above it** (`§cl2a` left the
  per-target `sizeof` figures reading as AB4's).

**Two crypto facts I got wrong, recorded so nobody repeats them.** ★★ **AAD authenticates; it does NOT separate
keystreams** — XChaCha20's keystream is `f(key, nonce)`, so "bind the sender in the AAD" prevents nothing. Under a
**shared** key the **nonce** is the only place sender-distinguishing entropy can live. And ★ **a fresh per-post random
seed must be the load-bearing uniquifier**; a per-node counter cannot be, because every member holds the same key.

**Process.** ⚠ **The session scratchpad is SHARED between concurrent sessions — prefix every path with your slice tag.**
⚠ **Never `git add -A` while a slice is in flight** — mine swept a coder's five source files into a docs-titled commit.
★ **A dead error branch is evidence about its callee**: when a guard cannot fire, ask whether the callee can produce the
condition (that is how `§ab3` found a shipped device hang).
