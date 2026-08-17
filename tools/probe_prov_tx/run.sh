#!/usr/bin/env bash
# Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
#
# §PROV-TX structural probe ([[B207]]) — source-shape checks + a negative control for each.
#
# WHY IT IS NOT A `pio` ENV, and why the checks are structural: `src/firmware_config.cpp` is compiled by NEITHER the
# native suite (`test_build_src = no`) NOR the simulator (which builds `lib/core` + `lib/console`), and no scenario runs
# a console verb. The BEHAVIOUR of the transaction is natively tested in `test/test_firmware_provisioning_service.cpp`;
# what remains — that the console holds none of [[B207]]'s six live mutations, and that no FALLIBLE key primitive
# survives anywhere near the post-save block — is a SOURCE FACT, and is asserted as one. See probe.py's header.
#
# ★★ IT IS COMMITTED, DELIBERATELY, like tools/probe_board_ui and tools/probe_console_sink (owner ruling 2026-08-04): a
#    reconstruction recipe in a note is not a storage location, and this project has already LOST a proven 33-assert
#    scenario to a session scratchpad.
#
# USAGE:  tools/probe_prov_tx/run.sh          # the checks AND the negative controls (the controls run BY DEFAULT)
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)     # ★ absolute — a relative path in a cwd-resetting shell once measured nothing
                                        #   at all (register B82). Never make these relative.
exec python3 "$HERE/probe.py"
