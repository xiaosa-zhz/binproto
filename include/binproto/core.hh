#pragma once

#include "details/layout.hh"
#include <cstddef>

namespace bpt {

namespace details {

template<typename View>
struct bv_traits {};

template<template<typename, std::endian, std::size_t> class VTMP, typename T, std::endian E, std::size_t P>
struct bv_traits<VTMP<T, E, P>> {
    using value_type = T;
    static constexpr std::endian endian = E;
    static constexpr std::size_t packed = P;
    template<typename U>
    using rebind = VTMP<U, E, P>;
};

template<std::meta::info Mem>
using member_type = [:type_of(Mem):];

template<typename Derived>
class view_base
{
    using view_type = Derived;
    using traits = bv_traits<view_type>;
    using value_type = traits::value_type;
    static constexpr auto endian = traits::endian;
    static constexpr auto packed = traits::packed;

    template<std::meta::info Mem>
    using rebind_mem_view = traits::template rebind<member_type<Mem>>;

    template<typename T>
    using rebind_type_view = traits::template rebind<T>;

public:
    [[nodiscard]]
    static consteval std::size_t wire_size() noexcept {
        return details::layout_of<value_type, packed>.total_size;
    }

    template<std::meta::info Mem>
        requires details::member_accessible<value_type, Mem>
    [[nodiscard]] static consteval std::meta::member_offset offset_of() noexcept {
        static constexpr auto group_offset
            = details::get_overall_offset_of_member(^^value_type, packed, Mem);
        if constexpr (is_bit_field(Mem)) {
            static constexpr auto bit_offset
                = details::bit_field_group_desc_of<Mem, packed>.bit_offset;
            return {
                .bytes = static_cast<std::ptrdiff_t>(group_offset + (bit_offset / CHAR_BIT)),
                .bits = static_cast<std::ptrdiff_t>(bit_offset % CHAR_BIT),
            };
        } else {
            return { .bytes = static_cast<std::ptrdiff_t>(group_offset), .bits = 0 };
        }
    }

    template<std::meta::info Mem>
        requires details::member_accessible<value_type, Mem> && (!is_bit_field(Mem))
    [[nodiscard]] constexpr rebind_mem_view<Mem> subview() const noexcept {
        static constexpr auto offset
            = details::get_overall_offset_of_member(^^value_type, packed, Mem);
        static constexpr auto member_size
            = details::layout_of<member_type<Mem>, packed>.total_size;
        const auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < offset + member_size) {
            [[unlikely]] return {};
        }
        return raw.subspan(offset, member_size);
    }

    [[nodiscard]] constexpr auto consumed() const noexcept {
        auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < wire_size()) {
            [[unlikely]] return decltype(raw){};
        }
        return raw.subspan(0, wire_size());
    }

    [[nodiscard]] constexpr auto remained() const noexcept {
        auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < wire_size()) {
            [[unlikely]] return decltype(raw){};
        }
        return raw.subspan(wire_size());
    }

    [[nodiscard]] constexpr view_type consumed_view() const noexcept {
        auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < wire_size()) {
            [[unlikely]] return {};
        }
        return raw.subspan(0, wire_size());
    }

    template<typename Succeeded>
    [[nodiscard]] constexpr rebind_type_view<Succeeded> remained_view() const noexcept {
        auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < wire_size()) {
            [[unlikely]] return {};
        }
        return raw.subspan(wire_size());
    }

    [[nodiscard]]
    constexpr bool read(value_type& value) const noexcept {
        const auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < wire_size()) {
            [[unlikely]] return false;
        }
        details::read<endian, packed>(value, raw.subspan(0, wire_size()));
        return true;
    }

    template<std::meta::info Mem>
        requires details::member_accessible<value_type, Mem>
    [[nodiscard]] constexpr bool read(member_type<Mem>& value) const noexcept {
        if constexpr (is_bit_field(Mem)) {
            static constexpr std::size_t offset
                = details::get_overall_offset_of_member(^^value_type, packed, Mem);
            static constexpr std::size_t group_size = details::align_bits_byte(
                details::bit_field_group_desc_of<Mem, packed>.group_bit_width);
            const auto raw = ((const view_type*)this)->buffer();
            if (raw.size() < offset + group_size) {
                [[unlikely]] return false;
            }
            details::read_sub_bits<Mem, endian, packed>(value, raw.subspan(offset, group_size));
            return true;
        } else {
            return subview<Mem>().read(value);
        }
    }
};

} // namespace details

