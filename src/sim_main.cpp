// MeshRoute — sim_main.cpp
//
// Native build's entry point. Iteration 1 just prints the protocol-
// constants summary so we can confirm the build wires together.
// Iteration 2 will load a scenario JSON from spec/test/ or
// spec/scenarios/, instantiate one or more nodes, and emit NDJSON
// events to stdout for differential testing against the Lua model.

#include <cstdio>

#include "protocol_constants.h"

namespace meshroute {

static void print_protocol_summary() {
    namespace P = meshroute::protocol;
    std::printf("MeshRoute v0.1 — protocol summary\n");
    std::printf("  Protocol version       : %d\n", PROTOCOL_VERSION);
    std::printf("  RF plan (compile-time) : %.4f MHz / SF%d / BW%.1f kHz / CR4/%d / duty %d%%\n",
                (double)LORA_FREQ,
                LORA_SF, (double)LORA_BW, LORA_CR, LORA_DUTY_CYCLE_PCT);
    std::printf("  Preamble symbols       : %u\n", P::preamble_sym);
    std::printf("  SF margin (Q4)         : %d  (%.4f dB)\n",
                P::sf_margin_q4, P::q4_to_db(P::sf_margin_q4));
    std::printf("  Beacon period (discovery): %u ms  [steady-state beacon_period_ms is T-class]\n",
                P::discovery_beacon_period_ms);
    std::printf("  Hop budget slack       : %u\n", P::hop_budget_slack);
    std::printf("  Bounded caps           : seen_origins=%u q_queried=%u id_bind=%u\n",
                P::cap_seen_origins, P::cap_q_queried, P::cap_id_bind);
    std::printf("  Max payload (hard cap) : %u bytes  (LoRa frame %u - hdr %u - inner %u)\n",
                P::max_payload_bytes_hard_cap,
                P::lora_max_frame_bytes, P::data_hdr_len, P::data_inner_overhead);
}

}  // namespace meshroute

int main(int argc, char** argv) {
    meshroute::print_protocol_summary();
    if (argc > 1) {
        std::printf("\nTODO(iteration-2): load scenario %s and emit NDJSON.\n", argv[1]);
    } else {
        std::printf("\nUsage: %s <scenario.json>   (TODO iteration-2)\n", argv[0]);
    }
    return 0;
}
