# Command Reference

> Status: Inventory refreshed. Command names, canonical forms, availability, and state-effect classes were audited against the current production parsers, feature gates, and board profiles on 2026-08-31. Detailed argument rules, output examples, and error guidance remain to be reviewed.

This page inventories the textual commands accepted by a MeshRoute node. It covers 49 primary command names plus `?`, the alias for `help`. Radio frame opcodes, OLED button actions, simulator-only operations, and host-tool subcommands are outside this inventory.

## Access and availability

- **Local** means the command is accepted through the local textual command dispatcher. That is USB when the build has `MR_CONSOLE=1`, and BLE on the XIAO nRF52840 when BLE is enabled.
- The `production` build has no USB console because it sets `MR_CONSOLE=0`.
- BLE refuses the whole `help`/`?` family and any argument-bearing `peers` form, including `peers all`.
  Target administration (`admin-id`, `acl`) is USB-only. Controller administration (`admin-key`,
  `admin-target`) permits only public list/show on secured BLE; all other forms are USB-only.
- The `ui` family is compiled only into OLED builds. In the current build matrix those are ESP32 Heltec V3/V4 builds, where BLE is not implemented, so `ui preset` is available through USB only.
- **Common** means all current device profiles, subject to having a local transport.
- **Normal** means a single-layer, non-gateway build.
- **Mobile role** means a normal build with the mobile feature compiled in and the node currently configured as mobile.
- **Gateway** means a dual-layer gateway build.
- **Remote management** is compiled out of the dedicated mobile profile.

## Effect classes

| Class | Meaning |
| --- | --- |
| Read | Reads local state without intentionally changing it or using radio airtime. |
| Session | Changes RAM or the current runtime session; the change does not itself survive reboot. |
| Persistent | Writes device storage. The table states whether the change applies live or after reboot. |
| Air | Sends immediately or schedules work over the LoRa radio. Acceptance is not proof of airtime or delivery. |
| Recovery | Reboots, halts, erases, or replaces operational state. |
| Secret | Handles a passphrase, private key, or other sensitive material. |
| Bench | Test or fault-injection control; not ordinary user operation. |

## General information and diagnostics

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `help` or `?` | USB only; Common | Read | Prints only the primary command names compiled into this build, one per line, followed by `docs/manual/command-reference.md`. Argument-bearing/topic help is retired and refuses with that same pointer. This manual is the sole detailed command reference. BLE refuses the whole help family with `console_only`. |
| `version` | Local; Common | Read | Reports firmware build, revision, board, and last reset cause. |
| `whoami` | Local; Common | Read | Reports this node's identity and role. BLE returns the companion `ready` object. |
| `status` | Local; Common | Read | Reports node, radio, queue, sleep, and fault-state diagnostics. |
| `duty` | Local; Common | Read | Reports current duty-cycle use and availability. |
| `limits` | Local; Common | Read | Reports local channel, DM, and duty headroom. |
| `cfg` | Local; Common | Read | Reports current configuration. Accepted keys are classified below. |
| `routes` | Local; Common | Read | Lists the current routing table. |
| `peers` | Local; Common | Read | Lists the bounded keyed address book. |
| `peers all` | USB only; Common | Read | Adds ID-only diagnostic rows to the address-book listing. |
| `lookup 0x<hash>` | Local; Common | Read | Looks up a hash in the local static ID-binding cache; it does not query the mesh. |
| `nameof 0x<hash>` | Local; Common | Read | Looks up the cached name and known IDs for a hash. |
| `hashof <id> [-t\|-s]` | Local; Common | Read | Looks up an ID in the team and/or static address spaces. |
| `faults` | Local; hardware builds | Read | Reads the retained hardware fault ring. |

## Identity, peer keys, and discovery

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `resolve 0x<hash> [hard]` | Local; Common | Air + Session | Starts an asynchronous hash-owner query; `hard` bypasses cached resolution. |
| `reqpubkey <0xhash\|id> [-t\|-s]` | Local; Common | Air + Session | Requests a peer public key. A bare ID lets the node select a non-ambiguous plane. |
| `peerkey <64-hex-ed25519-pubkey> ["<name>"]` | Local; Common | Session + Persistent | Pins a peer key in RAM and attempts to mirror it to the persistent peer store. |
| `peername 0x<hash> "<name>"` | Local; Common | Session + Persistent | Renames a cached peer without replacing its key or confidence. |
| `regen` | Local; Common | Persistent + Recovery | Replaces this node's ordinary messaging identity while retaining its configured name and short ID. Preserves the separate administration stores; CLIENT builds append the warning below. |

