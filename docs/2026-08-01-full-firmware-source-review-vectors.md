# Full firmware source review — vectors and method

**Date:** 2026-08-01  
**Status:** Proposed review framework  
**Purpose:** Define the independent review vectors, evidence, and acceptance questions for a future review of the complete MeshRoute firmware source tree. This document is a checklist and review plan, not a record of findings.

## 1. Review objective

The review should answer four questions for every firmware subsystem:

1. Can malformed input, corrupt persistent state, resource exhaustion, or unexpected hardware state make the device unsafe, unavailable, or silently incorrect?
2. After power loss, reset, timeout, interrupted I/O, or a peripheral failure, does the device recover into a known and observable state?
3. Are security, protocol, and product invariants preserved across all conversions, queues, retries, persistence operations, and build variants?
4. Is each important claim supported by a host test, simulator test, build gate, static check, or explicit on-device test?

The review must follow complete lifecycles rather than assess functions in isolation. Examples include:

- persisted value: parse -> validate -> load -> apply -> modify -> save -> reload;
- radio packet: IRQ -> read -> decode -> authenticate -> route/deliver -> acknowledge -> re-arm RX;
- message: command input -> internal carrier -> queue -> transmit/retry -> completion -> inbox/UI output;
- failure: detection -> capture -> reset/recovery -> persistent record -> operator-visible diagnosis.

## 2. Repository review map

| Layer | Principal scope | Review emphasis |
|---|---|---|
| Device integration | `src/fw_main.cpp`, `src/device_*`, `src/firmware_*`, `src/fw_context.h` | boot, persistence, fault handling, transports, OTA, board-specific behavior |
| Hardware abstraction | `lib/hal/` | radio lifecycle, timers, IRQs, power, airtime accounting, hardware contracts |
| Protocol core | `lib/core/` | codecs, state machines, routing, delivery, cryptography, bounded resource use |
| Console and external interfaces | `lib/console/`, USB/BLE/remote command paths | validation, authorization, framing, output correctness, backpressure |
| Board/build definitions | `platformio.ini`, board/variant files, feature macros | pin and capability accuracy, build parity, resource budgets |
| Verification | `test/`, `sim/`, `tools/`, `.github/workflows/` | invariant coverage, regression gates, hardware-test gaps |
| Vendored code | vendored MeshCore/Monocypher and other third-party sources | version, integration boundary, configuration, license, known-risk exposure; avoid local edits unless explicitly justified |

## 3. Severity and evidence model

### 3.1 Severity

| Level | Meaning |
|---|---|
| **P0 — critical** | Plausible remote compromise, secret disclosure, regulatory violation, bricking, unrecoverable persistent corruption, or a product claim that can be falsely reported as successful. |
| **P1 — high** | Repeatable crash/hang/reboot loop, substantial silent data loss, broken recovery, cross-tenant/team leakage, major radio outage, or failure common enough to affect field operation. |
| **P2 — medium** | Bounded functional defect, degraded operation, incomplete validation, important observability gap, or missing regression coverage for a credible failure. |
| **P3 — low** | Maintainability, documentation, low-probability robustness, or test-quality issue without a demonstrated material failure. |

Severity is based on impact and reachability, not on patch size.

### 3.2 Required structure of a finding

Every reported finding should contain:

- a stable identifier and review vector;
- exact file and line evidence;
- the violated invariant;
- a concrete trigger or failure sequence;
- user/device impact and severity rationale;
- affected boards, roles, and build features;
- proposed solution, including compatibility or migration concerns;
- the regression test or bench procedure that would close it;
- confidence level and any remaining assumption.

“Potential issue” without a reachable failure path should be recorded as a question or test gap, not promoted to a defect.

## 4. Main review vectors

The vectors are intentionally overlapping. A defect may be filed under one primary vector and cross-referenced from others.

### V01. Boot, reset, and lifecycle control

**Core question:** Does every reset cause lead through a deterministic initialization sequence into either normal operation or an explicit degraded state?

Review:

- static construction and `setup()` ordering;
- use of peripherals, logging, flash, timers, radio, and watchdog before initialization;
- reset-cause capture and clearing;
- expected versus unexpected reset classification;
- watchdog start/feed placement and boot operations outside watchdog coverage;
- boot-loop behavior after corrupt configuration, failed storage mount, radio-init failure, or failed migration;
- behavior of halted, maintenance, OTA, factory-reset, and unprovisioned states;
- readiness point: when the firmware may declare itself healthy or confirm an update.

