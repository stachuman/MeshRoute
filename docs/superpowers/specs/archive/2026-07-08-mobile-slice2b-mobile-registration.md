<!-- Author: Stanislaw Kozicki <cgpsmapper@gmail.com> -->

# Mobile v1 — Slice 2b: mobile-side registration driver — DIRECTLY IMPLEMENTABLE

**Status:** coder instruction (2026-07-08). **Slice 2b of mobile v1** (design `2026-07-07-mobile-node-handling-assumptions.md` §13/§17; builds on 2a). The user commits; I quality-gate. This is the **MOBILE side** — the FSM that discovers a host, claims a local-id, and adopts it. 2a (host side) is done + gated. **Origin-stamp + outbound delivery = Slice 3 (not here).**

## ★★ NON-NEGOTIABLE: the static mesh must be UNAFFECTED
This whole slice is **mobile-only** — every new path is entered only when `_cfg.is_mobile`:
- The registration **driver** (a new timer + `mobile_reg_fire()`) is armed **only if `_cfg.is_mobile`** (a static node never arms it).
- The **OFFER RX** branch in `handle_j` runs only for `(_cfg.is_mobile && j.opcode==OFFER && j.is_mobile)` — a static node hits the existing "OFFER deferred → ignore" fall-through unchanged.
- **No static path touched** — `join_start_claim`/`join_choose_candidate_id`/`join_adopt`/`set_identity`/the DAD timers are reused as-is or via mobile-only wrappers; the static DAD FSM is not modified.
- **Gate:** native suite green + **s18 fresh-md5 byte-identical** (currently `3ac88d40e00d2605ff66659f696d52bf`, 253088/0 — re-establish pre-slice; s18 has no mobiles → the driver never arms → must be identical).

## Design decisions
1. **Single-PHY MVP.** The mobile DISCOVERs on its **configured PHY** (`_cfg.routing_sf`/freq/bw). Multi-PHY scan-roaming (retune across a scan-list) is **deferred** — the common case is one PHY, and the retune loop is complex. `_mobile_scan_cfg` is NOT added this slice.
2. **Adopt reuses `set_identity`.** On a stood CLAIM the mobile calls `set_identity(local_id, _key_hash32)` (node.cpp:38) exactly like `join_adopt` (node_join.cpp:183) — its `_node_id` becomes the **host-assigned local-id**, and `schedule_triggered_beacon()` announces it. Then it fills `_my_mobile_reg`.
3. **Claim-stands (from 2a).** The mobile adopts after the guard window unless DENY'd — no positive confirm. A lost CLAIM self-heals via the periodic re-CLAIM.
4. **FSM mirrors the static DAD** (listen→claim→guard→adopt) with a mobile front-end (DISCOVER→collect-OFFERs→pick).