## Controller administration stores (Slice 4)

Available only on CLIENT builds (the dedicated mobile profiles). These commands provision local trust;
they do not issue remote requests. `self` is the ordinary messaging identity, not a dedicated seed slot.
`keyN` means exactly key0 through key9. Public output includes the full 64-hex key and its fingerprint:
the first eight bytes of BLAKE2b-512 over that public key, rendered as 16 lowercase hex characters.

| Form | Transport | Effect |
| --- | --- | --- |
| `admin-key list` | USB / secured BLE | Lists self and occupied dedicated slots, then the end line. |
| `admin-key show <self\|keyN>` | USB / secured BLE | Shows one public identity; never a seed. |
| `admin-key generate <keyN>` | USB only | Generates and durably saves a dedicated seed in an empty slot. |
| `admin-key import <keyN> <64hex-seed>` | USB only | Imports a seed into an empty slot; derives its public identity before saving. |
| `admin-key export <keyN>` | USB only | Prints the secret seed. Keep it off retained logs; self cannot be exported here. |
| `admin-key remove <keyN> confirm` | USB only | Removes one dedicated key, not its grants on other nodes. |
| `admin-key reset confirm` | USB only | Recovers an invalid keyring to valid empty; not a wipe command for a valid store. |
| `admin-target list [page=0\|page=1\|page=2\|page=3]` | USB / secured BLE | Lists occupied rows in eight physical slots per page; default page 0, explicit footer. |
| `admin-target show <label=LABEL\|fp=16hex>` | USB / secured BLE | Resolves one stored full target key and shows its public row. |
| `admin-target add <LABEL> <64hex-public-key> hash=<0x1..8hex> [layer=<id[,id[,id]]>]` | USB only | Adds one of at most 32 targets, independent of the messaging peer book. |
| `admin-target set <label=LABEL\|fp=16hex> label=<NEWLABEL> hash=<0x1..8hex> layer=<none\|id[,id[,id]]>` | USB only | Replaces all mutable label/routing hints; the full target key stays immutable. |
| `admin-target remove <label=LABEL\|fp=16hex> confirm` | USB only | Removes the selected public target-book row, not the target's ACL grant. |
| `admin-target reset confirm` | USB only | Recovers an invalid target book to valid empty; unreadable IO still refuses writes. |

Labels are 1–16 characters from `A–Z`, `a–z`, `0–9`, `_`, `.`, `-`. Selectors must resolve uniquely;
unknown/ambiguous selectors refuse, with no fallback to the first row or the peer book. Routing hash is
a delivery hint, not a fingerprint or proof of trust; the routing hash must be nonzero. Layer hints
contain one to three destination IDs in 1–255, excluding the origin; no hint means same-layer.
Slots are not evicted and occupied key slots are not
overwritten; duplicate identities refuse. Failed saves are errors, not a guarantee that old flash bytes
survived a partial write. An unreadable store refuses every write; only explicit reset recovers invalid state.

Non-public controller forms over BLE return exactly:

```json
{"err":"admin-client","msg":"console_only"}
```

After a successful CLIENT `regen`, the existing success/name line is followed on the requesting sink by:

```text
> regen note old self ACL grants do not follow the new key; dedicated keys and targets preserved
```

Reprovision old self grants through physical USB as needed. Dedicated seeds and target-book rows survive
ordinary regeneration; factory reset erases them. The future busy-debt refusal is `> regen err remote_busy`,
but Slice 4 has no live request/result producer. Seed import/export intentionally shares a principal;
copies cannot be separately revoked through that one ACL entry. Hardware persistence/BLE residue remains
Bench Parts 55b/56 and 59; independent Slice 4 software QA PASS does not claim those ran.

