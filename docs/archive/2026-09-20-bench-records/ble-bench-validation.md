# BLE companion — on-metal bench validation

The Step 1–6+8 BLE companion is code-complete + builds green, but **nothing BLE has run on hardware**.
This is the checklist to validate it on a XIAO nRF52840 + the S140 SoftDevice. Work top-to-bottom; each
step has an **expect** (what you should see) and a **gate** (stop + report if it fails). USB console is
115200 (`pio device monitor` or `python tools/meshroute_client.py monitor`).

The two validations that matter most (unverifiable any other way) are **G (the SD-RNG keystone)** and
**H (the LoRa-timing soak)** — do not skip them.

---

## 0. Prerequisite — is the S140 SoftDevice actually flashed?  [HARD GATE]

The firmware build assumes S140 (the ldscript reserves 0x0..0x27000 for it). The Adafruit/Seeed XIAO
bootloader *bundles* S140 v7.3.0 (`docs/ota.md`), but confirm before trusting it:
- **Double-tap reset** → the `XIAO-SENSE` (or similar) UF2 drive mounts → open `INFO_UF2.TXT`. It should
  name a **SoftDevice: S140** / a `SoftDevice` line. (If it lists none / "no SoftDevice", S140 is absent.)
- Or simply proceed: if Step 3 shows `ble = on (secured...)` **and** Step 4 sees the node advertising,
  S140 is present and working. If boot **hangs/crashes** right after the `ble =` line, or `ble = INIT
  FAILED`, S140 is the prime suspect.

**Gate:** if S140 is not present, flash it first (Adafruit nRF52 bootloader w/ S140, or the OTAFIX fork)
— BLE cannot init without it. Everything below assumes it's there.

---

## 1. Flash + provision a node

```
pio run -e xiao_sx1262 -t upload        # or drag .pio/build/xiao_sx1262/firmware.uf2 onto the UF2 drive
pio device monitor                      # 115200
```
Provision it as a normal mesh node (only needed once; persists in NV):
```
cfg set node_id 1
reboot
cfg set sf_list 7,8,9
```
**Expect:** boot banner shows `node id = 1`, `data sf = [7,8,9]`. (Same as the non-BLE bring-up.)

---

## 2. Enable BLE

```
cfg set ble_mode on        # reboot-to-apply
reboot
```
**Expect** in the boot banner, after the join line:
```
  ble       = on  (secured: MITM passkey pairing — PIN in `cfg`)
```
`cfg` should now show `ble_mode=on ble_period=15 ble_pin=123456`.

**Gate:** if you see `ble = INIT FAILED` → `setPIN` or Bluefruit.begin() failed (suspect S140, §0). If the
board **resets/hangs** at this line → SoftDevice not present or a stack-init fault. Report the last lines.

---

## 3. Verify advertising

Two ways:
- **The new client** (install once: `pip install -r tools/requirements-ble.txt`):
  ```
  python tools/meshroute_client_ble.py scan
  ```
  **Expect:** `MeshRoute-1  rssi=-NN  MeshRoute-1` (it found the node advertising the **NUS service UUID**).
- **nRF Connect** (phone) or `bluetoothctl scan on`: a `MeshRoute-1` device advertising service
  `6E400001-…` (Nordic UART).

**Gate:** no advertising → S140 not running, or `ble_mode` didn't persist (re-check `cfg`).

---

## 4. Connect + pair  [validates Step 6 / §A.3]

Pairing a static-PIN MITM peripheral is an **OS-level ceremony** (the OS shows the PIN dialog) — bleak's
`pair()` can't supply the passkey on every backend. So pair ONCE via the OS, then run the client with
`--no-pair` (the bond persists; the OS reconnects to it automatically):
- **Windows:** Settings → Bluetooth & devices → **Add device → Bluetooth → `MeshRoute-<id>` → enter PIN
  `123456`**. Then `python tools/meshroute_client_ble.py --no-pair monitor`. (bleak `pair()` returns `None`
  here without bonding — the client now prints this recipe + exits if you skip the OS step.)
- **Linux:** `bluetoothctl` → `agent KeyboardDisplay` → `default-agent` → `pair <addr>` (PIN `123456`) →
  `trust <addr>`. Then `--no-pair monitor`.
