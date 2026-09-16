# P1 simple-action proof

Run `bash tools/probe_deferred_actions/run.sh` (controlled gate) and repeat with
`--no-neg` (positive-only diagnostic). `--out DIRECTORY` retains generated source,
commands, per-variant compilation/run logs, source hashes and results. The default
runs absent, nRF, ESP and ESP-without-power-saving board-function variants. Pins
are derived beside their declarations in `run.py`.

The existing inbox builder compiles the **whole real firmware_commands.cpp and
firmware_inbox.cpp**, with the real core/storage dependencies and existing host
fixtures. The runner uniquely extracts the current board-owned action functions
and OTA startup/stop/accessor definitions. It substitutes reset, delay, fault and
DFU-register hardware primitives. Flush observation forwards to the supplied
sink; a linker wrapper records and calls the **real** Node::clear_learned_state.
The actual hang loop is terminated by a host alarm. These are executed production
functions, not copies of their algorithm.

The whole command TU uses the existing ESP-style fake platform. The four backend
variants apply to the **extracted board functions**, not four complete firmware
builds. Actual gateway and heltec_mobile compilation/stack/link measurements are
separate gates. This instrument does not prove watchdog timing, physical reset,
radio operation, flash durability or real OTA uploads.

The probe retains complete local output and operation transcripts, including
both independent inbox erases, NV failure, warning order, halt, flushes and reset
marks. Typed tests cover caller-owned sinks, combined erase outcomes before a
non-returning reset, frozen debug admission, idempotent OTA entry, startup failure,
reset-hook installation and build support. Controls must compile and fail an
executed assertion; compiler failure, signal death or a missing assertion marker
is an unusable control, never RED. Source-reader controls reject missing/duplicate
owners and exclude comment/string shadows. Production inputs are hashed before
and after each run.

For an independent local-equivalence baseline, provide the three pristine source
files `src/fw_main.cpp`, `src/firmware_commands.cpp`, `src/device_ota.cpp` in a
separate directory and run `--baseline-source DIRECTORY`. Compare all `TRANSCRIPT`
lines byte for byte with the final run, for all four variants. Baseline checks omit
the newly introduced typed API; baseline source hashes and the comparison belong
in the slice receipt. `--backend 0|1|2` is an iteration filter, not the full gate.
