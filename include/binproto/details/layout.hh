#pragma once

#include <cstddef>
#include <climits>
#include <cstring>
#include <limits>
#include <meta>
#include <type_traits>
#include <utility>
#include <span>
#include <bit>
#include <algorithm>
#include <ranges>

namespace bpt::details {

    template<typename T, std::meta::info Mem>
    concept member_accessible = std::is_class_v<T>
                             && (is_nonstatic_data_member(Mem) || is_base(Mem))
                             && requires (T& v) { v.[:Mem:]; };

    consteval bool is_fundamental(std::meta::info type) noexcept {
        return is_arithmetic_type(type) || is_enum_type(type);
    }
    
    template<typename T>
    concept fundamental = (is_fundamental(^^T));

    struct bit_field_info {
        std::size_t index = 0;
        std::size_t bit_offset = 0;
    };

    struct member_offset_info {
        std::size_t index = 0;
        std::size_t offset = 0;
        std::size_t group_bit_width = 0;
        const bit_field_info* bit_field_group_data = nullptr;
        std::size_t bit_field_group_size = 0;

        constexpr std::span<const bit_field_info> get_bit_field_group() const noexcept {
            return std::span(bit_field_group_data, bit_field_group_size);
        }
    };

    struct packed_layout {
        std::span<const member_offset_info> offsets;
        std::size_t total_size = 0;
    };

    constexpr std::size_t align_to(std::size_t offset, std::size_t align) noexcept {
        return (offset + align - 1) / align * align;
    }

    constexpr std::size_t align_bits_byte(std::uint8_t bits) noexcept {
        return (bits + CHAR_BIT - 1) / CHAR_BIT;
    }

    // Forward declaration
    consteval packed_layout generate_member_offset_table(std::meta::info type,
                                                         std::size_t required_align);

    consteval std::meta::access_context unprivileged() noexcept {
        return std::meta::access_context::unprivileged();
    }

    // This function returns all bases and fields that contribute to the wire format layout,
    // including all unnamed bit-fields.
    consteval auto members_for_layout(std::meta::info type) {
        auto result = bases_of(type, unprivileged());
        for (auto member : members_of(type, unprivileged())) {
            if (is_nonstatic_data_member(member) || is_bit_field(member)) {
                result.push_back(member);
            }
        }
        return result;
    }

    consteval auto generate_class_layout(std::meta::info type, std::size_t required_align) {
        const auto layout_members = members_for_layout(type);
        packed_layout result;
        std::vector<member_offset_info> offsets;
        offsets.reserve(layout_members.size());
        std::size_t index = 0;
        std::size_t current_offset = 0;
        std::size_t current_bit_offset = 0;
        std::vector<bit_field_info> bit_field_group;
        auto accumulate_bit_field_group = [&](std::meta::info member = {}) {
            auto finalize_group = [&] {
                if (!bit_field_group.empty()) {
                    auto group_data = std::define_static_array(bit_field_group);
                    offsets.push_back({
                        .index = index,
                        .offset = current_offset,
                        .group_bit_width = current_bit_offset,
                        .bit_field_group_data = group_data.data(),
                        .bit_field_group_size = group_data.size(),
                    });
                    current_offset += align_bits_byte(current_bit_offset);
                    current_bit_offset = 0;
                    bit_field_group.clear();
                }
            };
            if (member == std::meta::info{}) {
                finalize_group();
                return;
            }
            const std::size_t bit_width = bit_size_of(member);
            if (bit_width == 0) {
                finalize_group();
                return;
            }
            if (bit_width >= std::numeric_limits<std::size_t>::digits) {
                throw std::meta::exception(
                    "bit-field width exceeds the width of size_t",
                    member);
            }
            if (current_bit_offset + bit_width > std::numeric_limits<std::size_t>::digits) {
                throw std::meta::exception(
                    "consecutive bit-field group exceeds the width of size_t",
                    member);
            }
            if (bit_width == 1 && is_signed_type(type_of(member))) {
                throw std::meta::exception(
                    "signed bit-field of width 1 is not supported",
                    member);
            }
            if (current_bit_offset == 0) {
                const std::size_t natural_align = alignment_of(type_of(member));
                const std::size_t effective_align = std::ranges::min(required_align, natural_align);
                current_offset = align_to(current_offset, effective_align);
            }
            if (has_identifier(member)) {
                bit_field_group.push_back({ .index = index, .bit_offset = current_bit_offset });
                ++index;
            }
            current_bit_offset += bit_width;
        };
        for (const auto member : layout_members) {
            if (is_const_type(type_of(member))) {
                throw std::meta::exception(
                    "const-qualified member is not supported",
                    member);
            }
            if (is_bit_field(member)) {
                accumulate_bit_field_group(member);
                continue;
            }
            accumulate_bit_field_group();
            // generate normal member offset info
            const std::size_t natural_align = alignment_of(member);
            const std::size_t effective_align = std::ranges::min(required_align, natural_align);
            current_offset = align_to(current_offset, effective_align);
            offsets.push_back({ .index = index, .offset = current_offset });
            const auto subobj_layout
                = generate_member_offset_table(type_of(member), required_align);
            current_offset += subobj_layout.total_size;
            ++index;
        }
        accumulate_bit_field_group();
        const std::size_t struct_align = std::ranges::min(required_align, alignment_of(type));
        result.offsets = std::define_static_array(offsets);
        result.total_size = align_to(current_offset, struct_align);
        return result;
    }

