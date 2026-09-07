// MeshRoute — shared command-byte validation (Slice 6, R-RA-7 / R-RA-24′).
#pragma once
#include <cstddef>
#include <cstdint>

namespace meshroute::console {

enum class LineErr : uint8_t { ok, embedded_nul, embedded_cr, embedded_lf, too_long };
inline constexpr size_t local_command_max_bytes = 1023;
// Bound to the live depth-4 carrier cap minus AUTH_EXECUTE overhead in test_console_line.cpp.
inline constexpr size_t remote_command_max_bytes = 201;

// The caller supplies a readable span. Length takes precedence over byte errors; the first bad
// byte takes precedence over later ones. No truncation and no per-transport validation twin.
inline constexpr LineErr validate_command_line(const char* line, size_t len, size_t max_bytes) {
    if (len > max_bytes) return LineErr::too_long;
    for (size_t i = 0; i < len; ++i) {
        if (line[i] == '\0') return LineErr::embedded_nul;
        if (line[i] == '\r') return LineErr::embedded_cr;
        if (line[i] == '\n') return LineErr::embedded_lf;
    }
    return LineErr::ok;
}

inline constexpr const char* line_err_name(LineErr e) {
    switch (e) {
        case LineErr::ok:           return "ok";
        case LineErr::embedded_nul: return "embedded_nul";
        case LineErr::embedded_cr:  return "embedded_cr";
        case LineErr::embedded_lf:  return "embedded_lf";
        case LineErr::too_long:     return "too_long";
    }
    return "unknown";
}

}  // namespace meshroute::console