template<typename ValueType, std::endian GlobalEndian = std::endian::native, std::size_t Packed = 1uz>
class binary_view : private details::view_base<binary_view<ValueType, GlobalEndian, Packed>>
{
    using base = details::view_base<binary_view>;
public:
    using value_type = ValueType;
    using buffer_type = std::span<std::byte>;
    static constexpr std::endian endian = GlobalEndian;
    static constexpr std::size_t packed = Packed;

    constexpr binary_view() = default;
    constexpr binary_view(const binary_view&) = default;
    constexpr binary_view& operator=(const binary_view&) = default;

    // binary_view do not check if the raw data is enough for the value during construction,
    // only when actually reading or writing the value, it will check if the raw data is enough.
    constexpr binary_view(buffer_type raw) noexcept : raw(raw) {}

    using base::wire_size;
    using base::offset_of;
    using base::subview;
    using base::consumed;
    using base::remained;
    using base::consumed_view;
    using base::remained_view;
    using base::read;

    [[nodiscard]]
    constexpr bool write(const value_type& value) const noexcept {
        if (raw.size() < wire_size()) {
            [[unlikely]] return false;
        }
        details::write<endian, packed>(value, raw.subspan(0, wire_size()));
        return true;
    }

    template<std::meta::info Mem>
        requires details::member_accessible<value_type, Mem>
    [[nodiscard]] constexpr bool write(typename [:add_const(type_of(Mem)):]& value) const noexcept {
        if constexpr (is_bit_field(Mem)) {
            static constexpr std::size_t offset
                = details::get_overall_offset_of_member(^^value_type, packed, Mem);
            static constexpr std::size_t group_size = details::align_bits_byte(
                details::bit_field_group_desc_of<Mem, packed>.group_bit_width);
            if (raw.size() < offset + group_size) {
                [[unlikely]] return false;
            }
            details::write_sub_bits<Mem, endian, packed>(value, raw.subspan(offset, group_size));
            return true;
        } else {
            return this->template subview<Mem>().write(value);
        }
    }

    [[nodiscard]]
    constexpr buffer_type buffer() const noexcept { return raw; }

private:
    buffer_type raw;
};

template<typename ValueType, std::endian GlobalEndian = std::endian::native, std::size_t Packed = 1uz>
class readonly_binary_view : private details::view_base<readonly_binary_view<ValueType, GlobalEndian, Packed>>
{
    using base = details::view_base<readonly_binary_view>;
public:
    using value_type = ValueType;
    using buffer_type = std::span<const std::byte>;
    static constexpr std::endian endian = GlobalEndian;
    static constexpr std::size_t packed = Packed;

    constexpr readonly_binary_view() = default;
    constexpr readonly_binary_view(const readonly_binary_view&) = default;
    constexpr readonly_binary_view& operator=(const readonly_binary_view&) = default;

    // binary_view do not check if the raw data is enough for the value during construction,
    // only when actually reading or writing the value, it will check if the raw data is enough.
    constexpr readonly_binary_view(buffer_type raw) noexcept : raw(raw) {}

    constexpr readonly_binary_view(const binary_view<value_type, endian, packed>& view) noexcept
        : raw(view.buffer())
    {}

    using base::wire_size;
    using base::offset_of;
    using base::subview;
    using base::consumed;
    using base::remained;
    using base::consumed_view;
    using base::remained_view;
    using base::read;

    [[nodiscard]]
    constexpr buffer_type buffer() const noexcept { return raw; }

private:
    buffer_type raw;
};

template<typename ValueType, std::size_t Packed = 1uz>
consteval std::span<const std::meta::member_offset> layout() noexcept {
    auto offsets = details::layout_of<ValueType, Packed>.offsets
        | std::views::transform([](const details::member_offset_info& info) {
            if (info.group_bit_width > 0) {
                return info.get_bit_field_group()
                    | std::views::transform([&info](const details::bit_field_info& bit_info) {
                        return std::meta::member_offset{
                            .bytes = static_cast<std::ptrdiff_t>(info.offset + (bit_info.bit_offset / CHAR_BIT)),
                            .bits = static_cast<std::ptrdiff_t>(bit_info.bit_offset % CHAR_BIT),
                        };
                    })
                    | std::ranges::to<std::vector<std::meta::member_offset>>();
            } else {
                return std::vector<std::meta::member_offset>{
                    {
                        .bytes = static_cast<std::ptrdiff_t>(info.offset),
                        .bits = 0,
                    }
                };
            }
        })
        | std::ranges::to<std::vector>();
    return std::define_static_array(offsets | std::views::join);
}

} // namespace bpt
