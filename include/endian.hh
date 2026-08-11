#pragma once

#include <cstddef>
#include <cstdint>
#include <bit>
#include <array>

namespace bpt {

    template<typename T, std::endian Endian, std::size_t Alignment = alignof(T)>
    class alignas(Alignment) endian_arithmetic : private std::array<std::byte, sizeof(T)>
    {
        using storage_type = std::array<std::byte, sizeof(T)>;
    public:
        using value_type = T;
        static constexpr std::endian endian = Endian;
        static constexpr std::size_t alignment = Alignment;

        constexpr endian_arithmetic() = default;
        constexpr endian_arithmetic(const endian_arithmetic&) = default;
        constexpr endian_arithmetic& operator=(const endian_arithmetic&) = default;

        constexpr endian_arithmetic(value_type value) noexcept {
            set(value);
        }

        constexpr endian_arithmetic& operator=(value_type value) noexcept {
            set(value);
            return *this;
        }

        constexpr operator value_type() const noexcept {
            return get();
        }

        constexpr void set(value_type value) noexcept {
            if constexpr (endian != std::endian::native) {
                value = std::byteswap(value);
            }
            static_cast<storage_type&>(*this) = std::bit_cast<storage_type>(value);
        }

        constexpr value_type get() const noexcept {
            value_type value = std::bit_cast<value_type>(static_cast<const storage_type&>(*this));
            if constexpr (endian != std::endian::native) {
                value = std::byteswap(value);
            }
            return value;
        }
    };

    using i8le = endian_arithmetic<std::int8_t, std::endian::little>;
    using i16le = endian_arithmetic<std::int16_t, std::endian::little>;
    using i32le = endian_arithmetic<std::int32_t, std::endian::little>;
    using i64le = endian_arithmetic<std::int64_t, std::endian::little>;
    using u8le = endian_arithmetic<std::uint8_t, std::endian::little>;
    using u16le = endian_arithmetic<std::uint16_t, std::endian::little>;
    using u32le = endian_arithmetic<std::uint32_t, std::endian::little>;
    using u64le = endian_arithmetic<std::uint64_t, std::endian::little>;
    using i8be = endian_arithmetic<std::int8_t, std::endian::big>;
    using i16be = endian_arithmetic<std::int16_t, std::endian::big>;
    using i32be = endian_arithmetic<std::int32_t, std::endian::big>;
    using i64be = endian_arithmetic<std::int64_t, std::endian::big>;
    using u8be = endian_arithmetic<std::uint8_t, std::endian::big>;
    using u16be = endian_arithmetic<std::uint16_t, std::endian::big>;
    using u32be = endian_arithmetic<std::uint32_t, std::endian::big>;
    using u64be = endian_arithmetic<std::uint64_t, std::endian::big>;

namespace packed {

    using i8le = endian_arithmetic<std::int8_t, std::endian::little, 1>;
    using i16le = endian_arithmetic<std::int16_t, std::endian::little, 1>;
    using i32le = endian_arithmetic<std::int32_t, std::endian::little, 1>;
    using i64le = endian_arithmetic<std::int64_t, std::endian::little, 1>;
    using u8le = endian_arithmetic<std::uint8_t, std::endian::little, 1>;
    using u16le = endian_arithmetic<std::uint16_t, std::endian::little, 1>;
    using u32le = endian_arithmetic<std::uint32_t, std::endian::little, 1>;
    using u64le = endian_arithmetic<std::uint64_t, std::endian::little, 1>;
    using i8be = endian_arithmetic<std::int8_t, std::endian::big, 1>;
    using i16be = endian_arithmetic<std::int16_t, std::endian::big, 1>;
    using i32be = endian_arithmetic<std::int32_t, std::endian::big, 1>;
    using i64be = endian_arithmetic<std::int64_t, std::endian::big, 1>;
    using u8be = endian_arithmetic<std::uint8_t, std::endian::big, 1>;
    using u16be = endian_arithmetic<std::uint16_t, std::endian::big, 1>;
    using u32be = endian_arithmetic<std::uint32_t, std::endian::big, 1>;
    using u64be = endian_arithmetic<std::uint64_t, std::endian::big, 1>;

} // namespace bpt::packed

} // namespace bpt
