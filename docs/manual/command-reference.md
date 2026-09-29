# Command Reference

> Status: Source-audited 2026-09-20 at `d11b5a9`, including the completed remote-admin v2 implementation. Some older command families still need a detailed argument/output pass; see the remaining work below. Source verification is not a claim of hardware qualification.

This page inventories the textual commands accepted by a MeshRoute node. It covers 53 primary command names across all build profiles, plus `?`, the alias for `help`; no single board exposes every name. Radio frame opcodes, OLED button actions, simulator-only operations, and host-tool subcommands are outside this inventory.

## Access and availability

- **Local** means the command is accepted through the local textual command dispatcher. That is USB when the build has `MR_CONSOLE=1`, and BLE on the XIAO nRF52840 when BLE is enabled.
- The `production` build has no USB console because it sets `MR_CONSOLE=0`.
- BLE refuses the whole `help`/`?` family and any argument-bearing `peers` form, including `peers all`.
  Local target administration (`admin-id`, `acl`) is USB-only; selected forms also admit an authenticated
  remote owner, as listed below. Controller administration (`admin-key`,
  `admin-target`) permits only public list/show on secured BLE; all other forms are USB-only.
- The `ui` family is compiled only into OLED builds. In the current build matrix those are ESP32 Heltec V3/V4 builds, where BLE is not implemented, so `ui preset` is available through USB only.
- **Common** means all current device profiles, subject to having a local transport.
- **Normal** means a single-layer, non-gateway build.
- **Mobile role** means a normal build with the mobile feature compiled in and the node currently configured as mobile.
- **Gateway** means a dual-layer gateway build.
- **Remote target / ACCEPT** means a static or gateway build: it accepts remote requests but does not issue them.
- **Remote controller / CLIENT** means a dedicated mobile build: it issues remote requests but ignores incoming remote commands. Heltec mobile can use its USB console without an iOS companion; secured BLE is another controller transport on the nRF52 mobile.
- These endpoint capabilities are fixed by the build. `cfg set mobile` changes a runtime role, not the compiled remote endpoint.

### Reading command forms

`<...>` denotes a required value; `[...]` denotes an optional part; alternatives are separated by `|`.
Do not type the angle brackets. Names, subcommands and flags are case-sensitive. Enter one command per
line, not a multiline body: USB ignores CR and submits on LF. The shared validator refuses NUL, CR or
LF remaining inside a dispatched command span. The local USB line bound is 1023 bytes,
BLE accepts at most 274 command bytes, and the command after a remote `--` is at most 201 bytes.
These are byte limits, not display-width or character limits; individual commands/carriers can be smaller.