Evidence should include cold boot, warm reboot, watchdog reset, hard fault/panic, brownout where supported, and peripheral-init failure tests.

### V02. Flash, NVS, filesystem, and persistent-state management

**Core question:** Can any interrupted write, corrupt byte, stale schema, full store, or excessive write rate cause a soft brick, silent misconfiguration, or premature flash wear?

Required invariants:

- every persisted object has an explicit magic/schema/version/length validity rule;
- all ranges and cross-field relationships are validated before values become indexes, sizes, timeouts, divisors, radio parameters, or state-machine inputs;
- power loss at any write boundary leaves either the old valid state or the new valid state;
- an invalid object falls back to a defined safe state and produces an observable diagnostic;
- writes are change-detected, rate-limited, and bounded; erase/write amplification is known;
- migration is explicit and testable, with no accidental partial interpretation of old layouts;
- mount/repair/reformat behavior distinguishes recoverable data from disposable data;
- sequence counters, nonce/counter leases, boot counters, and cursors cannot roll back into unsafe reuse;
- factory reset has precise scope and cannot leave mutually inconsistent records.

Inspect configuration, identity/key material, fault log, inbox records and metadata, counters/leases, provisioning state, and any external QSPI store separately. Produce a persistent-record inventory with owner, medium, size, validity rule, write frequency, failure behavior, and recovery path.

### V03. General fault handling, watchdogs, and recovery

**Core question:** Are faults caught without making the original failure worse, and is enough trustworthy evidence retained to diagnose the next boot?

Review:

- hard-fault/panic/watchdog handlers for allocation, locking, flash access, logging, or other unsafe work;
- retained-memory validation before it is trusted;
- stack-frame and reset-register capture correctness per architecture;
- fault-log metadata validation before indexing its ring;
- nested/repeated fault behavior and fault during fault-log persistence;
- watchdog ownership, timeout adequacy, task coverage, and feeds that may mask a stuck subsystem;
- bounded recovery from TX timeout, radio error, storage error, queue saturation, and transport failure;
- safe-mode or boot-loop escape policy where persistent input repeatedly crashes startup;
- operator visibility through local and remote diagnostics without disclosing secrets.

Fault injection should cover deliberate hang, illegal access/panic, corrupt retained scratch, corrupt fault metadata, failed fault-log write, and repeated crash-on-boot.

### V04. Board definitions and hardware management

**Core question:** Does each supported build describe and operate only the hardware actually present on that board?

Review:

- pin maps, boot-strapping pins, polarity, pull state, interrupt capability, and pins shared with flash/display/GNSS/USB;
- radio type and control seam, SPI bus ownership, chip-select/reset/busy/DIO wiring, and optional RF front-end control;
- power rail enable/hold sequencing and peripheral reset timing;
- ADC reference/divider assumptions, battery measurement, charging state, and board power policy;
- display, buttons, LEDs, BLE/USB, GNSS, QSPI, and other optional peripheral presence;
- behavior when an optional peripheral is absent or fails initialization;
- compile-time capability flags versus runtime probing;
- differences between board revision, role, and feature selection;
- shutdown/reboot/sleep state of output pins and peripherals.

For every board environment, produce a capability-and-pin table tied to authoritative board documentation and an on-device smoke-test list.

### V05. Radio/PHY lifecycle and RF correctness

**Core question:** Can the radio always return to a known receive-ready state after success, timeout, interrupt races, configuration changes, and driver errors?

Review:

- initialization, RX arm/re-arm, TX start/completion/abort, CAD/LBT, timeout, and recovery transitions;
- DIO/IRQ flag ownership, stale edges, missed edges, shared interrupt causes, and ISR-to-main synchronization;
- error return handling for every radio-driver operation;
- reconfiguration of frequency, spreading factor, bandwidth, coding rate, preamble, sync word, and output power;
- live versus reboot-to-apply settings and whether logs show the actual operating point;
- TX power semantics at the chip/board/antenna boundary, including any FEM gain/control;
- packet-length limits and buffer agreement between driver, codec, and protocol;
- receive while sleeping, wake latency, and radio state across CPU sleep;
- radio-driver object lifetime and evidence of memory corruption around driver state;
- RF test needs: sensitivity, output power, harmonics/spurs, antenna path, and coexistence.

