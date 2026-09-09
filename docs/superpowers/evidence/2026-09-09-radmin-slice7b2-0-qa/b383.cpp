// Preflight-only: real old codec behavior, no production repair.
#include "remote_codec.h"
#include "frame_codec.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
using namespace meshroute;
static unsigned checks;
static void require(bool ok) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL check %u\n", checks); std::exit(1); }
}
int main() {
    std::array<uint8_t, 32> key{}; key.fill(0x45);
    const RemoteKeys keys{{}, key};
    const RemoteSource source{true, 0x12345678};
    RemoteCarrier carrier{}; carrier.outer_data_type = DATA_TYPE_REMOTE_RESP;
    for (const auto opcode : {RemoteRespOpcode::terminal, RemoteRespOpcode::protocol_error}) {
        RemoteMessage msg{};
        msg.outer_type = DATA_TYPE_REMOTE_RESP;
        msg.opcode = static_cast<uint8_t>(opcode);
        msg.slot = 3; msg.request_id = 0x1234;
        const std::array<uint8_t, 2> invalid{
            static_cast<uint8_t>(opcode == RemoteRespOpcode::terminal ? 0x08 : 0x01), 0xD1};
        std::array<uint8_t, 64> wire{};
        size_t written = 999;
        require(remote_body_encode(wire, written, msg, invalid, keys, source, carrier) == RemoteStatus::ok);
        require(written == kRemoteOverheadAuthResponse + invalid.size());
        RemoteDecoded decoded{}; decoded.msg.request_id = 0xFEEDFACE;
        std::array<uint8_t, 4> plain{}; plain.fill(0xC7);
        require(remote_body_decode(decoded, msg.outer_type, {wire.data(), written},
                                   keys, source, carrier, plain) == RemoteStatus::bad_result_code);
        require(plain[0] == invalid[0]);
        require(plain[1] == invalid[1]);
        require(plain[2] == 0xC7 && plain[3] == 0xC7);
        require(decoded.msg.request_id == 0xFEEDFACE);
        require(!decoded.authenticated);
        std::printf("opcode %u: valid tag / invalid result -> bad_result_code; plaintext %02x %02x; decoded sentinel retained\n",
                    msg.opcode, plain[0], plain[1]);
        wire[written - 1] ^= 1;
        plain.fill(0xC7);
        require(remote_body_decode(decoded, msg.outer_type, {wire.data(), written},
                                   keys, source, carrier, plain) == RemoteStatus::auth_failed);
        require(std::all_of(plain.begin(), plain.end(), [](uint8_t b) { return b == 0xC7; }));
        require(decoded.msg.request_id == 0xFEEDFACE);
        require(!decoded.authenticated);
    }
    std::printf("PASS: %u checks; bad-tag controls preserve the plaintext buffer\n", checks);
}
