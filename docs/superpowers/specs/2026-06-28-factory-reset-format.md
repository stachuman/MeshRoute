<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# factory_reset → full InternalFS format (recover the corruption-brick class)

**Status:** coder instruction. The user commits + flashes; I gate. Device-only path (no wire change). Small, focused.

## Why
`factory_reset confirm` → `mrnv::factory_erase()` does **targeted `remove()`** of the NV files, explicitly **NOT** `InternalFS.format()` (device_nv.h:189). That cannot recover **FS-metadata corruption** — the exact brick just hit on the bench: `cfg set tx_power -6` → `nv_save_failed`, and it **survived both a reboot and `factory_reset confirm`** (the reboot's `mount_or_repair` only formats on a 1-byte-read-detected corruption — it misses a write-breaking fault; `factory_erase` removes files but never formats). A full `InternalFS.format()` rebuilds the FS metadata → recovers it.

**Verified safe:** `device_ota` does NOT use InternalFS (a format won't touch OTA/DFU state); `handle_factory_reset` already warns on a false return (fw_main.cpp:659).

## The change — `factory_erase()` (the InternalFS / nRF52 branch only)
Replace the five targeted `remove()`s with a full format, preserving the fault log:
```cpp
inline bool factory_erase() {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();
    static mrfault::FaultLog fl;            // STATIC, not stack — keep the console-path frame small (the do_post_ack lesson)
    const bool had = load_faults(fl);       // preserve /mrfault across the format (best-effort)
    const bool ok  = InternalFS.format();   // FULL format: clears FS-metadata corruption that remove() can't.
                                            // ok==false => the flash cannot be formatted => WORN (the real dead-node signal).
    InternalFS.begin();                     // re-mount the clean FS so the load*() at boot run normally
    if (had) save_faults(fl);               // restore the fault history onto the clean FS
    return ok;
}
```
Net file outcome = same as today (cfg/id/peers/inbox-meta gone, `/mrfault` kept) — but achieved via a **format**, so it ALSO clears metadata corruption. The ESP32 branch (`Preferences.clear()`) is already a full namespace wipe — **no change there**.

## /mrfault decision (flagged for review)
- **CHOSEN — B (preserve):** keep `/mrfault` across the format. Honors the existing deliberate keep; the HW fault history is especially worth keeping on a node you suspect is worn. Lost only if the FS is too corrupt to read it (the deep-recovery case) — acceptable.
- **Alternative — A (format-all):** drop `/mrfault` too (simpler, true clean slate). Flip by deleting the `load_faults`/`save_faults` lines.

## Bonus: factory_reset is now also a worn-flash test
`format()` returns false **iff** the flash can't be formatted → `handle_factory_reset` prints `factory_reset WARN`. So on the bench node:
- `factory_reset confirm` → **no warn**, and `cfg set` works after = it was **corruption, recovered**.
- → **WARN** (or `cfg set` still fails) = **worn flash** → retire the node (it can still run in no-persist mode).

## Why NOT auto-heal at boot (deliberately out of scope)
- A boot-time write+read-back probe in `mount_or_repair` would add flash **wear every boot** — ironic for a wear/corruption problem.
- `format()`-on-save-failure would **nuke the identity on a transient fail**.
- So a deliberate, user-invoked `factory_reset` is the right, safe trigger. The boot probe's blind spot (1-byte read → misses write-breaking corruption) is documented and lived-with, recovered manually via this command.

## Tests / gate
- **Native:** `factory_erase()` is a device-only path (native stub returns `true`) — no native test of the format itself. Gate = native suite still **compiles + passes** + all **4 boards build**.
- **★ Device (the real gate):** on the bricked bench node — flash → `factory_reset confirm` → retry `cfg set tx_power -6`.
  - `cfg ok` → corruption fixed (the recovery works).
  - still `nv_save_failed` / `factory_reset WARN` → worn flash confirmed (the node's NV is dead).
- **Regression:** on a healthy node, `factory_reset confirm` then confirm `/mrfault` survived (fault history intact) — verifies choice B and that the format+restore is non-destructive to the diagnostic.
