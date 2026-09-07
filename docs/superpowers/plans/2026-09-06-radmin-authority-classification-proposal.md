<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 — command authority classification PROPOSAL for the owner's one-shot ruling (QA, 2026-09-06)

**★ RULED 2026-09-07 (R-RA-33): the owner confirmed this table as a whole — "with rest - I do agree" — with ONE
exception, R-RA-32 (`pull_inbox` / `mark_read` = operator, remote view excludes direct messages). This file is now the
owner's one-shot classification; the Author lands it as the signed table the generator consumes.**

**What this is.** R-RA-1 makes the command list GENERATED and the classification the OWNER's, taken once over the
complete list; R-RA-21 fixes the policy (open = exact `status`/`routes`; operator = ordinary target-applicable
commands; owner = identity/security/ACL/key material, destructive inbox/storage, factory reset, fault injection, OTA;
physical = first-owner/recovery/root-identity on a target and management-seed operations on a controller — USB-only;
controller-only wrappers and trust stores are never recursively target-dispatchable; anything ambiguous refuses
closed). Design §13 adds a second attribute: DISRUPTIVE commands (reboot, halt, erase, identity regen, OTA entry,
crash tests, retune/detach network/radio changes) return `scheduled` and activate later.

**How to use it.** Every SEMANTIC command/sub-command of the generated inventory at `a0ff994` (+ Slice 4's rows from
its brief) is listed ONCE with a PROPOSED class and flag derived mechanically from that policy. ⚖ marks a judgment
call. **Rule by exception:** confirm the table and name the rows you change; the Author lands the signed table, the
generator consumes it, and the Slice 6 checker refuses any row it does not cover. Transport arms (`ble_dispatch_line`
JSON twins, `service_console`'s USB `peerkey` envelope, the help router) inherit their semantic row and are not
classified separately.

Classes: **open** · **operator** · **owner** · **physical** (USB-only local) · **controller-local** (never a target
command) · **legacy** (deleted in Slice 9; never given a remote authority). Flag: **D** = disruptive (`scheduled`).

## A. Diagnostics and identity readers

| command | proposed | D | note |
| --- | --- | --- | --- |
| `status` | **open** | | R-RA-21: exact, argument-free line only |
| `routes` | **open** | | idem |
| `version` | operator | | |
| `whoami` | operator | | prints id/hash/name |
| `duty` | operator | | |
| `limits` | operator | | |
| `faults` | operator | | fault-log READ; the injection verbs are below |
| `debug` / `debug off` | operator | | trace toggle |
| `lookup` | operator | | hash-locate diagnostic (airs a flood) |
| `nameof` / `hashof` | operator | | |
| `peers` / `peers all` | operator | | key hashes + names; the ⚖ full-key exposure is the pinned `ed_pub` of PEERS, not this node's secret |
| `teststatus` | operator | | |
| `help` | — (local only) | | never remote (R-RA-29 shape); not a table row |

## B. Configuration (`cfg`, `cfg set <key>`)

| command                                                                                                                                                                                                    | proposed    | D     | note                                                                                        |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------- | ----- | ------------------------------------------------------------------------------------------- |
| `cfg` (read)                                                                                                                                                                                               | operator    |       |                                                                                             |
| `cfg set name` / `leaf_name`                                                                                                                                                                               | operator    |       |                                                                                             |
| `cfg set node_id` / `leaf_id` / `layer0_id` / `l1_layer_id` / `l1_node_id` / `n_layers`                                                                                                                    | operator    | **D** | changes routable identity/topology ⇒ detaches (§13)                                         |
| `cfg set freq` / `bw` / `cr` / `sf_list` / `routing_sf` (alias `control_sf`) / `tx_power` / `l1_freq` / `l1_bw` / `l1_cr` / `l1_sf_list` / `l1_routing_sf`                                                 | operator    | **D** | retune (§13)                                                                                |
| `cfg set beacon_ms` / `l1_beacon_ms` / `window_period_ms` / `l0_window_ms` / `l0_window_offset_ms` / `l1_window_ms` / `l1_window_offset_ms` / `gw_announce_interval` / `gw_announce_pct` / `gw_herd_slack` | operator    |       | cadence/scheduler, no detach                                                                |
| `cfg set hop_cap` / `team_hop_cap` / `nav` / `nav_ignore` / `lbt` / `duty` / `active_fraction` / `ch_min_ms` / `dm_min_ms` / `intra_layer_relay` / `intro_attach`                                          | operator    |       | ordinary radio/anti-spam policy                                                             |
| `cfg set gateway_only` / `host_mobiles` / `mobile` / `mobile_autoregister`                                                                                                                                 | operator    | **D** | role changes that detach/attach planes                                                      |
| `cfg set lat` / `lon`                                                                                                                                                                                      | operator    |       |                                                                                             |
| `cfg set e2e_dm`                                                                                                                                                                                           | ⚖ **owner** |       | the DM encryption default is a security policy                                              |
| `cfg set team_channel_crypt`                                                                                                                                                                               | ⚖ **owner** |       | channel crypto policy                                                                       |
| `cfg set ble_mode` (+ `on`/`off`/`periodic`) / `ble_period` / `ble_pin`                                                                                                                                    | ⚖ **owner** |       | the BLE admin surface and its PIN are security material (design §12.1 "security, identity") |

## C. Network membership and roles

| command | proposed | D | note |
| --- | --- | --- | --- |
| `create` (+ `name`/`sf_list`/`duty`/`active_fraction`/`ch_min_ms`/`dm_min_ms`) | operator | **D** | R-RA-21: "network create/join/leave/switch" is operator; retune ⇒ scheduled |
| `join` (and the gateway-build alias arm) | operator | **D** | |
| `leave` | operator | **D** | rebuilds `/mrcfg`, idles the radio |
| `gateway` (the gateway provisioning line) | operator | **D** | |
| `joinprofile list` | operator | | |
| `joinprofile set` / `set name` / `clear` / `reset` / `reset confirm` | operator | | stored join presets (user configuration, not a secret) |
| `mobile register` / `register scan` / `unregister` / `gateways` / `query` / `status` | operator | | `register`/`unregister` change the home binding but not the radio ⚖ (no D) |
| `route add` / `route del` | operator | | routing table edits |
| `team new` | ⚖ **owner** | **D** | mints the TEAM CONTENT KEY (a secret) and switches plane |
| `team <id>` (join/switch) | operator | **D** | plane switch; the key must be granted separately |
| `team keys` | operator | | metadata-only listing |
| `team grantkey` | ⚖ **owner** | | airs the team content key (sealed) to a member |
| `team exportkey` | **owner** | | R-RA-21 names it: private key material |
| `team forgetkey` / `forgetkey confirm` | **owner** | | destroys a retained secret |

## D. Messaging and address book

| command | proposed | D | note |
| --- | --- | --- | --- |
| `send` / `send_channel` / `send_layer` | operator | | the target sends AS ITSELF; ⚖ a remote operator can post in the target's name |
| `resolve` / `resolve hard` | operator | | |
| `reqpubkey` (+ `-s` / `-t`) | operator | | |
| `peerkey` | ⚖ **owner** | | installs a PINNED (verified) peer key = trust material |
| `peername` | operator | | a label |
| `pull_inbox` / `mark_read` | **operator** ✅ RULED (R-RA-32) | | remote form = every record class EXCEPT direct messages; `mark_read` marks only what the remote view shows; local form unchanged. (Proposed owner; overruled by the owner 2026-09-07.) |
| `del_msg` / `clear_inbox` | **owner** | | R-RA-21: destructive inbox operations |

## E. Disruptive actions, fault injection, OTA

| command | proposed | D | note |
| --- | --- | --- | --- |
| `reboot` / `prep-restart` | operator | **D** | R-RA-21: "operational reboot/prep-restart, subject to §13 scheduling" |
| `sleep` / `sleep off` | operator | ⚖ **D** | light-sleep gating removes console responsiveness, not the RF path |
| `regen` | **owner** | **D** | identity (§13 lists ordinary regen as disruptive; §6.2/§6.3 preserve admin state) |
| `factory_reset` / `factory_reset confirm` | **owner** | **D** | |
| `ota` | **owner** | **D** | R-RA-21/§12.1: enters a LOCAL receiver mode only |
| `crashtest fault` / `hang` / `reboot` | **owner** | **D** | fault injection |
| `testsend` / `testch` (+ `-a`/`-e`/`-t`) / `testclear` | ⚖ operator | | scheduled test workloads that TRANSMIT; not fault injection |

## F. The OLED panel

| command | proposed | D | note |
| --- | --- | --- | --- |
| `ui preset` / `list` / `set` / `clear` / `reset` / `reset all` | operator | | the wearer's phrases; ⚖ `reset all` is destructive storage but of UI text, not a secret |

## G. Target-side remote-admin stores (Slice 3) — the physical class

| command | proposed | D | note |
| --- | --- | --- | --- |
| `acl list` | ⚖ **owner** (remote) / physical (local) | | §6.6: a remote OWNER may list; an operator cannot inspect full keys |
| `acl add` / `acl set` / `acl remove` | **owner** (remote) / physical (local first owner) | | §6.6: a remote owner may add/set/remove; last-owner and self-slot rules in the service |
| `acl reset` | **physical** | | corrupt-ACL recovery is USB-only (§12.1) |
| `admin-id show` | ⚖ **owner** | | public material; ⚖ a remote owner may read the target's admin key |
| `admin-id generate` / `rotate` / `reset` | **physical** | | root identity generate/recover/rotate is USB-only (§12.1) |

## H. Controller-side (Slice 4) and legacy — never target-dispatchable

| command | proposed | note |
| --- | --- | --- |
| `admin-key list` / `show` / `show self` / `generate` / `import` / `export` / `remove` / `reset` | **controller-local** | §12.1: a controller-side trust surface; secret ops USB-only (R-RA-30) |
| `admin-target list` / `show` / `add` / `set` / `remove` / `reset` | **controller-local** | idem |
| `remote …` (Slice 8) | **controller-local** | the wrapper itself |
| `rcmd` | **legacy** | Slice 9 deletes |
| `password` / `unlock` / `lock` / `password rotate` | **legacy** | Slice 9 deletes (§12.1 "not part of v2") |
| the `remote_encode` / `remote_exec` rows (`status`/`routes`/`duty`/`limits`/`reboot`) | **legacy** | the old verb map Slice 9 deletes; their SEMANTIC rows are classified above |

## I. The judgment calls, collected (⚖) — where your word decides

1. `cfg set e2e_dm` / `team_channel_crypt` / `ble_mode` / `ble_period` / `ble_pin` → **owner** (security policy) rather
   than ordinary configuration.
2. `team new` and `team grantkey` → **owner** (they create/air a secret), while `team <id>` and `team keys` stay operator.
3. `peerkey` → **owner** (installs pinned trust), `peername` operator.
4. ✅ RULED R-RA-32: `pull_inbox` / `mark_read` → **operator**, with the REMOTE form excluding direct messages (proposed owner; overruled).
5. `send` / `send_channel` / `send_layer` → operator (a remote operator may transmit in the target's name).
6. `testsend` / `testch` / `testclear` → operator (test workloads, not fault injection).
7. `sleep` flagged disruptive (console responsiveness), `mobile register`/`unregister` NOT flagged.
8. `acl list` and `admin-id show` → owner when REMOTE (an operator never sees full keys), physical when local.
9. `ui preset reset all` → operator (UI text, not a secret).

All nine judgment calls are RULED (R-RA-33 confirms 1-3 and 5-9 as proposed; R-RA-32 overrules 4). Everything not marked ⚖ follows R-RA-21's wording directly. Rows that Slice 4/5/6 add later are classified at
their closure under the same policy and appended here by the Author; the checker refuses an unclassified row.
