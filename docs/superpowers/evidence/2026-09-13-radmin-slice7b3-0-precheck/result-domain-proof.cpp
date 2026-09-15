// QA source-boundary reproduction; calls the real codec, adds no producer.
// Default is the b9d75aa baseline. --allocated is the future 7b-3-0 contract.
#include "remote_codec.h"
#include "frame_codec.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace meshroute;
static unsigned checks;
static void require(bool ok) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL check %u\n", checks); std::exit(1); }
}
int main(int argc, char** argv) {
    require(argc == 1 || (argc == 2 && std::strcmp(argv[1], "--allocated") == 0));
    const unsigned ceiling = argc == 2 ? 8 : 7;
    require(kRemoteTerminalMax == ceiling);
    require(sizeof(RemoteTerminal) == 1);
    std::array<uint8_t, 32> key{}; key.fill(0x45);
    const RemoteKeys keys{{}, key};
    const RemoteSource source{true, 0x12345678};
    RemoteCarrier carrier{}; carrier.outer_data_type = DATA_TYPE_REMOTE_RESP;
    for (unsigned domain = 0; domain < 4; ++domain) {
        const bool auth = (domain & 1) == 0;
        const bool terminal = domain < 2;
        unsigned accepted = 0, rejected = 0;
        for (unsigned code = 0; code < 256; ++code) {
            RemoteMessage msg{}; msg.outer_type = DATA_TYPE_REMOTE_RESP;
            msg.opcode = terminal ? 1 : 4; msg.slot = auth ? 3 : 15;
            msg.request_id = 0x1234; msg.response_seq = 7;
            const std::array<uint8_t, 2> payload{static_cast<uint8_t>(code), 0xD1};
            std::array<uint8_t, 64> wire{}; size_t written = 999;
            require(remote_body_encode(wire, written, msg, payload, keys, source, carrier) == RemoteStatus::ok);
            require(written == (auth ? kRemoteOverheadAuthResponse : kRemoteOverheadOpenResponse) + payload.size());
            RemoteDecoded decoded{}; decoded.msg.request_id = 0xFEEDFACE;
            std::array<uint8_t, 4> plain{}; plain.fill(0xC7);
            const bool valid = terminal ? code <= ceiling : (!auth || code == 0);
            const RemoteStatus status = remote_body_decode(decoded, msg.outer_type, {wire.data(), written}, keys, source, carrier, plain);
            require(status == (valid ? RemoteStatus::ok : RemoteStatus::bad_result_code));
            if (auth) {
                require(plain[0] == code && plain[1] == 0xD1);
                require(plain[2] == 0xC7 && plain[3] == 0xC7);
            } else {
                require(std::all_of(plain.begin(), plain.end(), [](uint8_t b) { return b == 0xC7; }));
            }
            if (valid) {
                ++accepted;
                require(decoded.msg.request_id == msg.request_id);
                require(decoded.authenticated == auth);
                require(decoded.body.size() == 2 && decoded.body[0] == code && decoded.body[1] == 0xD1);
                require(decoded.result_kind == (terminal ? RemoteResultKind::terminal : auth ? RemoteResultKind::protocol_error : RemoteResultKind::none));
                if (terminal) require(static_cast<unsigned>(decoded.terminal) == code);
                if (terminal || auth) require(decoded.result_detail.size() == 1 && decoded.result_detail[0] == 0xD1);
                else require(decoded.result_detail.empty());
            } else {
                ++rejected;
                require(decoded.msg.request_id == 0xFEEDFACE);
                require(!decoded.authenticated);
                require(decoded.result_kind == RemoteResultKind::none);
            }
            if (auth) {
                wire[written - 1] ^= 1; plain.fill(0xC7);
                RemoteDecoded failed{}; failed.msg.request_id = 0xFEEDFACE;
                require(remote_body_decode(failed, msg.outer_type, {wire.data(), written}, keys, source, carrier, plain) == RemoteStatus::auth_failed);
                require(std::all_of(plain.begin(), plain.end(), [](uint8_t b) { return b == 0xC7; }));
                require(failed.msg.request_id == 0xFEEDFACE && !failed.authenticated);
            }
        }
        std::printf("%s %s: encode 256/256; decode accepted %u / rejected %u\n", auth ? "authenticated" : "open", terminal ? "TERMINAL" : "PROTOCOL_ERROR", accepted, rejected);
    }
    std::printf("PASS: %u checks; terminal ceiling 0x%02x; valid-tag failure writes plaintext; bad-tag controls preserve it\n", checks, ceiling);
}