    consteval auto generate_array_layout(std::meta::info type, std::size_t required_align) {
        const std::size_t ext = extent(type);
        const auto element_layout
            = generate_member_offset_table(remove_extent(type), required_align);
        packed_layout result;
        const std::size_t stride = element_layout.total_size;
        std::vector<member_offset_info> offsets(ext);
        std::ranges::generate(offsets, [stride, n = 0uz] mutable {
            return member_offset_info{ .offset = n++ * stride };
        });
        result.offsets = std::define_static_array(offsets);
        result.total_size = ext * stride;
        return result;
    }

    consteval packed_layout generate_member_offset_table(std::meta::info type,
                                                         std::size_t required_align) {
        type = dealias(type);
        if (is_class_type(type)) {
            return generate_class_layout(type, required_align);
        } else if (is_bounded_array_type(type)) {
            return generate_array_layout(type, required_align);
        } else {
            return packed_layout{ .offsets = {}, .total_size = size_of(type) };
        }
    }

    consteval std::size_t get_overall_offset_of_member(std::meta::info type,
                                                       std::size_t packed,
                                                       std::meta::info mem) {
        type = dealias(type);
        if (!is_class_type(type)) {
            throw std::meta::exception("type is not a class type", type);
        }
        if (!is_nonstatic_data_member(mem)) {
            throw std::meta::exception("member is not a non-static data member", mem);
        }
        const auto layout = generate_member_offset_table(type, packed);
        const auto data_members = subobjects_of(type, unprivileged());
        // test direct members first
        for (const auto offset : layout.offsets) {
            if (offset.group_bit_width > 0) {
                for (auto info : offset.get_bit_field_group()) {
                    if (data_members[info.index] == mem) {
                        return offset.offset;
                    }
                }
            } else if (data_members[offset.index] == mem) {
                return offset.offset;
            }
        }
        // maybe in base classes
        for (auto i = 0uz; const auto base : bases_of(type, unprivileged())) {
            const auto base_type = type_of(base);
            const bool accessible = extract<bool>(
                substitute(^^member_accessible, { base_type, reflect_constant(mem) }));
            if (accessible) {
                return get_overall_offset_of_member(base_type, packed, mem)
                     + layout.offsets[i].offset;
            }
            ++i;
        }
        // not found
        throw std::meta::exception("member not found in type", mem);
    }

    struct bit_field_desc {
        std::size_t bit_offset = 0;
        std::size_t group_bit_width = 0;
    };

    consteval bit_field_desc get_bit_field_group_desc(std::meta::info mem, std::size_t packed) {
        const auto parent = parent_of(mem);
        const auto members = subobjects_of(parent, unprivileged());
        const auto layout = generate_member_offset_table(parent, packed);
        for (auto offset : layout.offsets) {
            if (offset.group_bit_width == 0) {
                continue;
            }
            const auto group = offset.get_bit_field_group();
            for (auto info : group) {
                if (members[info.index] == mem) {
                    return {
                        .bit_offset = info.bit_offset,
                        .group_bit_width = offset.group_bit_width,
                    };
                }
            }
        }
        throw std::meta::exception("member is not a bit-field or part of a bit-field group", mem);
    }

