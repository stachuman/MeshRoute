// MeshRoute — lib/console/console_parse.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// Heap-free console line parsers shared by the device console and the sim's
// FirmwareNode. hal.h discipline: no std::string/heap, C++17-includable.
// See docs/specs/2026-05-30-device-console-design.md.
#pragma once
#include "command.h"   // Command, CmdKind, SendCmd
#include "node.h"      // NodeConfig
#include <cstddef>
#include <cstdint>

namespace meshroute::console {

enum class ParseErr : uint8_t { ok, empty, unknown_verb, bad_args };

// Parses a `send <dst> <body...>` line into `out`. `out.body` BORROWS into
// `line` (valid only while `line` lives — caller passes its line buffer and
// calls Node::on_command before reusing it). Only `send` is handled here;
// control verbs (cfg/start/verbose/status) are dispatched by the caller.
ParseErr parse_command(const char* line, size_t len, Command& out);

enum class CfgErr : uint8_t { ok, unknown_key, bad_value };

// Parses one `cfg <key> <val>` line, mutating the targets in place.
CfgErr parse_cfg(const char* line, size_t len, NodeConfig& cfg,
                 uint8_t& node_id, uint32_t& key_hash32);

}  // namespace meshroute::console
