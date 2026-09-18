// MeshRoute — tools/probe_ble_line/probe_main.cpp
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// §0f BLE INBOUND-LINE PROBE — the REAL `src/device_ble.h` intake, host-compiled and host-EXECUTED.
//
// WHY THIS EXISTS. `platformio.ini`'s native env sets `test_build_src = no` and the simulator compiles `lib/core`
// only, so NO automated gate in this repository compiles a single `src/` translation unit. `service_rx()` and
// `dispatch_current_line()` — the byte machine that decides WHICH LINES EXIST over BLE — were therefore covered by
// nothing at all. Slice 0f moves that machine's admission edge from 160 to a derived capacity, so the edge itself
// has to be measured, not argued.
//
// WHAT IS REAL HERE, said plainly:
//   • REAL: `mrble::begin()`, `mrble::service_rx()`, `mrble::dispatch_current_line()`, `mrble::tx_line()` and the
//     derived capacity constants — all compiled from `src/device_ble.h` itself, through a probe-local Bluefruit
//     shim that holds NO line buffer, NO newline logic and NO length limit.
//   • REAL: `meshroute::pack_unicast_inner` from `lib/core/frame_codec.cpp`, executed to PIN the transitional
//     226-byte cross-layer body-cap mirror at cap (must pack) and cap+1 (must refuse).
//   • FAKE, and only this: the Bluefruit/NUS transport surface and two Nordic register/SVCALL stubs that
//     `device_rng.h` names but that no code path here calls.
//   • NOT MEASURED HERE: what the COMMAND does with an admitted line. `err_unsupported`, `too_large` and the
//     queue result are `Node::on_command`'s, and they are owned by the native tests — this probe proves only that
//     the BYTES arrive intact, which is the precondition those named refusals depend on.
//
// ⚠ ONE KNOWN FIDELITY GAP, DECLARED: the shared Arduino fake has no `println(uint8_t, int)`, so the connect /
//   disconnect debug prints bind a different overload than the real core. Those callbacks are never invoked by this
//   probe and are not on the intake path; the fake is deliberately NOT forked to "fix" it (U1).
#include <Arduino.h>
#include "device_ble.h"

#include "frame_codec.h"
#include "protocol_constants.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

// ---- the host stand-ins the real headers declare -------------------------------------------------------------------
FakeSerial        Serial;
AdafruitBluefruit Bluefruit;
static NRF_RNG_Type   g_rng_regs;
NRF_RNG_Type* const   NRF_RNG = &g_rng_regs;
static uint32_t rng_available_status=NRF_SUCCESS, rng_vector_status=NRF_SUCCESS;
static uint8_t rng_available=3, rng_next=1;
uint32_t sd_rand_application_vector_get(uint8_t* p, uint8_t n) {
    if(rng_vector_status!=NRF_SUCCESS)return rng_vector_status;
    for(uint8_t i=0;i<n;++i)p[i]=rng_next++;
    return NRF_SUCCESS;
}
uint32_t sd_rand_application_bytes_available_get(uint8_t* a) { *a=rng_available; return rng_available_status; }

namespace {

int g_checks = 0, g_fails = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) { std::printf("  ok   %s\n", what.c_str()); }
    else    { ++g_fails; std::printf("  FAIL %s\n", what.c_str()); }
}

// ---- the dispatch spy: the REAL DispatchFn seam ---------------------------------------------------------------------
std::vector<std::string> g_dispatched;      // every line handed to the dispatcher, byte-for-byte
std::string              g_reply;           // what the spy answers with (empty = no reply)

size_t spy_dispatch(const char* line, size_t len, char* out, size_t cap) {
    g_dispatched.emplace_back(line, len);
    // The production contract: the dispatcher NUL-terminates `line` itself before calling us, and we return the
    // number of bytes written into `out`. Both are exercised.
    if (g_reply.empty() || g_reply.size() > cap) return 0;
    std::memcpy(out, g_reply.data(), g_reply.size());
    return g_reply.size();
}

// ---- the driver: push `text` in chunks of `chunk`, draining the REAL service_rx() after each ------------------------
// The chunk boundary is expressed ONLY as "when service_rx() is called", which is exactly how ATT writes reach the
// firmware: the SoftDevice fills the BLEUart FIFO, loop() drains it. Nothing about the line machine is modelled here.
void feed(const std::string& text, size_t chunk) {
    size_t off = 0;
    while (off < text.size()) {
        const size_t n = (text.size() - off < chunk) ? (text.size() - off) : chunk;
        mrble::g_bleuart.push(text.data() + off, n);
        off += n;
        mrble::service_rx();
    }
}