    template<std::size_t NBits>
    using minimal_unsigned_type = [:[] consteval {
        if constexpr (NBits <= 8) {
            return ^^decltype(std::uint8_t{});
        } else if constexpr (NBits <= 16) {
            return ^^decltype(std::uint16_t{});
        } else if constexpr (NBits <= 32) {
            return ^^decltype(std::uint32_t{});
        } else if constexpr (NBits <= 64) {
            return ^^decltype(std::uint64_t{});
        } else {
            return ^^decltype(std::size_t{});
        }
    }():];

    template<typename T, std::size_t N>
    constexpr T low_bits_mask() noexcept {
        static_assert(N <= std::numeric_limits<T>::digits);
        return static_cast<T>(std::numeric_limits<T>::max() >> (std::numeric_limits<T>::digits - N));
    }

    template<std::endian Endian, std::size_t N>
    constexpr minimal_unsigned_type<N * CHAR_BIT> load_group_value(const std::byte* raw) noexcept {
        using value_type = minimal_unsigned_type<N * CHAR_BIT>;
        static_assert(N > 0, "group size must be greater than 0");
        value_type value = 0;
        if consteval {
            for (auto i = 0uz; i < N; ++i) {
                const auto byte = std::to_integer<value_type>(raw[i]);
                if constexpr (Endian == std::endian::little) {
                    value = static_cast<value_type>(value | (byte << (i * CHAR_BIT)));
                } else if constexpr (Endian == std::endian::big) {
                    value = static_cast<value_type>((value << CHAR_BIT) | byte);
                } else {
                    static_assert(false, "cannot reach here");
                }
            }
        } else {
            std::memcpy(&value, raw, N);
            if constexpr (Endian != std::endian::native) {
                value = std::byteswap(value);
            }
            if constexpr (Endian == std::endian::big) {
                value >>= ((sizeof(value_type) - N) * CHAR_BIT);
            }
        }
        return value;
    }

    template<std::endian Endian, std::size_t N>
    constexpr void store_group_value(std::byte* raw, minimal_unsigned_type<N * CHAR_BIT> value) noexcept {
        using value_type = minimal_unsigned_type<N * CHAR_BIT>;
        if consteval {
            for (auto i = 0uz; i < N; ++i) {
                if constexpr (Endian == std::endian::little) {
                    raw[i] = static_cast<std::byte>(value >> (i * CHAR_BIT));
                } else if constexpr (Endian == std::endian::big) {
                    raw[N - 1 - i] = static_cast<std::byte>(value >> (i * CHAR_BIT));
                } else {
                    static_assert(false, "cannot reach here");
                }
            }
        } else {
            if constexpr (Endian == std::endian::big) {
                value <<= ((sizeof(value_type) - N) * CHAR_BIT);
            }
            if constexpr (Endian != std::endian::native) {
                value = std::byteswap(value);
            }
            std::memcpy(raw, &value, N);
        }
    }

    template<typename T>
    using value_rep_type = std::array<std::byte, sizeof(T)>;

    template<typename T, std::size_t Packed>
    inline constexpr auto layout_of = generate_member_offset_table(^^T, Packed);

    template<std::meta::info Mem, std::size_t Packed>
        requires (is_bit_field(Mem))
    inline constexpr auto bit_field_group_desc_of = get_bit_field_group_desc(Mem, Packed);

    template<typename FloatType>
    using integer_rep_type = [:[] consteval {
        static_assert(std::numeric_limits<FloatType>::is_iec559,
                      "only IEEE 754 floating-point types are supported");
        if constexpr (sizeof(FloatType) == 4) {
            return ^^decltype(std::uint32_t{});
        } else if constexpr (sizeof(FloatType) == 8) {
            return ^^decltype(std::uint64_t{});
        } else {
            static_assert(false, "only 32-bit and 64-bit floating-point types are supported");
        }
    }():];

    consteval bool is_sane_endian(std::endian endian = std::endian::native) noexcept {
        return endian == std::endian::little || endian == std::endian::big;
    }

