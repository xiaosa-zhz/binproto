#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <string_view>

// Small helpers shared by the real-world format tests.
namespace testutil {

    // Convert a raw byte literal into a std::array<std::byte, N> golden buffer.
    template<std::size_t N>
    constexpr std::array<std::byte, N> to_bytes(const std::uint8_t (&values)[N]) noexcept {
        std::array<std::byte, N> out{};
        for (std::size_t i = 0; i < N; ++i) {
            out[i] = std::byte{values[i]};
        }
        return out;
    }

    // Compare the leading N bytes of a wire buffer against the golden bytes,
    // reporting the first mismatching offset. Returns true on full match.
    template<std::size_t N>
    bool expect_bytes(const std::span<const std::byte> actual,
                      const std::array<std::byte, N>& expected,
                      std::string_view name,
                      std::string_view detail) {
        if (actual.size() < N) {
            std::println("[{:<15} {:<13}] buffer too small: {} < {}",
                         name, detail, actual.size(), N);
            return false;
        }
        for (std::size_t i = 0; i < N; ++i) {
            if (actual[i] != expected[i]) {
                std::println("[{:<15} {:<13}] byte {:>2} mismatch: got {:#04x}, expected {:#04x}",
                             name, detail, i,
                             std::to_integer<unsigned>(actual[i]),
                             std::to_integer<unsigned>(expected[i]));
                return false;
            }
        }
        return true;
    }

} // namespace testutil