- **macOS / iOS:** connecting + subscribing TX (`6E400003`) auto-pops the system PIN dialog → enter `123456`.

**Expect on the USB console:**
```
[ble] connected (pairing required before GATT)
[ble] pairing OK
[ble] link secured (paired/bonded)
```
And the client prints `subscribed TXD — link READY`.

**Gate:** `[ble] pairing FAILED` → wrong PIN or an IO-caps mismatch. Connected but no `secured` and the
client's `start_notify` errors → the §A.3 gate is rejecting an unpaired link (that's *correct* — finish
pairing). If GATT works **without** any pairing → the `setPermission` gate isn't taking effect (report).

---

## 5. Round-trip a command + a delivery over BLE  [validates Step 5]

With the client connected (`repl`):
```
whoami
```
**Expect:** `● READY id=1 key=<hex> leaf=0 mode=existing gw=False sf=8` (the `write_ready` JSON).
```
send 2 hello
```
**Expect:** `✓ ack queued ctr=N qd=…` (the `write_ack`). If node 2 is in range + provisioned, then
`✓ ACKED dst=2 ctr=N`. Send *to* node 1 from another node → the client shows `★ RECV from X: …` (the
`write_push` JSON twin of the USB `RECV` line). USB console shows the same events in plain text.

**Gate:** command accepted on USB but no JSON over BLE → the dispatch hook / fan-out path. A `{"err":…}`
for a valid command → check it isn't `line_too_long` / a parse mismatch.

---

## 6. Bond persists across reconnect

Disconnect the client (Ctrl+C), reconnect (`monitor`). **Expect:** it reconnects **without re-entering the
PIN** (bond persisted to InternalFS). The USB shows `[ble] link secured` again (no `pairing OK` — it used
the stored bond).

---

## 7. ⚠ KEYSTONE — `regen` under a live BLE link  [the SD-RNG guard; HARDWARE-ONLY validation]

This is the one that the whole `device_rng.h` / `mrrng::sd_enabled()` design exists to make safe. With a
BLE client **connected** (SoftDevice live), on the USB console:
```
regen
```
**Expect:** it mints a new identity and prints the new `key_hash32` — **no crash, no hang**. The node keeps
running; the client stays connected (or reconnects).

**Gate (CRITICAL):** if the board **HardFaults / resets** on `regen` while BLE is up, the SD-RNG guard isn't
working (the bare-metal `NRF_RNG` path ran under S140). That would mean `mrrng::sd_enabled()` wasn't set or
the SD entropy path is wrong — report immediately; do not ship `ble_mode != off` until fixed.

---

## 8. ⚠ Step 9 — does BLE jitter the LoRa timing?  [the keystone RISK, spec §3]

The open question static analysis couldn't answer: does S140 advertising/connection preempt the app enough
to blow the tuned RTS/CTS/DATA/ACK windows. Two-node A/B:
1. Two nodes (1 + 2), both provisioned, in range. Run a DM exchange loop (e.g. `send 2 …` repeatedly, or a
   scripted burst) and note the delivery / ACK rate with **`ble_mode off`** on both (`cfg set ble_mode off;
   reboot`).
2. Set **`ble_mode on`** on node 1 (or both), **connect a phone** (worst case: active connection), and
   repeat the same DM loop.

**Expect:** delivery/ACK rate with BLE active ≈ the bare-metal rate (within noise). Watch the USB trace for
new RTS-timeout / CTS-miss / retry churn that only appears with BLE up.

**Gate:** if delivery drops materially only when BLE is active/connected → the §3 risk is real. Mitigation
is already designed: `ble_mode off` on relays/infrastructure (they keep the exact bare-metal path), BLE only
on carried/mobile nodes inside their connect windows. Report the numbers — that decides whether `periodic`
windows or a tighter advertising interval are needed.

---

## What to report back

For any gate hit: the **last ~15 USB console lines**, which step, and (for §8) the before/after delivery
numbers. For a clean run: "0–8 all pass" + the §8 A/B rates. That closes out the BLE companion as
hardware-validated and unblocks Phase 3 (inbox sync) with confidence.