    template<std::endian Endian>
    constexpr std::size_t bit_field_shift(std::size_t group_length,
                                          std::size_t member_width,
                                          std::size_t bit_offset) noexcept {
        if constexpr (Endian == std::endian::little) {
            return bit_offset;
        } else if constexpr (Endian == std::endian::big) {
            return group_length * CHAR_BIT - member_width - bit_offset;
        } else {
            static_assert(false, "unsupported endian");
        }
    }

    template<std::meta::info Mem, std::size_t N = bit_size_of(Mem)>
    constexpr auto to_member_value(minimal_unsigned_type<N> raw_bits) noexcept {
        static constexpr auto member_width = bit_size_of(Mem);
        static constexpr auto member_type = type_of(Mem);
        static constexpr auto value_info = is_enum_type(member_type)
            ? underlying_type(member_type)
            : member_type;
        using raw_type = minimal_unsigned_type<N>;
        auto value = [&raw_bits] {
            if constexpr (is_signed_type(value_info)) {
                // sign-extend
                static constexpr auto mask = low_bits_mask<raw_type, member_width>();
                const raw_type fill = static_cast<raw_type>(
                    raw_type{0} - static_cast<raw_type>(raw_bits >> (member_width - 1))
                );
                const raw_type extended = static_cast<raw_type>(
                    raw_bits | (fill & static_cast<raw_type>(~mask))
                );
                return static_cast<typename [:value_info:]>(
                    std::bit_cast<std::make_signed_t<raw_type>>(extended));
            } else {
                return static_cast<typename [:value_info:]>(raw_bits);
            }
        }();
        if constexpr (is_enum_type(member_type)) {
            return static_cast<typename [:member_type:]>(value);
        } else {
            return value;
        }
    }

    template<std::meta::info Mem, typename T>
    constexpr auto member_value_to_bits(T value) noexcept {
        static constexpr auto member_type = type_of(Mem);
        static constexpr auto member_width = bit_size_of(Mem);
        using raw_type = minimal_unsigned_type<member_width>;
        static_assert(is_same_type(member_type, ^^T),
                      "value must be of the same type as the member");
        static constexpr auto mask = low_bits_mask<raw_type, member_width>();
        const auto underlying = [&value] {
            if constexpr (is_enum_type(member_type)) {
                return std::to_underlying(value);
            } else {
                return value;
            }
        }();
        return static_cast<raw_type>(static_cast<raw_type>(underlying) & mask);
    }

    template<std::meta::info Mem, std::endian Endian, std::size_t Packed, typename T>
        requires (is_bit_field(Mem))
    constexpr void read_sub_bits(T& obj, std::span<const std::byte> raw) noexcept {
        static constexpr auto member = Mem;
        static constexpr auto [bit_offset, group_bit_width] = bit_field_group_desc_of<Mem, Packed>;
        static constexpr auto member_width = bit_size_of(member);
        static constexpr auto member_type = type_of(member);
        static constexpr auto group_length = align_bits_byte(group_bit_width);
        static_assert(is_sane_endian() && is_sane_endian(Endian),
                      "only little-endian and big-endian are supported");
        static_assert(is_same_type(member_type, ^^T),
                      "obj must be of the same type as the member");
        static constexpr auto shift
            = bit_field_shift<Endian>(group_length, member_width, bit_offset);
        using group_type = minimal_unsigned_type<group_bit_width>;
        static constexpr auto mask = low_bits_mask<group_type, member_width>();
        const group_type raw_bits = static_cast<group_type>(
            (load_group_value<Endian, group_length>(raw.data()) >> shift) & mask
        );
        obj = to_member_value<member>(
            static_cast<minimal_unsigned_type<member_width>>(raw_bits));
    }

    template<std::meta::info Mem, std::endian Endian, std::size_t Packed, typename T>
        requires (is_bit_field(Mem))
    constexpr void write_sub_bits(const T& obj, std::span<std::byte> raw) noexcept {
        static constexpr auto member = Mem;
        static constexpr auto [bit_offset, group_bit_width] = bit_field_group_desc_of<Mem, Packed>;
        static constexpr auto member_width = bit_size_of(member);
        static constexpr auto member_type = type_of(member);
        static constexpr auto group_length = align_bits_byte(group_bit_width);
        static constexpr auto shift
            = bit_field_shift<Endian>(group_length, member_width, bit_offset);
        static_assert(is_sane_endian() && is_sane_endian(Endian),
                      "only little-endian and big-endian are supported");
        static_assert(is_same_type(member_type, ^^T),
                      "obj must be of the same type as the member");
        using group_type = minimal_unsigned_type<group_bit_width>;
        static constexpr auto member_mask = low_bits_mask<group_type, member_width>();
        group_type group_value = load_group_value<Endian, group_length>(raw.data());
        const auto member_bits = member_value_to_bits<member>(obj);
        group_value &= static_cast<group_type>(~(member_mask << shift));
        group_value |= static_cast<group_type>(
            (static_cast<group_type>(member_bits) & member_mask) << shift
        );
        store_group_value<Endian, group_length>(raw.data(), group_value);
    }

