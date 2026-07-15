<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Command-Sink Consolidation — design

**Status:** design (2026-07-13, brainstorming output). **Prerequisite** for `2026-07-13-remote-management-auth-design.md` (remote-auth becomes a thin adapter once this lands). Pure refactor — no behaviour change.

## 1. Problem — three drifting dispatchers
The console command surface is dispatched **three** times, and they drift:
- `service_console()` — the **serial** (USB) line reader → the `handle_*` functions.
- `ble_dispatch_line(line,out,cap)` — **BLE**, a subset, its own JSON.
- `remote_exec(from,query)` — **remote/`rcmd`**, 8 commands **hand-reimplemented as text** (a `status`/`cfg`/… copy separate from the real ones).

The command *logic* is already factored into `handle_*` (`handle_cfg_set`, `handle_team`, `handle_mobile`, `handle_gateway`, `handle_routes`, `handle_pull_inbox`, …). What's triplicated is the **line→handler dispatch**, and it's blocked from full reuse by **two output conventions**:

| convention | mechanism | sites | examples |
|---|---|---|---|
| **text** | the **global `mrcon`** (a `Print`-derived sink, hardwired to `Serial`) | **377 total in `fw_main.cpp`** — of which the **command-response** subset (the `mrcon` calls *inside* the 23 `handle_*` bodies, ~300) is the migration | `cfg`, `status`, `team`, `mobile`, `gateway`, `create/join`, … |
| **JSON** | a **`JsonSink` param** (`void(*)(const char*,size_t)`) | ~10 | `routes`, `pull_inbox`, `mark_read`, `ready`, `limits` |

