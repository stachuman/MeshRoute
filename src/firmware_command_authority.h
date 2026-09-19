// MeshRoute — src/firmware_command_authority.h
// Slice 6: R-RA-33 / R-RA-32 semantic policy; transport eligibility stays in the inventory.
// No capability gates, mutable state, handlers or scheduling. Local callers never consult this table.
#pragma once
#include <string_view>
#include "firmware_command_context.h"

namespace mrfw {

enum class CommandClass : uint8_t { open, operator_, owner, physical, controller_local, local_only };
struct CommandPolicy {
    const char* verb;
    const char* subverb;
    CommandClass cls;
    bool disruptive;
};

// Checked against the ruled Markdown transcription AND the generated command inventory.
inline constexpr CommandPolicy kCommandPolicy[] = {
    {"acl", "—", CommandClass::owner, false},
    {"acl", "add", CommandClass::owner, false},
    {"acl", "list", CommandClass::owner, false},
    {"acl", "remove", CommandClass::owner, false},
    {"acl", "reset", CommandClass::physical, false},
    {"acl", "set", CommandClass::owner, false},
    {"admin-id", "—", CommandClass::owner, false},
    {"admin-id", "generate", CommandClass::physical, false},
    {"admin-id", "reset", CommandClass::physical, false},
    {"admin-id", "rotate", CommandClass::physical, false},
    {"admin-id", "show", CommandClass::owner, false},
    {"admin-key", "—", CommandClass::controller_local, false},
    {"admin-key", "export", CommandClass::controller_local, false},
    {"admin-key", "generate", CommandClass::controller_local, false},
    {"admin-key", "import", CommandClass::controller_local, false},
    {"admin-key", "list", CommandClass::controller_local, false},
    {"admin-key", "remove", CommandClass::controller_local, false},
    {"admin-key", "reset", CommandClass::controller_local, false},
    {"admin-key", "show", CommandClass::controller_local, false},
    {"admin-key", "show self", CommandClass::controller_local, false},
    {"admin-target", "—", CommandClass::controller_local, false},
    {"admin-target", "add", CommandClass::controller_local, false},
    {"admin-target", "list", CommandClass::controller_local, false},
    {"admin-target", "remove", CommandClass::controller_local, false},
    {"admin-target", "reset", CommandClass::controller_local, false},
    {"admin-target", "set", CommandClass::controller_local, false},
    {"admin-target", "show", CommandClass::controller_local, false},
    {"cfg", "—", CommandClass::operator_, false},
    {"cfg set", "—", CommandClass::operator_, false},
    {"cfg set", "active_fraction", CommandClass::operator_, false},
    {"cfg set", "beacon_ms", CommandClass::operator_, false},
    {"cfg set", "ble_mode", CommandClass::owner, false},
    {"cfg set", "ble_mode off", CommandClass::owner, false},
    {"cfg set", "ble_mode on", CommandClass::owner, false},
    {"cfg set", "ble_mode periodic", CommandClass::owner, false},
    {"cfg set", "ble_period", CommandClass::owner, false},
    {"cfg set", "ble_pin", CommandClass::owner, false},
    {"cfg set", "bw", CommandClass::operator_, true},
    {"cfg set", "ch_min_ms", CommandClass::operator_, false},
    {"cfg set", "cr", CommandClass::operator_, true},
    {"cfg set", "dm_min_ms", CommandClass::operator_, false},
    {"cfg set", "duty", CommandClass::operator_, false},
    {"cfg set", "e2e_dm", CommandClass::owner, false},
    {"cfg set", "freq", CommandClass::operator_, true},
    {"cfg set", "gateway_only", CommandClass::operator_, true},
    {"cfg set", "gw_announce_interval", CommandClass::operator_, false},
    {"cfg set", "remote_action_activation_ms", CommandClass::operator_, false},
    {"cfg set", "gw_announce_pct", CommandClass::operator_, false},
    {"cfg set", "gw_herd_slack", CommandClass::operator_, false},
    {"cfg set", "hop_cap", CommandClass::operator_, false},
    {"cfg set", "host_mobiles", CommandClass::operator_, true},
    {"cfg set", "intra_layer_relay", CommandClass::operator_, false},
    {"cfg set", "intro_attach", CommandClass::operator_, false},
    {"cfg set", "l0_window_ms", CommandClass::operator_, false},
    {"cfg set", "l0_window_offset_ms", CommandClass::operator_, false},
    {"cfg set", "l1_beacon_ms", CommandClass::operator_, false},
    {"cfg set", "l1_bw", CommandClass::operator_, true},
    {"cfg set", "l1_cr", CommandClass::operator_, true},
    {"cfg set", "l1_freq", CommandClass::operator_, true},
    {"cfg set", "l1_layer_id", CommandClass::operator_, true},
    {"cfg set", "l1_node_id", CommandClass::operator_, true},
    {"cfg set", "l1_routing_sf", CommandClass::operator_, true},
    {"cfg set", "l1_sf_list", CommandClass::operator_, true},
    {"cfg set", "l1_window_ms", CommandClass::operator_, false},
    {"cfg set", "l1_window_offset_ms", CommandClass::operator_, false},
    {"cfg set", "lat (alias: lon)", CommandClass::operator_, false},
    {"cfg set", "layer0_id", CommandClass::operator_, true},
    {"cfg set", "lbt", CommandClass::operator_, false},
    {"cfg set", "leaf_id", CommandClass::operator_, true},
    {"cfg set", "leaf_name", CommandClass::operator_, false},
    {"cfg set", "mobile", CommandClass::operator_, true},
    {"cfg set", "mobile_autoregister", CommandClass::operator_, true},
    {"cfg set", "mobile_autoregister true", CommandClass::operator_, true},
    {"cfg set", "n_layers", CommandClass::operator_, true},
    {"cfg set", "name", CommandClass::operator_, false},
    {"cfg set", "nav", CommandClass::operator_, false},
    {"cfg set", "nav_ignore", CommandClass::operator_, false},
    {"cfg set", "node_id", CommandClass::operator_, true},
    {"cfg set", "routing_sf (alias: control_sf)", CommandClass::operator_, true},
    {"cfg set", "sf_list", CommandClass::operator_, true},
    {"cfg set", "team_channel_crypt", CommandClass::owner, false},
    {"cfg set", "team_hop_cap", CommandClass::operator_, false},
    {"cfg set", "tx_power", CommandClass::operator_, true},
    {"cfg set", "window_period_ms", CommandClass::operator_, false},
    {"clear_inbox", "—", CommandClass::owner, false},
    {"crashtest", "—", CommandClass::owner, true},
    {"crashtest", "fault", CommandClass::owner, true},
    {"crashtest", "hang", CommandClass::owner, true},
    {"crashtest", "reboot", CommandClass::owner, true},
    {"create", "—", CommandClass::operator_, true},
    {"create", "active_fraction", CommandClass::operator_, true},
    {"create", "ch_min_ms", CommandClass::operator_, true},
    {"create", "dm_min_ms", CommandClass::operator_, true},
    {"create", "duty", CommandClass::operator_, true},
    {"create", "name", CommandClass::operator_, true},
    {"create", "sf_list", CommandClass::operator_, true},
    {"debug", "—", CommandClass::operator_, false},
    {"debug", "off (alias: 0)", CommandClass::operator_, false},
    {"del_msg", "—", CommandClass::owner, false},
    {"duty", "—", CommandClass::operator_, false},
    {"factory_reset", "—", CommandClass::owner, true},
    {"factory_reset", "confirm", CommandClass::owner, true},
    {"faults", "—", CommandClass::operator_, false},
    {"gateway", "—", CommandClass::operator_, true},
    {"hashof", "—", CommandClass::operator_, false},
    {"help (alias: ?)", "—", CommandClass::local_only, false},
    {"join", "—", CommandClass::operator_, true},
    {"join (alias: create)", "—", CommandClass::operator_, true},
    {"joinprofile", "—", CommandClass::operator_, false},
    {"joinprofile", "clear", CommandClass::operator_, false},
    {"joinprofile", "list", CommandClass::operator_, false},
    {"joinprofile", "reset", CommandClass::operator_, false},
    {"joinprofile", "reset confirm", CommandClass::operator_, false},
    {"joinprofile", "set", CommandClass::operator_, false},
    {"joinprofile", "set name", CommandClass::operator_, false},
    {"leave", "—", CommandClass::operator_, true},
    {"limits", "—", CommandClass::operator_, false},
    {"lookup", "—", CommandClass::operator_, false},
    {"mark_read", "—", CommandClass::operator_, false},
    {"mobile", "—", CommandClass::operator_, false},
    {"mobile", "gateways", CommandClass::operator_, false},
    {"mobile", "query", CommandClass::operator_, false},
    {"mobile", "register", CommandClass::operator_, false},
    {"mobile", "register scan", CommandClass::operator_, false},
    {"mobile", "status", CommandClass::operator_, false},
    {"mobile", "unregister", CommandClass::operator_, false},
    {"nameof", "—", CommandClass::operator_, false},
    {"ota", "—", CommandClass::owner, true},
    {"peerkey", "—", CommandClass::owner, false},
    {"peername", "—", CommandClass::operator_, false},
    {"peers", "—", CommandClass::operator_, false},
    {"peers", "all", CommandClass::operator_, false},
    {"prep-restart", "—", CommandClass::operator_, true},
    {"pull_inbox", "—", CommandClass::operator_, false},
    {"reboot", "—", CommandClass::operator_, true},
    {"regen", "—", CommandClass::owner, true},
    {"remote", "—", CommandClass::controller_local, false},
    {"remote-ack", "—", CommandClass::controller_local, false},
    {"remote-result", "—", CommandClass::controller_local, false},
    {"remote-retry", "—", CommandClass::controller_local, false},
    {"reqpubkey", "-s", CommandClass::operator_, false},
    {"reqpubkey", "-t", CommandClass::operator_, false},
    {"reqpubkey", "—", CommandClass::operator_, false},
    {"resolve", "—", CommandClass::operator_, false},
    {"resolve", "hard", CommandClass::operator_, false},
    {"route", "—", CommandClass::operator_, false},
    {"route", "add", CommandClass::operator_, false},
    {"route", "del", CommandClass::operator_, false},
    {"routes", "—", CommandClass::open, false},
    {"send", "—", CommandClass::operator_, false},
    {"send_channel", "—", CommandClass::operator_, false},
    {"send_layer", "—", CommandClass::operator_, false},
    {"sleep", "—", CommandClass::operator_, true},
    {"sleep", "off", CommandClass::operator_, true},
    {"status", "—", CommandClass::open, false},
    {"team", "—", CommandClass::operator_, true},
    {"team", "exportkey", CommandClass::owner, false},
    {"team", "forgetkey", CommandClass::owner, false},
    {"team", "grantkey", CommandClass::owner, false},
    {"team", "keys", CommandClass::operator_, false},
    {"team", "new", CommandClass::owner, true},
    {"team forgetkey", "confirm", CommandClass::owner, false},
    {"testch", "—", CommandClass::operator_, false},
    {"testclear", "—", CommandClass::operator_, false},
    {"testsend", "—", CommandClass::operator_, false},
    {"testsend|testch", "-a", CommandClass::operator_, false},
    {"testsend|testch", "-e", CommandClass::operator_, false},
    {"testsend|testch", "-t", CommandClass::operator_, false},
    {"teststatus", "—", CommandClass::operator_, false},
    {"ui", "—", CommandClass::operator_, false},
    {"ui", "preset", CommandClass::operator_, false},
    {"ui", "preset clear", CommandClass::operator_, false},
    {"ui", "preset list", CommandClass::operator_, false},
    {"ui", "preset reset", CommandClass::operator_, false},
    {"ui", "preset reset all", CommandClass::operator_, false},
    {"ui", "preset set", CommandClass::operator_, false},
    {"version", "—", CommandClass::operator_, false},
    {"whoami", "—", CommandClass::operator_, false},
};

// Inventory aliases remain in their cells (one row, not a second authority map).
namespace command_policy_detail {
using View = std::string_view;
inline constexpr bool whitespace(char c) { return c == ' ' || c == '\t'; }

// Match a spelling without allocating. Multiple inter-token spaces are accepted conservatively;
// the real parser still decides grammar. Subcommand prefixes must NOT fall back to a weaker bare
// family: existing handlers accept prefixes such as "team newfreq=..." (handle_team's mint_form).
inline size_t spelling_match(View input, View spelling, bool boundary) {
    size_t i = 0;
    for (size_t j = 0; j < spelling.size(); ++j) {
        if (spelling[j] == ' ') {
            if (i == input.size() || !whitespace(input[i])) return 0;
            while (i < input.size() && whitespace(input[i])) ++i;
        } else {
            if (i == input.size() || input[i] != spelling[j]) return 0;
            ++i;
        }
    }
    if (boundary && i < input.size() && !whitespace(input[i])) return 0;
    return i;
}

inline size_t alternatives_match(View input, View cell, bool boundary) {
    size_t best = 0;
    while (!cell.empty()) {
        const size_t split = cell.find_first_of("|,");
        View spelling = cell.substr(0, split);
        while (!spelling.empty() && spelling.front() == ' ') spelling.remove_prefix(1);
        while (!spelling.empty() && spelling.back() == ' ') spelling.remove_suffix(1);
        const size_t n = spelling_match(input, spelling, boundary);
        if (n > best) best = n;
        if (split == View::npos) break;
        cell.remove_prefix(split + 1);
    }
    return best;
}

inline size_t cell_match(View input, View cell, bool boundary) {
    const size_t alias = cell.find(" (alias: ");
    if (alias == View::npos) return alternatives_match(input, cell, boundary);
    const size_t primary = alternatives_match(input, cell.substr(0, alias), boundary);
    const View aliases = cell.substr(alias + 9, cell.size() - alias - 10);
    const size_t other = alternatives_match(input, aliases, boundary);
    return primary > other ? primary : other;
}
}  // namespace command_policy_detail

inline const CommandPolicy* command_policy_lookup(const char* line, size_t len) {
    using namespace command_policy_detail;
    const View input(line, len);
    const CommandPolicy* best = nullptr;
    size_t best_verb = 0, best_sub = 0;
    for (const auto& row : kCommandPolicy) {
        const size_t verb = cell_match(input, row.verb, true);
        if (!verb) continue;
        size_t tail = verb;
        while (tail < len && whitespace(input[tail])) ++tail;
        const bool bare = View(row.subverb) == "—";
        const size_t sub = bare ? 0 : cell_match(input.substr(tail), row.subverb, false);
        if (!bare && !sub) continue;
        if (!best || verb > best_verb || (verb == best_verb && sub > best_sub)) {
            best = &row;
            best_verb = verb;
            best_sub = sub;
        }
    }
    return best;
}

inline bool command_authority_admits(const CommandPolicy& row, const CommandContext& ctx,
                                     const char* line, size_t len) {
    switch (ctx.authority) {
        case CommandAuthority::local:
            return true;
        case CommandAuthority::remote_open:
            return row.cls == CommandClass::open && std::string_view(line, len) == row.verb;
        case CommandAuthority::remote_operator:
            return row.cls == CommandClass::open || row.cls == CommandClass::operator_;
        case CommandAuthority::remote_owner:
            return row.cls == CommandClass::open || row.cls == CommandClass::operator_
                || row.cls == CommandClass::owner;
    }
    return false;
}

}  // namespace mrfw
