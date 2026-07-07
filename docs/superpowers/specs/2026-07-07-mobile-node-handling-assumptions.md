<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile Node Handling — Design Assumptions (LIVING DOC)

**Status:** accumulated design assumptions for the mobile-node feature (the last big MeshRoute item). NOT a spec/plan yet — this is the shared context we build the algorithm on. Sections marked **[DECIDED]** are locked; **[OPEN]** are still to resolve (the immediate two are §12/§13). Update this doc as we go.

## 1. What a mobile node is  [DECIDED]
A device that **physically moves during operation** (walker ~1.35 m/s, cyclist ~3.4, courier/vehicle ~5+). It is a **static config flag** (`is_mobile`, set at provision/join — not runtime-toggled), inherited from the Lua model. Its **key-hash is a permanent identity**; its position, layer, and `node_id` are all transient.

## 2. Movement patterns  [DECIDED]
- **Gradual** — moves between *neighbouring* nodes; the direct-neighbour set changes one node at a time (trackable).
- **Teleport** — vanishes and reappears in a *completely new location*, near none of its former neighbours (only reactive re-discovery / self-re-announce can find it).

## 3. Core consequence of §2  [DECIDED]
Because **teleport is in scope**, the algorithm **must have a reactive re-discovery core** (flood / RREQ / locate-by-hash). Any proactive "follow the mobile" mechanism is an **optimization on top** for the gradual case — never the whole answer.

## 4. Registrar + mailbox model  [REVISED 2026-07-07 — registrar DYNAMIC & storage-free; mailbox is a separate OWNED node]
The earlier single "fixed home" splits into **two roles on different nodes**:
- **Registrar (`home_id`) — DYNAMIC live-delivery waypoint.** `home_id` is *always the node the mobile currently registers with* (§13), and it **changes as the mobile moves**. It answers the locate (H-query) and does the last-mile forward. **It holds NO inbox** — it is usually *someone else's* node, so we never burden it with our mail. [DECIDED]
- **Mailbox — a SEPARATE node the owner OWNS.** For store-and-forward the owner declares a **secondary address**: a node they own (its own hash, that trusts them). When the mobile is off/unreachable a sender may send the DM **there** instead; the owner reads it later (companion), and *maybe* the mailbox forwards it on when the mobile reappears [OPEN §-mailbox]. Sending to the mailbox is a **plain DM to a normal owned node** — it **reuses the existing persistent inbox**, no new protocol. [DECIDED shape]

This still decouples the sender from movement (locate resolves the current registrar), still survives teleport/cross-layer (staleness → §13 redirect / re-locate), and — the point of the revision — **the storage burden sits on a node the owner controls, not a stranger's.**

