// MeshRoute — src/firmware_join_service.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §UI-15 slice 1 — ONE TYPED STATIC-JOIN TRANSACTION. Spec:
// docs/superpowers/specs/2026-08-18-ui15-provisioning-implementation-plan.md (v6 — the authoritative version; v5's field list said `uint32_t bw_hz` and was corrected BY this slice's measurement) §2.2, dispatch brief
// docs/superpowers/plans/2026-08-19-ui15-slice1-typed-join.md.
//
// ★★★ WHAT THIS SLICE IS, STATED AS A PROPERTY RATHER THAN AN INTENTION: it is a **REFACTOR (C1)**, and its ONE hard
//     property is that `join` emits the SAME BYTES it emitted before. Nothing here is a behaviour change, a new
//     refusal the operator can reach, or a new durable field. The decision logic MOVED; it did not change.
//
// ★★ WHY IT MOVED AT ALL, and it is the same argument §PROV-TX made for the team half: `src/firmware_config.cpp` is
//    compiled by NEITHER the native suite (`test_build_src = no`) NOR the simulator (which compiles `lib/core` +
//    `lib/console` only), and no corpus scenario ever runs a console verb. ⇒ logic left inside `handle_join` is
//    unreachable by EVERY automated gate. This header is pure — no `Print`, no Arduino, no globals — so
//    `test/test_firmware_join_service.cpp` reaches all of it, and `handle_join` shrinks to parse -> request -> render.
//    §UI-15 slice 6's OLED adapter will fill the SAME `JoinRequest` from a screen; that is the other half of the
//    reason, and it is why the seams below are injected rather than called directly.
//
// ★★ WHY IT IS A **SIBLING** OF `ProvisioningService` AND NOT AN OVERLOAD OF IT (spec §2.2, explicit):
//    `apply_team` is the TEAM vocabulary — `TeamRequest`, `ProvVerdict`, a 12-arm `ProvErr` whose every arm is about
//    membership, keys or the mobile plane. ⛔ NONE of it describes a static join, and pressing `ProvErr` into service
//    here would have made both vocabularies mean less. What IS reused, unchanged, is `ICfgStore` (U1) — one durable
//    seam, one whole-record write, and the same "false = THE WRITE FAILED (nothing may be applied live)" contract.
//
// ★★★ THE ORDER IS THE CONTRACT, and it is the pre-existing order of `handle_join`, not a new one:
//       validate -> load -> compose ONE candidate -> **ONE `store.save`** -> the live seam (retune + re-DAD).
//     A refusal performs 0 loads, 0 writes and 0 live calls; a load failure 0 writes and 0 live calls; a SAVE FAILURE
//     performs ⛔ **ZERO live calls**, so no radio moves and no DAD airtime is spent on a join that was never
//     persisted. ⓘ That last property was ALREADY true of `handle_join` (its `return` sits between the save and
//     `provision_apply_live`) — this file makes it *measurable* rather than merely written down.
// ⛔⛔ AND THE LIMIT OF THAT GUARANTEE, in the same words §PROV-TX had to use: it is **EXACTLY ONE WRITE IS ATTEMPTED
//     AND NOTHING IS APPLIED WHEN IT REPORTS FAILURE**. It is ⛔ NOT a claim that a failed physical write left the
//     stored record byte-intact — a backend can fail after a partial write ([[B193]]) and no host fake can say
//     otherwise. ⇒ nothing here or in its console voice may say "no flash was changed".
#pragma once
#include <cstdint>
#include "device_nv.h"                 // mrnv::Blob — THE durable carrier (never rebuilt field-by-field, U2)
#include "firmware_config_service.h"   // ICfgStore — REUSED UNCHANGED (U1); this file adds no second durable seam
#include "firmware_config_parse.h"     // valid_layer0_id / valid_freq_mhz / valid_bw_khz / valid_routing_sf — THE
                                       // domain predicates, shared with the console parser (⛔ never re-spelled here)
#include "protocol_constants.h"        // protocol::khz_to_hz — the ONE kHz -> Hz conversion (U1/U2)

