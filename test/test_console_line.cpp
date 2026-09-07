// Slice 6: the shared validator and its live codec-cap binding. No firmware TU is compiled here.
#include "doctest.h"
#include "console_line.h"
#include "remote_codec.h"
#include "frame_codec.h"
#include <array>
#include <cstring>

using namespace meshroute::console;

TEST_CASE("consoleline empty and ordinary spans") {
    CHECK(validate_command_line(nullptr, 0, 0) == LineErr::ok);
    CHECK(validate_command_line("status", 6, local_command_max_bytes) == LineErr::ok);
    CHECK(validate_command_line("abc\0tail", 3, 3) == LineErr::ok);
    constexpr auto constant = validate_command_line("routes", 6, 6);
    CHECK(constant == LineErr::ok);
}

TEST_CASE("consoleline every byte value at first middle and last position") {
    for (const size_t pos : {size_t(0), size_t(2), size_t(4)}) {
        for (unsigned byte = 0; byte < 256; ++byte) {
            char line[] = "abcde";
            line[pos] = static_cast<char>(byte);
            const LineErr expected = byte == 0 ? LineErr::embedded_nul
                : byte == 13 ? LineErr::embedded_cr : byte == 10 ? LineErr::embedded_lf : LineErr::ok;
            CHECK(validate_command_line(line, 5, 5) == expected);
        }
    }
}

TEST_CASE("consoleline all three inclusive surface boundaries") {
    std::array<char, local_command_max_bytes + 2> line;
    line.fill('x');
    // 274 is the BLE grammar's measured product bound; its actual caller must pass the device constant,
    // not this fixture value. The caller binding is separately controlled by the console-sink probe.
    for (const size_t cap : {local_command_max_bytes, size_t(274), remote_command_max_bytes}) {
        CHECK(validate_command_line(line.data(), cap, cap) == LineErr::ok);
        CHECK(validate_command_line(line.data(), cap + 1, cap) == LineErr::too_long);
    }
    CHECK(local_command_max_bytes == 1023);
    CHECK(remote_command_max_bytes == 201);
}

TEST_CASE("consoleline reason precedence is length then earliest byte") {
    const char line[] = {'\r', '\n', '\0'};
    CHECK(validate_command_line(line, 3, 2) == LineErr::too_long);
    CHECK(validate_command_line(line, 3, 3) == LineErr::embedded_cr);
    CHECK(validate_command_line(line + 1, 2, 2) == LineErr::embedded_lf);
    CHECK(validate_command_line(line + 2, 1, 1) == LineErr::embedded_nul);
}

TEST_CASE("consoleline remote bound follows the live smallest authenticated carrier") {
    meshroute::RemoteCarrier carrier{};
    carrier.outer_data_type = meshroute::DATA_TYPE_REMOTE_CMD;
    carrier.cross_layer = true;
    carrier.path_depth = 4;
    carrier.dst_hash_on_wire = true;
    size_t cap = 0;
    CHECK(meshroute::remote_body_cap(carrier, cap) == meshroute::RemoteStatus::ok);
    CHECK(cap == 226);
    CHECK(remote_command_max_bytes == cap - meshroute::kRemoteOverheadAuthExecute);
}

TEST_CASE("consoleline exhaustive reason names") {
    const char* names[] = {"ok", "embedded_nul", "embedded_cr", "embedded_lf", "too_long"};
    for (unsigned i = 0; i <= static_cast<unsigned>(LineErr::too_long); ++i)
        CHECK(std::strcmp(line_err_name(static_cast<LineErr>(i)), names[i]) == 0);
    CHECK(std::strcmp(line_err_name(static_cast<LineErr>(255)), "unknown") == 0);
}
