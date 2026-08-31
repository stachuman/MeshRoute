# Command Reference

> Status: First inventory complete. Command names, canonical forms, availability, and state-effect classes were audited against current production parsers on 2026-08-21. Detailed argument rules, output examples, and error guidance remain to be reviewed.

This page inventories the textual commands accepted by a MeshRoute node. It covers 47 primary command names plus `?`, the alias for `help`. Radio frame opcodes, OLED actions, simulator-only operations, and host-tool subcommands are outside this inventory.

## Access and availability

- **Local** means the command is accepted through the local textual command dispatcher. That is USB when the build has `MR_CONSOLE=1`, and BLE on the XIAO nRF52840 when BLE is enabled.
- The `production` build has no USB console because it sets `MR_CONSOLE=0`.
- BLE refuses `help`, `?`, and any argument-bearing `peers` form, including `peers all`. Other commands reach the shared parser or dispatcher, although their output format is not necessarily identical to USB.
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
| `help` or `?` | USB only; Common | Read | Prints the built-in console summary. It is a convenience view, not the authority for this reference. |
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
| `regen` | Local; Common | Persistent + Recovery | Replaces this node's cryptographic identity while retaining its configured name and short ID. |

## Messaging

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `send <id\|0xhash> "<text>" [-a] [-e] [-t] [-K] [-l]` | Local; Common | Air | Sends a direct message. Address form, plane, encryption, acknowledgement, introduction, and location gates require detailed treatment in the messaging chapter. |
| `send_channel <0..255> "<text>" [-t] [-g] [-e] [-l]` | Local; Common | Air | Originates a channel post on the selected plane or planes. |
| `send_layer <0xhash> <layer,...> "<text>" [-a] [-e] [-K]` | Local; Common | Air | Sends through an explicit cross-layer path. The parser recognizes `-l`, but execution refuses it because this carrier has no location form. |

## Inbox

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `pull_inbox <dm_since> <chan_since>` | Local; Common | Read | Streams DM and channel inbox records followed by an end marker. |
| `mark_read <dm\|chan> <seq>` | Local; Common | Persistent when the inbox backend is enabled | Advances the selected inbox read cursor. |
| `del_msg <dm\|chan> <seq>` | Local; Common | Persistent + Recovery | Deletes one selected inbox record through a durable tombstone when the inbox backend is enabled. |
| `clear_inbox confirm` | Local; Common | Persistent + Recovery | Wipes both inbox record stores after explicit confirmation. Preserves each sequence high-water, resets both read cursors and increments the storage epoch once. Leaves all non-inbox state untouched; it does not replace `prep-restart` or `factory_reset`. |

A received **custody-failure report** appears on USB as one `CUSTODY FAILURE reporter=… stage=… reason=… …`
line and in `pull_inbox` as `{"ev":"custody_failure",…}`. It reports that a **relay** could not complete
onward custody; it is **not** proof the destination missed the message. Delete it with `del_msg dm <seq>`
like any record. (§CUSTODY-G, 2026-08-31.)

⛔ CORRECTED 2026-08-31 (the paragraph below was stale — [[B134]]/[[B260]] made BOTH platforms durable):
~~The external-flash inbox backend is currently enabled on the XIAO nRF52840 build. The ESP32 inbox backend
remains disabled, so these commands are accepted there but have no durable records to operate on.~~
Every board now runs the one durable `SegmentedInboxStore` — nRF52 over QSPI/InternalFS, ESP32 over
LittleFS/NVS — so these commands operate on durable records on every platform.

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

The textual `joinprofile` storage commands are implemented. The broader UI-15 on-device provisioning workflow remains marked **planned** until its completion and metal validation are confirmed.

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

The `ui preset` family administers the seventeen stable preset slots the on-device compose lists render: one
mandatory `emergency`, eight `dm` (`dm1`..`dm8`), and eight `channel` (`channel1`..`channel8`). Slot identity is
the token, never a list position. The family answers in NDJSON on both transports; a mistyped line gets a usage
line instead.

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `ui preset list` | Local; Common | Read | Emits all 17 `ui_preset` records in stable slot order, including disabled slots, then `ui_presets_end` with the capacity, both active counts and the catalog generation. |
| `ui preset set <emergency\|dm1..dm8\|channel1..channel8> loc=<on\|off> "<text>"` | Local; Common | Persistent + live | Validates the full record and enables that slot. Text is 1-17 printable ASCII bytes with at least one non-space; `"`, `\`, CR and LF are rejected. Answers with the resulting record. |
| `ui preset clear <dm1..dm8\|channel1..channel8>` | Local; Common | Persistent + live | Disables the slot and clears its text and location flag. `clear emergency` is refused with `mandatory`. |
| `ui preset reset <emergency\|dm1..dm8\|channel1..channel8>` | Local; Common | Persistent + live | Restores that slot's compiled default; slots 3-8 return to disabled. Answers with the resulting record. |
| `ui preset reset all` | Local; Common | Persistent + live + Recovery | Restores the complete compiled catalog. Answers with the full list. The generation still advances. |

Storage is a separate versioned UI record (`/mrui`), deliberately isolated from `/mrcfg`: editing a phrase can
never reprovision radio, identity, team or key configuration. A factory reset erases it with the rest of the
`mr` namespace.

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

## Bench and fault-injection controls

These commands are present in production command dispatch so the deployed image can be exercised on hardware. They are inventoried here but should not appear in ordinary first-time workflows.

| Command or form | Effect | First classification |
| --- | --- | --- |
| `route add <destination> <next-hop> <hops> [score-q4]` | Session + Bench | Injects a route candidate. |
| `route del <destination>` | Session + Bench | Removes the selected route. |
| `testsend <destination> <run> [-a] [-e] -t <ms,...>` | Air + Session + Bench | Schedules tagged direct-message transmissions. |
| `testch <channel> <run> -t <ms,...>` | Air + Session + Bench | Schedules tagged channel transmissions. |
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