    template<member_offset_info Info, std::endian Endian, std::size_t Packed, typename T>
    constexpr void read_sub_bits_group(T& obj, std::span<const std::byte> raw) noexcept {
        static constexpr auto bit_field_group = Info.get_bit_field_group();
        static constexpr auto data_members
            = std::define_static_array(subobjects_of(^^T, unprivileged()));
        static constexpr auto group_bit_width = Info.group_bit_width;
        static constexpr auto group_length = align_bits_byte(group_bit_width);
        static_assert(is_sane_endian() && is_sane_endian(Endian),
                      "only little-endian and big-endian are supported");
        using group_type = minimal_unsigned_type<group_bit_width>;
        const group_type group_value = load_group_value<Endian, group_length>(raw.data());
        template for (constexpr auto info : bit_field_group) {
            static constexpr auto member = data_members[info.index];
            static constexpr auto member_width = bit_size_of(member);
            static constexpr auto shift
                = bit_field_shift<Endian>(group_length, member_width, info.bit_offset);
            const group_type raw_bits = static_cast<group_type>(
                (group_value >> shift) & low_bits_mask<group_type, member_width>());
            using rep_type = minimal_unsigned_type<member_width>;
            obj.[:member:] = to_member_value<member>(static_cast<rep_type>(raw_bits));
        }
    }

    template<member_offset_info Info, std::endian Endian, std::size_t Packed, typename T>
    constexpr void write_sub_bits_group(const T& obj, std::span<std::byte> raw) noexcept {
        static constexpr auto bit_field_group = Info.get_bit_field_group();
        static constexpr auto data_members
            = std::define_static_array(subobjects_of(^^T, unprivileged()));
        static constexpr auto group_bit_width = Info.group_bit_width;
        static constexpr auto group_length = align_bits_byte(group_bit_width);
        static constexpr bool full_coverage = [] consteval {
            if (group_bit_width % CHAR_BIT != 0) {
                return false;
            }
            return std::ranges::fold_left(bit_field_group, 0uz, [](auto s, auto info) {
                return s + bit_size_of(data_members[info.index]);
            }) == group_bit_width;
        }();
        static_assert(is_sane_endian() && is_sane_endian(Endian),
                      "only little-endian and big-endian are supported");
        using group_type = minimal_unsigned_type<group_bit_width>;
        group_type group_value = 0;
        if constexpr (!full_coverage) {
            group_value = load_group_value<Endian, group_length>(raw.data());
        }
        template for (constexpr auto info : bit_field_group) {
            static constexpr auto member = data_members[info.index];
            static constexpr auto member_width = bit_size_of(member);
            static constexpr auto shift
                = bit_field_shift<Endian>(group_length, member_width, info.bit_offset);
            static constexpr auto member_mask = low_bits_mask<group_type, member_width>();
            const auto member_bits = member_value_to_bits<member>(obj.[:member:]);
            group_value &= static_cast<group_type>(~(member_mask << shift));
            group_value |= static_cast<group_type>(
                (static_cast<group_type>(member_bits) & member_mask) << shift
            );
        }
        store_group_value<Endian, group_length>(raw.data(), group_value);
    }

    template<std::endian Endian, details::fundamental T>
    constexpr void read_from_bytes(T& value, std::span<const std::byte> raw) noexcept {
        static constexpr auto type = remove_cvref(^^T);
        value_rep_type<T> buffer [[indeterminate]];
        std::ranges::copy_n(raw.data(), buffer.size(), buffer.data());
        if constexpr (Endian != std::endian::native) {
            if constexpr (is_floating_point_type(type)) {
                value = std::bit_cast<T>(std::byteswap(
                    std::bit_cast<integer_rep_type<T>>(buffer)));
            } else if constexpr (is_enum_type(type)) {
                value = static_cast<T>(std::byteswap(
                    std::bit_cast<std::underlying_type_t<T>>(buffer)));
            } else {
                value = std::byteswap(std::bit_cast<T>(buffer));
            }
        } else {
            value = std::bit_cast<T>(buffer);
        }
    }