## 5. Delivery modes  [settled by §13 + §4; one optimization OPEN]
- **Live (default)** — the §13 path: DM to `home_id` (the registrar), which does the last-mile forward. Works while the registrar is fresh.
- **Direct (optional optimization)** — a sender that has cached the mobile's *actual* attachment may route straight to it, skipping the registrar hop. ⚠ inherits staleness (a cached attachment can be dead) → falls back to re-locate.
- **Mailbox (the guarantee)** — mobile off/unreachable → send to the owner's **separate owned mailbox node** (§4); it holds the DM (existing inbox), owner reads later. **NOT the registrar.**
- **Guarantee rule [DECIDED]:** live is the fast path (via a stranger's registrar); on failure → re-locate (§13 redirect); still unreachable → the **owned mailbox**. The guarantee lives on a node the owner controls.

## 6. Delivery semantics  [DECIDED]
No buffering **in the mesh**, and **not on the registrar** (a stranger's node). Best-effort live transport; store-and-forward is a **separate node the owner owns** (§4 mailbox / secondary address), reusing the existing receiver-side persistent inbox.

## 7. Traffic  [DECIDED]
**Bidirectional** — a mobile both sends and receives.

## 8. Transit rule — two SEPARATE planes  [REVISED 2026-07-07 — v1 keeps the current firmware rule]
The **static network** and the **dynamic (mobile) network** are **separate routing planes**: the static plane **ignores mobiles** (never routes *through* one) and mobiles **ignore the static plane** (never carry static traffic). The **only bridge is the `home_node`** — a static node that hosts the mobile and does the ≤1-hop last mile.
- **v1: the current firmware rule is CORRECT, not divergent** — `route_uses_mobile_as_transit` (node_routing.cpp:617) blocking all through-mobile routing *is* "static ignores mobile." **No change needed for v1.** (Delivery never needs a mobile relay — the home_node reaches the mobile in **1 hop**, §13 — so the earlier "static DM must cross a mobile cluster" worry is **moot**.)
- **Intra-team routing = the normal DV on the mobile plane [RESOLVED 2026-07-07, §9]:** team members emit `is_mobile`+`team_id` **routing BCNs** (static ignores them) → a **team-scoped mobile-plane routing table** → multi-hop intra-team routing, **FULLY REUSED** (not new code). A mobile **not** in a team does not route (identity-only, reaches its home_node only). *(So the mobile plane is itself a normal routing network, disambiguated from the static plane by `is_mobile`; the home_node bridges the two.)*

## 9. Two routing regimes  [REVISED 2026-07-07]
- **Static → mobile [v1]:** via the `home_node` bridge (§4/§13) — the static plane routes to the home_node, which does the ≤1-hop last mile. No mobile relay involved.
- **Mobile ↔ mobile within a team [RESOLVED 2026-07-07 — REUSE the DV stack on the mobile plane]:** a team runs the **standard DV routing on the `is_mobile`+`team_id` plane** — team members emit **full routing BCNs marked `is_mobile`+`team_id`** (the static plane ignores them, §8), building a **team-scoped routing table** among the members. Intra-team traffic (unicast + the group channel) routes **multi-hop via the normal routing stack — nothing new**. The mobile keeps a **mobile-plane routing table** (the team); it does NOT mirror the static routing table — for static reach it just hits its home_node (1 hop). A **lone / non-team mobile stays identity-only** (no routing BCN — reaches its home_node only). *(This resolves F3: multi-hop team group-chat works, by reuse.)*

## 10. Power / sleep  [DECIDED]
Battery-bound. The mobile **sleeps** (duty-cycled) — which argues *against* proactive fast-beaconing and *for* §5's inbox/pull. Care-of registration must be **cheap** (piggyback on existing beacon activity + an occasional uplink when it settles).

## 11. Cross-layer  [DECIDED 2026-07-07 — lighter under Option B]
A mobile **cannot seamlessly cross a layer** — crossing = **re-register with a `home_node` on the NEW layer** for a fresh local id (Option B makes this cheap: no DAD, just a register exchange; the hash stays stable, the old registrar ages out + gets the redirect breadcrumb §13). A brief **park** during the transition is acceptable. **Reaching a cross-layer mobile reuses the existing cross-layer DM:** the sender re-locates (H-query resolves to the new-layer home_node) and routes to it **via a gateway** (the standard bridge) → the home_node does the ≤1-hop last mile. **No mobile-specific cross-layer machinery.** ⚠ **E1 caveat (§17):** the existing cross-layer *layer-resolution* is imperfect (pre-existing, all originators) — v1 accepts the best-effort behavior (mobile→mobile is more reliable via the home_node proxy + epoch).

## 12. Team formation = a mobile network (overlay)  [REVISED 2026-07-07 — C-frame/create reuse, PHY-bound, `is_mobile`+team-id-scoped]
Mobiles moving together recognize a **team** and sync via beacons.
- **Recognition is on-air already** — the `is_mobile` bit (§14). Explicit **team-id/hash rides a beacon EXT-TLV** (no spare flag bit; the ext block is free — no wire_version bump). [DECIDED wire path]
**A team is a mobile OVERLAY on the creator's layer** — same PHY, same byte-0 `leaf_id` nibble, but every team frame is **`is_mobile`-marked + team-id-scoped** (option (b): never its own nibble, never a gateway). Static nodes on that layer ignore it (`is_mobile`); members process it and self-filter by team-id.
- **Creation = `create` marked mobile [REVISED]** — mints a **team-id T**; the team **inherits the creator's layer PHY unchanged** (freq / control-SF / BW / sf_list). `create` may NOT pick new PHY — the **no-gateway guarantee** (a mobile is only ever on ONE PHY). Announced via a **C frame marked `is_mobile` + T** (team-id-scoped, NOT a new `leaf_id`). *(Supersedes the earlier self-asserted-proximity / `TEAM_ANNOUNCE` sketch — `create`=mint, `J`=join does the same with standard parts.)*
- **Membership = `J` marked mobile + T** — a mobile already on the team's PHY joins via the standard J-flow (team-id instead of a fresh layer). **Group chat = a channel marked `is_mobile` + T** — team-broadcast = a channel M-frame, members self-filter by T, static ignores (the RTS-M `is_mobile` rule). Reuses the whole create/config/channel stack.
- **★ PHY-binding [DECIDED — strict]:** a team is **single-PHY** (the creator's layer PHY). **Membership does NOT prevent static registration — it only constrains the PHY:** each member independently keeps its **own** home_node **on the team's PHY** for global reach. A mobile on a *different*-PHY layer cannot join without first re-registering onto the team's PHY.
- **Home_node death / re-register [DECIDED]:** detected via **beacon-loss** (timeout ≪ the 24 h TTL); the member re-registers the normal way **but only onto a same-PHY network** to keep the team; a **different-PHY** move = that member **leaves the team** (the team persists for members still on the PHY). With **no** home_node at all, it stays on the team PHY — team intact, just no static access.
- **Home_node-less creation [DECIDED]:** a mobile with no static mesh creates T on a **default/agreed PHY** (nothing to inherit) — same overlay model, a pure mobile network.
- *(Wire: the **C** and **M** frames gain an `is_mobile`+team-id mobile-network variant — PLANNED, to fold into `frames.md`.)*
- **⚠ RTS-M needs an `is_mobile` mark [DECIDED — small wire add]:** a mobile's M_BROADCAST (team-announce / team-broadcast) must self-mark as **mobile-originated** (a **rsv bit** — RTS byte-5 b1..0 / byte-3 b0 are free; the `rts_flags` nibble is full) so **static nodes do NOT re-flood it** (the §8 relation-transit rule, *broadcast side*) — only mobiles / the team relay it within the cluster. Bonus: this same mobile-scoped M_BROADCAST **is** the team-broadcast last-mile (one broadcast reaches the co-located team — resolves the §14 N-unicasts-vs-broadcast question toward **broadcast**).
- **Intra-team mobile↔mobile routing = v2** (§9) — `is_mobile`+`team_id`-scoped relay; deferred.
- *(SUPERSEDED by the overlay model above: the earlier "attachment-point / implicit care-of / aggregation" bullets were the pre-overlay ad-hoc sketch — replaced by explicit registration (§13) + the mobile-network overlay. Kept out to avoid misleading the coder.)*
- **✅ Team addressing [DECIDED]:** the **team-id IS the address** — no roster. **Internal** group chat = a **channel on T** (members self-filter by team-id). *(An EXTERNAL static→whole-team message is deferred — reach individual members via their home_nodes, or a member relays into the channel; not a v1 need.)*
- **✅ Team lifecycle [DECIDED — v1, TTL-driven like registration]:** **join** = `J` marked mobile+T; **leave** = stop beaconing the team-id → ages out (24h-after-last-heard); **split** (a subgroup wanders off; both halves still hold the id) = **best-effort accepted for v1** — a team-broadcast reaches the half nearest the resolved attachment point (re-mint-on-minter-absence deferred); **merge** (two teams meet) = **stay distinct** (each keeps its id; a member holds exactly one).

## 13. Registration + locate + DM + last-mile — THE CONCRETE MECHANISM  [mostly DECIDED]
**Registration [DECIDED]:** a mobile always registers with a **≤1-hop host**, **discovered via the J-flow** (handshake below) — there is **no fixed "declared home"**; the registrar is whichever host it currently registers with (dynamic, §4). Registration is **individual OR team**, **valid 24h after last-heard** (refreshed while the registrar keeps hearing it). The registrar holds its mobiles in the **`_mobile_reg` table** (§17 — NOT `id_bind`).

**Mobile node_id = a home-assigned LOCAL id [DECIDED — Option B, 2026-07-07]:** the mobile does NOT DAD a global id (it can't create a mesh, only join). On register the **home_node hands it a local id** — no mobile-side listen-and-claim (battery-friendly), scales via per-home_node pools, and "re-join on move" collapses to "re-register → new local id." The mobile **stamps `origin = home_node_id`** on its outbound so the mesh bills an accountable global node (`sender_hash = mobile` still carries E2E identity; the reverse E2E-ACK routes by hash, so `origin` needn't be the mobile). ⚠ **A local id can collide with some normal node's global id** (we deliberately don't carve the id space), so last-mile frames must self-describe as **mobile-addressed** — §14.

**Registration handshake = the J family, mostly reuse [DECIDED 2026-07-07].** The register exchange IS the existing `J` flow `DISCOVER (broadcast) → OFFER → CLAIM` (`is_mobile` already on J byte-1 b6). Today DISCOVER/OFFER are *deferred* (join = DAD listen+CLAIM); a mobile **activates** them:
- **M1 → J-DISCOVER (`is_mobile`), leaf-EXEMPT** — processed by any node on the freq/sf/bw regardless of `leaf_id` (like the gateway membership-exemption); M1 has no layer yet.
- **willing hosts → J-OFFER** — already carries `responder_key_hash32` (= the home_node hash); **add a `proposed_mobile_id`** = a free LOCAL id from the host's pool (the Option-B assignment; a small OFFER wire-add). **Storm control:** SNR-weighted **backoff + suppression** (the strongest host offers first, others fall silent once enough are out — reuses the flood-backoff pattern); hosting is a node config (default-on). M1 listens a window, picks the **strongest**.
- **M1 → J-CLAIM** the chosen offer — reuses CLAIM, **no new APP codes**. M1 **adopts the offer's `leaf_id` as its current layer**, and learns that layer's **gateways from the host's BCN** (existing type-4 TLV) for cross-layer sends.
- **Old-home update is M1's job [DECIDED]:** on re-register M1 itself sends the redirect breadcrumb to its **previous** home_node (§13 staleness) — the *new* host is NOT told about the old (the mobile owns its old-cache update).

**Locate [DECIDED] — reuse the H-query, minimize flooding:** a mobile's hash resolves to a **`home_id`** (the current registrar). The registrar **proxies the mobile's hash** — answers the H-query with *its own* id — and that `hash→home_id` binding **caches along the whole return path** (id_bind already path-caches). First resolve floods; everything after is cached.

**DM [DECIDED]:** A knows `home_id(M)` → normal DM `dst=home_id, dst_hash=M` → registrar does the last mile. A doesn't → one H-query, then cached.

**★ REUSE — this IS the cross-layer-bridge pattern (CONFIRMED by a full mechanism review 2026-07-07):** `dst=home_id, dst_hash=M` → registrar receives (dst==me), sees `dst_hash ≠ my key` (fork at node_mac_rx.cpp:586), **resolves the hash → the mobile's current node_id (`id_bind_find_by_hash`, node_hashlocate.cpp:142) → forwards a NORMAL id-addressed DM.** This is byte-for-byte the `bridge_cross_layer` path (node_mac_rx.cpp:786-840 — `id_on_leaf_by_hash → it.dst=node_id → rt_find → standard RTS/CTS/DATA/ACK`) and `l2c_enqueue_forward` (node_join.cpp:382). The registrar is a **"same-layer bridge to a registered mobile."** Only genuinely NEW: **the registration table** — which largely **REUSES `id_bind`** (hash→node_id + TTL, node.h:1130). The last mile needs **NO new wire** (§14). **Multiple mobiles per registrar** (your constraint): they share `home_id`; the DM's `dst_hash` disambiguates *which* one → its node_id → its own normal last hop. Anti-spam stays correct because the forward **preserves `TxItem.origin`** (the real sender), the key `track_originator_observation`/`channel_origin_admit` bill.

**Staleness = REDIRECT, not re-flood [DECIDED]:** a DM hitting a **stale** registrar comes back as an **H-answer carrying the new `home_id`** (if the stale node knows it) → re-caches → A retries. Cold miss → fresh H-query.
- ✅ **Redirect breadcrumb [DECIDED]:** on re-register the mobile sends a **short DM of a special app-type** to its **previous** registrar carrying the new `home_id`. **Best-effort** — if it's not sent or is lost, **normal recovery** covers it (the stale registrar returns "unknown" → fresh H-query). Reuses the DM path; no new frame.

**Single vs team [DECIDED]:** `dst_hash = mobile-hash` → one recipient; `dst_hash = team-hash` → the registrar **fans out to all members** it holds.

**Registration cadence [OPEN]:** event-driven-on-settle only (cheap, gap while moving) vs. + a slow refresh timer (bounded staleness, more uplinks) — the freshness↔battery knob.

## 14. Wire facts  [DECIDED — verified vs frame_codec.cpp, NOT comments]
- `is_mobile` is a **live beacon flag: byte-2 bit 5 (0x20)** (pack :37 / parse :92). A beacon from a mobile is already marked; receivers already set the `_mobile_peer` bitset.
- **No spare flag bit** — beacon bytes 2-3 are full (`byte-2`: has_schedule/self_gateway/is_mobile/has_seen_bitmap/has_ext + n_entries[2:0]; `byte-3`: n_entries_hi[2:0]/heard_set_complete/wire_version[3:0]).
- Richer team/mobile info therefore rides the **EXT block** (`has_ext`, 0x08) as a new ext-TLV (like channel-digest / suspect-nodes) — **no wire_version bump, no flag-day.**
- **★ Last-mile = a NORMAL id-addressed handshake — RTS/CTS/DATA/ACK/anti-spam 100% REUSED (review 2026-07-07).** Every hop mechanism keys on a node id (RTS `next`, CTS `rx_id`, DATA `next`, ACK `to` [src-less], anti-spam `src`/`origin`); the cross-layer bridge already delivers a resolved-hash DM as a standard id-addressed `TxItem` (`bridge_cross_layer` node_mac_rx.cpp:812/883; `l2c_enqueue_forward` node_join.cpp:382). The registrar mirrors it via `id_bind_find_by_hash`, `TxItem.origin` preserved. ⛔ The old "hash-addressed dst=0 last-mile" is DROPPED. The handshake *logic* is untouched — only the id-field must be **marked** as mobile (next bullet), because of Option B's local id.
- **★ Mobile-communication MARK [needed under Option B's local id; verified vs frame_codec.cpp].** A local id can equal a normal node's global id, so any last-mile frame carrying the mobile's local id in a *receiver-addressing* field must self-describe or a colliding node mis-accepts:
    - **RTS `next` + DATA `next`** (deliver to mobile) → use **`addr_len[3:1]`**, the purpose-built alt-addressing field present on BOTH (RTS byte-3 :399, DATA byte-0 :754; "hierarchy deferred", parse rejects non-zero today). ROOM ✓.
    - **ACK `to`** (to mobile, its outbound flow) → **`byte-1 rsv` bits 3..1** (:364). ROOM ✓.
    - **CTS `rx_id`** (to mobile, outbound flow) → **NO spare bit** — flags nibble full (`(sf-5)`[3] + `already_received`[1], :343-346), 3-byte frame. ⚠ the lone wire wall.
    - Sender-field appearances (RTS `src`, CTS `tx_id` = the mobile) don't cause mis-accept, but the frame's mark lets anti-spam key the mobile's local id separately from a same-valued global id.
    - **✅ CTS mark [DECIDED 2026-07-07 — by-context]:** the CTS carries **no** mobile mark; it relies on the **marked-RTS context** — a node close enough to mis-act on a CTS almost surely heard the marked RTS that opened that flight, so it already knows local-id X is a mobile. Residual (a *hidden* colliding node with a coincident pending TX) = at most one collision + retry, **never a mis-delivery**. Rejected: grow CTS +1 B (airtime on a frequent frame); locally-free id / mini-DAD (can't see the mobile's own extra neighbours).
- **Team broadcast is the ONLY open last-mile wire question:** `dst_hash=team` has no single node_id → either **N id-addressed unicasts** (full reuse, N txns, fine for a small cluster) or a **team-scoped local broadcast** (reuse `M_BROADCAST` — already CTS-less — + a team-hash accept-match). Decide when we design teams (§12).
- **DM already carries `dst_hash`** (DST_HASH flag) — used for the E2E crypto bind AND the registrar's multi-mobile disambiguation; no new DM field.

## 15. Implementation status (what EXISTS vs what's NEW)  [reference]
**EXISTS (from the frozen Lua port):** `is_mobile` on beacon/Q/J wire + config/NV plumbing; **identity-only beacons** (a LONE mobile emits zero route entries — but a **team mobile emits FULL routing BCNs marked `is_mobile`+`team_id`** for the mobile-plane routing, §9); **mobile-peer recognition** (`_mobile_peer` bitset); **transit avoidance** (`route_uses_mobile_as_transit` — but origin-agnostic, see §8); seen-bitmap / suspect-gossip / bidi-census exclusion for mobiles; sync-response mobile penalty (+8 s self, +2 s requester).
**NEW / CHANGE needed:** the `_mobile_reg` registry (§17, NOT id_bind) + H-query→home_id proxy + the redirect breadcrumb (§4/§13) [NEW]; the mailbox is PARKED (app-level); the RTS `MOBILE` mark + `addr_len` receiver mark + ACK rsv mark (§14/§17) [NEW]; the 9-B mobile OFFER (§17) [NEW]; a new **M-frame mobile-variant** for the team channel (§17) [NEW]; **§8 transit needs NO v1 change** (separate planes — current rule correct); direct-cluster mobile↔mobile routing (§9) [**v2**]; cross-layer by re-register (§11) [NEW, reuses the cross-layer DM]; team = mobile-network overlay (§12) [NEW].

## 16. Sim harness  [DECIDED — ready]
`simulation/s07_seattle_mobile_meshroute.json` — 36-node realistic asymmetric Seattle mesh, 3 mobiles (walk/bike/courier), mobility firing (`asymmetry_coherence_ms=60000`), metal-like ids (17..52), runs clean under `-e meshroute`. This is where we test proposals. (Metal-fidelity for cross-layer mobile later needs the ≥16 layer-id + per-layer-slop work in `2026-07-07-lus-metal-fidelity.md`.)

## 17. Sealed critical-gap resolutions  [DECIDED 2026-07-07 — from the full-design review + code audit]
These CORRECT the reuse-claims in §13/§14 and define the previously-undefined critical behaviors.

**Mobile mark (wire):**
- **A1 — a `MOBILE` rsv bit on the RTS** (byte-5 b1) = "the src/originator is a mobile." Distinct from `addr_len=1` (receiver `next` = a mobile local-id): home→mobile = `addr_len=1`/`MOBILE=0`; mobile→home OR a mobile M_BROADCAST = `MOBILE=1`/`addr_len=0`. On `MOBILE=1` the host **skips neighbour-learning for `src`, keys anti-spam/dedup/CTS `rx_id` on the (mobile,local-id) context** — and it IS the marked-RTS the CTS-by-context needs. **Unifies A1 + the §12 M_BROADCAST mark into ONE bit.** (Required — RTS `src` is a bare byte today; `node_mac_rx.cpp` treats it as a global id.)
- **A2 — host-less origin:** a team-only mobile (no home_node) stamps `origin = its team-local id`; anti-spam is team-plane-scoped.

**Registration (corrects "reuses id_bind"):**
- **`_mobile_reg` = a NEW table** (`hash → {local-id, epoch, last-heard}`) at the host — **NOT `id_bind`** (one-id-per-hash + evicts other holders, `node_hashlocate.cpp:37-45`). Proxy H-answer = "I'm the registrar → answer my own id" (any holder may answer, marked `claimed`). Last-mile = `_mobile_reg` → local-id. `id_bind` still caches `hash→home_id` **sender-side** (fine).
- **9-B mobile OFFER** = the J-OFFER + a `proposed_mobile_id` byte; `parse_j` widened to accept 9 B when `is_mobile` (**no compat break** — OFFER is a deferred opcode; `frame_codec.cpp:688` exact-parse must be widened).
- **Confirmation:** the mobile CLAIMs the chosen host → the host **records it in `_mobile_reg` + confirms**; registered **on the confirm** (retry CLAIM if none). No silent claim-stands.
- **Epoch:** a mobile-incremented **registration epoch** rides the CLAIM + the proxy H-answer; a sender holding two answers (old + new registrar in the 24 h overlap) takes the **highest**.

**Delivery:**
- **Sleep:** the mobile **declares its wake interval** at registration; the host retries the last-mile aligned to it, up to **N**, then fails (best-effort; mailbox parked).
- **Dest layer [E1 — VERIFIED 2026-07-07]:** the cross-layer *layer-resolution* is a **PRE-EXISTING gap** — the H-answer's `target_layer` is the RESPONDER's leaf, not the dest's (`node_hashlocate.cpp:473`), so a cross-layer relay can answer wrong; the gateway then re-resolves on the target leaf with a timeout (`node_mac_rx.cpp:853`). The mobile **inherits** it (no regression): mobile→mobile is more reliable (the dest's home_node proxy answers with its own = the dest's current layer, freshest via the C1 epoch) but can hit it if a relay answers first. **v1 accepts the existing best-effort** (retries + gateway re-resolve self-heal); flagged for a **future non-mobile fix** (a layer field in id_bind / explicit paths) — NOT a v1 blocker. (§11)

**Non-critical (all confirmed 2026-07-07):**
- **A3** local-id pool = **17..254** (avoid 0/0xFF + the 1..16 gateway band).
- **B1** DISCOVER scan-set = a **configured PHY list** (usual freq/SF/BW first).
- **B3** no host in range = **exponential-backoff re-DISCOVER** (stay host-less meanwhile).
- **B4** storm constants = listen-window ≈ **1–2 beacon intervals**; **SNR-proportional** offer backoff; host **suppresses after K≥3** offers heard.
- **C2** mobile `hash→home_id` cache TTL = **short (minutes)**.
- **D3** host **rate-limits** its mobiles (a per-mobile budget cap — a mobile can't drain the host's duty).
- **F1** team-id = `hash(creator_key ‖ nonce)` (32-bit).
- **F2** team config = inherited PHY + a **channel-id** + the team-id.
- **F5** host-less default PHY = a **configured well-known** freq/SF/BW.

**Also NEEDS-NEW-CODE (not "free" reuse):** the team group-chat needs a **new M-frame mobile-variant** (`team_id`+`is_mobile` fields + ingest keyed on them) — the M-frame is strictly `leaf_id`-gated (`node_channel.cpp:190`).

## Open questions rollup  [updated 2026-07-07]
- **✅ §13 redirect breadcrumb — RESOLVED:** special-app-type DM to the previous registrar with the new `home_id`; best-effort; H-query fallback.
- **✅ §4 home model — RESOLVED:** `home_id` = the DYNAMIC current registrar (storage-free); mailbox = a SEPARATE owned node.
- **✅ §13 mobile id — RESOLVED (Option B):** home-assigned LOCAL id (no mobile DAD), `origin=home_node` stamp on outbound.
- **✅ §13 registration handshake — RESOLVED:** reuses the J family (DISCOVER→OFFER→CLAIM, `is_mobile` exists); mobile activates the deferred DISCOVER/OFFER, **leaf-exempt** discovery, OFFER carries a proposed LOCAL id (small wire-add), SNR-backoff+suppression storm control, adopts the offer's leaf, gateways from BCN, no new APP codes; old-home update = mobile's own breadcrumb. *(Validated by the M1→A / M1→M2 scenario walkthrough.)*
- **⏸ §4 mailbox — PARKED (user):** handle later at **app level** (a DM with an app flag). Store / forward / discovery all deferred.
- **✅ §14 CTS mark — RESOLVED (by-context):** CTS carries no mark; relies on the marked-RTS context (residual = a rare collision + retry, never mis-delivery). Documented in `docs/frames.md`.
- **✅ §12 team — RESOLVED (v1, REVISED → mobile-network overlay):** `is_mobile`+team-id overlay on the creator's layer (same PHY/nibble, no gateway); `create`=mint / `J`=join / **channel**=group-chat (full create/config/channel reuse); **PHY-bound** (members' home_nodes must match the team PHY); TTL join/leave, best-effort split, distinct merge; RTS-M `is_mobile` mark.
- **✅ Edge cases — RESOLVED:** M1 mid-move (re-register+breadcrumb+redirect, cadence-gated); home_node death (detect via **beacon-loss** ≪ 24 h TTL → re-register: any-PHY for a lone mobile, **same-PHY-or-leave** for a team member); two-mobiles-same-local-id = non-issue (per-host-distinct-id invariant).
- **§13 registration cadence [minor]:** event-driven-on-settle is the v1 default; a slow refresh timer is a tuning option (freshness ↔ battery) — fold into the spec.
- **✅ §11 cross-layer — RESOLVED:** re-register on the new layer (cheap under Option B); reach via the existing cross-layer DM (H-query → new-layer home_node → gateway → last mile); brief park accepted.

**⏭ DEFERRED to v2 (not in the v1 spec):** mailbox store/forward/discovery (app-level DM flag); split re-mint; registration slow-timer. *(Intra-team routing is now §9-RESOLVED by reuse of the DV stack on the mobile plane — no longer a v2 item.)*

**✅✅ v1 DESIGN FULLY SEALED (2026-07-07)** — single-mobile (register→locate→last-mile+mark→redirect) + teams (mobile-network overlay §12, DV routing on the `is_mobile`+`team_id` plane §9) + separate-planes transit + cross-layer-by-re-register. Critical gaps + non-critical gaps + F3 + E1 all resolved (§17). **→ Drafting the sliced implementation spec.**