## Messaging

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `send <id\|0xhash> "<text>" [-a] [-e] [-t] [-K] [-l]` | Local; Common | Air | Sends a direct message. Address form, plane, encryption, acknowledgement, introduction, and location gates require detailed treatment in the messaging chapter. |
| `send_channel <0..255> "<text>" [-t] [-g] [-e] [-l]` | Local; Common | Air | Originates a channel post on the selected plane or planes. |
| `send_layer <0xhash> <layer,...> "<text>" [-a] [-e] [-K]` | Local; Common | Air | Sends through an explicit cross-layer path. The parser recognizes `-l`, but execution refuses it because this carrier has no location form. |

## Inbox

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `pull_inbox <dm_since> <chan_since>` | Local; Common | Read | Streams raw DM records first, then channel records, followed by `inbox_end`. The DM stream includes delivery receipts and custody-failure reports used by companion sync and diagnostics. |
| `mark_read <dm\|chan> <seq>` | Local; Common | Persistent | Advances and persists the selected inbox read cursor; reports `marked` or `io_error`. |
| `del_msg <dm\|chan> <seq>` | Local; Common | Persistent + Recovery | Deletes one selected inbox record through a durable tombstone; reports `erased`, `not_found`, or `io_error`. |
| `clear_inbox confirm` | Local; Common | Persistent + Recovery | After exact confirmation, attempts to wipe both record stores while preserving their sequence high-waters, resets both read cursors, and advances their shared storage epoch. Reports `cleared` or `io_error`; on failure, messages may remain. Leaves all non-inbox state untouched. |