## Fix 1 — mobile state (`node.h`)
Add to the `Node` class (identity-level, single-layer — a mobile has one attachment):
```cpp
struct MyMobileReg {
    bool     active = false;            // registered to a host?
    uint8_t  home_id = 0;              // the host's node_id (our registrar / home_id)
    uint8_t  my_local_id = 0;         // our host-assigned local-id (== _node_id once adopted)
    uint32_t home_key_hash32 = 0;     // stable home identity (for home-lost / redirect later)
    uint8_t  home_leaf_id = 0;        // the leaf we registered on
    uint16_t epoch = 0;               // §17 registration epoch (mobile-incremented per (re)register)
    uint64_t last_heard_home_ms = 0;  // last BCN heard from home_id (home-lost timeout)
};
MyMobileReg _my_mobile_reg;
// small OFFER collection during a DISCOVER window:
struct OfferCand { uint8_t responder_id; uint32_t responder_hash; uint8_t proposed_local_id; float snr_db; };
OfferCand _mobile_offers[protocol::cap_mobile_offers];   // e.g. 8
uint8_t   _mobile_offers_n = 0;
uint8_t   mobile_home_id() const { return _my_mobile_reg.active ? _my_mobile_reg.home_id : 0; }  // test/diag accessor
```
**`protocol_constants.h`:** `cap_mobile_offers = 8`; `mobile_discover_backoff_min_ms = 5000`, `_max_ms = 120000` (exp backoff, B3); `mobile_offer_window_ms = 2000` (collect window, ≈ B4); `mobile_home_lost_ms = 90000` (no BCN from home → re-register); `mobile_reclaim_ms = 600000` (10-min refresh — self-heal + TTL, well under the host's 24 h).

**`node.h` timer ids** (base 1..73 used): add `kMobileDiscoverTimerId = 74`, `kMobileClaimGuardTimerId = 75`.

## Fix 2 — the driver FSM (`node.cpp` on_timer + a new `node_mobile.cpp` or in `node_join.cpp`)
**Arm on boot** — where the node starts its timers (near where `kBeaconTimerId` is armed), **only for a mobile**:
```cpp
    if (_cfg.is_mobile) (void)_hal.after(0, kMobileDiscoverTimerId);   // kick the registration FSM
```
**`on_timer` dispatch** (node.cpp:695) — add cases:
```cpp
    case kMobileDiscoverTimerId:  mobile_discover_fire();    break;
    case kMobileClaimGuardTimerId: mobile_claim_guard_fire(); break;
```
**`mobile_discover_fire()`** — DISCOVER + open the collect window:
```cpp
void Node::mobile_discover_fire() {
    if (!_cfg.is_mobile) return;                                  // hard guard
    if (_my_mobile_reg.active &&
        (_hal.millis() - _my_mobile_reg.last_heard_home_ms) < protocol::mobile_home_lost_ms) {
        (void)_hal.after(protocol::mobile_reclaim_ms, kMobileDiscoverTimerId);  // still homed → just refresh later
        return;
    }
    // (home lost or never registered) → re-enter discovery
    if (_my_mobile_reg.active) mobile_reset_registration("home_lost");
    _mobile_offers_n = 0;
    j_discover_in d{}; d.leaf_id = _cfg.leaf_id; d.gateway_capable = false; d.is_mobile = true; d.key_hash32 = _key_hash32;
    uint8_t buf[6]; const size_t n = pack_j_discover(d, std::span<uint8_t>(buf, sizeof buf));
    if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    (void)_hal.after(protocol::mobile_offer_window_ms, kMobileClaimGuardTimerId);   // collect, then decide
}
```
**`mobile_claim_guard_fire()`** — pick the strongest OFFER + CLAIM; or backoff:
```cpp
void Node::mobile_claim_guard_fire() {
    if (!_cfg.is_mobile || _my_mobile_reg.active) return;
    if (_mobile_offers_n == 0) {                                  // no host → exp backoff re-DISCOVER
        _mobile_backoff_ms = _mobile_backoff_ms ? min(2*_mobile_backoff_ms, protocol::mobile_discover_backoff_max_ms)
                                                : protocol::mobile_discover_backoff_min_ms;
        (void)_hal.after(_mobile_backoff_ms, kMobileDiscoverTimerId);
        return;
    }
    _mobile_backoff_ms = 0;
    uint8_t best = 0; for (uint8_t i = 1; i < _mobile_offers_n; ++i)
        if (_mobile_offers[i].snr_db > _mobile_offers[best].snr_db) best = i;
    const OfferCand& o = _mobile_offers[best];
    // CLAIM the offered local-id (is_mobile); reuse the CLAIM emit shape from join_start_claim (node_join.cpp:130)
    j_claim_in c{}; c.leaf_id=_cfg.leaf_id; c.gateway_capable=false; c.is_mobile=true; c.key_hash32=_key_hash32;
    c.proposed_node_id = o.proposed_local_id; c.claim_epoch = ++_my_mobile_reg.epoch; c.nonce = /* rng nonce */;
    uint8_t buf[11]; const size_t n = pack_j_claim(c, std::span<uint8_t>(buf, sizeof buf));
    if (n) tx_initiating(buf, n, static_cast<int16_t>(_cfg.routing_sf), LbtKind::flood, 0);
    // claim-stands: adopt after a short guard (no DENY-listen complexity for v1 — the host recorded us on the CLAIM)
    set_identity(o.proposed_local_id, _key_hash32);              // _node_id := local-id (like join_adopt)
    _joined = true;
    _my_mobile_reg = { true, o.responder_id, o.proposed_local_id, o.responder_hash, _cfg.leaf_id,
                       _my_mobile_reg.epoch, _hal.millis() };
    schedule_triggered_beacon();
    (void)_hal.after(protocol::mobile_reclaim_ms, kMobileDiscoverTimerId);   // periodic re-CLAIM (self-heal + refresh)
}
```
*(`mobile_reset_registration()` sets `_my_mobile_reg.active=false` + `set_identity(protocol::unjoined_node_id,_key_hash32)` — reuse `reset_join_for_reprovision` node_join.cpp:314 semantics, mobile-gated.)*

**Home-lost detection:** in the beacon-RX path, when a BCN from `_my_mobile_reg.home_id` (matching `home_key_hash32`) is heard, stamp `_my_mobile_reg.last_heard_home_ms = now`. (The timer checks the timeout.)

## Fix 3 — OFFER RX (`node_join.cpp` `handle_j`, the deferred line ~289)
Replace the "OFFER: deferred (mobile side)" fall-through with a mobile-only collector:
```cpp
    if (j.opcode == static_cast<uint8_t>(j_opcode::offer)) {
        if (!_cfg.is_mobile || !j.is_mobile || _my_mobile_reg.active) return;   // only an unregistered mobile collects
        if (_mobile_offers_n < protocol::cap_mobile_offers)
            _mobile_offers[_mobile_offers_n++] = { j.responder_node_id, j.responder_key_hash32,
                                                   j.proposed_mobile_id, meta.snr_db };
        return;
    }
```
(`meta.snr_db` is on `RxMeta`, hal.h:38 — already passed to `handle_j`. The leaf-exemption from 2a doesn't apply to OFFER; but an OFFER arrives on the mobile's own PHY/leaf, so the existing leaf gate is fine — the mobile set `leaf_id` from... it hasn't adopted yet, so **relax the leaf gate for a mobile receiving an OFFER too**: extend 2a's `mobile_discover` exemption to `(is_mobile && (DISCOVER || OFFER)) && _cfg.is_mobile`-context — verify the gate lets the OFFER through to this branch.)

## ★ Filtering-correctness checklist (verify, don't assume)
- The driver arms **only** under `_cfg.is_mobile` — confirm a static node never enters `mobile_*_fire`.
- The OFFER branch returns for a static node (the `!_cfg.is_mobile` guard) → the existing ignore-path is preserved.
- **⚠ Mobile beacon caching:** once adopted, the mobile beacons with `src = local-id` + `is_mobile` + its hash. **Verify a static hearer does NOT install a normal `id_bind`/route from a mobile's beacon** that could collide a global id (the `is_mobile` beacon bit + existing mobile-exclusions should already gate this — CONFIRM in `node_beacon.cpp` RX and flag if not; if it does cache, that's a real static-safety bug to fix here).
- s18 byte-identical (no mobiles → driver never arms).