The review should model the radio as a state machine and verify every exit path, not only the happy path.

### V06. Power, sleep, thermal, and energy behavior

**Core question:** Is entry into and recovery from every power state safe, and is energy use bounded under normal and failure conditions?

Review:

- sleep eligibility and pending-work checks;
- wake sources, wake polarity, stale wake flags, deadline conversion, and timer wraparound;
- radio, console, BLE, display, GNSS, and storage behavior before and after sleep;
- watchdog behavior during sleep;
- prevention of sleep during TX, flash write, OTA, or critical protocol transitions;
- busy loops, retry storms, permanently asserted interrupts, and other high-current failure modes;
- battery voltage interpretation and low-voltage/brownout policy;
- thermal and duty constraints for the radio and charging hardware;
- current measurements for boot, idle RX, sleep, TX, BLE, display, and failure recovery.

### V07. Time, timers, concurrency, interrupts, and tasking

**Core question:** Are shared state and deadlines correct across ISR/main/task boundaries and counter wraparound?

Review:

- ISR-safe operations and interrupt acknowledgement ordering;
- whether `volatile` is being used where atomicity or a critical section is actually required;
- read-modify-write races, lost flags, torn multiword values, and stale snapshots;
- ownership of radio, storage, console, and protocol objects across tasks;
- timer identifier uniqueness, cancellation, re-arm semantics, and callback reentrancy;
- 32-bit `millis()` wrap, 32/64-bit conversion, signed/unsigned deadline comparisons, and overflow in multiplication;
- long blocking calls and watchdog starvation;
- ESP32 task affinity/stack assumptions and nRF52 interrupt-priority/SoftDevice constraints;
- ordering when multiple events become ready in the same service pass.

Tests should deliberately place events at wrap boundaries and interleave IRQ, timeout, RX, and TX-complete events.

### V08. Memory safety and static resource budgets

**Core question:** Are all memory accesses bounded, and do stack/RAM/heap demands remain safe on the smallest supported target and worst execution path?

Review:

- array indexes, lengths, offsets, alignment, casts, packed structs, and arithmetic before buffer operations;
- fixed-capacity container full/empty behavior;
- large automatic objects, nested call depth, callback depth, recursion, and fault-handler stack use;
- global/static RAM inventory and linker-reported headroom per hardware environment;
- task stack allocation and measured high-water marks;
- dynamic allocation, `String` use, fragmentation, allocation failure, and allocation from hot/ISR paths;
- lifetime of borrowed pointers, spans/views, references into queues, and storage returned from decoders;
- zeroization/initialization of padding and structures sent, persisted, compared, or hashed;
- stack canaries and sanitizer/static-analysis coverage where host builds allow it.

The result should include flash/RAM size deltas and explicit budgets, not just a successful link.

### V09. Resource exhaustion, queues, and backpressure

**Core question:** Does the firmware degrade predictably when every bounded resource is full or unavailable?

Review:

- TX/RX queues, retry tables, route tables, dedup caches, inbox rings, timer slots, BLE/USB buffers, and remote-response slots;
- admission and eviction policy, priority inversion, starvation, and fairness;
- whether rejected work receives an accurate terminal result;
- retry amplification, synchronized retry storms, and attacker-controlled resource occupation;
- saturation counters and visibility of drops;
- recovery after pressure subsides;
- behavior when storage is full or a transport never drains.

Every fixed-capacity structure should have tests for capacity minus one, exact capacity, capacity plus one, repeated overflow, and recovery.

### V10. Wire codecs and untrusted-input validation

**Core question:** Can arbitrary bytes from RF, BLE, USB, storage, or OTA reach internal logic without complete structural and semantic validation?

Review:

- minimum/maximum lengths before every read or copy;
- version/type/enum/range validation and handling of unknown extensions;
- integer overflow in length and offset computation;
- canonical encoding, endianness, signedness, and packed-layout assumptions;
- separation of decode, authentication, and state mutation;
- malformed nested records and inconsistent declared/actual lengths;
- duplicate, replayed, truncated, and trailing data;
- fuzz targets for codecs, command parsers, JSON/binary framing, and persistent blobs.

A parser returning failure is insufficient if it has already mutated state or read out of bounds.

