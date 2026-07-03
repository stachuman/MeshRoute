# Anti-spam & rate limits

*The friendly overview lives in [how-it-works.md](how-it-works.md#protecting-the-airtime-budget); the wire forms are in [frames.md](frames.md) (RTS `E2E_ACK`, the C frame); the behaviour map is [protocol.md §11](protocol.md); the full design + worked numbers are in [the duty-channel-cap spec](superpowers/specs/2026-06-30-antispam-duty-channel-cap.md).*

Radio airtime is MeshRoute's scarce resource — in the EU a node may legally transmit only ~1 % of the time. So the network rations it: no single device may flood the shared air and drown out everyone else. Most of the time you never notice. This page explains what the limits are, how to see yours, and what to do when a message is held.

## Two kinds of traffic, two kinds of limit

**Direct messages (DMs)** are cheap — they travel one path to one recipient. They're bounded by your radio's duty-cycle budget and a per-neighbour fairness backstop (a relay throttles any one sender that hogs its airtime), plus a small **3-second floor** so a DM can't go out per-keystroke. In normal use you'll never hit these.

**Channel (group) messages** are the expensive ones: each floods the *whole* leaf — every node re-broadcasts it. So the network caps **how many distinct channel messages each sender may originate** in a rolling 5-minute window, and puts a **10-second floor** between them. The cap isn't a flat number — it adapts:

- **Lower spreading factor (SF) → more messages.** A low SF is fast on air, so the leaf can carry more; SF7 allows several times what SF12 does.
- **Bigger mesh → fewer per sender.** The leaf's total channel capacity is fixed by the duty cycle; more active senders share it, so each gets a smaller slice.
- **Higher-duty band → more.** A 10 %-duty band carries ~10× a 1 % band.

As a feel: on a 1 %-duty band a leaf sustains very roughly **4–15 channel messages per minute** total (SF8→SF6); one busy sender is a slice of that. It is deliberately modest — a channel flood is genuinely expensive.

## Checking your limits

Ask the node — the phone app does this for you with a **`limits`** query. It returns your live position: how many channel messages you have left this window, and **when you can send next** ("next DM in 1.2 s · next channel: ready"). The node ships the numbers; the app renders them, so you never have to guess why something is waiting.

## When a message is held

You're always told — nothing is dropped silently on your side:

- **"Rate-limited — retry in N s"** (`send_blocked`) — your own cap or spacing floor held it. Wait it out.
- **"Not delivered"** (`send_failed`) — no acknowledgement came back; the far side or a relay didn't take it. Retry later.
- **Channel delivery** (`channel_sent`) — confirms whether a neighbour actually re-broadcast your channel message.

The app uses these to pace itself, so it won't keep firing into a limit.

## Tuning (operators)

Three knobs are **per-leaf** config — set them on one node and they propagate to the leaf (via the config epoch), the same way the SF list or duty does:

| `cfg set` key | default | effect |
|---|---|---|
| `active_fraction` | `0.125` | assumed fraction of nodes active in channels — **smaller ⇒ a more generous** per-sender channel cap |
| `ch_min_ms` | `10000` | minimum ms between a sender's channel messages |
| `dm_min_ms` | `3000` | minimum ms between a sender's own DMs |

e.g. `cfg set ch_min_ms 5000`. The current values show in `cfg` and in the `limits` output.

## Notes for operators

- **Gateways are exempt.** A dual-layer gateway *bridges* traffic between two leaves — it is a conduit, not an originator, so none of these limits apply to what it relays (even though on the far leaf it looks like the source).
- **Delivery-acks are never throttled.** An end-to-end delivery confirmation is exempt from the backstop — throttling it would make the sender re-send, creating the very traffic the limit meant to prevent. A node that lies about this (flags a non-ack) is caught and penalised.
- **The tunables ride the leaf-config frame.** Changing any of them re-fingerprints the leaf so joiners resync. Because that frame grew to carry them, **run the same firmware on every node of a leaf** — mixed old/new firmware would misread each other's config. Reflash a leaf together.

<details>
<summary><b>↓ deeper — the numbers</b></summary>

The channel cap is a fair share of the leaf's *duty-bounded* channel capacity. Because every node must re-broadcast every flood within its own duty budget `D`, the leaf sustains at most `C = D / T_ch` distinct channel messages per window (`T_ch` = one flood's airtime at the leaf's channel SF). That ceiling is **independent of mesh size**. It's shared among the active originators (`≈ active_fraction × N`), so each sender's cap is `C / (active_fraction × N)` — shrinking as the mesh grows, rising as the SF drops. On a 1 %-duty band with a ~32-byte message, `C ≈ 15 / 8.3 / 4.5` messages·min⁻¹ at SF6 / 7 / 8.

DMs aren't on this cap at all — a DM touches ~6 nodes, not the whole leaf, so it's governed by the plain duty budget plus the per-neighbour airtime backstop. The backstop keys on the *physical* sender (never the encrypted end-to-end origin), so it still works even though the network can't see who a DM is from.

Full model + worked calculations: [the duty-channel-cap spec](superpowers/specs/2026-06-30-antispam-duty-channel-cap.md).
</details>
