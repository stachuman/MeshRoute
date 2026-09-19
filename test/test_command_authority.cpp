// Slice 6: pure policy tests. Real seam execution is in probe_inbox_verbs, not this binary.
#include "doctest.h"
#include "firmware_command_authority.h"
#include "console_line.h"
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace {
using namespace mrfw;

// Test-side expansion of the inventory's presentation cells. Neither production lookup nor the
// generator supplies expected spellings/classes to these tests.
std::vector<std::string> spellings(std::string cell) {
    const auto alias = cell.find(" (alias: ");
    if (alias != std::string::npos) {
        cell.replace(alias, 9, "|");
        cell.pop_back();
    }
    std::vector<std::string> out;
    size_t first = 0;
    while (first < cell.size()) {
        size_t last = cell.find_first_of("|,", first);
        if (last == std::string::npos) last = cell.size();
        std::string s = cell.substr(first, last - first);
        while (!s.empty() && s.front() == ' ') s.erase(0, 1);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        out.push_back(s);
        first = last + 1;
    }
    return out;
}

CommandContext context(CommandAuthority authority) {
    return {CommandTransport::remote, authority, false, 42, meshroute::console::remote_command_max_bytes};
}
const CommandPolicy* lookup(const char* line) { return command_policy_lookup(line, std::strlen(line)); }

TEST_CASE("cmdauthority every ruled row and every alias resolves with and without arguments") {
    for (const auto& row : kCommandPolicy) {
        for (const auto& verb : spellings(row.verb)) {
            for (const auto& sub : spellings(row.subverb)) {
                const std::string base = verb + (sub == "—" ? "" : " " + sub);
                for (const auto& line : {base, base + " argument"}) {
                    const auto* actual = command_policy_lookup(line.data(), line.size());
                    CHECK(actual != nullptr);
                    if (!actual) continue;
                    CHECK(actual->cls == row.cls);
                    CHECK(actual->disruptive == row.disruptive);
                }
            }
        }
    }
}

TEST_CASE("cmdauthority owner and physical distinctions are semantic") {
    for (const char* line : {"rcmd 1 status", "password x", "unlock x", "lock"}) CHECK(lookup(line) == nullptr);
    struct Example { const char* line; CommandClass cls; bool disruptive; };
    const Example examples[] = {
        {"cfg set e2e_dm 1", CommandClass::owner, false},
        {"cfg set name Stan", CommandClass::operator_, false},
        {"mobile register scan", CommandClass::operator_, false},
        {"peers all", CommandClass::operator_, false}, {"peers", CommandClass::operator_, false},
        {"status", CommandClass::open, false}, {"status x", CommandClass::open, false},
        {"team new", CommandClass::owner, true}, {"acl list", CommandClass::owner, false},
        {"acl reset confirm", CommandClass::physical, false}, {"admin-id show", CommandClass::owner, false},
        {"admin-id generate", CommandClass::physical, false},
        {"admin-key list", CommandClass::controller_local, false},
        {"help", CommandClass::local_only, false},
        {"team forgetkey confirm", CommandClass::owner, false},
    };
    for (const auto& example : examples) {
        const auto* row = lookup(example.line);
        CHECK(row != nullptr);
        if (!row) continue;
        CHECK(row->cls == example.cls);
        CHECK(row->disruptive == example.disruptive);
    }
}

TEST_CASE("cmdauthority six classes by four authorities truth table") {
    const bool expected[4][6] = {
        {true, true, true, true, true, true},
        {true, false, false, false, false, false},
        {true, true, false, false, false, false},
        {true, true, true, false, false, false},
    };
    for (unsigned authority = 0; authority < 4; ++authority) {
        for (unsigned cls = 0; cls < 6; ++cls) {
            const CommandPolicy row{"status", "—", static_cast<CommandClass>(cls), false};
            CHECK(command_authority_admits(row, context(static_cast<CommandAuthority>(authority)),
                                           "status", 6) == expected[authority][cls]);
        }
    }
    const CommandPolicy poisoned{"not-a-command", "—", static_cast<CommandClass>(255), true};
    CHECK(command_authority_admits(poisoned, context(CommandAuthority::local), nullptr, 0));
    CHECK_FALSE(command_authority_admits(poisoned, context(static_cast<CommandAuthority>(255)), "", 0));
}

TEST_CASE("cmdauthority open access is exact and argument free") {
    for (const char* verb : {"status", "routes"}) {
        const auto* row = lookup(verb);
        CHECK(row != nullptr);
        if (!row) continue;
        CHECK(command_authority_admits(*row, context(CommandAuthority::remote_open), verb, std::strlen(verb)));
        for (const auto& extra : {std::string(verb) + " x", std::string(verb) + " ",
                                 std::string(" ") + verb, std::string(verb) + "\t"}) {
            CHECK_FALSE(command_authority_admits(*row, context(CommandAuthority::remote_open),
                                                extra.data(), extra.size()));
            CHECK(command_authority_admits(*row, context(CommandAuthority::remote_operator),
                                           extra.data(), extra.size()));
        }
    }
}

TEST_CASE("cmdauthority unknown primary and token boundaries fail closed") {
    for (const char* line : {"", "unknown", "statusx", "routesX", "admin-identity", "Status", " status"})
        CHECK(lookup(line) == nullptr);
    CHECK(command_policy_lookup("status x", 6) == lookup("status"));
}

TEST_CASE("cmdauthority owner-prefixed subcommands never downgrade to the bare family") {
    // Existing handle_team prefix arms accept these spellings; this is a conservative authorization
    // test, not a claim that each malformed tail executes successfully in the real handler.
    for (const char* line : {"team   new", "team newfreq=868", "team grantkey0x12345678",
                             "team forgetkey0x12345678 confirm", "acl\treset confirm", "admin-id  rotate"}) {
        const auto* row = lookup(line);
        CHECK(row != nullptr);
        if (row) CHECK_FALSE(command_authority_admits(*row, context(CommandAuthority::remote_operator),
                                                     line, std::strlen(line)));
    }
}

TEST_CASE("cmdauthority disruptive set equals the independent ruled list") {
    std::set<std::string> expected = {
        "create", "create name", "create sf_list", "create duty", "create active_fraction",
        "create ch_min_ms", "create dm_min_ms", "join", "join (alias: create)", "leave", "gateway",
        "team", "team new", "reboot", "prep-restart", "sleep", "sleep off",
        "regen", "factory_reset", "factory_reset confirm", "ota", "crashtest", "crashtest fault",
        "crashtest hang", "crashtest reboot",
    };
    for (const char* key : {"node_id", "leaf_id", "layer0_id", "l1_layer_id", "l1_node_id", "n_layers",
                            "freq", "bw", "cr", "sf_list", "routing_sf (alias: control_sf)", "tx_power",
                            "l1_freq", "l1_bw", "l1_cr", "l1_sf_list", "l1_routing_sf", "gateway_only",
                            "host_mobiles", "mobile", "mobile_autoregister", "mobile_autoregister true"})
        expected.insert(std::string("cfg set ") + key);
    std::set<std::string> actual;
    for (const auto& row : kCommandPolicy)
        if (row.disruptive) actual.insert(std::string(row.verb) + (std::strcmp(row.subverb, "—") ? " " + std::string(row.subverb) : ""));
    CHECK(actual == expected);
}

TEST_CASE("cmdcontext carries the supplied transport authority presence id and bound") {
    const auto ctx = context(CommandAuthority::remote_owner);
    CHECK(ctx.transport == CommandTransport::remote);
    CHECK(ctx.authority == CommandAuthority::remote_owner);
    CHECK_FALSE(ctx.physical_presence);
    CHECK(ctx.request_id == 42);
    CHECK(ctx.line_max_bytes == 201);
    CHECK(DispatchOutcome::completed != DispatchOutcome::scheduled);
    CHECK(DispatchOutcome::unmatched != DispatchOutcome::internal_failure);
}
}  // namespace