namespace mrfw {

// ---- the typed REQUEST -----------------------------------------------------------------------------------------
// ★ TYPED, not textual, and NOT a `Print&`: the console PARSES (`kv_next` -> `PhyArgs` -> `phy_args_in_range`) and
// hands VALUES over; §UI-15 slice 6's OLED screen will fill the same struct from a stored profile.
//
// ⛔⛔ `freq_mhz` IS A `double` AND MUST STAY ONE — this is a CORRECTED design decision, not an oversight, and the
//    correction is recorded because the wrong version was specified first (plan v2 -> v3). An integer kHz field is a
//    **REAL RF DEFECT** here: the build's own default carrier is **869.4625 MHz = 869462.5 kHz**, so `uint32_t
//    freq_khz` rounds it to 869462 and **CHANGES THE FREQUENCY THE BENCH ACTUALLY RUNS ON**. It would also have
//    broken this slice's byte-identity claim, since `Blob::freq_mhz` is itself a `double` fed straight from `atof`.
//    ⓘ Only the STORED `/mrjoin` profile of slice 2 is integral; the TRANSIENT request preserves the parser exactly.
//
// ★★ `layer` IS THE **FULL** 1..255 BYTE. The nibble is DERIVED here (`join_leaf_of_layer`) and never carried as a
//    second field — see `blob_put_static_join` for why the two must stay distinguishable.
//
// ⛔⛔ AND `bw_khz` IS A `double` FOR THE SAME REASON `freq_mhz` IS, ONE FIELD OVER — **MEASURED, NOT FORESEEN, AND
//    THE PLAN'S OWN FIELD LIST SAID `uint32_t bw_hz`.** The A/B byte-identity harness built for this slice caught it:
//    with the request carrying pre-converted Hz, `join layer=4 freq=869.525 bw=nan sf=9` went from *joining at
//    `bw_hz = 0`* (what the verb does today) to *refused with the usage line* — because `protocol::khz_to_hz(NaN)`
//    yields 0 and no `uint32_t` predicate can accept it back. `firmware_config_parse.h` carries an EXPLICIT standing
//    instruction about exactly that: every one of these call sites has always ACCEPTED a NaN, and starting to reject
//    one is *"a behaviour change smuggled into a refactor (C1). **Preserve it; close it in a fix slice if the owner
//    wants it.**"* ⇒ the request carries the RAW operator kHz, `validate_join` runs the **SAME** `valid_bw_khz` the
//    console parser runs, and the NaN is preserved bit-for-bit. ⓘ [[JOIN-BW-NAN]] is registered against the defect
//    itself; ⛔ it is NOT fixed here.
// ★ AND THE `khz_to_hz` CONVERSION LIVES IN THE TRANSACTION, not at the caller: one carrier field, one conversion,
//   so a request cannot hold a `bw_khz` and a `bw_hz` that disagree (U2 — the field-drop rot starts with two fields
//   that must be kept in step by hand). `JoinResult::bw_hz` reports what actually landed in the record.
struct JoinRequest {
    uint8_t  layer      = 0;     // the FULL 1..255 layer id (0 = unset, and refused)
    double   freq_mhz   = 0.0;
    double   bw_khz     = 0.0;   // the RAW operator value in kHz — FRACTIONAL (62.5 / 41.67 / 31.25 are real LoRa BWs)
    uint8_t  routing_sf = 0;
};

// ---- explicit outcomes -----------------------------------------------------------------------------------------
// The SYNCHRONOUS verdict, and ⛔ `started` is deliberately not called `joined`: a successful transaction only
// **STARTS DAD**. The real outcome arrives later as a `PushKind::join_adopted`, and correlating it is §UI-15 slice
// 6's problem (spec §2.3) — ⛔ nothing here may be rendered as "JOINED".
enum class JoinVerdict : uint8_t {
    started,     // EXACTLY ONE durable write, then the live retune + re-DAD. ⛔ NOT "joined" — DAD has only begun.
    refused,     // a validation refusal: ZERO loads, ZERO writes, ZERO live calls, ZERO airtime
    nv_failed,   // the ONE save attempt failed: ZERO live calls, ZERO airtime, live radio/config untouched
};
// ⛔ ITS OWN VOCABULARY. `ProvErr` is the TEAM transaction's and must not be pressed into service here (spec §2.2).
// ★ The four DOMAIN arms are distinct although the CONSOLE renders all four with the one shared usage line: the
//   distinction is what slice 6's screen needs (it can say WHICH field the operator got wrong, where a usage line
//   cannot), and keeping them separate now costs nothing.
enum class JoinErr : uint8_t {
    none,
    invalid_layer,     // layer0_id domain 1..255 (0 = unset)
    invalid_freq,      // outside the current board's configured RF envelope
    invalid_bw,        // 7..500 kHz, tested on the RAW operator double (see JoinRequest::bw_khz)
    invalid_sf,        // routing SF 5..12
    nv_load_failed,    // the record could not be read, so the non-provisioning fields cannot be preserved
    nv_save_failed,    // the single write failed
};
inline const char* join_err_name(JoinErr e) {
    switch (e) {
        case JoinErr::none:           return "none";
        case JoinErr::invalid_layer:  return "invalid_layer";
        case JoinErr::invalid_freq:   return "invalid_freq";
        case JoinErr::invalid_bw:     return "invalid_bw";
        case JoinErr::invalid_sf:     return "invalid_sf";
        case JoinErr::nv_load_failed: return "nv_load_failed";
        case JoinErr::nv_save_failed: return "nv_save_failed";
    }
    return "?";     // total function; `-Werror=switch` fires before this can be reached for a valid enumerator
}

// The typed OUTCOME. ★ It reports the two LAYER numbers and the bandwidth AS PERSISTED, so the caller's JSON ack is
// read off what was actually written rather than re-derived from its own parse — one composition authority (U2).
struct JoinResult {
    JoinVerdict verdict = JoinVerdict::refused;
    JoinErr     err     = JoinErr::none;
    uint8_t     layer   = 0;   // the FULL byte, as persisted into `Blob::layer0_id`
    uint8_t     leaf    = 0;   // `layer & 0x0F`, as persisted into `Blob::leaf_id`
    uint32_t    bw_hz   = 0;   // as persisted into `Blob::bw_hz`. ⛔ MEANINGFUL ONLY once the candidate exists, i.e.
                               //   never on a validation or load refusal (0 there, because nothing was composed).
};

// ---- the seams -------------------------------------------------------------------------------------------------
// ★★ THE LIVE SEAM, and it exists because a natively-tested save-before-apply transaction **CANNOT call the
//    device-only `provision_apply_live()`** — that function reaches `g_node`, `g_hal` and the radio, none of which
//    the host build has. ONE operation, `void`, and INFALLIBLE by construction: everything it does (radio retune,
//    NodeConfig copy, routing-state wipe, `CmdKind::join`) either succeeds or is a fire-and-forget live action.
// ⛔ There is deliberately NO second method. A `notify_ui()` or a `fire_dad()` split would let a caller apply half a
//    join; the device implementation delegates to `provision_apply_live(blob, /*do_dad=*/true)` and nothing else.
// ★ THE FAKE PINS THE ORDERING, which is the whole reason the seam is injected: **zero** calls on a validation, load
//   or save failure · **exactly one** after a successful save · and **save-before-live**.
struct IJoinLive {
    virtual ~IJoinLive() = default;
    virtual void apply_and_start(const mrnv::Blob& b) = 0;
};

// ---- shared validation -----------------------------------------------------------------------------------------
// ⛔ NOT A SECOND DEFINITION OF VALIDITY (U1): all four predicates are `firmware_config_parse.h`'s — the SAME four
//    `phy_args_in_range` composes for the console, on the SAME value types. That identity is what makes the console
//    path provably non-diverging rather than merely believed to be: for every input `phy_args_in_range` accepts,
//    these four accept, INCLUDING the deliberately-preserved NaN (see `JoinRequest::bw_khz`).
// ⓘ THE DOMAIN IS THEREFORE SCREENED TWICE ON THE CONSOLE PATH, AND THAT IS LOAD-BEARING RATHER THAN REDUNDANT:
//   `PhyArgs::layer` is a `long`, so the console must reject 256/257/-1 BEFORE narrowing to this struct's `uint8_t`
//   — 257 would otherwise narrow to a perfectly valid 1 and JOIN THE WRONG LAYER. ⛔ Do not delete the console's
//   `phy_args_in_range(pa, /*with_layer=*/true)` clause; this function is the floor under the OLED path (slice 6),
//   which has no `PhyArgs` in front of it at all.
inline JoinErr validate_join(const JoinRequest& req) {
    if (!valid_layer0_id((long)req.layer))       return JoinErr::invalid_layer;
    if (!valid_freq_mhz(req.freq_mhz))           return JoinErr::invalid_freq;
    if (!valid_bw_khz(req.bw_khz))               return JoinErr::invalid_bw;
    if (!valid_routing_sf((long)req.routing_sf)) return JoinErr::invalid_sf;
    return JoinErr::none;
}

// The wire leaf nibble a full layer id maps to (byte-0 frame filter). ONE spelling of `& 0x0F` for the transaction
// and its result, so the candidate and the JSON ack cannot drift apart.
inline uint8_t join_leaf_of_layer(uint8_t layer) { return (uint8_t)(layer & 0x0F); }

// ---- the ONE candidate composition (U2 — ⛔ never rebuild the carrier field-by-field at a second site) ----------
// ★★★ `layer0_id` TAKES THE **FULL BYTE** AND `leaf_id` THE **NIBBLE**, AND ⛔ THIS MUST NOT BE "TIDIED".
//     `cfg set layer0_id` validates 0..255 while `leaf_id` is the 0..15 wire nibble, and the LIVE apply mirrors the
//     NIBBLE into `layers[0].layer_id` (`src/firmware_config.cpp`'s `provision_apply_live`, matching single-layer
//     init at `lib/core/node.cpp:459`). ⇒ a request for layer 17 legitimately PERSISTS 17, LIVES as 1 and PUSHES
//     leaf 1. §UI-15 slice 6's join-completion correlation rule depends on exactly that distinction (spec §2.3.7:
//     persisted↔persisted on the full byte, nibble↔nibble on the push), and collapsing the two here would make OLED
//     join permanently fail on every layer above 15.
// ⛔ THE LEAF NAME BYTES ARE **NOT** ZEROED, ONLY ITS LENGTH — §clean-join, verbatim from the pre-slice code: the
//    field is len-gated everywhere, so zeroing the bytes would be a byte-level change to the persisted record for no
//    behavioural gain. (The byte-identity harness compares the WHOLE `Blob`, so this is measured, not assumed.)
inline void blob_put_static_join(mrnv::Blob& b, const JoinRequest& req) {
    b.freq_mhz = req.freq_mhz; b.bw_hz = meshroute::protocol::khz_to_hz(req.bw_khz); b.routing_sf = req.routing_sf;
    b.leaf_id = join_leaf_of_layer(req.layer); b.layer0_id = req.layer;   // full layer id stored; leaf = layer & 0x0F (byte-0 wire filter)
    b.node_id = 0; b.joined = 0; b.lineage_id = 0; b.config_epoch = 0;    // unprovisioned -> DAD + adopt the leaf's lineage via pull
    b.leaf_name_len = 0;                                                  // §clean-join: don't carry the OLD leaf's name into the new network — present as freshly-joined (config-not-yet-pulled). A managed leaf repopulates via the config pull; an unmanaged one shows blank until `cfg set leaf_name`. (Bytes need not be zeroed — len-gated.)
}

// ---- the service -----------------------------------------------------------------------------------------------
// RAM: two references and nothing else — no cached candidate, no draft. The transaction is a single call, so there
// is no state between calls to go stale.
class JoinService {
  public:
    JoinService(ICfgStore& store, IJoinLive& live) : _store(store), _live(live) {}

