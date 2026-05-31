// MeshRoute — lib/core/wire.h
// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
//
// C0: shared wire primitives for the §10 cmd-nibble frame format. Header-only,
// no heap, no exceptions. Internal to the codec layer (meshroute_core, C++20) —
// NOT part of hal.h, which stays C++17-clean.
//
//   * byte 0 of every frame is cmd(4 hi) | flags(4 lo)  (ROADMAP §10)
//   * Writer/Reader are bounded cursors: on overflow / read-past-end they set
//     !ok() and never touch out-of-bounds memory (return 0 on over-read).
//   * key_hash32 / 16-bit counters are LE; channel-msg-id is BE (later frames).

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace meshroute::wire {

// §10.1 primary command codes (byte 0 high nibble).
enum class Cmd : uint8_t {
    B = 0x0, R = 0x1, C = 0x2, D = 0x3, K = 0x4,
    N = 0x5, Q = 0x6, H = 0x7, F = 0x8, J = 0x9, EXT = 0xF
};

constexpr uint8_t cmd_byte(Cmd c, uint8_t flags4) {
    return static_cast<uint8_t>((static_cast<uint8_t>(c) << 4) | (flags4 & 0x0F));
}
constexpr Cmd     cmd_of(uint8_t b)   { return static_cast<Cmd>(b >> 4); }
constexpr uint8_t flags_of(uint8_t b) { return static_cast<uint8_t>(b & 0x0F); }

// Bounded byte writer. !ok() after the first overflowing write.
class Writer {
public:
    explicit Writer(std::span<uint8_t> out) : _out(out) {}
    void u8(uint8_t v) {
        if (_pos >= _out.size()) { _ok = false; return; }
        _out[_pos++] = v;
    }
    void u16_le(uint16_t v) { u8(uint8_t(v));       u8(uint8_t(v >> 8)); }
    void u32_le(uint32_t v) { u8(uint8_t(v));       u8(uint8_t(v >> 8));
                              u8(uint8_t(v >> 16)); u8(uint8_t(v >> 24)); }
    void u16_be(uint16_t v) { u8(uint8_t(v >> 8));  u8(uint8_t(v)); }
    void u32_be(uint32_t v) { u8(uint8_t(v >> 24)); u8(uint8_t(v >> 16));
                              u8(uint8_t(v >> 8));  u8(uint8_t(v)); }
    bool   ok()   const { return _ok; }
    size_t size() const { return _pos; }
private:
    std::span<uint8_t> _out;
    size_t _pos = 0;
    bool   _ok  = true;
};

// Bounded byte reader. !ok() after the first read past the end (returns 0).
class Reader {
public:
    explicit Reader(std::span<const uint8_t> in) : _in(in) {}
    uint8_t u8() {
        if (_pos >= _in.size()) { _ok = false; return 0; }
        return _in[_pos++];
    }
    uint16_t u16_le() { uint16_t a = u8(); uint16_t b = u8(); return uint16_t(a | (b << 8)); }
    uint32_t u32_le() { uint32_t a = u8(), b = u8(), c = u8(), d = u8();
                        return a | (b << 8) | (c << 16) | (d << 24); }
    uint16_t u16_be() { uint16_t a = u8(); uint16_t b = u8(); return uint16_t((a << 8) | b); }
    uint32_t u32_be() { uint32_t a = u8(), b = u8(), c = u8(), d = u8();
                        return (a << 24) | (b << 16) | (c << 8) | d; }
    bool   ok()        const { return _ok; }
    size_t remaining() const { return _in.size() - _pos; }
private:
    std::span<const uint8_t> _in;
    size_t _pos = 0;
    bool   _ok  = true;
};

}  // namespace meshroute::wire
