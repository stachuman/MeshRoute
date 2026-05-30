# "How MeshRoute Works" — explainer page (design)

**Date:** 2026-05-30  **Status:** PROPOSAL — shape approved in brainstorming; awaiting spec review.

**Deliverable:** a public, GitHub-facing explainer at `docs/how-it-works.md`, linked
from the top of the README. It sits between the two docs we already have: it is NOT
the marketing pitch (that's the README's *What / Why / Principles*) and NOT the
protocol spec (that's the sim's `PROTOCOL.md`). The middle layer — **how it actually
works**, explained so a GitHub reader genuinely gets it.

---

## 1. Audience & goals
- **Reader: layered (simple → deep).** Opens so anyone grasps the gist; progressively
  reveals the real mechanism for those who want it.
- **Success:** a curious reader finishes the top and gets *why it's different*; a
  technical reader expands the asides and understands *how a message actually travels*
  — without opening the spec.
- **Non-goals:** byte-level wire format, exhaustive subsystem coverage, config/API
  reference. Those live in `PROTOCOL.md` / the code; the page links out to them.

## 2. Scope — what the deep end covers
In depth, the **differentiators + scale**:
1. deliberate distance-vector **routing** (vs flooding);
2. airtime-aware **channel access** (RTS/CTS + listen-before-talk + duty-cycle budget);
3. adaptive **spreading factor**;
4. the **layer + gateway** model that scales past one mesh.

Brief only — an "also in the box" list, one line + link each: joining, anti-spam,
end-to-end ACK, mobility.

## 3. Spine & section outline
**"Follow a message"** — open with the problem, trace one message (Alice → … → Dave),
then the short list, then status.

1. **The problem** *(hook)* — flooding (flood-based meshes like Meshtastic/MeshCore)
   rebroadcasts everything to everyone: simple and robust when small, but in dense
   networks it drowns the shared airtime — LoRa is slow and duty-cycle-capped (EU 1%),
   so every redundant repeat steals time others need, collisions climb, the mesh chokes
   on its own chatter. *Analogy: a room where everyone repeats everything, louder each
   time.* → **Diagram 1 (flooding vs routing).**
2. **The idea** — MeshRoute treats airtime as scarce and routes *on purpose*: nodes
   learn who-reaches-whom and send each message along a chosen path. Honest trade-off:
   a little more latency and bookkeeping, for far less airtime waste where it counts.
3. **A message's journey** *(the spine)* — five steps, each one idea + an optional
   `↓ deeper` aside (see §4):
   - **Find the route** — small periodic *beacons* advertise reachability ("I reach Dave
     in 2 hops, good signal"); each node keeps a lightweight, lazily-refreshed table with
     a few alternates; Alice's node picks the best next hop. *deeper: SNR scoring, K
     alternates, aging/pruning.* → **Diagram (journey strip, optional).**
   - **Grab the channel** — listen first, then a tiny **RTS→CTS** handshake reserves the
     moment (no two senders colliding) and respects a duty-cycle budget. *deeper: LBT,
     duty tiers, NACK/backoff.* → **Diagram 2 (RTS-CTS-DATA-ACK).**
   - **Pick the spreading factor** — strong link → fast SF (cheap airtime); weak link →
     slow, far-reaching SF. The receiver knows the link quality, so it names the SF in
     the CTS; every hop is right-sized. *deeper: SNR→SF, control-vs-data SF split.* →
     **Diagram (SF vs range, optional).**
   - **Confirm + repeat** — DATA on the chosen SF, receiver ACKs; the next hop repeats
     the dance. Hop by hop, deliberately, the message walks to Dave. *deeper: hop
     budget/TTL, loop guards, fall back to an alternate if a hop fails.*
   - **Cross a layer** — big networks split into **layers** (~250 nodes, own control
     channel); a **gateway** (a node in two layers) carries the message across along an
     explicit layer path — scaling past one mesh without one giant flood domain. →
     **Diagram 3 (layers + gateway).**
4. **Also in the box** — one line + link each: joining (gets an address, no central
   authority) · anti-spam · end-to-end ACK · mobility.
5. **Where this is** — status (proposal; first firmware soon) + pointers (repo, the
   deeper `PROTOCOL.md`).

## 4. The layering mechanic
Main thread = the accessible journey narrative. Deep end = short collapsible GitHub
`<details>` **`↓ deeper`** asides hung off each journey step. Casual readers skip them;
technical readers expand them for the real mechanism. One doc, two depths, no wall.

## 5. Diagrams (prioritized — "a diagram or two": lead with the must-haves)
- **D1 — Flooding vs routing (must-have):** same small network side by side — flooding
  lights every link, MeshRoute lights one path. The thesis in one picture. Likely a
  small **custom figure** (a two-panel SVG/PNG) since the "everything lights up" effect
  is hard in Mermaid.
- **D2 — RTS-CTS-DATA-ACK handshake (strong):** one hop as a **Mermaid `sequenceDiagram`**
  (sender ↔ receiver: listen → RTS → CTS(SF) → DATA → ACK). Native GitHub render.
- **D3 — Layers + gateway (strong):** two layers + a bridging gateway + a cross-layer
  path. **Mermaid `graph`** or a simple figure.
- **D4/D5 — journey strip / SF-vs-range (optional):** add only if a section needs the lift.

**Tech default:** Mermaid for the structural diagrams (sequence, topology) — versioned,
GitHub-native, never drifts from the text; one custom figure (D1) if it reads better
drawn. One consistent, simple visual style. The page must read fine with **D1 + D2 (+ D3)**
even if the optional ones are dropped.

## 6. Voice & length
Plain, concrete, lightly opinionated; analogies over jargon; short paragraphs; active
voice; no academic tone. ~3–4 screens of main thread plus the optional asides.

## 7. Home & linkage
`docs/how-it-works.md`. README gains a prominent **"How it works →"** link right after
the pitch; the README's *why/principles* stays. (Mirroring to a GitHub wiki page later
is out of scope.)

## 8. Out of scope / deferred
Byte-level wire format, full subsystem tour, config/API reference, any rewrite of
`PROTOCOL.md`. The page links to those for readers who want them.

## 9. Open questions
1. **Diagram appetite:** plan is D1 + D2, plus D3 if the "scale" section wants it; D4/D5
   only if they earn it. Confirm at review.
2. **Naming competitors:** the README already names Meshtastic/MeshCore; the page names
   them once, neutrally, as the flooding contrast (rec) vs staying generic
   ("flood-based meshes"). Confirm.
