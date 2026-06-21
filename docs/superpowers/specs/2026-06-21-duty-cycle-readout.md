<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# `duty` — duty-cycle consumption readout (console + companion)

**Status:** READY FOR CODER. Small. A query command + one Node accessor + a companion JSON field. I quality-gate; author commits.

Operators (and the iOS companion) need to see how much of the duty-cycle budget is spent — a simple **0–100 %** where **100 % = the node must stay silent**, plus **how long until it can transmit again**.

## 1. The model (already in the code — just surface it)
`node_mac.cpp:duty_over_budget()` already computes everything:
- **budget** = `_duty_cycle_budget_ms` = `duty_cycle × duty_cycle_window_ms` (e.g. 0.10 × 3 600 000 = 360 000 ms). `0` = duty disabled (no limit).
- **used** = `_hal.airtime_used_ms(_cfg.duty_cycle_window_ms)` — rolling TX airtime in the window.
- **recovery** = `_hal.oldest_tx_end_ms() + window − now` — when the oldest TX ages out of the window and frees airtime (the same `wait_ms` `duty_over_budget` returns); fall back to the full window if no oldest.

## 2. Node accessor — `duty_status()` (`node.h`, host-testable)
```cpp
struct DutyStatus { uint8_t pct; uint32_t avail_ms; bool enabled; };
DutyStatus duty_status() const;
```
- `enabled = (_duty_cycle_budget_ms != 0)`. If disabled → `{0, 0, false}` (no limit).
- `used = _hal.airtime_used_ms(_cfg.duty_cycle_window_ms);`
- `pct = used >= budget ? 100 : (uint8_t)(used * 100 / budget);` (current consumption; do NOT add a hypothetical frame — this is "where am I now", not `duty_over_budget`'s pre-check).
- `avail_ms`: `0` when `pct < 100` (can TX now); when `pct >= 100`, `= oldest_tx_end_ms() + window − now` (clamp ≥ 0; full window if no oldest). This is "when SOME availability returns" — good enough for the simple form.

Pure accessor — no state change. Uses the HAL methods `duty_over_budget` already relies on, so it works on device AND sim, and a fake-HAL native test can drive it.

## 3. Console command — `duty` (fw_main-direct, like `whoami`/`status`)
```
> duty: 42%                                    (enabled, headroom)
> duty: 100% — SILENT, ~73 s to availability   (over budget)
> duty: disabled (no duty limit)               (duty_cycle <= 0)
```
Read-only; no args. Add a `[help] diag:` mention (next to `status`).

## 4. Companion JSON (the iOS ask)
The companion needs it two ways — pick both, they're cheap:
- **On demand (the companion's primary fetch):** the app sends the `duty` line over the BLE command channel (RXD, like any console line); the node replies on TXD with one JSON object — `{"ev":"duty","pct":42,"avail_ms":0,"enabled":true}` — written via `console_json` (mirror how the other query/`ready` writers serialize). `avail_ms` = ms until availability (0 = now); `enabled=false` = no limit (show "—"/"unlimited"). The app polls `duty` while a silent-countdown banner is on screen.
- **In the `ready` snapshot:** add `"duty_pct":42` (and `"duty_avail_ms":0`) so the app shows it on connect; it refreshes by sending `duty` (or polling it). The app uses 100 % → a "node silent, ~Ns" banner; the `avail_ms` drives a countdown.

Document it in `ios-companion/INBOX_SYNC_CONTRACT.md` (a short "duty readout" entry + the `ready` fields).

## 5. Tests + gate
- **Native unit** (fake HAL): `airtime_used_ms` 0 → `pct 0, enabled true`; used = ½ budget → `pct 50, avail 0`; used ≥ budget → `pct 100, avail_ms > 0` (from `oldest_tx_end_ms`); `duty_cycle = 0` → `enabled false, pct 0`.
- **JSON unit** (`test_console_json`): `duty` → `{"ev":"duty","pct":…,"avail_ms":…,"enabled":…}`; `ready` carries `duty_pct`.
- Full native suite green; 1 board builds; `help` shows `duty`.

## 6. Build order
1. `duty_status()` in `node.h` (§2) + the native unit.
2. `duty` console command in `fw_main` (§3) + `help` line.
3. `console_json`: the `duty` JSON line + `duty_pct`/`duty_avail_ms` in the `ready` writer (§4) + the JSON unit.
4. Update `INBOX_SYNC_CONTRACT.md`.
5. Hand back green-shaped + uncommitted → I gate (native + 1 board + JSON unit).
