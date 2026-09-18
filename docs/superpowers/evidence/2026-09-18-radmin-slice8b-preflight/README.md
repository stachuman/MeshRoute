# Slice 8b revision-2 preflight artifacts

These are bounded coder preflight instruments and logs, not an implementation gate. The parent
[receipt](../2026-09-18-radmin-slice8b.md) defines the measurements and their limits.

- `state.json`, `inputs.json`, `preparation-inputs.json` and `preparation.patch` record the admission tree.
- `primary-inputs.json` is the 358-path production/test/tool/platformio preservation subset.
- `layout/` holds baseline/candidate translation units, private headers, exact commands and measured symbols.
  Offset symbols use offset + 1. Candidate headers are evidence only, not production includes.
- `b408-proof.cpp` fills the actual Node E2E ring using the existing test friend and exercises both producers.
- `control-ack-proof.cpp` drives the actual local parser/controller/codec with a recording carrier.
- `observation-audit.json` is a static source/fence audit, not an event-delivery execution proof.
- `logs/` includes final results and labelled failed fixture-development attempts.
- `preservation.json` records the final shared-tree audit. `archive-sha256.json` hashes this archive's artifacts
  except itself; neither manifest purports to be a production freeze.

The archived Python drivers are the exact session drivers. They read the isolated work directory from
`/tmp/mr-s8b-r2-active`; its original value was `/tmp/mr-s8b-r2-ksqkjdbw`. They do not patch production. The
preparation driver creates a complete clone-plus-overlay snapshot of the current checkout and verifies the two
base commits. To reproduce later, first verify the archived source hashes, make a fresh complete snapshot with
that driver, copy the two proof `.cpp` files into the newly named work directory, and build native there with
`pio test -e native`. Run the B408 and control-ACK drivers, then the control-ACK-negative driver; it creates its
own private execute-only copy. Run the layout driver for the private header measurement. Retain the original
archive and direct new results to the new work directory.

Other exact invocations, run from the isolated snapshot:

```text
./.pio/build/native/program
python3 tools/probe_board_abi.py
python3 docs/superpowers/evidence/2026-09-13-radmin-slice7b3-0-reference.py --freeze-check --compare test/test_remote_codec.cpp --selftest
```

The reference run used PyNaCl 1.5.0 from the existing isolated environment at
`/tmp/mr-s8ac-r4-b1edsao2/reference-env/bin/python`. No simulator rebuild or full slice gate was run.