> **Scope note (verified 2026-07-13):** the 377 `fw_main` `mrcon` sites are **not** all command-response output. ~56 are **debug/diagnostic**: the boot banner + `"node = up"` (`setup()`, 26 sites) and the async `RECV`/`ACKED`/`FAILED`/`CH` push-surfacing + loop diagnostics (`mesh_service_once`, 30 sites). Those are **not** responses to a dispatched line and stay on the global `mrcon`. Only the **command-response** `mrcon` migrates to the `out` sink (exact per-command tally lands in the plan).
>
> **The response surface is `handle_*` PLUS `dump_*` (verified 2026-07-13).** The verb table lives in **`service_debug(line,len)`** (`fw_main.cpp:1622`, called from `service_console`), and it dispatches to *two* families of response producer: the **`handle_*`** functions (23) **and** a set of **`dump_*` response helpers** — `dump_status` / `dump_routes` / `dump_duty` / `dump_limits` / `dump_help` / `dump_faults` / `print_banner` — that also write to the global `mrcon`. Both families are the migration; both gain the `Print& out` param. (BLE's `ble_dispatch_line` today re-implements `version`/`duty`/`limits`/`whoami` with its *own* JSON writers — that duplication is what collapses once `dispatch(line,len,out)` exists.)

**Debug vs response — the sink rule (user decision, 2026-07-13):** the unified `out` sink carries a command's **RESPONSE only**. **Debug/diagnostic output is USB/serial-only** and is NEVER routed through `out` (never captured for BLE/remote) — it stays on the global `mrcon` (`Serial`, or the `NullPrint` on production). This applies both to the boot/loop/push sites above **and** to any debug/trace line *inside* a handler: a handler writes its answer to `out`, but any diagnostic chatter it emits goes to `mrcon`. Rationale: a captured BLE/remote response must be exactly the command's answer — not interleaved with serial debug spam — and debug over-the-air is out of scope (and a size/security risk).

**Root cause of the drift:** the `JsonSink` handlers are already transport-reusable (BLE/remote pass their own sink), but the **text handlers write their response to the *global* `mrcon`**, which a non-serial transport can't capture — so `remote_exec` reimplements instead of reusing. Those command-response `mrcon` sites (~300 of the 377) are the *only* blocker to "one dispatch, everyone reuses the handlers"; the ~56 debug sites are serial-only by design and never need capture.

## 2. Design — one `Print&` sink, one dispatch (approach A, full unification)
Unify on the abstraction that already half-exists: **a `Print&` output sink passed to every handler.**

- **`Sink = Print&`.** `mrcon` is already `Print`-derived → it becomes simply *the serial sink instance*. A new **`BufferSink : Print`** (appends to a bounded `char[]`) is the capture sink for BLE and remote. The `JsonSink` function-pointer is **replaced** by `Print&` (JSON handlers write their formatter output via `out.write(buf,n)`).
- **Every `handle_*` gains a `Print& out` param** and its **command-response** `mrcon.print/println/printf/write` → `out.…`. This is the ~300-site migration — **mostly mechanical** (`mrcon.` → `out.`, add the param), because the handlers already use the `Print` API on `mrcon`. **NOT mechanical for the two exceptions the plan must handle per-site:** (a) a **debug/trace** line inside a handler stays `mrcon.` (serial-only, per the debug rule) — it does NOT become `out.`; (b) the ~56 boot/loop/push `mrcon` sites are outside any handler and are untouched. So the migration is "convert the response prints, leave the debug prints" — a read-per-handler classification, not a blind sed.
- **One `dispatch(const char* line, size_t len, Print& out)`** — the verb `if/else` extracted from `service_console` — is the single line→handler map. The three transports become **thin adapters** that only pick the sink and ship the bytes:
  - **serial:** `dispatch(line, len, mrcon)` — output goes to `Serial` exactly as today.
  - **BLE:** `BufferSink b; dispatch(line, len, b); mrble::tx_line(b.data, b.len);`
  - **remote:** (in the remote-auth spec) `dispatch(line, len, captureSink)` → seal the buffer back.
- **Deletions:** `remote_exec`'s hand-rolled command block and `ble_dispatch_line`'s reimplemented verbs — replaced by the single `dispatch`. The transport-specific *wrappers* (BLE's JSON envelope, the sealed response) stay as adapters; the *commands* do not.

Output **format is unchanged per command** (text stays text, JSON stays JSON) — this refactor moves *where output goes* (a sink), not *what it says*. Format normalization (e.g. JSON everywhere) is a **separate, later** pass on the now-clean base.

### 2a. Convergence decision (2026-07-13) — resolves the §5↔§6 tension
The same verb emits **different bytes per transport today** (`status`: USB `dump_status` multi-line vs remote's compact `up=…` single line; `duty`: USB text vs BLE `write_duty` structured JSON). So "one dispatch, byte-identical on *all* transports" is self-contradictory for the overlap verbs (`status`/`cfg`/`duty`/`limits`/`version`/`whoami`/`routes`/`prep-restart`) — unifying the dispatch **is** the format change. **User decision:** *we are unifying* (the output is going to change anyway — e.g. `status`+`version` merge later), with these guard-rails:
- **USB is the byte-identical anchor.** `dispatch(line,len,mrcon)` produces the **canonical** output = today's USB text. USB output stays byte-identical (the regression battery, §5).
- **Keep the iOS companion (BLE) contract safe.** The BLE verbs the companion parses as structured JSON (`duty`/`limits`/`routes`/`pull_inbox`/`mark_read`/`ready`/`version`) keep their **exact current JSON** — `ble_dispatch_line` retains those handlers; it does NOT route them through the canonical text `dispatch`. BLE only *gains* a `dispatch` fallback for verbs it doesn't already special-case (additive → contract-safe).
- **Remote (`rcmd`) is don't-care here.** It converges to `dispatch` (its compact per-command text is replaced by the canonical output); the remote command surface is reworked in the remote-auth spec, so its output changing now is fine.
- The **shared JSON handlers** (`handle_routes`/`pull_inbox`/`mark_read`) are JSON on every transport already → they migrate to `Print& out` cleanly and stay byte-identical on USB **and** BLE.

## 3. Two problems this incidentally fixes
1. **`MR_CONSOLE=0` (production) diagnostics.** Today production makes `mrcon` a `NullPrint` (output compiled out), which would have starved a remote-loopback. With a `Print&` param, serial passes the (null) `mrcon` but **BLE/remote pass a `BufferSink` that is independent of `NullPrint`** → over-the-air diagnostics work on production regardless. The earlier `MR_CONSOLE=0` tension dissolves.
2. **Native testability.** A sink-parameterized `dispatch(line, testSink)` can be driven from a **native doctest** — feed a command, assert the captured output. Today's global-`mrcon` console is untestable off-device. This is a real coverage win and the regression backbone for the migration.

## 4. Migration order (for the plan — each step builds + boards-green)
1. Define `BufferSink : Print` (+ confirm `mrcon` satisfies the same `Print&`); introduce the `Print& out` param on the **`JsonSink` handlers first** (they're already sink-shaped — swap the func-ptr for `Print&`).
2. Migrate the text handlers to `Print& out` in **batches by handler** (`handle_cfg_set`, `handle_team`, … one commit each) — convert each handler's **response** `mrcon.` → `out.`, **leave any debug/trace `mrcon.` as-is** (serial-only, per the debug rule); the caller passes `mrcon` so serial output is byte-identical. No behaviour change per batch. (Each batch: read the handler, classify each `mrcon` site response-vs-debug, convert only the responses.)
3. Extract `dispatch(line, len, out)` from `service_console`; point serial at it.
4. Rewire `ble_dispatch_line` → `dispatch(…, BufferSink)`; delete its reimplemented verbs (keep the BLE-specific JSON-envelope wrapping).
5. **Delete `remote_exec`'s command block**; `rcmd` now routes through `dispatch(…, BufferSink)` (the auth/seal wrapper lands with the remote-auth spec).
6. Add native dispatch tests (§3.2).

## 5. Gate
- **★ USB output parity vs the committed reference:** run a fixed **console command battery** over USB before and after — the output must be **byte-identical** to the committed baseline (USB is the canonical anchor, §2a).
- **★ iOS companion contract preserved (BLE):** the companion-parsed BLE JSON verbs (`duty`/`limits`/`routes`/`pull_inbox`/`mark_read`/`ready`/`version`) emit **byte-identical JSON** before/after (their handlers are retained, §2a). Non-companion verbs newly answerable over BLE via the `dispatch` fallback are additive.
- **Remote (`rcmd`) is exempt** — its output converges to the canonical `dispatch` and is reworked in the remote-auth spec (§2a).
- **Native:** the new `dispatch(line, testSink)` tests pass; overall native green.
- **Boards:** all envs build (`MR_PROFILE_GATEWAY` / `MR_PROFILE_MOBILE` / full) — the console lives in `fw_main`/`src`, device-side.
- **s18 byte-identity:** untouched — the consolidation is device-side (`fw_main`/console); the sim/`lus` node logic in `lib/core` routing is not in scope. `md5(s18) == 3ac88d40…` must still hold (the mandatory gate).

## 6. Relationship to remote-auth
Once `dispatch(line, out)` exists, remote management (`2026-07-13-remote-management-auth-design.md`) is a **thin adapter**: `open-a-sealed-DM → authenticate against the pinned admin key → default-deny classify → dispatch(line, captureSink) → seal the buffer back`. The `status`/`routes` cleartext reads are the same `dispatch` with an unauth path. No command is reimplemented ever again.

## 7. Self-review
- **Placeholders:** none — the one deferred item (format normalization) is explicitly a later pass, not a gap here.
- **Consistency:** the `Print&` choice makes the ~300-site response migration mostly mechanical *and* subsumes the `JsonSink` handlers under one abstraction; the transports reduce to sink-selection. Internally coherent. The debug-vs-response split (debug = serial-only `mrcon`, response = `out`) is applied uniformly — boot/loop/push sites and in-handler debug lines both stay on `mrcon`.
- **Scope:** one focused refactor (dispatch unification), sized by the ~300-site command-response migration (of 377 `fw_main` `mrcon` sites; ~56 debug + any in-handler debug stay serial-only); batched by handler so each step is independently reviewable + boards-green. Appropriate for one plan.
- **Verification (2026-07-13):** the three dispatchers, `mrcon`-is-`Print`, the `MR_CONSOLE=0` NullPrint, and `remote_exec`'s 8 hand-rolled commands (`reboot`/`prep-restart`/`status`/`uptime`/`version`/`faults`/`cfg`/`duty`) were confirmed against the code; the original "376" was corrected to the response-only scope + the debug-is-serial-only rule added.
- **Ambiguity:** `Sink = Print&` (not a new bespoke interface) is stated explicitly to avoid a re-invented sink type.
