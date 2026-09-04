<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->
# Remote-admin v2 Slice 0b — Quality-Agent pre-check ledger (2026-09-04): B279, `regen` and the supplied sink

Authority: design §19 Slice 0b; register row B279 (close shape quoted there). Verified at `HEAD 0ee0f9b`.

## 0. What 0b is, in one line

`regen` is the last dispatcher-reachable verb that writes to the GLOBAL console instead of the supplied `Print&`:
pass the sink through `do_regen` and `print_identity`, byte-identical text on USB, the same text on BLE through its
own `LineSink`, no cross-sink leak, one mutation restoring the global sink. `src/`-only ⇒ corpus-inert; a BLE
observable ⇒ bench part owed.

## 1. Source facts

| fact | anchor |
| --- | --- |
| the arm: `dispatch()` matches `regen` and calls parameterless `do_regen()` — `out` is discarded | `src/firmware_commands.cpp:1216` |
| `do_regen()`: mints a seed, saves `/mrid`, re-derives, re-installs the crypto identity, prints `> regen err nv_save_failed` or `> regen ok` + `print_identity(idb)` — every print to `mrcon` | `:654-665` |
| `print_identity(const mrnv::IdBlob&)`: `  key_hash32= 0x<8 hex>` + optional `  name="…"` + newline, all to `mrcon`; callers: `do_regen` and `setup()` (boot banner) — NOT `status` (the comment "Shared by boot, status, regen" is drifted: V1, fix it) | `:639-649`, `src/fw_main.cpp:876` |
| the BLE path: `parse_command` → `unknown_verb` → `LineSink ls(ble_sink); if (dispatch(line, len, ls)) { ls.flush(); return 0; }` ⇒ `regen` matches, the sink stays EMPTY, the text leaks to USB | `src/fw_main.cpp:591-593`; `LineSink` 1700 B `src/dispatch_sink.h:63` |
| the only other `mrcon.` writers in the file are `peer_store_restore()` (boot, `:109-113`) — not dispatcher-reachable; `firmware_config.cpp`/`firmware_inbox.cpp`/`firmware_remote.cpp` have ZERO direct `mrcon.` writes | `grep -c 'mrcon\.'` = 12 in `firmware_commands.cpp`, 0 elsewhere |
| the existing wiring gates that drive `dispatch()` with a real `Print&`: `tools/probe_inbox_verbs/run.sh` (39 checks / 8 controls), `tools/probe_console_sink/run.sh` (the guarded console + BLE `LineSink` idiom) | those runners |

## 2. Shape the brief must pin

- `do_regen(Print& out)` and `print_identity(const IdBlob&, Print& out)`; `setup()` passes `mrcon` explicitly; the
  dispatcher passes `out`. Identity/NV behaviour byte-identical (same seed source `mrrng::fill`, same `save_id`,
  same `set_identity`/`set_crypto_identity` order).
- USB: `> regen ok  key_hash32= 0x…  name="…"` byte-identical to today. BLE: the SAME bytes arrive through the
  `LineSink` (line-flushed), and NOTHING reaches `mrcon`.
- Failure arm: `> regen err nv_save_failed` through the supplied sink.
- Controls: a mutation restoring `mrcon` in `do_regen` (USB still passes, BLE goes empty ⇒ RED at the BLE arm);
  a mutation restoring `mrcon` in `print_identity` (the boot banner still passes ⇒ the BLE arm must catch it);
  no-cross-sink control (a capture on `mrcon` stays empty during a BLE `regen`).
- Fix the drifted comment "Shared by boot, status, regen" (V1).

## 3. Gate and fence

- Fence: `src/firmware_commands.{h,cpp}` (the two signatures + the arm), `src/fw_main.cpp` (`setup()`'s explicit
  `mrcon` argument only), the probe arms (`tools/probe_inbox_verbs/` or `tools/probe_console_sink/` — whichever
  drives `dispatch()` with a fake `/mrid` store; the brief names it), evidence. ⛔ No `lib/`, no NV format.
- Corpus 36/36 by construction; boards flash ±small, RAM +0; census; probes; sweep; checkers; the inventory:
  `regen`'s row does not change (same verb, same function) — but `firmware_commands.cpp` line numbers move ⇒
  regenerate with `--write`.
- Bench: **Part 59** (BLE + USB): over BLE-NUS `regen` returns the `> regen ok  key_hash32= 0x…` line to the app and
  prints nothing on USB; over USB it prints exactly as before; a reboot shows the new hash in the banner.

## 4. Owner decision

None.
