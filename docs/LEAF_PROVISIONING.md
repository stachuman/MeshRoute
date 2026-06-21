<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Leaf provisioning — operator guide

A node finds and talks on a network using a small **manual floor**: *frequency + bandwidth + control SF + level_id*. These can't be auto-distributed — you can't receive the config that tells you how to receive. **Everything operational beyond the floor** (data SFs, duty, leaf name) is **pulled and applied automatically** once a node hears its managed leaf.

The simple front-door is **three verbs — `join` / `create` / `leave` — that apply LIVE (no reboot).** Inspect anytime with `status`, `cfg`, `whoami`. (These are normal-node verbs; gateways are provisioned differently — see the bottom note.)

> `level_id` is the user-facing 1..255 network selector; the on-wire leaf nibble is `level_id & 0x0F` (only 16 distinct leaves on the air, so two `level_id`s with the same low nibble collide — `status` shows `level_id (→nibble N)` so you can spot a clash). Coding rate is a fixed low default (4/5) and isn't an argument — LoRa CRs interoperate.

## The three verbs

| goal | command |
|---|---|
| **join** an existing network | `join <freq_MHz> <bw_kHz> <ctrl_sf> <level_id>` |
| **create** a new managed leaf (this node = mother) | `create <freq_MHz> <bw_kHz> <ctrl_sf> <level_id> <sf_list> <duty%> "<leaf name>"` |
| **leave** the network (reset, keep only freq) | `leave` |

All three persist to NV **and take effect immediately** — the radio re-tunes, the node re-DADs, the config applies, with no reboot.

---

## 1. Join an existing network

```
join 868.0 250 8 2
```
- Sets the radio floor live (freq / bw / control SF / leaf nibble = `level_id & 0x0F`), drops any old id, and re-DADs a fresh id (normal nodes pick **17..254**; 1..16 are reserved for gateways).
- It then hears the leaf's managed beacon (`leaf_id == mine`, `lineage_id ≠ 0`), **auto-pulls the leaf config** (data SFs / duty / name), adopts it live, and persists it.
- Nothing else to do. `status` shows the adopted lineage/epoch once synced; a `send` before sync returns `send_failed{reason:joining}` (transient — retry once synced).

> If the leaf is **unmanaged** (nobody ran `create`/`leaf create`), there is nothing to pull — the node runs on its manual floor (no data SFs until you set them).

## 2. Create a fresh leaf — the first "mother" node

```
create 868.0 250 8 2 7,9 10 "north field"
```
- Everything `join` does, **plus** the leaf's distributed config: data SFs (`7,9`), duty (`10` = 10 %, stored as the 0..1 fraction), and the quoted leaf name (spaces allowed).
- **Mints a managed lineage** (random, never 0) at **epoch 1** → this node becomes the leaf mother. Every node that `join`s the same `level_id` later pulls *this* config.
- Pin a fixed id with `cfg set node_id <17..254>` if you want one; otherwise it auto-DADs.

## 3. Re-join to a DIFFERENT network (incl. managed → managed)

```
leave
join 869.4 125 7 5
```
- **`leave`** wipes membership to default (lineage / epoch / node id / data config) but **keeps the frequency**, leaving the node unprovisioned and idle.
- Then `join <B…>` rendezvouses on network B and pulls B's config.
- This is the clean primitive for a managed node moving between managed leaves (no reflash needed — `leave` resets the lineage that the membership filter would otherwise keep pinned to the old leaf).

---

## Advanced / scripting — the low-level path

The verbs are the front-door over the granular knobs, which still exist (fine-tuning, scripts, gateways). `cfg set …` is **reboot-to-apply**; the verbs are live.

| command | effect |
|---|---|
| `cfg set freq <MHz>` / `control_sf <sf>` / `leaf_id <0..15>` / `bw <kHz>` / `cr <5..8>` | the manual radio/control floor (reboot) |
| `cfg set sf_list 7,9` / `cfg set duty 0.1` | data config (on a managed node a write bumps the leaf epoch → propagates) |
| `cfg set node_id <0\|17..254>` | pin an id (0 = auto-DAD); gateways use 1..16 |
| `leaf create` | mint a managed lineage → become a mother (epoch 1) — kept as an alias of `create`'s minting step |
| `leaf name <text>` | set the leaf name (part of the config fingerprint) |
| `regen` | mint a new node identity (forces a fresh DAD; does NOT clear the lineage — use `leave` for that) |
| `status` · `cfg` · `whoami` | inspect id / level_id / lineage / epoch / radio |
| `reboot` | apply persisted `cfg set` changes |

> **Gateways** (multi-layer bridges) are provisioned with the `l0_*` / `l1_*` / `n_layers` keys and use the **1..16** id range — see the gateway-provision spec, not this guide. A dedicated `join_as_gateway` verb is future work.
