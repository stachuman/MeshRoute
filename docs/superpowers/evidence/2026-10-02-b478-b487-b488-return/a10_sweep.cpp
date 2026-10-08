// Author: Stanislaw Kozicki <cgpsmapper@gmail.com>
// B492 / A10 proof 1 — the memory-safety sweep of ONE arm of `ui_fmt_identity` (the header this TU sees first).
// Every call writes into ONE padded backing array: PAD guard bytes, the 48-byte capacity window, PAD guard bytes. An
// arm that writes outside its declared capacity therefore lands on a guard INSIDE this array — observed, never a
// physical out-of-bounds access. Prints one summary line; writes every call's 48-byte window to argv[1] for the
// cross-arm comparison. Name lengths 0-32, budgets 0-32, capacities 0-48, key hashes {0, 0xA0000011}.
#include <firmware_ui_model.h>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    static const char kName[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";       // 32 counted bytes
    static const uint32_t kHashes[] = {0u, 0xA0000011u};
    constexpr std::size_t kPad = 64, kCapMax = 48;
    constexpr unsigned char kGuard = 0xA5;
    static unsigned char backing[kPad + kCapMax + kPad];
    FILE* dump = argc > 1 ? std::fopen(argv[1], "wb") : nullptr;
    unsigned long calls = 0, zero_cap_touched = 0, no_nul = 0, outside = 0, outside_bytes = 0;
    long max_over = 0;   // furthest byte past the declared capacity that changed
    char first[3][96] = {};
    int nfirst = 0;
    for (uint32_t hash : kHashes)
        for (unsigned len = 0; len <= 32; ++len)
            for (unsigned cols = 0; cols <= 32; ++cols)
                for (std::size_t cap = 0; cap <= kCapMax; ++cap) {
                    std::memset(backing, kGuard, sizeof backing);
                    char* out = reinterpret_cast<char*>(backing) + kPad;
                    mrui::ui_fmt_identity(out, cap, len ? kName : nullptr, uint8_t(len), hash, uint8_t(cols));
                    ++calls;
                    bool bad = false;
                    if (cap == 0) {
                        for (unsigned char c : backing) if (c != kGuard) { bad = true; ++zero_cap_touched; break; }
                    } else {
                        if (!std::memchr(out, 0, cap)) { ++no_nul; bad = true; }
                        unsigned long here = 0;
                        for (std::size_t i = 0; i < sizeof backing; ++i) {
                            const bool inside = i >= kPad && i < kPad + cap;
                            if (!inside && backing[i] != kGuard) {
                                ++here;
                                if (i >= kPad + cap && long(i - kPad - cap) + 1 > max_over) max_over = long(i - kPad - cap) + 1;
                            }
                        }
                        if (here) { ++outside; outside_bytes += here; bad = true; }
                    }
                    if (bad && nfirst < 3)
                        std::snprintf(first[nfirst++], sizeof first[0], "len=%u cols=%u cap=%zu hash=%08lx", len, cols, cap,
                                      (unsigned long)hash);
                    if (dump) std::fwrite(backing + kPad, 1, kCapMax, dump);
                }
    if (dump) std::fclose(dump);
    std::printf("{\"arm\":\"%s\",\"calls\":%lu,\"cap0_touched\":%lu,\"positive_cap_without_nul\":%lu,"
                "\"calls_writing_outside_capacity\":%lu,\"bytes_outside\":%lu,\"max_bytes_past_capacity\":%ld,"
                "\"first\":[\"%s\",\"%s\",\"%s\"]}\n", MR_A10_ARM, calls, zero_cap_touched, no_nul, outside, outside_bytes,
                max_over, first[0], first[1], first[2]);
    return 0;
}