void reset_bus() { mrble::g_bleuart.reset(); g_dispatched.clear(); g_reply.clear(); }

std::string repeat(char c, size_t n) { return std::string(n, c); }

// ---- FNV-1a-64 over a file, recomputed independently of the runner's python implementation --------------------------
bool fnv1a_file(const char* path, unsigned long long& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    unsigned long long h = 14695981039346656037ULL;
    int c;
    while ((c = std::fgetc(f)) != EOF) { h ^= static_cast<unsigned long long>(static_cast<unsigned char>(c)); h *= 1099511628211ULL; }
    std::fclose(f);
    out = h;
    return true;
}

std::string slurp(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return std::string();
    std::string s; char buf[4096]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

size_t count_of(const std::string& hay, const std::string& needle) {
    size_t n = 0, p = 0;
    while ((p = hay.find(needle, p)) != std::string::npos) { ++n; p += needle.size(); }
    return n;
}

// ---- the canonical maximal spellings, BUILT FROM THE HEADER'S OWN DERIVED TERMS -------------------------------------
// ⛔ Nothing here re-derives a length: every count comes from `src/device_ble.h`, so a wrong constant produces a
//    wrong LINE and the length assertions below catch it, instead of the probe agreeing with the bug.
std::string line_send_max() {                       // `send 0xffffffff "<239>" -a -e -t -K -l`
    return "send 0xffffffff \"" + repeat('S', meshroute::protocol::dm_max_body_bytes) + "\" -a -e -t -K -l";
}
std::string line_send_layer_max() {                 // `send_layer 0xffffffff 255,255,255 "<226>" -a -e -K -l`
    return "send_layer 0xffffffff 255,255,255 \"" + repeat('X', mrble::kSendLayerBodyCapBytes) + "\" -a -e -K -l";
}
std::string line_send_layer_queue() {               // the plaintext form that actually queues: `-a -K` only
    return "send_layer 0xffffffff 255,255,255 \"" + repeat('X', mrble::kSendLayerBodyCapBytes) + "\" -a -K";
}

const char kRefusal[] = "{\"err\":\"line_too_long\"}\n";

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: probe <path-to-device_ble.h>\n"); return 2; }
    const char* hdr_path = argv[1];

    std::printf("== §0f BLE inbound-line probe (the REAL src/device_ble.h intake) ==\n");
    std::printf("   compiled header FNV-1a-64 = 0x%016llx   inspected: %s\n",
                static_cast<unsigned long long>(PROBE_BLE_HDR_HASH), hdr_path);
    std::printf("   derived: send=%zu  send_layer=%zu (body cap %zu, path %zu, XL inner overhead %zu)  remote=%zu"
                "  =>  storage=%zu\n",
                mrble::kSendLineMaxBytes, mrble::kSendLayerLineMaxBytes, mrble::kSendLayerBodyCapBytes,
                mrble::kLayerPathBytes, mrble::kXlInnerOverheadBytes, mrble::kRemoteLineMaxBytes,
                mrble::kLineStorageBytes);

    // =================================================================================================================
    // A. THE SOURCE-INTEGRITY PIN — the header I COMPILED is the header I am INSPECTING.
    //    ⛔ This is the anti-vacuity pin: "compile a copy, inspect the live tree" is the classic worthless probe.
    // =================================================================================================================
    unsigned long long live_hash = 0;
    const bool got = fnv1a_file(hdr_path, live_hash);
    check(got && live_hash == static_cast<unsigned long long>(PROBE_BLE_HDR_HASH),
          "A1  the COMPILED header and the INSPECTED header are the same text (FNV-1a-64 pin)");

    const std::string src = slurp(hdr_path);
    check(!src.empty(), "A2  the header source was read");

    // ---- the derivation is structural: no naked literal may control admission ----------------------------------------
    check(src.find("char                       g_line[kLineStorageBytes];") != std::string::npos,
          "A3  `g_line`'s extent is the DERIVED constant, not a numeric literal");
    check(src.find("kLineStorageBytes    = kProductLineMaxBytes + 1;") != std::string::npos,
          "A4  the storage is derived as `kProductLineMaxBytes + 1` (the grammar maximum plus its NUL)");
    check(src.find("meshroute::protocol::dm_max_body_bytes") != std::string::npos,
          "A5  the `send` body term is BOUND to `protocol::dm_max_body_bytes` (239 is never copied)");
    check(src.find("meshroute::protocol::max_payload_bytes_hard_cap - kXlInnerOverheadBytes") != std::string::npos,
          "A6  the `send_layer` body cap mirrors pack_unicast_inner's arithmetic (226 is never copied)");
    check(count_of(src, "meshroute::protocol::gw_env_max_hops") >= 2,
          "A7  the hop-path and layer-id terms are BOUND to `protocol::gw_env_max_hops`");
    check(count_of(src, "sizeof(\"") >= 12,
          "A8  the fixed grammar terms are spelled `sizeof(\"literal\") - 1`, so adding/removing one moves the buffer");
    check(src.find("else if (g_pos < sizeof(g_line) - 1)") != std::string::npos,
          "A9  admission reads the extent off `g_line` itself — there is exactly ONE capacity expression");
    check(src.find("static const char kTooLong[] = \"{\\\"err\\\":\\\"line_too_long\\\"}\\n\";") != std::string::npos,
          "A10 the loud overflow refusal literal is unchanged");
    check(src.find("g_line[160]") == std::string::npos && src.find("g_line[275]") == std::string::npos,
          "A11 no literal-extent `g_line[...]` declaration survives anywhere in the header");

    // =================================================================================================================
    // B. THE TRANSITIONAL 226-BYTE MIRROR, PINNED BY THE REAL PACKER.
    //    `pack_unicast_inner` sizes the cross-layer inner at RUNTIME; 0f mirrors that arithmetic as a constant, so the
    //    mirror must be EXECUTED against the packer, at the cap and one byte past it.
    // =================================================================================================================
    {
        using namespace meshroute;
        const uint8_t xl_flags = static_cast<uint8_t>(DATA_FLAG_CROSS_LAYER | DATA_FLAG_DST_HASH | DATA_FLAG_SOURCE_HASH);
        const uint8_t n_layers = protocol::gw_env_max_hops;                 // console max depth: 1 (ours) + 3 hops
        uint8_t ids[protocol::gw_env_max_hops] = { 1, 2, 3, 4 };
        uint8_t inner[protocol::max_payload_bytes_hard_cap] = {};           // == TxItem.inner[]
        std::vector<uint8_t> body(mrble::kSendLayerBodyCapBytes + 1, 'X');

        const size_t at_cap = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), xl_flags, 0xffffffffu,
                                                 ids, n_layers, /*cur=*/1, /*origin=*/7, /*source_hash=*/0xdeadbeefu,
                                                 body.data(), static_cast<uint8_t>(mrble::kSendLayerBodyCapBytes), 0, 0);
        check(at_cap == protocol::max_payload_bytes_hard_cap,
              "B1  the real pack_unicast_inner ACCEPTS a body of exactly `kSendLayerBodyCapBytes` and fills inner[] "
              "to the hard cap");

        const size_t over = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), xl_flags, 0xffffffffu,
                                               ids, n_layers, /*cur=*/1, /*origin=*/7, /*source_hash=*/0xdeadbeefu,
                                               body.data(), static_cast<uint8_t>(mrble::kSendLayerBodyCapBytes + 1), 0, 0);
        check(over == 0, "B2  ...and REFUSES cap+1 (the mirror is the packer's real edge, not an estimate)");

        // Depth is what binds the LINE: one hop fewer buys 1 body byte and costs 4 path bytes.
        uint8_t ids3[protocol::gw_env_max_hops] = { 1, 2, 3, 0 };
        const size_t at_cap3 = pack_unicast_inner(std::span<uint8_t>(inner, sizeof inner), xl_flags, 0xffffffffu,
                                                  ids3, static_cast<uint8_t>(n_layers - 1), /*cur=*/1, /*origin=*/7,
                                                  0xdeadbeefu, body.data(),
                                                  static_cast<uint8_t>(mrble::kSendLayerBodyCapBytes + 1), 0, 0);
        check(at_cap3 == protocol::max_payload_bytes_hard_cap,
              "B3  one layer shallower the packer takes exactly ONE more body byte — so the DEEPEST path, which costs "
              "4 line bytes to buy 1, is the longest line");
    }

    // =================================================================================================================
    // C. THE EXECUTED INTAKE. Everything below runs the real `service_rx()`/`dispatch_current_line()`.
    // =================================================================================================================
    const bool started = mrble::begin(/*mode=*/1, /*period_min=*/0, /*pin=*/123456u, "probe", &spy_dispatch);
    mrble::g_conn_count=1; mrble::g_conn_handle=1; // connected transport fixture, required by tx_line
    check(started, "C1  the real mrble::begin() started the transport and installed the dispatch seam");

    const std::string L274 = line_send_layer_max();
    const std::string L268 = line_send_layer_queue();
    const std::string L272 = line_send_max();

    check(L274.size() == mrble::kSendLayerLineMaxBytes && L274.size() == mrble::kLineStorageBytes - 1,
          "C2  the canonical maximal `send_layer` spelling is exactly the derived maximum, i.e. storage-minus-NUL");
    check(L272.size() == mrble::kSendLineMaxBytes,
          "C3  the canonical maximal by-hash `send` spelling is exactly the derived `send` maximum");
    check(L268.size() + 2 * (sizeof(" -a") - 1) == L274.size(),
          "C4  the plaintext queue-positive is the same line minus `-e -l` (the two options that refuse semantically)");

    // ---- chunking invariance on the binding transport-positive -------------------------------------------------------
    std::vector<std::string> got_by_chunk;
    const size_t chunks[3] = { 1, 20, 244 };
    for (size_t i = 0; i < 3; ++i) {
        reset_bus();
        feed(L274 + "\n", chunks[i]);
        char label[128];
        std::snprintf(label, sizeof label,
                      "C%zu  the %zu-byte `send_layer` transport-positive dispatches ONCE, byte-identically, "
                      "under %zu-byte BLE writes", 5 + i, L274.size(), chunks[i]);
        check(g_dispatched.size() == 1 && g_dispatched[0] == L274, label);
        got_by_chunk.push_back(g_dispatched.empty() ? std::string("<none>") : g_dispatched[0]);
    }
    check(got_by_chunk[0] == got_by_chunk[1] && got_by_chunk[1] == got_by_chunk[2],
          "C8  ...and the three chunkings produce the SAME bytes (an ATT boundary changes nothing)");

    reset_bus(); feed(L274 + "\n", L274.size() + 1);
    check(g_dispatched.size() == 1 && g_dispatched[0] == L274,
          "C9  one single write carrying the whole line + newline dispatches identically");

    reset_bus(); feed(L274 + "\n", 60);                    // 275 = 4*60 + 35 -> a deliberately uneven final chunk
    check(g_dispatched.size() == 1 && g_dispatched[0] == L274,
          "C10 an uneven final chunk dispatches identically");

    reset_bus();
    mrble::g_bleuart.push(L274.data(), L274.size()); mrble::service_rx();
    const size_t after_body = g_dispatched.size();
    mrble::g_bleuart.push("\n", 1); mrble::service_rx();
    check(after_body == 0 && g_dispatched.size() == 1 && g_dispatched[0] == L274,
          "C11 a chunk boundary immediately BEFORE the newline holds the line, then dispatches it whole");

    reset_bus(); feed(L268 + "\n", 20);
    check(g_dispatched.size() == 1 && g_dispatched[0] == L268,
          "C12 the plaintext `send_layer` queue-positive dispatches byte-identically");

    reset_bus(); feed(L272 + "\n", 20);
    check(g_dispatched.size() == 1 && g_dispatched[0] == L272,
          "C13 the maximal by-hash `send` line (full 239-B DM body + five flags) dispatches byte-identically");

    // ---- CR handling, empties, the smallest line ----------------------------------------------------------------------
    reset_bus(); feed("status\r\n", 3);
    const std::string crlf_line = g_dispatched.empty() ? std::string() : g_dispatched[0];
    reset_bus(); feed("status\n", 3);
    const std::string lf_line = g_dispatched.empty() ? std::string() : g_dispatched[0];
    check(crlf_line == lf_line && crlf_line == "status" && crlf_line.find('\r') == std::string::npos,
          "C14 CRLF and LF produce the SAME line bytes, with the CR absent");

    reset_bus(); feed("\n", 1);
    check(g_dispatched.empty() && mrble::g_bleuart.tx.empty(), "C15 an empty LF line dispatches nothing and answers nothing");
    reset_bus(); feed("\r\n", 1);
    check(g_dispatched.empty() && mrble::g_bleuart.tx.empty(), "C16 an empty CRLF line dispatches nothing and answers nothing");

    reset_bus(); feed("x\n", 1);
    check(g_dispatched.size() == 1 && g_dispatched[0] == "x", "C17 a one-byte line dispatches once");

    // ---- the reply path is untouched -----------------------------------------------------------------------------------
    reset_bus(); g_reply = "{\"ev\":\"ack\",\"code\":\"queued\"}\n";
    feed("status\n", 4);
    check(mrble::g_bleuart.tx == g_reply && mrble::g_bleuart.tx_calls.size() == 1,
          "C18 a non-empty dispatch reply is written to BLE unchanged, in one write");
    reset_bus();
    feed("status\n", 4);
    check(mrble::g_bleuart.tx.empty(), "C19 a zero-length dispatch reply writes nothing");

    // ---- THE REFUSAL EDGE ------------------------------------------------------------------------------------------------
    const std::string LOVER = repeat('Z', mrble::kLineStorageBytes);           // storage-many payload bytes = one too many
    for (size_t i = 0; i < 3; ++i) {
        reset_bus();
        feed(LOVER + "\n", chunks[i]);
        char label[160];
        std::snprintf(label, sizeof label,
                      "C%zu  a %zu-byte line (one past the derived edge) dispatches NOTHING and answers exactly one "
                      "`line_too_long`, under %zu-byte writes", 20 + i, LOVER.size(), chunks[i]);
        check(g_dispatched.empty() && mrble::g_bleuart.tx == kRefusal && mrble::g_bleuart.tx_calls.size() == 1, label);
    }

    reset_bus(); feed(repeat('Z', mrble::kLineStorageBytes - 1) + "\n", 20);
    check(g_dispatched.size() == 1 && g_dispatched[0].size() == mrble::kLineStorageBytes - 1 && mrble::g_bleuart.tx.empty(),
          "C23 the byte BELOW the edge is admitted whole — the boundary is exactly storage-minus-NUL");

    reset_bus(); feed(repeat('Z', 600) + "\n", 37);
    check(g_dispatched.empty() && mrble::g_bleuart.tx_calls.size() == 1 && mrble::g_bleuart.tx == kRefusal,
          "C24 a line far past the edge still refuses EXACTLY ONCE (the excess is consumed, never re-entered)");

    reset_bus(); feed(repeat('Z', 600) + "\nstatus\n", 13);
    check(g_dispatched.size() == 1 && g_dispatched[0] == "status" && mrble::g_bleuart.tx_calls.size() == 1,
          "C25 an over-long line is never split into a second command — the following line is the only dispatch");

    reset_bus(); feed(LOVER + "\n", 20); feed(L274 + "\n", 20);
    check(g_dispatched.size() == 1 && g_dispatched[0] == L274 && mrble::g_bleuart.tx_calls.size() == 1,
          "C26 after a refusal the intake resets: the next valid maximal line dispatches normally");

    // B292: direct replies use the same bounded transport as streamed output. Inspect each write,
    // as well as the aggregate, so a fake transport that accepts oversize writes cannot hide the defect.
    for (uint16_t mtu : {uint16_t{23},uint16_t{247}}) {
        Bluefruit.conn.mtu=mtu;
        for (size_t n : {size_t{245},size_t{256}}) {
            reset_bus(); g_reply=std::string(n-1,'R')+"\n"; feed("status\n",20);
            check(mrble::g_bleuart.tx==g_reply,"D1 complete 245..256-byte direct reply survives");
            bool bounded=true; for (const auto& write:mrble::g_bleuart.tx_calls) bounded &= write.size()<=size_t(mtu-3);
            check(bounded && mrble::g_bleuart.tx_calls.size()>1,"D2 every notification respects negotiated ATT payload");
        }
    }
    uint8_t entropy[8]{};
    mrrng::sd_enabled()=true; rng_next=1;
    check(mrrng::fill_checked(entropy,sizeof entropy),"E1 checked SoftDevice entropy accumulates partial pool reads");
    bool bytes=true;for(unsigned i=0;i<8;++i)bytes &= entropy[i]==i+1;
    check(bytes,"E2 all eight bytes come from the provider in order");
    rng_available_status=1;
    check(!mrrng::fill_checked(entropy,sizeof entropy),"E3 pool status failure refuses");rng_available_status=NRF_SUCCESS;
    rng_vector_status=1;
    check(!mrrng::fill_checked(entropy,sizeof entropy),"E4 vector status failure refuses");rng_vector_status=NRF_SUCCESS;
    rng_available=0;
    check(!mrrng::fill_checked(entropy,sizeof entropy),"E5 empty pool reaches bounded failure");rng_available=3;
    mrrng::sd_enabled()=false;
    check(!mrrng::fill_checked(entropy,sizeof entropy) && NRF_RNG->TASKS_STOP==1,"E6 stalled peripheral fails and is stopped");
    check(!mrrng::fill_checked(nullptr,1),"E7 null nonempty destination refuses before hardware access");
    std::printf("\nprobe: %d checks against the REAL device_ble.h intake, %d failed\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}
