#!/usr/bin/env python3
"""B423: deletion of the radio-only alias exposes an unfenced policy census.

Read-only with respect to the candidate. The restored-alias control is in memory;
the two real-header C++ programs build in a temporary directory.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=Path, required=True)
parser.add_argument('--base', type=Path, required=True)
args = parser.parse_args()
sys.path.insert(0, str(args.root / 'tools'))
import check_command_authority as authority

header_path = 'src/firmware_command_authority.h'
table_path = authority.G.AUTHORITY_TABLE
inventory_path = authority.G.TRACKED_OUTPUT
header = (args.root / header_path).read_text()
table = (args.root / table_path).read_text()
inventory = (args.root / inventory_path).read_text()
alias = 'reboot (alias: prep-restart)'
base_header = (args.base / header_path).read_text()
base_table = (args.base / table_path).read_text()
header_row, = [x for x in base_header.splitlines(True) if alias in x]
table_row, = [x for x in base_table.splitlines(True) if '`' + alias + '`' in x]
assert alias not in header and alias not in table
assert len(authority.parse_inventory(inventory)) == 197
assert authority.check(table, header, inventory) == []
print('PASS: candidate inventory 197; authority agrees after six semantic-row deletions')
restored_header = header.replace('inline constexpr CommandPolicy kCommandPolicy[] = {\n',
                                 'inline constexpr CommandPolicy kCommandPolicy[] = {\n' + header_row)
errors = authority.check(table + table_row, restored_header, inventory)
assert errors == ["orphan ruled row: ('reboot (alias: prep-restart)', '—')"], errors
print('RED with alias restored:', errors[0])

program = r'''
#include "firmware_command_authority.h"
#include <cstdio>
#include <cstring>
#include <string_view>
int main() {
    unsigned scheduled = 0, refused = 0;
    for (const auto& p : mrfw::kCommandPolicy) {
        if (!p.disruptive) continue;
        const std::string_view v = p.verb;
        if (v == "reboot" || v == "reboot (alias: prep-restart)" || v == "prep-restart"
            || v == "ota" || v == "factory_reset" || v == "sleep" || v == "crashtest")
            ++scheduled;
        else ++refused;
    }
    std::printf("scheduled-policy-rows=%u refused-policy-rows=%u\n", scheduled, refused);
    for (const char* line : {"reboot", "prep-restart", "ota", "factory_reset confirm",
             "sleep", "sleep on", "sleep off", "sleep off...", "sleep OFF",
             "crashtest hang", "crashtest fault-extra", "crashtest reboot-extra"}) {
        const auto* p = mrfw::command_policy_lookup(line, std::strlen(line));
        if (!p) return 2;
        std::printf("%s: operator=%d owner=%d disruptive=%d\n", line,
            p->cls == mrfw::CommandClass::operator_, p->cls == mrfw::CommandClass::owner,
            p->disruptive);
    }
}
'''
with tempfile.TemporaryDirectory(prefix='mr-s9-b423-') as temp:
    temp = Path(temp)
    source = temp / 'census.cpp'
    source.write_text(program)
    outputs = []
    for name, root in [('base', args.base), ('candidate', args.root)]:
        binary = temp / name
        subprocess.run(['c++', '-std=c++17', '-I' + str(root / 'src'), str(source),
                        '-o', str(binary)], check=True)
        output = subprocess.check_output([str(binary)], text=True)
        outputs.append(output.splitlines())
        print(name + ':\n' + output, end='')
    assert outputs[0][0] == 'scheduled-policy-rows=12 refused-policy-rows=36'
    assert outputs[1][0] == 'scheduled-policy-rows=11 refused-policy-rows=36'
    assert outputs[0][1:] == outputs[1][1:]
probe = args.root / 'tools/probe_deferred_actions/remote_rows.h'
assert probe.read_bytes() == (args.base / probe.relative_to(args.root)).read_bytes()
assert 'CHECK_REMOTE(scheduled_scope==12);' in probe.read_text()
print('PASS: 12 exercised command spellings retain their lookup results; unchanged probe requires 12 policy rows')
print('STOP-1 B423: real probe census is 11, but its unfenced assertion requires 12')