### V11. Internal data carriers and state-transition integrity

**Core question:** Are all fields and ownership semantics preserved when work moves between protocol phases?

Review:

- conversions among queued, pending, retransmitted, acknowledged, delivered, stored, and UI-visible forms;
- hand-built replacements of structs and aggregate initialization after fields are added;
- correlation identifiers, security intent, nonce/counter state, origin/destination, channel/DM kind, QoS, expiry, and completion context;
- `clear`, reset, reuse, move, and copy behavior;
- single source of truth for conversion helpers;
- switch exhaustiveness for every enum that drives behavior or output.

For each carrier, create a field-flow matrix from creation to terminal completion. Add sentinel-field tests so future field additions fail visibly if a conversion omits them.

### V12. Protocol state machines, routing, delivery, and isolation

**Core question:** Do all protocol paths converge on valid terminal states while preserving routing and isolation invariants?

Review:

- join/DAD/provisioning, beacon/synchronization, routing, channel and DM delivery, retry/ACK, mobile/team/gateway, and remote-command states;
- transition guards, timer ownership, retry limits, cleanup, idempotency, and restart behavior;
- deduplication identity and lifetime;
- TTL/hop/path handling and loop prevention;
- trust changes caused by observations or unauthenticated traffic;
- network, team, channel, role, and direct-message isolation;
- topology churn, stale routes, partition/rejoin, duplicate frames, delayed ACKs, and reboot mid-operation;
- parity between simulator behavior and device-integrated behavior.

State diagrams and transition tables should be checked against code; prose alone is not adequate evidence.

### V13. Cryptography, RNG, keys, counters, and identity

**Core question:** Are cryptographic guarantees preserved by the firmware around the primitive implementations?

Review:

- entropy initialization, health/failure handling, and early-boot RNG use;
- nonce uniqueness and counter persistence across reset, power loss, rollback, and factory reset;
- key generation, storage, import/export, derivation, rotation, deletion, and zeroization;
- domain separation and binding of sender, recipient, channel/team, message type, and protocol version;
- authentication-before-use, replay resistance, downgrade behavior, and failure uniformity;
- public identity collision/confusion handling;
- accidental leakage through logs, commands, crash records, stack, serialization, or display;
- exact integration/configuration of vendored cryptographic code.

Cryptographic primitives should not be re-reviewed as novel algorithms; review their version, configuration, call contracts, and surrounding protocol use.

### V14. Security boundaries and command authorization

**Core question:** Does every externally reachable operation enforce the intended authentication, authorization, freshness, and physical-presence policy?

Review:

- attack-surface inventory for RF frames, remote commands, BLE, USB console, Wi-Fi OTA, buttons, and provisioning;
- read versus mutating commands, and local versus remote privileges;
- pairing, unlock, admin, role, and team authorization state;
- replay/counter handling and reboot persistence;
- dangerous operations: reboot, erase/reset, OTA, reconfiguration, key export/change, crash test, transmit controls;
- denial-of-service cost asymmetry before authentication;
- secret and personal-data exposure through status, inbox, UI, errors, and diagnostics;
- secure default state on unprovisioned and freshly reset devices.

Produce a command/operation matrix containing ingress, required authority, replay defense, rate limit, audit visibility, and side effects.

### V15. Configuration, provisioning, and live reconfiguration

**Core question:** Can the device ever run with a configuration that could not be safely loaded again after reboot?

Review:

- syntax, type, range, enum, and cross-field validation;
- role/network/team/identity compatibility rules;
- defaults, sentinel meanings such as zero, and old-schema behavior;
- transaction ordering between validation, live apply, persistence, and user confirmation;
- rollback if hardware apply or storage write fails;
- values that require reboot versus values applied live;
- provenance of active values and whether status reports compile defaults or effective values;
- provisioning interruption, reprovisioning, and cleanup of stale role/team/routes/keys;
- import/export compatibility and protection of secrets.

No command should report success until the promised durable or live state has actually been achieved.

### V16. Inbox and durable application-data semantics

**Core question:** Does stored application data remain consistent, correctly classified, and recoverable under wrap, corruption, full capacity, and reset?

Review:

