<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 6.1: team-id provisioning + NV — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 6.1 of mobile v1** (design §12, F1) — the **first of three team sub-slices** (6.1 provisioning · 6.2 beacon-TLV + mobile-plane DV routing · 6.3 team channel). The user commits; I quality-gate. **This slice is config plumbing ONLY — no wire, no behaviour** (a team `_cfg.team_id` that everything downstream reads). A team is an `is_mobile`+`team_id` overlay; `team_id == 0` = **no team** (a lone mobile / any static node) = today's behaviour exactly.

## ★★ Static-safety
- `team_id` **defaults 0**; nothing reads it yet (6.2/6.3 add the behaviour). So this slice changes **no wire and no routing** — s18 stays byte-identical, all mobile scenarios unchanged.
- The console keys are role-config (like `mobile`), preserved across `create`/`join` (they load the NV first).
- **⚠ NV bump v17→v18** (adds `team_id`) → old NV is **size-rejected → reprovision** (the established pattern, e.g. v15→v16). Flashing 6.1 firmware requires re-provisioning the node; coordinate with any in-progress hardware testing (which runs on v17 and is unaffected until you flash).

## Fix 1 — `team_id` in config + NV (`node.h` + `src/device_nv.h` + `src/fw_main.cpp`)
- **`NodeConfig`** (node.h ~:101, beside `is_mobile`): `uint32_t team_id = 0;` (F1: a team-id is `hash(creator_key‖nonce)`, 32-bit).
- **`device_nv::Blob`** (device_nv.h): append `uint32_t team_id;` and **bump `kVersion` 17 → 18** (comment: "v18: team_id"). Old NV size-mismatches → defaults (reprovision).
- **Boot load** (fw_main.cpp on_init, beside `cfg.is_mobile = nv.is_mobile != 0`): `cfg.team_id = nv.team_id;`.
- **The cfg-key handler** (fw_main.cpp ~:585, beside `mobile`): `else if (!strcmp(key, "team_id")) { lc.team_id = (uint32_t)strtoul(val, nullptr, 0); b.team_id = lc.team_id; }` (0x-hex or decimal; LIVE + PERSISTED, reboot-to-apply like `mobile`).
- **`seed_blob_from_live`** + the create/join blob paths (the `b.is_mobile = nc.is_mobile` sites, fw_main.cpp:483/936/1945): add `b.team_id = nc.team_id;` — so **`create`/`join` PRESERVE the team_id** (same model as `is_mobile`; they load the NV, only overwrite radio/DAD).

## Fix 2 — the mint: `team new` (`src/fw_main.cpp`, a new console verb)
Creating a team = minting a fresh id. Add a `team new` command (normal-node only, like create/join):
```cpp
// `team new`  → mint team_id = hash(our key_hash32 ‖ a fresh HW-RNG nonce), set + persist. `team <id>` / `cfg set team_id <id>` = JOIN an existing team.
static void handle_team(const char* args) {
    if (!strncmp(args, "new", 3)) {
        uint32_t nonce; g_hal.rand_bytes(reinterpret_cast<uint8_t*>(&nonce), 4);
        const uint32_t t = fnv1a32(g_identity.key_hash32, nonce);          // T = hash(creator_key ‖ nonce) — reuse the existing 32-bit hash
        mrnv::Blob b{}; if (!mrnv::load(b)) seed_blob_from_live(b);
        b.team_id = t; mrnv::save(b); g_node.mutable_config().team_id = t;  // LIVE + PERSISTED
        mrcon.print(F("> team new -> team_id=0x")); /* print hex */ ; return;
    }
    // else: `team <hex>` = join (same as cfg set team_id)
}
```
(Register `team` in the command dispatch alongside `create`/`join`.)

## Fix 3 — `status` shows the team (`src/fw_main.cpp` ~:267, beside `mobile=`)
```cpp
    if (c.team_id) { mrcon.print(F("  team=0x")); /* print c.team_id hex */; mrcon.println(); }
```
(0 = no team → print nothing, or `team=none`.)

## Tests
- **Config/NV round-trip:** set `team_id` (cfg-key + `team new`) → persisted in the Blob (v18) → reloaded at boot into `_cfg.team_id`. An old (v17) NV → size-rejected → `team_id==0` (defaults).
- **Preserved across provisioning:** a node with `team_id=T`, then `join layer=… …` → `team_id` still `T` (loaded from NV, not overwritten) — mirrors the `is_mobile`-preservation test.
- **Mint:** `team new` → a non-zero `team_id`; two mints differ (nonce).
- **★ Static/behaviour regression:** `team_id` is never read on any data/routing/beacon path this slice → native + **s18 byte-identical**; all mobile scenarios unchanged (0 behaviour delta).

## Gate
- `pio test -e native` green (config/NV + preserved-across-join + mint).
- **s18 byte-identical** (`3ac88d40…`) — no wire/behaviour touched.
- 4 boards compile (**NV v18** — reprovision after flashing).

## Sites
`node.h`(`NodeConfig.team_id`) · `src/device_nv.h`(`Blob.team_id`; `kVersion=18`) · `src/fw_main.cpp`(boot load; cfg-key `team_id`; `b.team_id` at the 3 blob-preserve sites; `handle_team`/`team new`; `status` team=). **NO core `lib/` change — behaviour is 6.2 (routing) + 6.3 (channel).**

---

## Slice 6 roadmap (for context — 6.2/6.3 drafted after 6.1 lands)
- **6.2 — beacon team-TLV + mobile-plane DV:** a team-id EXT-TLV (free type 5) on a team mobile's beacon; a team mobile emits **FULL routing BCNs** (today `node_beacon.cpp:331` gates them off for a lone mobile) → a **team-scoped routing table**; `route_uses_mobile_as_transit` (node_routing.cpp:617) relaxed to allow transit **within the same team**. Proves intra-team multi-hop.
- **6.3 — team channel + M-frame:** a team-scoped M-frame variant (the M-frame is `leaf_id`-gated, node_channel.cpp:190) tagged `is_mobile`+`team_id`; `do_send_channel` reads `_cfg.team_id`; RX ingests only on a team-id match; the RTS-M `MOBILE` mark keeps static nodes from re-flooding it. Proves group chat.
