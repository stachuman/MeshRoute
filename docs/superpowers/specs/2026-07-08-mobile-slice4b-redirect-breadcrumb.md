<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 4b: redirect + breadcrumb (staleness) — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 4b of mobile v1** (design §13; builds on 4a). The user commits; I quality-gate. **Handles a MOVED mobile:** when a mobile re-registers (home H1 → H2), a sender that reaches only the STALE home H1 must not dead-end. 4a's epoch already resolves the overlap when a sender reaches BOTH homes (freshest-wins); 4b covers reaching **only** the stale one — H1 **redirects** to H2. **Best-effort** (§13): if the breadcrumb is lost, the fallback is H1's `_mobile_reg` entry ageing out (TTL) → a cold miss → fresh H-query → H2.

## The mechanism (two small pieces, both on 4a's rails)
1. **Breadcrumb:** on re-register, the mobile sends a tiny control DM to its **old** home: "I moved to H2 (epoch N+1)."
2. **Redirect:** the old home records that against its `_mobile_reg[M]` entry and, on any H-query for M, answers with **H2 + N+1** using 4a's `MOBILE_H_ANSWER` (instead of proxying itself). The sender caches M→H2 (fresher epoch) and retries — no dead-end.

## ★★ Static-safety
- The breadcrumb is a new mobile-only DATA TYPE; the redirect fields live only on `HostMobileEntry` (a host with `_mobile_reg_n==0` never touches them). The redirect answer reuses 4a's `MOBILE_H_ANSWER`. → **s18 byte-identical** (`3ac88d40…`).

## Fix 1 — `DATA_TYPE_MOBILE_BREADCRUMB` (`frame_codec.h`)
Add **`DATA_TYPE_MOBILE_BREADCRUMB = 9`**. Body (2 B): `[new_home_id: u8][new_epoch: u8]`. It rides a normal DM carrying **`SOURCE_HASH = M`** (the mobile's hash) so the old home can attribute it — no new inner codec, the body is the DM payload.

## Fix 2 — the mobile emits the breadcrumb on re-register (`node_mobile.cpp` `mobile_claim_guard_fire`)
`_my_mobile_reg` still holds the OLD home right up until the adopt overwrite (~:64). **Before** that overwrite, if there was a live prior registration, send the breadcrumb to the old home:
```cpp
    if (_my_mobile_reg.active && _my_mobile_reg.home_id != o.responder_id) {   // we actually MOVED
        uint8_t body[2] = { o.responder_id /*new home*/, static_cast<uint8_t>(_my_mobile_reg.epoch) /*new epoch, post-increment*/ };
        // a normal DM to the OLD home, SOURCE_HASH=M so the old home can attribute it; best-effort (no ack/retry)
        enqueue_data(_my_mobile_reg.home_id, body, 2, DATA_FLAG_SOURCE_HASH, "mobile_breadcrumb",
                     /*app_dm=*/false, /*type=*/DATA_TYPE_MOBILE_BREADCRUMB, CryptIntent::plaintext);
    }
    // ...then the existing adopt: _my_mobile_reg = { true, o.responder_id, ... }
```
(The outbound routes via the NEW home — `issue_send next=home_id` — then across the mesh to the old home. `origin=home_id` stamp applies as for any mobile outbound.)

## Fix 3 — the old home records the redirect (`node.h` + `node_mac_rx.cpp`)
- **`HostMobileEntry`** (node.h) — add `uint8_t redirect_home_id = 0;` (0 = not redirected) and `uint8_t redirect_epoch = 0;`.
- **`do_post_ack`** (node_mac_rx.cpp) — new case, gated on `_mobile_reg_n>0`:
  ```cpp
  if (pa.type == DATA_TYPE_MOBILE_BREADCRUMB && ui && ui->has_source_hash) {
      for (uint8_t i = 0; i < _active->_mobile_reg_n; ++i)
          if (_active->_mobile_reg[i].key_hash32 == ui->source_hash) {   // attribute: only M can move M
              _active->_mobile_reg[i].redirect_home_id = ui->body[0];
              _active->_mobile_reg[i].redirect_epoch   = ui->body[1];
              MR_EMIT("mobile_redirect_recorded", EF_I("m", i), EF_I("to", ui->body[0]));
              break;
          }
      become_free(); return;   // consumed (not delivered/inbox'd)
  }
  ```
  (No match → drop; a breadcrumb whose `source_hash` isn't a hosted mobile is spoofed/irrelevant.)

## Fix 4 — the proxy answers the redirect (`node_hashlocate.cpp` the 4a proxy hook, ~:451)
When the `_mobile_reg[M]` scan hits, honor a redirect:
```cpp
    if (_active->_mobile_reg[i].key_hash32 == h.key_hash32) {
        if (_active->_mobile_reg[i].redirect_home_id != 0) {          // §4b: we're stale — redirect to the new home
            node_id      = _active->_mobile_reg[i].redirect_home_id;
            mobile_epoch = _active->_mobile_reg[i].redirect_epoch;
        } else {                                                       // §4a: we ARE the home
            node_id      = _node_id;
            mobile_epoch = _active->_mobile_reg[i].epoch;
        }
        mobile_proxy = true; authoritative = false;
        break;
    }
```
So a stale home emits `MOBILE_H_ANSWER (M → new_home, new_epoch)` — 4a's freshest-wins does the rest at the sender.

## Tests
- **Breadcrumb emit:** a mobile re-registering H1→H2 (with a prior active reg) → a `DATA_TYPE_MOBILE_BREADCRUMB` DM to H1, `SOURCE_HASH==M`, body `[H2, epoch]`. First registration (no prior) → **no** breadcrumb.
- **Old home records:** feeding H1 that breadcrumb (SOURCE_HASH=M matching `_mobile_reg[M]`) → `redirect_home_id==H2`, `redirect_epoch` set. A breadcrumb with a non-hosted `source_hash` → ignored.
- **Redirect answer:** H1 (redirect set) fed an H-query for M → emits `MOBILE_H_ANSWER (M → H2, new_epoch)`, NOT M→H1.
- **★ Static regression:** a non-host node never allocates/reads the redirect fields; `do_post_ack` for existing types unchanged; s18.

## Gate
- `pio test -e native` green (emit + record + redirect-answer + static regression).
- **s18 fresh-md5 byte-identical** (`3ac88d40…`).
- **Sim (extend s21 or a 2-home variant):** a mobile registered at H1, then re-registered at H2, with a sender that reaches H1 first → the DM ends up delivered via H2 (the redirect), not dead-ended at H1. (A focused move-scenario; best-effort so assert eventual delivery.)
- 4 boards compile.

## Sites
`frame_codec.h`(`DATA_TYPE_MOBILE_BREADCRUMB=9`) · `node_mobile.cpp`(`mobile_claim_guard_fire` breadcrumb before adopt) · `node.h`(`HostMobileEntry.redirect_home_id`/`redirect_epoch`) · `node_mac_rx.cpp`(do_post_ack BREADCRUMB case) · `node_hashlocate.cpp`(proxy hook honors redirect ~:451) · tests. **Depends on 4a (`MOBILE_H_ANSWER` + epoch). Best-effort — TTL+re-query is the fallback.**