- merged inbox semantics and explicit DM/channel kind marking;
- record and metadata validation, sequence/cursor/epoch wrap, and segmentation;
- append atomicity and recovery from torn record or metadata writes;
- ordering, duplicate handling, deletion/eviction, unread state, and pagination/cursor contracts;
- RAM versus persistent backend parity and fallback behavior;
- full-store behavior and surfaced loss;
- flush timing, wear, shutdown/power-loss window, and boot scan duration;
- confidentiality of stored messages and exposure through local/remote commands.

Use a model-based test that compares the implementation to a simple reference inbox across randomized append, read, wrap, reset, corruption, and capacity events.

### V17. USB, BLE, console, remote-command, and serialization paths

**Core question:** Can any management or presentation transport block the mesh loop, corrupt framing, emit invalid output, or behave differently from another transport?

Review:

- bounded input lines/frames and resynchronization after overflow or truncation;
- UTF-8/JSON escaping, binary framing, numeric bounds, and output truncation;
- blocking writes, flushes, connection stalls, and backpressure;
- command parity and intentional differences among USB, BLE, and remote access;
- transport connect/disconnect races and partial messages;
- separation of parsing, authorization, action, and rendering;
- stable machine-readable response contracts and truthful error reporting;
- whether diagnostic output can perturb radio timing or watchdog service.

### V18. OTA, image authenticity, rollback, and recovery

**Core question:** Can only a complete authorized compatible image become bootable, with a reliable recovery route if it fails?

Review:

- image source authentication and authorization of OTA entry;
- signature/hash verification and key ownership;
- declared versus received size, short upload, streaming errors, and finalization semantics;
- board/hardware compatibility and anti-rollback/version policy;
- power loss at every phase and partition/bootloader behavior;
- healthy-image confirmation timing and rollback trigger;
- watchdog, radio, and network state while OTA is active;
- recovery without the normal console and behavior after repeated failed boots;
- whether OTA credentials, access point behavior, and user messages match the threat model.

OTA completeness and OTA authenticity are separate requirements; satisfying one does not satisfy the other.

### V19. Regulatory controls and airtime accounting

**Core question:** Can firmware settings or accounting errors cause transmission outside the permitted frequency, power, or duty constraints?

Review:

- allowed regional frequency/channel plans and validation of user-supplied RF settings;
- effective radiated power assumptions, board gain, antenna gain, and PA/FEM behavior;
- airtime formula, integer width, rounding, overflow, and agreement with actual PHY parameters;
- accounting of retries, acknowledgements, beacons, failed/aborted TX, and all traffic classes;
- persistence/reset behavior of regulatory budgets where applicable;
- LBT/CAD behavior and failure mode;
- fail-closed behavior when region or radio configuration is invalid;
- compile-time defaults for every shipped build.

### V20. Observability, diagnostics, privacy, and supportability

**Core question:** Can a field failure be distinguished and diagnosed without exposing sensitive data or changing timing enough to hide the problem?

Review:

- reset cause, boot sequence, uptime, radio readiness, queue depth, drops, TX timeouts, storage repair, and active configuration provenance;
- monotonic/saturating counter behavior and counter reset semantics;
- persistent versus volatile diagnostics and their write costs;
- bounded log formatting and availability on headless builds;
- redaction of keys, tokens, precise private content, and unnecessary identifiers;
- diagnostic commands under partial subsystem failure;
- debug feature compile/runtime gates and production defaults;
- time correlation among device, simulator, and test logs.

### V21. Feature flags, build matrix, portability, and size budgets

**Core question:** Do all claimed boards and feature combinations compile into coherent products rather than untested collections of stubs?

Review:

- each PlatformIO environment, role, board revision, and material feature combination;
- feature-gated declarations/definitions and inactive stubs;
- board-specific assumptions leaking into core or shared device code;
- compiler/toolchain/platform pinning, warnings, language-standard differences, and undefined behavior;
- generated artifacts and reproducible build inputs;
- flash/RAM/IRAM/partition budgets and regression limits;
- consistency between native tests, simulator, and embedded compilation;
- compile-time enforcement of invalid combinations.

Maintain a build-coverage matrix. “One environment built” is not evidence that a shared change is portable.

### V22. Verification strategy and release gates

**Core question:** Is each critical invariant protected at the cheapest reliable level, with hardware-only claims explicitly separated?

Review coverage should include:

