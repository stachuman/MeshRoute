# Mobile-home attachment — real-equipment test guide

This is the focused, operator-facing metal test for mobile discovery, attachment, home liveness and re-homing.
It complements the master checklist in `docs/2026-07-31-bench-test-script.md`; it does not replace unrelated radio,
OLED, inbox, team or gateway tests from that document.

## 1. When to run which tests

| Stage | Required firmware state | Run |
|---|---|---|
| A — first metal gate | B177 landed and passed code QG | MH-00 through MH-08 |
| B — proactive roaming | B178's refined trigger has landed and passed code QG | MH-09 in addition to Stage A |
| C — long-duration confidence | Release-candidate firmware | MH-10 and the optional default-duration repeats |

Do not run these tests against a tree while B177 is still being edited. Record the exact build revision for every
device. The current unicast RTS format is a flag-day format, so one old node can make an otherwise-correct topology
fail silently.

Current policy before B178 lands:

- a weak but still-answering home is retained;
- a missed home check may start a search and recovery;
- merely hearing a stronger home must not cause a proactive switch.

That limitation is intentional for Stage A. Do not report it as a B177 failure.

## 2. Equipment and topology

Minimum:

- one mobile, `M`;
- two ordinary, single-layer static nodes capable of hosting mobiles, `H1` and `H2`;
- three USB serial terminals with logging enabled;
- a repeatable way to attenuate a link: distance, shielding, attenuators, or reduced TX power.

Recommended:

- two to four additional mobiles for the simultaneous-start test;
- one passive observer, `O`, with `debug on`;
- independent power switches for `H1`, `H2`, and `M`;
- a clock visible in every log.

Logical arrangement:

```text
                    optional observer O
                           |
             H1  --------  M  --------  H2
              \________ same PHY / static leaf ________/
```

`H1` and `H2` must be normal single-layer nodes. A gateway is deliberately ineligible to host a mobile because it
time-multiplexes its radio between layers.

Use legal frequency, power, bandwidth, coding-rate and duty-cycle settings for the test location. Prefer low TX
power and physical separation/attenuation over transmitting at unnecessary power.

## 3. Build, flash and connect

### 3.1 Record the source state

From the repository root:

```bash
git rev-parse --short HEAD
git status --short
```

Prefer a clean tree. If testing a dirty build, archive the exact diff with the logs and label the run `DIRTY`; a
revision alone does not identify that firmware.

### 3.2 Build the environment matching each role

Heltec V3:

```bash
pio run -e heltec_v3
pio run -e heltec_mobile
```

XIAO nRF52840 + Wio-SX1262:

```bash
pio run -e xiao_sx1262
pio run -e xiao_mobile
```

Use the ordinary environment for `H1`, `H2`, and `O`; use the `*_mobile` environment for `M`. The mobile profile
removes remote-management code but the role still has to be configured with `cfg set mobile 1`.

### 3.3 Upload examples

Heltec, Linux:

```bash
pio run -e heltec_mobile -t upload --upload-port /dev/ttyUSB0
```

Heltec, Windows:

```powershell
pio run -e heltec_mobile -t upload --upload-port COM7
```

For XIAO, either use the configured PlatformIO upload method or double-tap RESET and copy the generated UF2 from:

```text
.pio/build/xiao_mobile/firmware.uf2
.pio/build/xiao_sx1262/firmware.uf2
```

### 3.4 Open consoles

The serial speed is 115200 baud:

```bash
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Enable decoded radio traces during the test:

```text
debug on
```

Use the terminal application's capture/logging feature. Do not open two serial monitors on the same port.

## 4. Test record

Fill this once per firmware build.

| Role | Board/env | Port | `version` | `whoami` id/hash | TX power | Notes |
|---|---|---|---|---|---|---|
| H1 |  |  |  |  |  |  |
| H2 |  |  |  |  |  |  |
| M |  |  |  |  |  |  |
| O |  |  |  |  |  |  |

Test configuration:

```text
Date/time UTC:
Operator:
Layer/full id:
Leaf nibble:
Frequency MHz:
Routing SF:
Data SF list:
Bandwidth kHz:
Coding rate:
Firmware tree clean: yes / no
Attached diff/log bundle:
```

For every failure, preserve all node logs from at least 30 seconds before the first unexpected event until 30
seconds after it. Record the command exactly as typed; do not summarize it from memory.

## 5. Common provisioning

If an existing static test network is already known-good, use it and record its settings. Otherwise this is an
example setup; substitute the legal local frequency as needed.

On `H1`:

```text
cfg set mobile 0
create layer=5 freq=869.0 bw=125 sf=7 sf_list=6,7 duty=1 name="MH metal"
cfg set host_mobiles on
debug on
```

On `H2`:

```text
cfg set mobile 0
join layer=5 freq=869.0 bw=125 sf=7
cfg set host_mobiles on
debug on
```

`host_mobiles` is live-only. Reapply `cfg set host_mobiles on` after every host reboot, even though the default is
normally on.

On `M`, starting from a known mobile configuration:

```text
cfg set mobile 1
cfg set mobile_autoregister 0
```

Then reboot `M` once. This avoids testing a live role transition whose state cleanup is not the subject of this
guide. After reboot:

```text
debug on
mobile unregister
mobile status
```

Expected baseline:

```text
"registered":false
"attachment":"dormant"
"home_link":"unknown"
"home_desired":false
```

The word `connected` must not appear in this status surface.

Before each scenario, record:

```text
H1: version, whoami, cfg, status
H2: version, whoami, cfg, status
M : version, whoami, cfg, mobile status
```

## 6. Stage A — run after B177 passes code QG

### MH-00 — build and topology sanity

Goal: eliminate mixed firmware, wrong PHY and ineligible-home errors before interpreting registration behavior.

- [ ] `version` reports the same revision/build family on every node.
- [ ] `cfg` shows identical frequency, routing SF, bandwidth, coding rate and leaf on `H1`, `H2`, and `M`.
- [ ] `H1` and `H2` report `mobile=0`, `gateway=0`, and one layer.
- [ ] `M` reports `mobile=1`.
- [ ] `whoami` hashes are unique unless a deliberate identity-copy test is being run.
- [ ] With all three in range, each static node receives the other static node's beacons.

If a gateway build is available, use it as a negative control: `cfg set host_mobiles on` must be refused and a
mobile must never list that gateway as its confirmed home. Do not use the gateway for the remaining scenarios.

Result: `PASS / FAIL / BLOCKED`

Notes/log names:

### MH-01 — manual first attachment and confirmation

Goal: prove that an explicit request survives the complete DISCOVER → OFFER → CLAIM → roster-confirmation sequence.

Setup: `H1` on and in strong range; `H2` off; `M` dormant with autoregistration off.

1. On `M`:

   ```text
   mobile register freq=869.0 sf=7 bw=125
   ```

2. Poll `mobile status` every one or two seconds until attachment completes.
3. On `H1`, run `status` after completion.

Pass:

- [ ] `M` enters `seeking` and/or `claiming` before `attached`.
- [ ] While `attachment` is `claiming`, `registered` remains `false`.
- [ ] Final state is `registered:true`, `attachment:"attached"`, `home_link:"confirmed"`.
- [ ] `home_confirm_age_ms` is present after confirmation and grows between polls.
- [ ] `H1 status` shows exactly one `DIRECT` row with `M`'s hash.
- [ ] The row's `local=` equals `M`'s reported local id.
- [ ] With `debug on`, `M` prints `mobile ATTACHMENT CONFIRMED by the home roster`.
- [ ] No surface says `connected`.

Data-plane control:

- [ ] Send a plaintext DM from `H1` to `M` by hash; it arrives once.
- [ ] Send a plaintext DM from `M` to `H1`; it arrives once and is acknowledged.

Fail immediately if `registered:true` appears while still `claiming`, the home has no direct row, or attachment is
reported without a confirming roster.

Result: `PASS / FAIL / BLOCKED`

Attach time from command to confirmation: ______ s

Notes/log names:

### MH-02 — the original real-world order: mobile boots before any home

Goal: directly test the failure that motivated the mobile-home work. No manual `mobile register` is allowed after
the home appears.

Preparation on `M`:

```text
mobile unregister
cfg set mobile_autoregister 1
```

Power off both homes, then reboot `M`.

1. Wait until `mobile status` shows `attachment:"seeking"` and a non-zero `retry_window_ms` has appeared at least
   once.
2. Power on `H1`, apply `cfg set host_mobiles on`, and touch no command on `M`.
3. Allow up to three minutes. The exponential retry window can be as large as 120 seconds, then the OFFER/CLAIM/
   confirmation exchange still needs time to finish.

Pass:

- [ ] `M` continues retrying while no home exists; it does not silently become dormant.
- [ ] After `H1` appears, `M` attaches without an operator command.
- [ ] `H1` has one matching direct hosted row.
- [ ] The post-attachment DM control from MH-01 passes.

Repeat matrix:

| Boot order | Repetitions | Passes | Longest attach time | Notes |
|---|---:|---:|---:|---|
| M first, H1 later | 3 |  |  |  |
| H1 first, M later | 1 |  |  |  |
| H1 and M together | 1 |  |  |  |

Acceptance: all five runs attach. One failure is reportable; do not average it away.

Result: `PASS / FAIL / BLOCKED`

Notes/log names:

### MH-03 — several mobiles start together

Goal: exercise startup jitter, the host's pending-OFFER ring and pending-id reservations on real RF.

Setup: one strong host `H1`; two mobiles minimum, four preferred. Each mobile has a unique identity,
`mobile_autoregister=1`, and the same PHY. Power the mobiles from one switched supply or reset them as closely
together as practical.

Pass:

- [ ] Every mobile eventually reaches confirmed `attached` without a manual retry.
- [ ] `H1 status` contains one `DIRECT` row per mobile.
- [ ] Every row has a different local id.
- [ ] No mobile remains indefinitely in `claiming`.
- [ ] A DM by hash reaches each mobile once.

| Mobile | Hash | Assigned local id | Attach time | Result |
|---|---|---:|---:|---|
| M1 |  |  |  |  |
| M2 |  |  |  |  |
| M3 |  |  |  |  |
| M4 |  |  |  |  |

Result: `PASS / FAIL / BLOCKED`

Notes/log names:

### MH-04 — explicit unregister is local and remains dormant

Goal: distinguish the persisted boot policy from the current volatile attachment request.

1. Start with `M` attached and `mobile_autoregister=1`.
2. On `M`, run:

   ```text
   mobile unregister
   mobile status
   ```

3. Watch `M`, `H1`, and `O` for 20 minutes.

Pass:

- [ ] Immediate state is `registered:false`, `attachment:"dormant"`, `home_link:"unknown"`,
      `home_desired:false`.
- [ ] The unregister command itself emits no J or P frame.
- [ ] No later DISCOVER or P probe is emitted by the registration FSM during the 20-minute observation.
- [ ] Ordinary periodic beacons from a still-addressed team mobile, if any, are not mistaken for an FSM restart.
- [ ] A later explicit `mobile register` attaches normally.
- [ ] A reboot with persisted autoregistration on starts automatic registration again.

Result: `PASS / FAIL / BLOCKED`

Notes/log names:

### MH-05 — healthy-home checks do not create a search storm

Goal: verify the conservative Stage-A policy with a second audible home.

1. Attach `M` to `H1` while `H2` is powered off.
2. Keep the attachment running for at least five minutes.
3. Power on `H2` at a stronger signal than `H1`; apply `cfg set host_mobiles on`.
4. Observe for at least two normal presence periods without dropping `H1`.

Pass:

- [ ] `M` remains attached to `H1`.
- [ ] `mobile status` may increase `candidates`, but a beacon alone must not be treated as verified authority.
- [ ] `H2` does not begin producing probe-triggered rosters for `M` while `H1` keeps answering.
- [ ] There is no repeated canvass burst and no unsolicited re-home.

Important observability limit: `mobile status` currently exposes neither the home quality tier nor the next-probe
countdown. Do not claim that this case proved a particular tier or exact cadence from that JSON alone. In a quiet
test, `debug on` traces show P traffic as `?` frames; use their timestamps and reported SNR. Presence tiers use these
boundaries: critical below -12 dB, weak from -12 to below -4 dB, ok from -4 to below +4 dB, strong at +4 dB or above.
Both directions matter.

Result: `PASS / FAIL / PARTIAL / BLOCKED`

Observed H1↔M SNRs and P-frame intervals:

Notes/log names:

### MH-06 — missed home checks cause loss detection and recovery

Goal: verify movement/outage handling without relying on proactive B178 behavior.

Setup: `M` attached to `H1`; `H2` has been audible for at least 60 seconds and the attachment is at least five
minutes old. Confirm DMs work before disturbing RF.

1. Power off or completely shield `H1` immediately after a confirmed presence exchange.
2. Leave `H2` on and reachable.
3. Poll `mobile status` every 10 seconds on a weak/ok link, or every minute if the last link was strong.
4. Do not type `mobile register`.

Pass:

- [ ] `home_link` moves through `checking` and then `lost`, rather than remaining permanently confirmed.
- [ ] `attachment` moves to `recovering` during the recovery cycle.
- [ ] A missed selected check is followed by search/recovery traffic; `H2` answers and `M` attaches to it.
- [ ] Final `home` is `H2`, with a fresh confirmation age.
- [ ] `H2 status` shows `M` as `DIRECT`.
- [ ] A DM by hash reaches `M` after the switch, and a DM from `M` reaches the static network.

For a previously strong link, the accepted worst idle-loss timing is approximately 495 seconds plus jitter, bounded
by about 527 seconds. A much faster strong-link declaration means the cadence changed; never declaring loss means
the liveness mechanism failed.

Result: `PASS / FAIL / BLOCKED`

Time from H1 loss to `checking`: ______ s

Time from H1 loss to new confirmed home: ______ s

Notes/log names:

### MH-07 — weak but answering home remains selected before B178

Goal: pin the current deliberate limitation separately from total home loss.

1. Attach `M` to `H1` with `H2` off. Wait at least five minutes.
2. Turn `H2` on at a strong signal.
3. Attenuate `H1` until P/roster traces in both directions are around the weak range (-12 to below -4 dB), but
   verify that `H1` continues answering and `home_confirm_age_ms` is regularly reset by confirmations.
4. Observe for at least three weak-tier periods (budget at least four minutes including jitter).

Stage-A pass:

- [ ] `M` stays attached to `H1` while `H1` answers.
- [ ] `H2` is only a passive candidate; it does not receive a stream of search requests.
- [ ] No re-home occurs merely because `H2` is stronger.
- [ ] When `H1` is then made completely unavailable, MH-06 recovery to `H2` succeeds.

This result becomes the negative control for MH-09 after B178. It is not the final desired roaming policy.

Result: `PASS / FAIL / PARTIAL / BLOCKED`

Notes/log names:

### MH-08 — host-row lifetime and B177 regression

Goal: verify that a hosted row is not immortal and that one-way beacon evidence is not mistaken for registration
authority.

The ordinary firmware's 25-minute boundary is valid but slow. For the first metal run, use a clearly labelled
time-compressed HOST build with `protocol::mobile_liveness_ms` changed from 1,500,000 ms to 120,000 ms. Build and save
that host binary separately. After this scenario, restore the default, rebuild, and reflash every shortened host.
Never use the shortened binary in range or battery tests.

Part A — silent mobile:

1. Attach `M` to shortened `H1`; confirm one direct row.
2. Power `M` completely off.
3. Run `status` on `H1` every 30 seconds.

Pass:

- [ ] The row age grows and the row is physically absent after the two-minute boundary.
- [ ] A newly registering, different mobile can reuse the released highest local id.
- [ ] No roster advertises the expired hash.

Part B — B177 beacon authority:

The standard console cannot independently stop P probes while preserving an attached mobile's beacons. Therefore a
normal two-board run cannot isolate the B177 branch: seeing the row stay fresh could be caused by a valid P probe.
Use one of these evidence levels:

- **Full metal evidence:** a temporary instrumented mobile build keeps the attachment/identity and 5-second beacons
  but suppresses P-probe transmission. On `M`, set `cfg set beacon_ms 5000` and verify it is live before starting
  the isolation interval. `H1` must visibly receive several `BCN` frames from `M`, while the hosted-row age continues
  to grow and the row expires at 120 seconds.
- **Standard-build partial evidence:** run Part A and rely on B177's mutation-proven native cases for the isolated
  beacon-vs-probe distinction. Mark this part `PARTIAL`, not `PASS`.

Do not use `mobile unregister` as the B177 isolation step: a non-team mobile drops its host-assigned identity and may
stop beaconing, making row expiry vacuous.

Result A: `PASS / FAIL / BLOCKED`

Result B: `PASS / PARTIAL / FAIL / BLOCKED`

Default firmware restored and reflashed: `[ ]`

Notes/log names:

## 7. Stage B — only after B178's refined proactive trigger lands

### MH-09 — weak home plus a genuinely eligible better candidate

Goal: prove proactive re-home without recreating the fleet-wide roster storm that caused B178's deferral.

Do not run this as a positive test before B178 lands; MH-07 is the correct expectation until then.

1. Attach `M` to `H1` with `H2` off. Wait beyond the five-minute anti-flap dwell.
2. Power on `H2` and let it remain passively observable beyond the 60-second candidate hold.
3. Arrange bidirectional RF so `H1` is weak but still answering and `H2` is at least two quality tiers better.
4. Observe P traffic and `mobile status`.

Pass:

- [ ] The mobile does not canvass immediately on first hearing `H2`.
- [ ] At most one bounded search is initiated once the refined eligibility conditions are all satisfied.
- [ ] `verified_candidates` rises only after bidirectional evidence, not on `H2`'s beacon alone.
- [ ] `M` changes home to `H2` without first declaring `H1` lost.
- [ ] `H1` records a bounded redirect breadcrumb or removes the stale direct row as specified by the landed design.
- [ ] DMs by hash work before and after the switch.
- [ ] No repeated roster/search storm occurs after the switch.

Result: `PASS / FAIL / PARTIAL / BLOCKED`

Notes/log names:

## 8. Stage C — release-candidate endurance

### MH-10 — default-duration row expiry

Repeat MH-08 Part A with the unmodified 1,500,000 ms (25-minute) constant. This validates real wall-clock behavior
without a bench-only build.

- [ ] Mobile is powered off, not merely out of application contact.
- [ ] Host row remains present before 25 minutes.
- [ ] Host row is absent at or just after the expiry sweep following 25 minutes.
- [ ] The local id can be assigned to a different mobile afterward.
- [ ] Default firmware hashes/revisions are recorded.

Result: `PASS / FAIL / BLOCKED`

Notes/log names:

## 9. Completion summary

| Test | Result | Firmware revision | Log/archive | Bug opened |
|---|---|---|---|---|
| MH-00 |  |  |  |  |
| MH-01 |  |  |  |  |
| MH-02 |  |  |  |  |
| MH-03 |  |  |  |  |
| MH-04 |  |  |  |  |
| MH-05 |  |  |  |  |
| MH-06 |  |  |  |  |
| MH-07 |  |  |  |  |
| MH-08A |  |  |  |  |
| MH-08B |  |  |  |  |
| MH-09 |  |  |  |  |
| MH-10 |  |  |  |  |

Stage-A release decision:

```text
PASS / FAIL / CONDITIONAL
Blocking failures:
Accepted partial evidence:
Follow-up bugs:
Default firmware restored on every device: yes / no
```

## 10. Failure-report minimum

A useful report contains:

1. the scenario id and exact failed checkbox;
2. `version`, `cfg`, `whoami`, and `mobile status` from the relevant nodes;
3. host `status`, including every hosted-mobile row;
4. complete timestamped logs from all participating nodes;
5. physical topology, distance/attenuation and TX power;
6. whether the failure repeats after a clean power cycle;
7. whether typing `mobile register` changes the result — record it only after preserving the autonomous failure.

Never repair the setup first and report only the repaired run. The pre-repair logs are normally the evidence that
distinguishes a lost transmission, a false attachment, a stale host row and a selection-policy error.
