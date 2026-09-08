<!-- Coder transcription: OpenAI Codex; classification belongs to the owner. -->
# Slice 6 — normalized command authority (R-RA-33 / R-RA-32)

This is the row-by-row transcription of the owner-ruled [classification proposal](../plans/2026-09-06-radmin-authority-classification-proposal.md).
It is consumed by the inventory generator and checked against the C++ policy table. Classes are semantic,
not transport permissions. R-RA-29's local USB-only guards remain independent. Dual owner-remote/physical-local
rows transcribe as owner per brief §4.4 (B346). Bare family rows carry the family's class; named subcommands
override them. Alias and shared-verb cells retain the inventory's spelling, not extra policy rows.

The two refusal discriminators (`peers <args> — refused console_only`, `joinprofile — refused gateway_build`)
bind to their bare semantic rows; they are not executable subcommands. The gateway join/create and legacy
reboot/prep-restart alias cells retain their own provenance but have the same class/flag as their spellings.

## Semantic policy

| verb | sub-verb | class | disruptive | authority |
| --- | --- | --- | --- | --- |
| `acl` | — | owner | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `acl` | `add` | owner | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `acl` | `list` | owner | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `acl` | `remove` | owner | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `acl` | `reset` | physical | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `acl` | `set` | owner | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `admin-id` | — | owner | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `admin-id` | `generate` | physical | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `admin-id` | `reset` | physical | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `admin-id` | `rotate` | physical | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `admin-id` | `show` | owner | no | [§G](../plans/2026-09-06-radmin-authority-classification-proposal.md#g-target-side-remote-admin-stores-slice-3--the-physical-class) |
| `admin-key` | — | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-key` | `export` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-key` | `generate` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-key` | `import` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-key` | `list` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-key` | `remove` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-key` | `reset` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-key` | `show` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-key` | `show self` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-target` | — | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-target` | `add` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-target` | `list` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-target` | `remove` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-target` | `reset` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-target` | `set` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `admin-target` | `show` | controller_local | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `cfg` | — | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | — | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `active_fraction` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `beacon_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `ble_mode` | owner | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `ble_mode off` | owner | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `ble_mode on` | owner | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `ble_mode periodic` | owner | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `ble_period` | owner | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `ble_pin` | owner | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `bw` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `ch_min_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `cr` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `dm_min_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `duty` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `e2e_dm` | owner | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `freq` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `gateway_only` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `gw_announce_interval` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `remote_action_activation_ms` | operator | no | [Slice 7a §4.7](../plans/2026-09-07-radmin-slice7a-activation-config.md#47-classification-of-the-new-row) |
| `cfg set` | `gw_announce_pct` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `gw_herd_slack` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `hop_cap` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `host_mobiles` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `intra_layer_relay` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `intro_attach` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l0_window_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l0_window_offset_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_beacon_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_bw` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_cr` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_freq` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_layer_id` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_node_id` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_routing_sf` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_sf_list` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_window_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `l1_window_offset_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `lat (alias: lon)` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `layer0_id` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `lbt` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `leaf_id` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `leaf_name` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `mobile` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `mobile_autoregister` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `mobile_autoregister true` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `n_layers` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `name` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `nav` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `nav_ignore` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `node_id` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `routing_sf (alias: control_sf)` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `sf_list` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `team_channel_crypt` | owner | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `team_hop_cap` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `tx_power` | operator | yes | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `cfg set` | `window_period_ms` | operator | no | [§B](../plans/2026-09-06-radmin-authority-classification-proposal.md#b-configuration-cfg-cfg-set-key) |
| `clear_inbox` | — | owner | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `crashtest` | — | owner | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `crashtest` | `fault` | owner | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `crashtest` | `hang` | owner | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `crashtest` | `reboot` | owner | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `create` | — | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `create` | `active_fraction` | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `create` | `ch_min_ms` | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `create` | `dm_min_ms` | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `create` | `duty` | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `create` | `name` | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `create` | `sf_list` | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `debug` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `debug` | `off (alias: 0)` | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `del_msg` | — | owner | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `duty` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `factory_reset` | — | owner | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `factory_reset` | `confirm` | owner | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `faults` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `gateway` | — | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `hashof` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `help (alias: ?)` | — | local_only | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `join` | — | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `join (alias: create)` | — | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `joinprofile` | — | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `joinprofile` | `clear` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `joinprofile` | `list` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `joinprofile` | `reset` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `joinprofile` | `reset confirm` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `joinprofile` | `set` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `joinprofile` | `set name` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `leave` | — | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `limits` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `lock` | — | legacy | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `lookup` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `mark_read` | — | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) · R-RA-32 |
| `mobile` | — | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `mobile` | `gateways` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `mobile` | `query` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `mobile` | `register` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `mobile` | `register scan` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `mobile` | `status` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `mobile` | `unregister` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `nameof` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `ota` | — | owner | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `password` | — | legacy | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `password rotate` | — | legacy | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `peerkey` | — | owner | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `peername` | — | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `peers` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `peers` | `all` | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `prep-restart` | — | operator | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `pull_inbox` | — | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) · R-RA-32 |
| `rcmd` | — | legacy | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `reboot` | — | operator | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `reboot (alias: prep-restart)` | — | operator | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `regen` | — | owner | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `reqpubkey` | `-s` | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `reqpubkey` | `-t` | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `reqpubkey` | — | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `resolve` | — | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `resolve` | `hard` | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `route` | — | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `route` | `add` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `route` | `del` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `routes` | — | open | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `send` | — | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `send_channel` | — | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `send_layer` | — | operator | no | [§D](../plans/2026-09-06-radmin-authority-classification-proposal.md#d-messaging-and-address-book) |
| `sleep` | — | operator | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `sleep` | `off` | operator | yes | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `status` | — | open | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `team` | — | operator | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `team` | `exportkey` | owner | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `team` | `forgetkey` | owner | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `team` | `grantkey` | owner | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `team` | `keys` | operator | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `team` | `new` | owner | yes | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `team forgetkey` | `confirm` | owner | no | [§C](../plans/2026-09-06-radmin-authority-classification-proposal.md#c-network-membership-and-roles) |
| `testch` | — | operator | no | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `testclear` | — | operator | no | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `testsend` | — | operator | no | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `testsend\|testch` | `-a` | operator | no | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `testsend\|testch` | `-e` | operator | no | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `testsend\|testch` | `-t` | operator | no | [§E](../plans/2026-09-06-radmin-authority-classification-proposal.md#e-disruptive-actions-fault-injection-ota) |
| `teststatus` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `ui` | — | operator | no | [§F](../plans/2026-09-06-radmin-authority-classification-proposal.md#f-the-oled-panel) |
| `ui` | `preset` | operator | no | [§F](../plans/2026-09-06-radmin-authority-classification-proposal.md#f-the-oled-panel) |
| `ui` | `preset clear` | operator | no | [§F](../plans/2026-09-06-radmin-authority-classification-proposal.md#f-the-oled-panel) |
| `ui` | `preset list` | operator | no | [§F](../plans/2026-09-06-radmin-authority-classification-proposal.md#f-the-oled-panel) |
| `ui` | `preset reset` | operator | no | [§F](../plans/2026-09-06-radmin-authority-classification-proposal.md#f-the-oled-panel) |
| `ui` | `preset reset all` | operator | no | [§F](../plans/2026-09-06-radmin-authority-classification-proposal.md#f-the-oled-panel) |
| `ui` | `preset set` | operator | no | [§F](../plans/2026-09-06-radmin-authority-classification-proposal.md#f-the-oled-panel) |
| `unlock` | — | legacy | no | [§H](../plans/2026-09-06-radmin-authority-classification-proposal.md#h-controller-side-slice-4-and-legacy--never-target-dispatchable) |
| `version` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
| `whoami` | — | operator | no | [§A](../plans/2026-09-06-radmin-authority-classification-proposal.md#a-diagnostics-and-identity-readers) |