- native unit and property tests for pure logic;
- malformed-input fuzzing for codecs, parsers, and persistent blobs;
- simulator scenarios for protocol transitions, topology, loss, duplication, delay, and reboot;
- embedded builds with warnings treated as errors;
- static analysis and host sanitizers where compatible;
- device fault injection for flash interruption/corruption, watchdog, radio errors, and resource exhaustion;
- on-metal functional tests for IRQs, RF, sleep current, power loss, USB/BLE, flash wear/recovery, and OTA rollback;
- multi-device interoperability and long-duration soak tests;
- explicit mapping from requirement/invariant to test/gate and from past field defect to regression test.

Tests must assert externally meaningful outcomes and terminal states, not merely that a method returned or a frame was emitted.

### V23. Architecture, ownership, dependencies, and maintainability

**Core question:** Is the code structured so that important invariants have one owner and future changes are unlikely to bypass them?

Review:

- dependency direction among device integration, HAL, core, and console;
- global mutable state and ownership in `fw_context`/firmware integration;
- large translation units, header-defined mutable state, and one-definition assumptions;
- duplicate validation, conversion, persistence, rendering, or state-transition logic;
- interfaces that expose representation rather than enforce invariants;
- comments/specifications that describe historical rather than current behavior;
- vendored dependency versions, local patches, licenses, update policy, and security advisories;
- dead code, temporary instrumentation, debug escape hatches, and obsolete compatibility paths;
- reviewability: separating behavior changes from broad mechanical refactors.

Architecture findings should identify the concrete defect class made likely by the structure; style preference alone is not a firmware risk.

## 5. Cross-cutting inventories to build first

Before deep review, create these inventories. They reduce duplicate work and reveal unowned boundaries:

1. **Persistent records:** medium, schema, validity rule, write triggers/rate, atomicity, fallback, migration, owner.
2. **External inputs:** RF frame, USB/BLE command, remote command, OTA body, stored blob, button/pin; maximum length and first validation point.
3. **State machines and timers:** states, events, deadlines, retries, cleanup, terminal outcomes, timer owner.
4. **Bounded resources:** capacity, admission, eviction/drop response, recovery, diagnostic counter.
5. **Shared state:** writers/readers, task/ISR context, synchronization, lifetime.
6. **Boards/features:** pins, peripherals, radio, storage, transport, role, build/test coverage.
7. **Security operations:** ingress, authority, freshness/replay defense, rate limit, side effect, audit trail.
8. **Data carriers:** field flow across creation, conversion, queueing, retry, completion, storage, and presentation.

## 6. Recommended review sequence

Use risk-first passes rather than reviewing directory order:

1. **Recoverability:** V01 boot, V02 persistence, V03 faults, V18 OTA.
2. **Physical device correctness:** V04 hardware, V05 radio, V06 power, V19 regulatory controls.
3. **Runtime integrity:** V07 concurrency/time, V08 memory, V09 resource exhaustion.
4. **Trust boundaries:** V10 untrusted input, V13 crypto/identity, V14 authorization, V15 configuration.
5. **Product semantics:** V11 carriers, V12 protocol, V16 inbox, V17 transports.
6. **Assurance:** V20 diagnostics, V21 build matrix, V22 verification, V23 architecture/dependencies.

Within each pass, start at externally controlled or persistent boundaries and trace inward to state mutation. Review code and tests together.

## 7. Completion criteria for the firmware review

The source review is complete when:

- every vector has a completed checklist, even if the result is “no finding”;
- all cross-cutting inventories exist and have named code owners;
- every P0/P1 claim has either reproducible evidence or is clearly labeled as an unverified hardware risk;
- each finding identifies affected boards/features and a closure test;
- build and test coverage gaps are distinguished from demonstrated implementation defects;
- all required on-device checks are collected into a separate bench plan;
- unresolved assumptions and accepted risks are explicitly recorded;
- no original specifications are silently rewritten to match the implementation.

## 8. Suggested review deliverables

Keep the review outputs separate from this framework and from the original specifications:

- `full-firmware-review-findings.md` — evidence-backed defects and proposals;
- `full-firmware-review-triage.md` — prioritized action list and dependencies;
- `full-firmware-review-bench-plan.md` — hardware-only verification procedures;
- `full-firmware-review-coverage-matrix.md` — vectors, source areas, boards, tests, and review status.

This separation lets the checklist remain stable while findings and implementation plans evolve.