## Tests
**Native (`test/test_node_r3.cpp` or a `test_mobile.cpp`):**
- A `_cfg.is_mobile` node: drive `mobile_discover_fire()` → a DISCOVER frame is emitted (is_mobile).
- Feed it two OFFERs via `handle_j` (different `snr_db`, different `proposed_mobile_id`) → `_mobile_offers_n==2`.
- Drive `mobile_claim_guard_fire()` → a CLAIM is emitted with the **stronger** offer's `proposed_mobile_id`; then `_my_mobile_reg.active==true`, `_node_id == that local-id`, `mobile_home_id() == the stronger offer's responder`.
- No OFFERs → `mobile_claim_guard_fire()` emits nothing + re-arms (backoff) — assert no adopt.
- **★ Static:** a non-mobile node never arms the timer (drive on_timer with kMobileDiscoverTimerId on a static node → no-op) and its `handle_j` OFFER path is the ignore-fall-through.

**Sim integration (the real proof):** run **s07** (`lus -e meshroute simulation/s07_seattle_mobile_meshroute.json`) → the 3 mobiles **register with hosts** (observable: `_mobile_reg` populated on hosts / the mobiles adopt local-ids — emit a `mobile_registered` event to assert). Runs clean, no crash.

## Gate
- `pio test -e native` green (incl. the new mobile-FSM cases + the static no-arm case) — via `./.pio/build/native/program`.
- **s18 fresh-md5 byte-identical** (`3ac88d40…`; re-establish pre-slice) — the static tripwire.
- s07 runs clean + mobiles register.
- 4 boards compile.

## Sites
`node.h`(`Node`: `MyMobileReg _my_mobile_reg`, `_mobile_offers[]`/`_n`, `_mobile_backoff_ms`, accessors; timer ids `kMobileDiscoverTimerId`/`kMobileClaimGuardTimerId`) · `protocol_constants.h`(the 6 constants) · `node.cpp`(`on_timer` cases ~695; arm-on-boot for a mobile) · new `node_mobile.cpp` (or in `node_join.cpp`): `mobile_discover_fire`/`mobile_claim_guard_fire`/`mobile_reset_registration` · `node_join.cpp`(`handle_j` OFFER branch ~289; verify leaf gate lets a mobile OFFER through) · `node_beacon.cpp`(home-lost `last_heard_home_ms` stamp + verify no mobile-beacon id_bind caching) · tests. **NO origin-stamp / MAC / routing change this slice (that's Slice 3).**
