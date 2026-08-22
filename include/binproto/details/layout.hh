#pragma once

#include <cstddef>
#include <limits>
#include <meta>
#include <type_traits>
#include <utility>
#include <memory>
#include <span>
#include <bit>
#include <algorithm>
#include <ranges>
#include <climits>

namespace bpt::details {

    template<typename T, std::meta::info Mem>
    concept member_accessible = std::is_class_v<T>
                             && (is_nonstatic_data_member(Mem) || is_base(Mem))
                             && requires (T& v) { v.[:Mem:]; };

    struct member_offset_info {
        std::size_t offset = 0;
        std::uint8_t bit_offset = 0;
        std::uint8_t group_bit_width = 0;
    };

    // Result of `generate_member_offset_table`: the packed offset of every
    // non-static data member together with the total packed buffer length
    // (the size an equivalent `#pragma pack(Packed)` struct would have,
    // i.e. the end offset rounded up to the struct's effective alignment).
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
                                                         std::endian endian,
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

    consteval auto generate_class_layout(std::meta::info type,
                                         std::endian endian,
                                         std::size_t required_align) {
        const auto layout_members = members_for_layout(type);
        packed_layout result;
        std::vector<member_offset_info> offsets;
        offsets.reserve(layout_members.size());
        std::size_t current_offset = 0;
        std::uint8_t current_bit_offset = 0;
        member_offset_info* group_begin = nullptr;
        member_offset_info* group_end = nullptr;
        auto accumulate_bitfield_group = [&](std::meta::info member = {}) {
            auto finalize_group = [&]() {
                if (group_begin != nullptr) {
                    for (member_offset_info& info : std::span(group_begin, group_end)) {
                        info.group_bit_width = current_bit_offset;
                    }
                    group_begin = nullptr;
                    group_end = nullptr;
                }
                if (current_bit_offset > 0) {
                    current_offset += align_bits_byte(current_bit_offset);
                    current_bit_offset = 0;
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
                offsets.push_back({ .offset = current_offset, .bit_offset = current_bit_offset });
                if (group_begin == nullptr) {
                    group_begin = std::addressof(offsets.back());
                }
                group_end = std::addressof(offsets.back()) + 1;
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
                accumulate_bitfield_group(member);
                continue;
            }
            accumulate_bitfield_group();
            // generate normal member offset info
            const std::size_t natural_align = alignment_of(member);
            const std::size_t effective_align = std::ranges::min(required_align, natural_align);
            current_offset = align_to(current_offset, effective_align);
            offsets.push_back({ .offset = current_offset });
            auto subobj_layout = generate_member_offset_table(type_of(member), endian, required_align);
            current_offset += subobj_layout.total_size;
        }
        accumulate_bitfield_group();
        const std::size_t struct_align = std::ranges::min(required_align, alignment_of(type));
        result.offsets = std::define_static_array(offsets);
        result.total_size = align_to(current_offset, struct_align);
        return result;
    }

    consteval auto generate_array_layout(std::meta::info type,
                                         std::endian endian,
                                         std::size_t required_align) {
        const std::size_t ext = extent(type);
        auto element_layout = generate_member_offset_table(remove_extent(type), endian, required_align);
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
                                                         std::endian endian,
                                                         std::size_t required_align) {
        type = dealias(type);
        if (is_class_type(type)) {
            return generate_class_layout(type, endian, required_align);
        } else if (is_bounded_array_type(type)) {
            return generate_array_layout(type, endian, required_align);
        } else {
            return packed_layout{ .offsets = {}, .total_size = size_of(type) };
        }
    }

    consteval std::size_t get_overall_offset_of_member(std::meta::info type,
                                                       std::endian endian,
                                                       std::size_t packed,
                                                       std::meta::info mem) {
        type = dealias(type);
        if (!is_class_type(type)) {
            throw std::meta::exception("type is not a class type", type);
        }
        if (!is_nonstatic_data_member(mem)) {
            throw std::meta::exception("member is not a non-static data member", mem);
        }
        const auto layout = generate_member_offset_table(type, endian, packed);
        const auto data_members = subobjects_of(type, unprivileged());
        // test direct members first
        for (auto i = 0uz; const auto data_member : data_members) {
            if (data_member == mem) {
                return layout.offsets[i].offset;
            }
            ++i;
        }
        // maybe in base classes
        for (auto i = 0uz; const auto base : bases_of(type, unprivileged())) {
            const auto base_type = type_of(base);
            const bool accessible = extract<bool>(
                substitute(^^member_accessible, { base_type, reflect_constant(mem) }));
            if (accessible) {
                return get_overall_offset_of_member(base_type, endian, packed, mem)
                     + layout.offsets[i].offset;
            }
            ++i;
        }
        // not found
        throw std::meta::exception("member not found in type", mem);
    }

    consteval std::size_t get_bit_field_group_width(std::meta::info member,
                                                    std::endian endian,
                                                    std::size_t packed) {
        const auto parent = parent_of(member);
        const auto member_index = [parent, member] {
            const auto data_members = subobjects_of(parent, unprivileged());
            return std::ranges::find(data_members, member) - data_members.begin();
        }();
        const auto layout = generate_member_offset_table(parent, endian, packed);
        return align_bits_byte(layout.offsets[member_index].group_bit_width);
    }

    template<std::endian Endian, std::size_t N>
    constexpr std::size_t load_group_value(const std::byte* raw) noexcept {
        std::size_t value = 0;
        for (auto i = 0uz; i < N; ++i) {
            const auto byte = std::to_integer<std::size_t>(raw[i]);
            if constexpr (Endian == std::endian::little) {
                value |= byte << (i * CHAR_BIT);
            } else {
                value = (value << CHAR_BIT) | byte;
            }
        }
        return value;
    }

    template<std::endian Endian, std::size_t N>
    constexpr void store_group_value(std::byte* raw, std::size_t value) noexcept {
        for (auto i = 0uz; i < N; ++i) {
            if constexpr (Endian == std::endian::little) {
                raw[i] = static_cast<std::byte>(value >> (i * CHAR_BIT));
            } else {
                raw[N - 1 - i] = static_cast<std::byte>(value >> (i * CHAR_BIT));
            }
        }
    }

    template<typename T>
    using value_rep_type = std::array<std::byte, sizeof(T)>;

    template<typename T, std::endian Endian, std::size_t Packed>
    inline constexpr auto layout_of = generate_member_offset_table(^^T, Endian, Packed);

    template<std::meta::info Mem, std::endian Endian, std::size_t Packed>
        requires (is_bit_field(Mem))
    inline constexpr std::size_t bit_field_group_width_of = get_bit_field_group_width(Mem, Endian, Packed);

    template<typename FloatType>
    using integer_rep_type = [:[] consteval {
        static_assert(std::numeric_limits<FloatType>::is_iec559, "only IEEE 754 floating-point types are supported");
        if constexpr (sizeof(FloatType) == 4) {
            return ^^decltype(std::uint32_t{});
        } else if constexpr (sizeof(FloatType) == 8) {
            return ^^decltype(std::uint64_t{});
        } else {
            throw std::meta::exception("only 32-bit and 64-bit floating-point types are supported", ^^FloatType);
        }
    }():];

    consteval bool is_sane_endian(std::endian endian = std::endian::native) noexcept {
        return (endian == std::endian::little || endian == std::endian::big);
    }

    template<std::meta::info Mem, std::endian Endian, std::size_t Packed, typename T>
        requires (is_bit_field(Mem))
    constexpr void read_sub_bits(T& obj, std::span<const std::byte> raw) noexcept {
        static constexpr auto member = Mem;
        static constexpr auto parent = parent_of(member);
        static constexpr auto member_index = [] consteval {
            const auto data_members = subobjects_of(parent, unprivileged());
            return std::ranges::find(data_members, member) - data_members.begin();
        }();
        static constexpr auto layout = layout_of<typename [:parent:], Endian, Packed>;
        static constexpr auto offset = layout.offsets[member_index];
        static constexpr auto member_width = bit_size_of(member);
        static constexpr auto member_type = type_of(member);
        static constexpr auto group_length = align_bits_byte(offset.group_bit_width);
        static_assert(is_sane_endian() && is_sane_endian(Endian),
                      "only little-endian and big-endian are supported");
        std::size_t group_value = load_group_value<Endian, group_length>(raw.data());
        if constexpr (Endian == std::endian::little) {
            group_value >>= offset.bit_offset;
        } else if constexpr (Endian == std::endian::big) {
            group_value >>= group_length * CHAR_BIT - member_width - offset.bit_offset;
        } else {
            static_assert(false, "cannot reach here");
        }
        group_value &= (1uz << member_width) - 1;
        const auto underlying_value = [&group_value] {
            static constexpr auto type = is_enum_type(member_type)
                ? underlying_type(member_type)
                : member_type;
            if constexpr (is_signed_type(type)) {
                const bool has_sign_bit = (group_value & (1uz << (member_width - 1))) != 0;
                if (has_sign_bit) {
                    group_value |= ~((1uz << member_width) - 1);
                }
                return static_cast<typename [:type:]>(
                    std::bit_cast<std::make_signed_t<std::size_t>>(group_value)
                );
            } else {
                return static_cast<typename [:type:]>(group_value);
            }
        }();
        const auto value = [&underlying_value] {
            if constexpr (is_enum_type(member_type)) {
                return static_cast<typename [:member_type:]>(underlying_value);
            } else {
                return underlying_value;
            }
        }();
        if constexpr (is_class_type(^^T)) {
            static_assert(is_same_type(parent, ^^T),
                          "obj must be of the same type as the parent of the member");
            obj.[:member:] = value;
        } else {
            static_assert(is_same_type(member_type, ^^T),
                          "obj must be of the same type as the member");
            obj = value;
        }
    }

    template<std::meta::info Mem, std::endian Endian, std::size_t Packed, typename T>
        requires (is_bit_field(Mem))
    constexpr void write_sub_bits(const T& obj, std::span<std::byte> raw) noexcept {
        static constexpr auto member = Mem;
        static constexpr auto parent = parent_of(member);
        static constexpr auto member_index = [] consteval {
            const auto data_members = subobjects_of(parent, unprivileged());
            return std::ranges::find(data_members, member) - data_members.begin();
        }();
        static constexpr auto layout = layout_of<typename [:parent:], Endian, Packed>;
        static constexpr auto offset = layout.offsets[member_index];
        static constexpr auto member_width = bit_size_of(member);
        static constexpr auto member_type = type_of(member);
        static constexpr auto group_length = align_bits_byte(offset.group_bit_width);
        static_assert(is_sane_endian() && is_sane_endian(Endian),
                      "only little-endian and big-endian are supported");
        const auto value = [&obj] -> typename [:member_type:] {
            if constexpr (is_class_type(^^T)) {
                static_assert(is_same_type(parent, ^^T),
                              "obj must be of the same type as the parent of the member");
                return obj.[:member:];
            } else {
                static_assert(is_same_type(member_type, ^^T),
                              "obj must be of the same type as the member");
                return obj;
            }
        }();
        const auto underlying_value = [&value] {
            if constexpr (is_enum_type(member_type)) {
                return std::to_underlying(value);
            } else {
                return value;
            }
        }();
        std::size_t group_value = [&underlying_value] {
            if constexpr (is_signed_type(member_type)) {
                return std::bit_cast<std::size_t>(
                    static_cast<std::make_unsigned_t<std::size_t>>(underlying_value)
                );
            } else {
                return static_cast<std::size_t>(underlying_value);
            }
        }();
        static constexpr auto shift = [] {
            if constexpr (Endian == std::endian::little) {
                return offset.bit_offset;
            } else if constexpr (Endian == std::endian::big) {
                return group_length * CHAR_BIT - member_width - offset.bit_offset;
            } else {
                static_assert(false, "cannot reach here");
            }
        }();
        static constexpr auto member_mask = (1uz << member_width) - 1;
        group_value &= member_mask;
        group_value <<= shift;
        std::size_t write_value = load_group_value<Endian, group_length>(raw.data());
        write_value &= ~(member_mask << shift);
        write_value |= group_value;
        store_group_value<Endian, group_length>(raw.data(), write_value);
    }

    template<std::endian Endian, std::size_t Packed, typename T>
    constexpr void read(T& value, std::span<const std::byte> raw) noexcept {
        static constexpr auto type = remove_cvref(^^T);
        static constexpr auto layout = layout_of<T, Endian, Packed>;
        if constexpr (is_class_type(type)) {
            static constexpr auto data_members = std::define_static_array(
                subobjects_of(type, unprivileged()));
            static_assert(data_members.size() == layout.offsets.size(),
                          "data member count mismatch");
            template for (constexpr auto I : std::views::iota(0uz, data_members.size())) {
                static constexpr auto member = data_members[I];
                static constexpr auto offset = layout.offsets[I];
                if constexpr (is_bit_field(member)) {
                    static constexpr auto group_size = align_bits_byte(offset.group_bit_width);
                    read_sub_bits<member, Endian, Packed>(value, raw.subspan(offset.offset, group_size));
                } else {
                    using member_type = [:type_of(member):];
                    static constexpr auto member_size = layout_of<member_type, Endian, Packed>.total_size;
                    read<Endian, Packed>(value.[:member:], raw.subspan(offset.offset, member_size));
                }
            }
        } else if constexpr (is_bounded_array_type(type)) {
            static constexpr auto elem_type = remove_extent(type);
            static constexpr auto elem_size = layout_of<typename [:elem_type:], Endian, Packed>.total_size;
            for (auto index : std::views::iota(0uz, extent(type))) {
                read<Endian, Packed>(value[index], raw.subspan(index * elem_size, elem_size));
            }
        } else {
            static_assert(is_arithmetic_type(type) || is_floating_point_type(type) || is_enum_type(type),
                          "only arithmetic, floating-point, enum, array, and class types are supported");
            value_rep_type<T> buffer [[indeterminate]];
            static_assert(layout.total_size == buffer.size(), "layout total size mismatch");
            std::ranges::copy_n(raw.data(), layout.total_size, buffer.data());
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
    }

    template<std::endian Endian, std::size_t Packed, typename T>
    constexpr void write(const T& value, std::span<std::byte> raw) noexcept {
        static constexpr auto type = remove_cvref(^^T);
        static constexpr auto layout = layout_of<T, Endian, Packed>;
        if constexpr (is_class_type(type)) {
            static constexpr auto data_members
                = std::define_static_array(subobjects_of(type, unprivileged()));
            static_assert(data_members.size() == layout.offsets.size(),
                          "data member count mismatch");
            template for (constexpr auto I : std::views::iota(0uz, data_members.size())) {
                static constexpr auto member = data_members[I];
                static constexpr auto offset = layout.offsets[I];
                if constexpr (is_bit_field(member)) {
                    static constexpr auto group_size = align_bits_byte(offset.group_bit_width);
                    write_sub_bits<member, Endian, Packed>(value, raw.subspan(offset.offset, group_size));
                } else {
                    using member_type = [:type_of(member):];
                    static constexpr auto member_size = layout_of<member_type, Endian, Packed>.total_size;
                    write<Endian, Packed>(value.[:member:], raw.subspan(offset.offset, member_size));
                }
            }
        } else if constexpr (is_bounded_array_type(type)) {
            static constexpr auto elem_type = remove_extent(type);
            static constexpr auto elem_size = layout_of<typename [:elem_type:], Endian, Packed>.total_size;
            for (auto index : std::views::iota(0uz, extent(type))) {
                write<Endian, Packed>(value[index], raw.subspan(index * elem_size, elem_size));
            }
        } else {
            static_assert(is_arithmetic_type(type) || is_floating_point_type(type) || is_enum_type(type),
                          "only arithmetic, floating-point, enum, array, and class types are supported");
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
            std::ranges::copy_n(buffer.data(), layout.total_size, raw.data());
        }
    }

} // namespace bpt::details