A received **custody-failure report** appears on USB as one `CUSTODY FAILURE reporter=… stage=… reason=… …`
line and in `pull_inbox` as `{"ev":"custody_failure",…}`. It means a relay could not complete onward custody;
it is not proof that the destination missed the message. Delete it with `del_msg dm <seq>` like any other record.
A report your node's **home** translated on your behalf carries four extra fields on the same USB line and in
the same `pull_inbox` event — `delegated=true`, `target_kind=node_id|hash`, **exactly one** of `target_id=` /
`target_hash=` (8 lower-case hex digits, the same form as `dst_hash`), and `mobile_ctr=` (the counter your
own send was given; the `ctr=` field keeps meaning the home's counter). It still means only that a relay
could not complete onward custody, and still is not proof the destination missed the message.
The OLED's normal inbox view hides protocol-internal outcome records, but `pull_inbox` deliberately includes them.

All current hardware boards use the durable `SegmentedInboxStore`: nRF52 stores records in QSPI with metadata in
InternalFS, while ESP32 stores records in LittleFS with metadata in NVS. If either store cannot initialize, inbox
operations fail rather than silently falling back to volatile storage.

## Static and gateway provisioning

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `join layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12>` | Local; Normal | Persistent + live + Air | Saves the requested network floor, applies it live, and starts address claiming. |
| `create layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12> sf_list=<list> duty=<percent> name="<text>" [active_fraction=<0..1>] [ch_min_ms=<ms>] [dm_min_ms=<ms>]` | Local; Normal | Persistent + live + Air | Creates a managed static network configuration and starts address claiming. |
| `leave` | Local; Common | Persistent + live + Recovery | Clears provisioning and runtime network state while retaining only the configured frequency. |
| `gateway l0=<layer>:<node>:<control-sf>:<data-sfs> l1=<...> [period=<ms>] [win0=<ms>:<offset>] [win1=<ms>:<offset>] [beacon=<ms>] [freq0=<MHz>] [freq1=<MHz>] [bw0=<kHz>] [bw1=<kHz>] [cr0=<5..8>] [cr1=<5..8>] [gateway_only=<0\|1>]` | Local; Gateway | Persistent; reboot required | Stores and validates a dual-layer gateway configuration. Normal builds explicitly refuse it. |
| `joinprofile list` | Local; Normal | Read | Lists stored join presets. |
| `joinprofile set <1..4> layer=<1..255> freq=<MHz> bw=<kHz> sf=<5..12> [name="<text>"]` | Local; Normal | Persistent | Stores one preset without joining a network. |
| `joinprofile clear <1..4>` | Local; Normal | Persistent + Recovery | Clears one stored preset. |
| `joinprofile reset confirm` | Local; Normal | Persistent + Recovery | Clears the entire preset store after confirmation. |

The textual `joinprofile` storage commands are implemented. The OLED UI-15 provisioning workflow is also implemented
and metal-qualified: `SETTINGS` → `PROVISION` can create a team or join a static network from one of these four
stored profiles when that operation is available on the current build.

## Mobile operation

The `mobile` family is compiled only where the mobile feature exists and refuses all forms unless the node is currently in the mobile role.

| Command or form | Effect | First classification |
| --- | --- | --- |
| `mobile register` | Session + Air | Starts registration on the current PHY. |
| `mobile register scan` | Session + Air | Scans the current and learned network PHYs. |
| `mobile register freq=<MHz> sf=<5..12> [bw=<kHz>]` | Session + live + Air | Retunes the live mobile PHY and starts registration without persisting that PHY through this command. |
| `mobile unregister` | Session | Ends the local attachment session without transmitting a deregistration message. |
| `mobile gateways` | Read | Lists learned gateways and networks. |
| `mobile query <gateway-id>` | Air | Requests a gateway's network directory; the answer is asynchronous. |
| `mobile status` | Read | Reports attachment, home-link, retry, candidate, and PHY state. |

## Teams and team keys

The `team` family is available on normal builds. Team membership, role projection, PHY changes, and keys are committed as one persistent operation before their live application.

| Command or form | Effect | First classification |
| --- | --- | --- |
| `team new [freq=<MHz> sf=<5..12> [bw=<kHz>]] [tkpub=<64-hex> tkpriv=<64-hex>]` | Persistent + live + Air + Secret | Creates a team, or adopts the supplied keypair instead of minting one. |
| `team <numeric-team-id> [freq=<MHz> sf=<5..12> [bw=<kHz>]] [tkpub=<64-hex> tkpriv=<64-hex>]` | Persistent + live; Air on membership change; Secret if a key is supplied | Joins or reapplies a team configuration. Decimal and `0x`-prefixed IDs are accepted. |
| `team 0` | Persistent + live + Recovery | Leaves the team. |
| `team exportkey` | Read + Secret | Emits the current team public and private channel-key pair. |
| `team grantkey <0xhash\|team-local-id> [name="<text>"] [-t]` | Air + Secret | Sends the team key to a verified peer in a sealed direct message. |

## OLED preset catalog

The OLED-only `ui preset` family administers the seventeen stable preset slots the on-device compose lists render: one
mandatory `emergency`, eight `dm` (`dm1`..`dm8`), and eight `channel` (`channel1`..`channel8`). Slot identity is
the token, never a list position. The family answers in NDJSON over USB; a mistyped line gets a usage line instead.

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `ui preset list` | USB; OLED builds | Read | Emits all 17 `ui_preset` records in stable slot order, including disabled slots, then `ui_presets_end` with the capacity, both active counts and the catalog generation. |
| `ui preset set <emergency\|dm1..dm8\|channel1..channel8> loc=<on\|off> "<text>"` | USB; OLED builds | Persistent + live | Validates the full record and enables that slot. Text is 1-17 printable ASCII bytes with at least one non-space; `"`, `\`, CR and LF are rejected. Answers with the resulting record. |
| `ui preset clear <dm1..dm8\|channel1..channel8>` | USB; OLED builds | Persistent + live | Disables the slot and clears its text and location flag. `clear emergency` is refused with `mandatory`. |
| `ui preset reset <emergency\|dm1..dm8\|channel1..channel8>` | USB; OLED builds | Persistent + live | Restores that slot's compiled default; slots 3-8 return to disabled. Answers with the resulting record. |
| `ui preset reset all` | USB; OLED builds | Persistent + live + Recovery | Restores the complete compiled catalog. Answers with the full list. The generation still advances. |

Storage uses a separate versioned UI record (`/mrui`), so editing a phrase does not reprovision radio, identity,
team, or key configuration. A factory reset erases it with the rest of the `mr` namespace.

Refusals are reported as `{"ev":"ui_preset_err","reason":"…"}` with exactly six values: `bad_slot`, `bad_text`,
`bad_location`, `mandatory`, `busy`, `store`. `store` covers both an unreadable record and a failed write; a
failed write may have changed flash partially, so it must not be read as "nothing was written".

While an emergency alarm is active, **every** mutating verb answers `busy` — including one that would change
nothing — so that an alarm's retry series cannot have its body or its location policy changed halfway through.
`ui preset list` is not a mutating verb and answers normally during an alarm.

An identical `set` performs no write. The generation is a persisted non-zero counter that advances only on a
successful durable update and is compared for equality, never ordering.

At boot the node prints nothing when the record is valid or absent. A corrupt record prints
`  ui presets = DEFAULTS (record invalid — repaired on next successful change)` and repairs itself on the next
successful change; an unreadable store prints `  ui presets = DEFAULTS (store unreadable — changes disabled)`
and refuses every mutation with `store` and no writes. `cfg` also reports
`  presets: generation=<n> dm_active=<n> channel_active=<n> saves=<n>`.

The textual `ui preset` storage and administration commands and the on-device compose-list rendering that
consumes this catalog are both implemented (UI-10/11, 2026-08-26): the compose lists show the enabled slots in
stable-slot order with an `L`/`-` location marker, and a catalog change between the wearer's selection and its
execution is refused on the panel as `PRESET CHANGED` rather than sending newly configured words.

## Configuration keys

`cfg set <key> <value>` is the sole generic configuration-write form. The live handler currently accepts 52 keys.

| Keys                                                                                                                                                                                                                               | Apply timing    | Storage         | First classification                                                                                                                                    |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- | --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `name`, `lat`, `lon`                                                                                                                                                                                                               | Live            | Identity record | Node name and position.                                                                                                                                 |
| `freq`, `routing_sf`, `control_sf`, `bw`, `cr`, `tx_power`                                                                                                                                                                         | Live            | Persistent      | Radio settings; `control_sf` is an accepted alias for `routing_sf`.                                                                                     |
| `sf_list`, `lbt`, `beacon_ms`, `e2e_dm`, `intro_attach`                                                                                                                                                                            | Live            | Persistent      | MAC and message-policy settings.                                                                                                                        |
| `gw_announce_pct`, `gw_announce_interval`, `gw_herd_slack`                                                                                                                                                                         | Live            | Persistent      | Gateway announcement policy.                                                                                                                            |
| `active_fraction`, `ch_min_ms`, `dm_min_ms`, `leaf_name`                                                                                                                                                                           | Live            | Persistent      | Managed-network activity and rate settings.                                                                                                             |
| `leaf_id`, `gateway_only`, `mobile`, `mobile_autoregister`                                                                                                                                                                         | Live            | Persistent      | Role and topology settings. Enabling `mobile_autoregister` may start a live registration session; disabling it does not end an already started session. |
| `nav`, `intra_layer_relay`, `host_mobiles`, `nav_ignore`, `hop_cap`, `team_hop_cap`, `team_channel_crypt`                                                                                                                          | Live            | Not persisted   | Session-only routing, hosting, and team-channel policy.                                                                                                 |
| `node_id`, `duty`                                                                                                                                                                                                                  | Reboot required | Persistent      | Values whose derived runtime state is initialized at boot.                                                                                              |
| `ble_mode`, `ble_period`, `ble_pin`                                                                                                                                                                                                | Reboot required | Persistent      | BLE startup policy and passkey.                                                                                                                         |
| `n_layers`, `layer0_id`, `window_period_ms`, `l0_window_ms`, `l0_window_offset_ms`, `l1_layer_id`, `l1_node_id`, `l1_routing_sf`, `l1_sf_list`, `l1_beacon_ms`, `l1_window_ms`, `l1_window_offset_ms`, `l1_freq`, `l1_bw`, `l1_cr` | Reboot required | Persistent      | Dual-layer gateway topology. The common parser accepts these keys; supported use outside gateway builds remains under review.                           |

Detailed value ranges and refusal messages will be added during the configuration-reference pass.

## Runtime control and recovery

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `sleep [on\|off]` | Local; Common | Session | Forces or cancels idle light-sleep while a host session exists. |
| `debug [on\|off]` | Local; Common | Session | Enables or disables per-frame tracing for the current boot. |
| `reboot` | Local; hardware builds | Recovery | Performs an expected software reset. |
| `ota` | Local; hardware builds | Recovery | Enters BLE DFU on nRF52, or toggles the Wi-Fi SoftAP updater on ESP32. |
| `prep-restart` | Local; Common | Recovery | Clears learned state and inbox records, retains provisioning, and halts normal operation until restart. |
| `factory_reset [confirm]` | Local; hardware builds | Persistent + Recovery | Without confirmation, prints the warning. With `confirm`, erases configuration, identity, peers, and inbox, then reboots. |

## Remote management

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `rcmd <destination-id> <remote-form>` | Local issuer; target requires Remote management | Air | Sends a bounded remote query or administration request by DM. |
| `password <passphrase>` | Local; Remote-management builds | Persistent + Secret | Derives and pins the local node's admin public key. It is not a remotely executable form. |
| `unlock <passphrase>` | Local; Remote-management builds | Session + Secret | Derives the operator admin identity into RAM for sealed remote commands. |
| `lock` | Local; Remote-management builds | Session + Secret | Wipes the unlocked operator identity from RAM. |

**Slice 1b limitation (R-RA-27; software QA-passed 2026-09-06):** static/gateway builds accept remote commands
but no longer stage remote responses. Their legacy `rcmd` issuer still sends commands, but cannot print replies
or receive counter-resynchronization hints. Mobiles ignore incoming remote commands at the existing fail-closed
guard. These strict receive roles are intentional on undeployed test hardware, with no legacy-switch widening;
the old static/gateway round-trip bench step is suspended until Slice 9 replaces the legacy path.

The target-side remote allow-list is narrower than the local dispatcher:

| Remote form inside `rcmd` | Authentication | Target effect |
| --- | --- | --- |
| `status` | Open, cleartext | Read |
| `routes` | Open, cleartext | Read |
| `duty` | Sealed | Read |
| `limits` | Sealed | Read |
| `reboot` | Sealed | Recovery after the response is sent |
| `prep-restart` | Sealed | Recovery after the response is sent |
| `password rotate <64-hex-new-admin-pubkey>` | Sealed with the old admin identity | Persistent + Secret |

Other text can be accepted by the issuing `rcmd` parser but is not executed by the target allow-list.

The current sealed remote-management path still uses the old monotonic replay-counter protocol. Historically,
a returned stale-counter hint let the issuer report that the command was not run, resynchronize from the returned
floor, and ask the operator to issue it again. Static/gateway issuers can no longer receive that hint after 1b;
do not assume retry resynchronization or a printed result. The proposed loss-independent open/operator/owner
administration execution protocol is not yet implemented. Local v2 target ACL/identity provisioning and
controller key/target stores exist after Slices 3/4; that does not make the legacy rcmd path a v2 issuer.

## Bench and fault-injection controls

These commands are present in production command dispatch so the deployed image can be exercised on hardware. They are inventoried here but should not appear in ordinary first-time workflows.

| Command or form | Effect | First classification |
| --- | --- | --- |
| `route add <1..254> <1..254> <1..255> [score-q4]` | Session + Bench | Injects a route candidate as destination, next hop, and hop count. |
| `route del <1..254>` | Session + Bench | Removes the selected route. |
| `testsend <1..254\|8-hex-hash> <alphanumeric-run> [-a] [-e] -t <ms,...>` | Air + Session + Bench | Schedules tagged direct-message transmissions. `-e` requires the hash form. |
| `testch <0..255> <alphanumeric-run> -t <ms,...>` | Air + Session + Bench | Schedules tagged channel transmissions. |
| `teststatus` | Read + Bench | Reports scheduled-send state and counters. |
| `testclear` | Session + Bench | Clears the scheduled-send queue. |
| `crashtest <hang\|fault\|reboot>` | Recovery + Bench | Deliberately hangs, faults, or reboots after `debug on`. |

## Remaining detail pass

Before this reference is marked complete, each inventory row still needs:

- exact argument constraints and defaults;
- stable success output and important error output;
- USB versus BLE output form;
- safety warnings and recovery guidance;
- a link to the workflow chapter that explains when to use it;
- metal evidence where source inspection alone cannot prove behavior.

## Audit basis

The 2026-08-31 refresh followed the live command paths in `src/firmware_commands.cpp`,
`src/firmware_config.cpp`, `src/firmware_inbox.cpp`, `src/firmware_remote.cpp`, `src/fw_main.cpp`, and
`lib/console/console_parse.cpp`, together with the feature/build gates in `lib/core/mr_features.h`,
`src/device_ble.h`, and `platformio.ini`.