USB replies are not uniformly JSON. BLE also has both specialized NDJSON replies and streamed handler
text; do not apply a single JSON parser to every command. The remote controller's BLE events are
described [below](#output-results-and-acknowledgement).

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

## Target administration identity and ACL

Available on ACCEPT builds. The administration identity is separate from the ordinary messaging
identity shown by `whoami`; `regen` does not rotate it. The ACL holds at most ten controller public keys
in stable slots 0–9. Roles are exactly `operator` and `owner`.

| Form | Access | Effect |
| --- | --- | --- |
| `admin-id show` | USB / remote owner | Prints the administration fingerprint and full public key, never its seed. |
| `admin-id generate` | USB only | Creates and saves an administration identity only when none exists. |
| `admin-id rotate confirm` | USB only | Replaces a valid administration identity, invalidating its sessions. Preserves ACL rows and the messaging identity. Every controller must pin the new target key. |
| `admin-id reset confirm` | USB only | Replaces an invalid identity record with a new identity; refuses absent, valid or unreadable records. |
| `acl list` | USB / remote owner | Lists occupied slots, roles, fingerprints and full public keys, then counts. Does not renumber holes. |
| `acl add <operator\|owner> <64-hex-public-key>` | USB / remote owner | Adds a controller key to an empty slot. The first entry must be an owner and the target identity must be usable. |
| `acl set <0..9> <operator\|owner>` | USB / remote owner | Changes an occupied slot's role. |
| `acl remove <0..9> confirm` | USB / remote owner | Removes one grant and invalidates that controller's session/work. |
| `acl reset confirm` | USB only | Recovers an invalid ACL to valid empty; grants nobody. Not a bulk-delete command for a valid ACL. |

The last owner cannot be removed or demoted. A remote owner cannot remove or demote its own
authenticating slot; local USB has no remote acting slot. Multiple owners are allowed. Duplicate and
all-zero public keys refuse; full stores never evict an existing grant. An absent ACL lists as empty.

Public listings use one fingerprint everywhere: the first eight bytes of BLAKE2b-512 over the 32-byte
public key, rendered as 16 lowercase hex characters, alongside the full 64-hex public key. A routing
hash is not this fingerprint and is not proof of identity.

Typical refusal lines are `> admin-id err <reason>` and `> acl err <reason>`. Important reasons include
`already_present`, `absent`, `store_invalid`, `store_io_failed`, `not_invalid`, `first_owner_required`,
`last_owner`, `self_slot`, `duplicate_key`, `acl_full`, `nv_save_failed` and `runtime_unavailable`, as
applicable to the operation. An IO failure refuses writes, including reset. A failed save does not
guarantee that the previous flash bytes survived; re-read before deciding what to do next.

The local BLE refusal for both target families is:

```json
{"err":"admin","msg":"console_only"}
```

### First-owner exchange over USB

Use the physical USB connection to verify both full keys before granting access. Substitute the values
read from the two devices; the placeholders below are not literal command arguments.

1. On the target, run `admin-id show`; if absent, run `admin-id generate`. Record its administration
   `pub=` and `fp=`. Run `whoami` separately to obtain its ordinary messaging routing hash.
2. On the controller, run `admin-key show self`. Alternatively, generate an empty dedicated slot with
   `admin-key generate key0` and use `admin-key show key0`.
3. On the target, run `acl add owner <CONTROLLER-PUBLIC-KEY>`, then `acl list` to verify the grant.
4. On the controller, run `admin-target add base <TARGET-ADMIN-PUBLIC-KEY> hash=<TARGET-ROUTING-HASH>`.
   Add `layer=<destination-layer,...>` only for a cross-layer route. Check `admin-target show label=base`.
5. With radio reachability established, run `remote base -e -- status`, or
   `remote base -e using=key0 -- status` if the dedicated key was granted.

After a target root rotation, its stored full key must be replaced through controller USB; `admin-target set`
cannot change that immutable key. Verify the new key physically, then remove/re-add the target when it is
not in use. The ordinary routing hash alone is insufficient to restore trust.

## Controller administration stores

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
ordinary regeneration; factory reset attempts to erase them. A CLIENT `regen` refuses with
`> regen err remote_busy` while requests, response assemblies, retained results or response-ACK debt remain.
Key removal and target edits/removal can refuse with `in_use` while a session, request or ACK debt owns
that slot. Successful local result delivery is not proof that the target received its response ACK.
Seed import/export intentionally shares a principal; copies cannot be separately revoked through that
one ACL entry. Do not use factory reset as routine busy-state recovery: it destroys provisioned trust.

## Messaging

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `send <id\|0xhash> "<text>" [-a] [-e] [-t] [-K] [-l]` | Local; Common | Air | Sends a direct message. Address form, plane, encryption, acknowledgement, introduction, and location gates require detailed treatment in the messaging chapter. |
| `send_channel <0..255> "<text>" [-t] [-g] [-e] [-l]` | Local; Common | Air | Originates a channel post on the selected plane or planes. |
| `send_layer <0xhash> <layer,...> "<text>" [-a] [-e] [-K]` | Local; Common | Air | Sends through an explicit cross-layer path. The parser recognizes `-l`, but execution refuses it because this carrier has no location form. |

The body is one double-quoted string; flags may precede or follow it. There is no quoted-string escape
syntax for embedding another `"`. For direct messages, `-a` requests an end-to-end acknowledgement,
`-e` requests encryption (accepted with a hash target, not a decimal ID), `-t` selects the team plane,
and `-K` suppresses the first-contact introduction. Without `-e`, the configured encryption policy applies;
its absence is not an instruction to send plaintext. `-l` requests this node's location and refuses if
the message cannot carry it securely.

For `send_channel`, no plane flag means global, `-t` means team, `-g` means explicit global, and `-t -g`
means both. Explicit `-e` requires team-only delivery; `-l` requires a sealed post. Channel posts do not
accept `-a`. For example, `send_channel 1 "Return to base now" -t -e` sends a sealed team-channel post
when the team/key/radio requirements are met. Acceptance alone is not proof every teammate received it.
See [Messaging](06-messaging.md) for the workflow. The OLED preset's 17-byte limit below is not a general
message limit; actual message capacity depends on carrier, encryption and attached metadata.

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

Over remote administration, `pull_inbox` hides private application DMs, for both operators and owners,
but retains channel messages, delivery receipts and custody diagnostics. Both `mark_read dm ...` and
`del_msg dm ...` refuse remotely with `remote_no_dm`, even when the selected record is a diagnostic,
because those operations share the private-DM cursor/store. Channel operations remain subject to their
normal authority (`mark_read`: operator; `del_msg`: owner). A remote owner can still run
`clear_inbox confirm`, which clears both stores, including private DMs; inability to read them is not
protection against an explicitly authorized erase.

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
| `team keys` | Read | Lists up to four retained team IDs and marks the active key; never prints key material. |
| `team forgetkey <team-id> confirm` | Persistent + Recovery | Removes one inactive retained team key. Decimal or `0x`-prefixed ID; the active key is protected. |

Leaving a team does not discard its retained key. When the four-key store is full, use `team keys`,
explicitly forget an inactive key, then retry the create/join. Forgetting is not team-key rotation and
does not revoke other members; restoring a forgotten content key requires a teammate's grant.

## OLED preset catalog

The OLED-only `ui preset` family administers the seventeen stable preset slots the on-device compose lists render: one
mandatory `emergency`, eight `dm` (`dm1`..`dm8`), and eight `channel` (`channel1`..`channel8`). Slot identity is
the token, never a list position. The family answers in NDJSON over USB; a mistyped line gets a usage line instead.

| Command or form | Access/build | Effect | First classification |
| --- | --- | --- | --- |
| `ui preset list [<1..5>]` | USB; OLED builds | Read | Emits ONE page of four `ui_preset` records in stable slot order, including disabled slots (page 5 is `channel8` alone), then `ui_presets_end` with the capacity (17), `text_max` (163), both active counts, the catalog generation and `page`/`pages`. A bare `list` is page 1; any other page token, or a second token, answers `bad_page`. Read pages 1-5 and start again if their generations differ. |
| `ui preset set <emergency\|dm1..dm8\|channel1..channel8> loc=<on\|off> "<text>"` | USB; OLED builds | Persistent + live | Validates the full record and enables that slot. Text is 1-163 printable ASCII bytes with at least one non-space; `"`, `\`, CR and LF are rejected. Answers with the resulting record. |
| `ui preset clear <dm1..dm8\|channel1..channel8>` | USB; OLED builds | Persistent + live | Disables the slot and clears its text and location flag. `clear emergency` is refused with `mandatory`. |
| `ui preset reset <emergency\|dm1..dm8\|channel1..channel8>` | USB; OLED builds | Persistent + live | Restores that slot's compiled default (`dm1`-`dm3`, `channel1`-`channel4` and `emergency` have one); every other slot returns to disabled. Answers with the resulting record. |
| `ui preset reset all` | USB; OLED builds | Persistent + live + Recovery | Restores the complete compiled catalog. Answers with the full unpaged list (17 records + an end record without page fields). The generation still advances. |

Storage uses a separate versioned UI record (`/mrui`), so editing a phrase does not reprovision radio, identity,
team, or key configuration. A factory reset erases it with the rest of the `mr` namespace.

Refusals are reported as `{"ev":"ui_preset_err","reason":"…"}` with exactly seven values: `bad_slot`, `bad_text`,
`bad_location`, `mandatory`, `busy`, `store`, `bad_page`. `store` covers both an unreadable record and a failed write; a
failed write may have changed flash partially, so it must not be read as "nothing was written".

While an emergency alarm is active, **every** mutating verb answers `busy` — including one that would change
nothing — so that an alarm's retry series cannot have its body or its location policy changed halfway through.
`ui preset list` is not a mutating verb and answers normally during an alarm.

An identical `set` performs no write. The generation is a persisted non-zero counter that advances only on a
successful durable update and is compared for equality, never ordering.

At boot the node prints nothing when the record is valid or absent. A corrupt record prints
`  ui presets = DEFAULTS (record invalid — repaired on next successful change)` and repairs itself on the next
successful change; an unreadable store prints `  ui presets = DEFAULTS (store unreadable — changes disabled)`
and refuses every mutation with `store` and no writes. An old version-1 record (the pre-W6 17-byte catalog) prints
`  ui presets = DEFAULTS (old v1 record — re-enter custom phrases)` at every boot, runs the compiled defaults with no
boot write and no migration, and is replaced by the first successful change. `cfg` also reports
`  presets: generation=<n> dm_active=<n> channel_active=<n> saves=<n>`.

The textual `ui preset` storage and administration commands and the on-device compose-list rendering that
consumes this catalog are both implemented (UI-10/11, 2026-08-26): the compose lists show the enabled slots in
stable-slot order with an `L`/`-` location marker, and a catalog change between the wearer's selection and its
execution is refused on the panel as `PRESET CHANGED` rather than sending newly configured words. Since W6
(2026-09-29) a phrase longer than 17 bytes is abbreviated on its row (16 bytes and `»`), and a double on any phrase
opens a review of the whole text — `BACK` selected, `SEND` to confirm — before anything is queued; a team change or a
re-keyed recipient refuses as `TEAM CHANGED` / `RECIPIENT CHANGED` with nothing sent.

## Configuration keys

`cfg set <key> <value>` is the sole generic configuration-write form. The live handler currently accepts
53 key spellings, including the `control_sf` alias. These local apply timings do not imply remote permission;
see [remote authority](#remote-authority-and-deferred-actions).

| Keys                                                                                                                                                                                                                               | Apply timing    | Storage         | First classification                                                                                                                                    |
| ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- | --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `name`, `lat`, `lon`                                                                                                                                                                                                               | Live            | Identity record | Node name and position.                                                                                                                                 |
| `freq`, `routing_sf`, `control_sf`, `bw`, `cr`, `tx_power`                                                                                                                                                                         | Live            | Persistent      | Radio settings; `control_sf` is an accepted alias for `routing_sf`.                                                                                     |
| `sf_list`, `lbt`, `beacon_ms`, `e2e_dm`, `intro_attach`                                                                                                                                                                            | Live            | Persistent      | MAC and message-policy settings.                                                                                                                        |
| `gw_announce_pct`, `gw_announce_interval`, `gw_herd_slack`                                                                                                                                                                         | Live            | Persistent      | Gateway announcement policy.                                                                                                                            |
| `remote_action_activation_ms` | Live, for newly prepared remote actions | Persistent | Zero selects a live-PHY-derived default; a nonzero value must satisfy the live floor and ceiling. See below. |
| `active_fraction`, `ch_min_ms`, `dm_min_ms`, `leaf_name`                                                                                                                                                                           | Live            | Persistent      | Managed-network activity and rate settings.                                                                                                             |
| `leaf_id`, `gateway_only`, `mobile`, `mobile_autoregister`                                                                                                                                                                         | Live            | Persistent      | Role and topology settings. Enabling `mobile_autoregister` may start a live registration session; disabling it does not end an already started session. |
| `nav`, `intra_layer_relay`, `host_mobiles`, `nav_ignore`, `hop_cap`, `team_hop_cap`, `team_channel_crypt`                                                                                                                          | Live            | Not persisted   | Session-only routing, hosting, and team-channel policy.                                                                                                 |
| `node_id`, `duty`                                                                                                                                                                                                                  | Reboot required | Persistent      | Values whose derived runtime state is initialized at boot.                                                                                              |
| `ble_mode`, `ble_period`, `ble_pin`                                                                                                                                                                                                | Reboot required | Persistent      | BLE startup policy and passkey.                                                                                                                         |
| `n_layers`, `layer0_id`, `window_period_ms`, `l0_window_ms`, `l0_window_offset_ms`, `l1_layer_id`, `l1_node_id`, `l1_routing_sf`, `l1_sf_list`, `l1_beacon_ms`, `l1_window_ms`, `l1_window_offset_ms`, `l1_freq`, `l1_bw`, `l1_cr` | Reboot required | Persistent      | Dual-layer gateway topology. The common parser accepts these keys; supported use outside gateway builds remains under review.                           |

Detailed value ranges and refusal messages will be added during the configuration-reference pass.

### Remote action activation delay

`cfg set remote_action_activation_ms <decimal-ms>` accepts `0` for the derived default, or a value from
the current PHY's reply-path budget through **299999 ms**, inclusive. The default is twice that budget;
there is no universal fixed minimum. Changing PHY can make a previously saved value unusable. Values
are never silently clamped, and an impossible PHY refuses even `0`. This setting is operator-class remotely.

USB `cfg` reports:

```text
  radmin: activation_ms=<effective> state=<state> cfg=<stored> floor=<minimum> default=<derived> ceiling=299999
```

BLE `cfg` exposes `remote_action_activation_ms` (the effective value) and
`remote_action_activation_state`. States are `default_derived`, `configured`, `below_floor`,
`above_ceiling`, `impossible_phy`. Only the first two permit scheduling; the other three report an
effective value of zero and refuse new deferred actions. A change does not retime an already prepared
action. A BLE `cfg set` returns the fresh configuration object while the handler's textual success/error
goes to USB; read the state/value back rather than treating receipt of that object as a successful write.

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

Remote-admin v2 is implemented. The old `rcmd`, `password`, `unlock` and `lock` commands are removed,
not aliases. Provision a controller key and target-book entry as described above. These four command
names are local to a CLIENT device, through USB or secured BLE; they cannot themselves be executed remotely.

### Issuing a request

| Form | Meaning |
| --- | --- |
| `remote <LABEL> -e [using=self\|using=keyN] [-a] -- <command>` | Authenticated, encrypted execution using an ACL-granted controller identity. Default credential is `self`. |
| `remote <LABEL> open -- status` | Unauthenticated, cleartext status query. |
| `remote <LABEL> open -- routes` | Unauthenticated, cleartext routing-table query. |
| `remote <LABEL> -e [using=self\|using=keyN] rollover` | Requests a safe new session epoch for the selected controller slot, without discarding unacknowledged work. |
| `remote <LABEL> -e [using=self\|using=keyN] rollover confirm` | Requests forced rollover: abandons outstanding work/results for that slot. Treat this as destructive session recovery. |
| `remote-retry <request-id>` | Recovers an outstanding or unknown authenticated request using its existing identity and sealed bytes, after comparing the target session. Not a new execution command. |
| `remote-result show <request-id>` | Re-emits a complete retained result on the requesting local transport, starting from the beginning. An incomplete result is not streamed as complete. |
| `remote-ack <request-id>` | Accepts/releases a completely delivered local result. For an authenticated terminal, creates the target-response ACK obligation. |

`LABEL` is a target-book label, not a node ID, routing hash or `fp=` selector. `keyN` is `key0`–`key9`.
Every execute form requires **exactly one of `-e` or `open`**; missing or combined modes refuse, with no
plaintext fallback. `using=...` and `-a` are forbidden with `open`; `-a` is also forbidden on rollover.
Put wrapper options before `--`; everything after it is the target's command, unquoted as a whole.
For example:

```text
remote base -e -- status
remote base -e using=key0 -- acl list
remote base -e -a -- cfg set beacon_ms 10000
```

`-a` asks for carrier-level end-to-end receipt diagnostics; it is independent of encryption and the
remote execution result. A carrier `acked` event is not a terminal result and does not prove successful
command effects. The target command is limited to 201 bytes with no embedded NUL/CR/LF. Open queries
must be exactly argument-free `status` or `routes`; additional arguments or trailing spaces refuse.

Open service has a **global quota of three admissions per target per rolling five minutes, shared by
all requesters**, plus a per-source occupied/cooldown restriction. Changing the requester does not
create another quota. Open requests have no authenticated transcript-recovery guarantee and can time
out without a reply when capacity or rate admission refuses them.

### Remote authority and deferred actions

Local command availability is not remote permission. A remote command must exist in the target build
and pass its authority check. Owners include operator privileges; no remote role grants physical or
controller-local authority.

| Authority / policy | Available forms |
| --- | --- |
| Open | Exact bare `status`, `routes` only. Authenticated operators/owners can also query them. |
| Operator | Ordinary diagnostics, messaging, channel read-cursor updates, most non-disruptive configuration, route/join-profile controls and `team keys`. Subject to feature and argument checks. |
| Owner additions | `acl list/add/set/remove`, `admin-id show`, `peerkey`, `del_msg` (channel only remotely), `clear_inbox confirm`, `team exportkey/grantkey/forgetkey`, and `cfg set e2e_dm`, `team_channel_crypt`, `ble_mode`, `ble_period`, `ble_pin`. |
| Physical USB only | `admin-id generate/rotate/reset`, `acl reset`. Never admitted remotely. |
| Controller-local / local-only | `admin-key`, `admin-target`, all four `remote*` commands, and `help`/`?`. Never admitted as target commands. |
| Deferred operator actions | `reboot`, `prep-restart`, `sleep [on\|off]`, when supported by the target. |
| Deferred owner actions | `ota`, `factory_reset confirm`, `crashtest hang\|fault\|reboot`; crash tests also require target-side `debug on` and the relevant hardware backend. |

The following disruptive changes are **refused remotely even for an owner**: `join`, `create`, `leave`,
`gateway`, team creation/membership changes, `regen`, and these `cfg set` keys:

```text
bw cr freq gateway_only host_mobiles l1_bw l1_cr l1_freq l1_layer_id l1_node_id
l1_routing_sf l1_sf_list layer0_id leaf_id mobile mobile_autoregister n_layers
node_id routing_sf control_sf sf_list tx_power
```

Use local provisioning for those operations. Non-disruptive family subcommands such as `team keys`
remain available at their stated authority. Inbox privacy restrictions apply even to an owner.

Deferred actions return `scheduled activation_ms=<delay>` rather than acting inside the command handler.
The target arms the deadline on the scheduled terminal's first checked send admission, either queued
or parked for later transmission; the action
becomes due on its authenticated response ACK or on expiry of that delay. A queued reply is not proof
the controller received it, so the action may still occur after a lost response. `scheduled` is a promise,
not confirmation of the final hardware outcome. Only one deferred promise can be outstanding on a target;
another eligible disruptive request gets `action_busy` and schedules nothing.
Neither safe nor forced rollover interrupts an executing operation or an already armed/due action;
those conditions return `executing`. Do not treat rollover as cancellation of a scheduled action.

Remote `ota` ensures the selected updater is started (Wi-Fi on ESP32, BLE DFU on nRF52); unlike the local
ESP32 toggle, repeating it is not an instruction to turn Wi-Fi OTA off. `factory_reset confirm` is destructive.
Remote `prep-restart` halts mesh radio and remote administration. **Restart the target locally afterward**;
neither remote reboot nor rollover can recover it while halted. The controller prints that warning before
submitting a `prep-restart` request.

### Output, results and acknowledgement

A request ID is 16 lowercase hex digits, nonzero, without `0x`. Copy the ID from the output; examples
below use `<id>` as a placeholder.

| USB output | What it proves |
| --- | --- |
| `> remote <id> accepted` | Local controller admission, not target receipt/execution. |
| `> remote <id> out <bytes>` | A chunk of the target handler's transcript, not necessarily a complete line. |
| `> remote <id> <result>` | A terminal or local outcome, interpreted below; some include `detail=<n>`. |
| `> remote <id> scheduled activation_ms=<n>` | A deferred-action promise with its chosen delay. |
| `> remote <id> retained` | Local delivery did not complete; keep the ID and request the result again. |
| `> remote <id> carrier <event> ctr=<n> ...` | Separate `acked`, `ack_timeout` or translated `custody_failure` diagnostics; not an execution result. |
| `> remote err <reason>` | A local parse/store/resource/admission refusal. |

`completed` means dispatch completed; **read the transcript as well**, since a handler may report a
domain error in its text. `refused` means the request was not admitted to that operation;
`unknown_command` means unclassified command. `output_truncated` means output was bounded, not that
effects were rolled back. `internal_error` is not a success. `action_busy` creates no new action.
`session_full`, `ingress_full`, `session_busy`, `executing` and `preparation_failed` are capacity/session
outcomes, not successful execution. `already_acknowledged` means the target already released that
transcript; an exact retry does not run the command again. `rollover_completed` reports a new session
epoch, with a nonzero abandoned-work count in `detail` when applicable.

An unanswered operation becomes **`unknown` after the 300-second outcome deadline**. It is not safe to
interpret this as “not executed.” The controller makes at most one automatic exact resend per waiting
stage, after 60 seconds for a same-layer route or 150 seconds for a cross-layer route. For explicit
recovery, use `remote-retry <id>`, not a fresh `remote ...` line. It compares the authenticated session;
if the epoch/slot changed, it keeps the outcome unknown instead of silently re-executing under a new
session. Exact retries in the same session replay the immutable transcript or report its acknowledged
state. The controller can attempt safe rollover when the target reports `session_full`; it does not
automatically force rollover.

USB normally accepts a known result after the entire output and terminal are written successfully,
then releases the local copy; a later `remote-result show` may therefore return `not_found`. If the ACK-debt
pool is full, release can refuse with `ack_debt_full` and the result remains retained. Unknown outcomes remain
retained for explicit recovery or acceptance. Requests, results and recovery state are bounded RAM,
not a durable command history across controller reboot.

On secured BLE, the four verbs acknowledge with `{"ack":"<verb>","id":"<id>"}` where applicable,
and local errors use `{"err":"remote","msg":"<reason>"}`. Result events are:

| Event name | Fields / interpretation |
| --- | --- |
| `remote_output` | `id`, `seq`, `body`; concatenate bodies in order. `seq` is the local output-fragment sequence, not a radio frame sequence. |
| `remote_terminal` | `id`, `result`, optionally `activation_ms` for scheduled or nonzero `detail` otherwise. |
| `remote_retained` | `id`; delivery was incomplete. |
| `remote_carrier` | `id`, `event`, `ctr`; custody reports also carry `origin`, `reporter`, `layer`, `reason`. |
| `remote_warning` | `body`; currently the `prep-restart` lockout warning. |

BLE retains a fully delivered result until `remote-ack <id>`; receiving `remote_terminal` alone does
not release it. After reconnect, a retained result may be offered again from sequence zero. Use
`remote-result show <id>` to request it explicitly (including from USB), and acknowledge only after
consuming the whole result. An incomplete result refuses ACK with `incomplete`. A transcript containing
NUL bytes cannot be delivered losslessly through the BLE text events and remains retained for USB.

The target's RESPONSE_ACK is distinct from both the local `remote-ack` command and the optional `-a`
carrier receipt. Its queued transmission is not proof of delivery: ACK debt is retained, with a bounded
retry burst and later retry opportunities on new requests to that target. It can keep `regen`/store
mutations busy. `remote-ack` of a delivered unknown result only releases local recovery state; it does
not prove target execution or send an ACK for an unknown terminal.

Common local refusals include `bad_args`, `unknown_target`, `ambiguous_target`, `store_invalid`,
`store_io_failed`, `request_table_full`, `assembly_full`, `result_full`, `ack_debt_full`,
`correlation_full`, `session_cache_full`, `carrier_unavailable`, `radio_enqueue_failed`, `not_found`
and `incomplete`. Correct the named cause rather than repeatedly issuing a fresh request ID for a
possibly completed action. For protocol-level details see [Protocol — remote administration](../protocol.md#15-remote-administration-v2).

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

Before this reference is marked complete, the older command families still need a uniform pass for:

- exact argument constraints and defaults;
- stable success output and important error output;
- USB versus BLE output form;
- safety warnings and recovery guidance;
- a link to the workflow chapter that explains when to use it;
- metal evidence where source inspection alone cannot prove behavior.

## Audit basis

The 2026-09-20 refresh used the production tree at `d11b5a9`: the command/config/inbox routers,
`firmware_admin_verbs.h`, `firmware_admin_client_verbs.h` and their stores, the remote controller,
action and activation modules, `firmware_command_authority.h`, `fw_main.cpp`, the console parser and
validator, and the core remote client/session/codec. Build/transport gates were checked in
`mr_features.h`, `device_ble.h` and `platformio.ini`.

The [generated command inventory](../superpowers/evidence/2026-09-04-radmin-command-inventory.md)
contains 197 command/subcommand/surface rows; it is not a count of primary names. Its generator check
and the authority-table checker (including six negative self-tests) pass against that tree. The manual
covers all 53 primary names and all 53 configuration-key spellings. No firmware or hardware gate was
rerun for this documentation-only refresh; [review notes](review-notes.md) retain the outstanding
manual/metal questions.