    // ★★★ THE TRANSACTION. Five steps, and the order IS the contract:
    //   1. VALIDATE the typed request        -> `refused`,  ⛔ 0 loads, 0 writes, 0 live calls
    //   2. `store.load` the whole record     -> `refused`/`nv_load_failed` on failure, ⛔ 0 writes, 0 live calls
    //   3. compose ONE candidate over it     (the non-provisioning fields are PRESERVED by loading first)
    //   4. EXACTLY ONE `store.save`          -> `nv_failed` on failure, ⛔ 0 live calls, so no retune and no airtime
    //   5. the live seam, on success ONLY    -> retune + membership reset + re-DAD
    // ⓘ `r.layer` / `r.leaf` are filled BEFORE the first refusal so a caller can report what was asked for even on a
    //   refusal; `r.bw_hz` cannot be — it does not exist until step 3 composes the candidate, and inventing it
    //   earlier would put a second `khz_to_hz` in the file.
    JoinResult apply_join(const JoinRequest& req) {
        JoinResult r{};
        r.layer = req.layer; r.leaf = join_leaf_of_layer(req.layer);

        const JoinErr ve = validate_join(req);
        if (ve != JoinErr::none) { r.err = ve; return r; }   // ⛔ REFUSED: 0 loads, 0 writes, 0 live calls

        mrnv::Blob cand{};                                   // value-initialised: a failed load may still have
        if (!_store.load(cand)) {                            // written into it (device_nv.h's §nv-ritual warning)
            r.err = JoinErr::nv_load_failed;
            return r;                                        // ⛔ 0 writes, 0 live calls
        }
        blob_put_static_join(cand, req);
        r.bw_hz = cand.bw_hz;                                // ★ read off the CANDIDATE, ⛔ never re-derived by the
                                                             //   caller: one conversion authority (U2)
        if (!_store.save(cand)) {                            // ★ EXACTLY ONE save ATTEMPT
            r.err     = JoinErr::nv_save_failed;
            r.verdict = JoinVerdict::nv_failed;
            return r;                                        // ⛔ 0 live calls — radio + live config untouched, 0 airtime
            // ⛔⛔ CORRECTED (QG 2026-08-19): this said "and NV untouched". THAT IS AN OVERCLAIM and the same error
            //   §B207 already had to withdraw. What is guaranteed is ONLY: no live apply, no airtime, and no FURTHER
            //   write attempted. A backend that reports failure may have written PARTIALLY — whether the stored record
            //   survives intact is [[B193]]'s open question and cannot be asserted here.
        }
        // ---------- POST-SAVE. The one operation below is INFALLIBLE given a validated request. ----------
        _live.apply_and_start(cand);
        r.verdict = JoinVerdict::started;
        return r;
    }

  private:
    ICfgStore& _store;
    IJoinLive& _live;
};

}  // namespace mrfw