    template<std::endian Endian, details::fundamental T>
    constexpr void write_to_bytes(const T& value, std::span<std::byte> raw) noexcept {
        static constexpr auto type = remove_cvref(^^T);
        auto buffer = [&value] {
            if constexpr (Endian != std::endian::native) {
                if constexpr (is_floating_point_type(type)) {
                    return std::bit_cast<value_rep_type<T>>(std::byteswap(
                        std::bit_cast<integer_rep_type<T>>(value)));
                } else if constexpr (is_enum_type(type)) {
                    return std::bit_cast<value_rep_type<T>>(std::byteswap(
                        std::to_underlying(value)));
                } else {
                    return std::bit_cast<value_rep_type<T>>(std::byteswap(value));
                }
            } else {
                return std::bit_cast<value_rep_type<T>>(value);
            }
        }();
        std::ranges::copy_n(buffer.data(), buffer.size(), raw.data());
    }

    template<std::endian Endian, std::size_t Packed, typename T>
    constexpr void read(T& value, std::span<const std::byte> raw) noexcept {
        static constexpr auto type = remove_cvref(^^T);
        static constexpr auto layout = layout_of<T, Packed>;
        if constexpr (is_fundamental(type)) {
            read_from_bytes<Endian>(value, raw.subspan(0, layout.total_size));
        } else if constexpr (is_class_type(type)) {
            template for (constexpr auto info : layout.offsets) {
                if constexpr (info.group_bit_width > 0) {
                    read_sub_bits_group<info, Endian, Packed>(
                        value, raw.subspan(info.offset, align_bits_byte(info.group_bit_width)));
                } else {
                    static constexpr auto member = subobjects_of(type, unprivileged())[info.index];
                    using member_type = [:type_of(member):];
                    static constexpr auto member_size = layout_of<member_type, Packed>.total_size;
                    read<Endian, Packed>(value.[:member:], raw.subspan(info.offset, member_size));
                }
            }
        } else if constexpr (is_bounded_array_type(type)) {
            static constexpr auto elem_type = remove_extent(type);
            static constexpr auto elem_size = layout_of<typename [:elem_type:], Packed>.total_size;
            for (auto index : std::views::iota(0uz, extent(type))) {
                read<Endian, Packed>(value[index], raw.subspan(index * elem_size, elem_size));
            }
        } else {
            static_assert(false, "only arithmetic, enum, array, and class types are supported");
        }
    }

    template<std::endian Endian, std::size_t Packed, typename T>
    constexpr void write(const T& value, std::span<std::byte> raw) noexcept {
        static constexpr auto type = remove_cvref(^^T);
        static constexpr auto layout = layout_of<T, Packed>;
        if constexpr (is_fundamental(type)) {
            write_to_bytes<Endian>(value, raw.subspan(0, layout.total_size));
        } else if constexpr (is_class_type(type)) {
            template for (constexpr auto info : layout.offsets) {
                if constexpr (info.group_bit_width > 0) {
                    write_sub_bits_group<info, Endian, Packed>(
                        value, raw.subspan(info.offset, align_bits_byte(info.group_bit_width)));
                } else {
                    static constexpr auto member = subobjects_of(type, unprivileged())[info.index];
                    using member_type = [:type_of(member):];
                    static constexpr auto member_size = layout_of<member_type, Packed>.total_size;
                    write<Endian, Packed>(value.[:member:], raw.subspan(info.offset, member_size));
                }
            }
        } else if constexpr (is_bounded_array_type(type)) {
            static constexpr auto elem_type = remove_extent(type);
            static constexpr auto elem_size = layout_of<typename [:elem_type:], Packed>.total_size;
            for (auto index : std::views::iota(0uz, extent(type))) {
                write<Endian, Packed>(value[index], raw.subspan(index * elem_size, elem_size));
            }
        } else {
            static_assert(false, "only arithmetic, enum, array, and class types are supported");
        }
    }

} // namespace bpt::details
