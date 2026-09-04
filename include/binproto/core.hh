#pragma once

#include "details/layout.hh"
#include <numeric>
#include <iterator>

namespace bpt {

template<typename ValueType, std::size_t Packed = 1uz>
consteval std::span<const std::meta::member_offset> layout() noexcept {
    return std::define_static_array(
        details::layout_of<ValueType, Packed>.offsets
        | std::views::transform([](const details::member_offset_info& info) {
            if (info.group_bit_width > 0) {
                return std::define_static_array(
                    info.get_bit_field_group()
                    | std::views::transform([&info](const details::bit_field_info& bit_info) {
                        return std::meta::member_offset{
                            .bytes = static_cast<std::ptrdiff_t>(info.offset + (bit_info.bit_offset / CHAR_BIT)),
                            .bits = static_cast<std::ptrdiff_t>(bit_info.bit_offset % CHAR_BIT),
                        };
                    })
                );
            } else {
                return std::define_static_array(
                    std::views::single(std::meta::member_offset{
                        .bytes = static_cast<std::ptrdiff_t>(info.offset),
                        .bits = 0,
                    })
                ).subspan(0, 1);
            }
        })
        | std::views::join
    );
}

template<std::meta::info Mem, typename ValueType, std::size_t Packed = 1uz>
    requires details::member_accessible<ValueType, Mem>
consteval std::meta::member_offset offset_of() noexcept {
    static constexpr auto group_offset
        = details::get_overall_offset_of_member(^^ValueType, Packed, Mem);
    if constexpr (is_bit_field(Mem)) {
        static constexpr auto bit_offset
            = details::bit_field_group_desc_of<Mem, Packed>.bit_offset;
        return {
            .bytes = static_cast<std::ptrdiff_t>(group_offset + (bit_offset / CHAR_BIT)),
            .bits = static_cast<std::ptrdiff_t>(bit_offset % CHAR_BIT),
        };
    } else {
        return { .bytes = static_cast<std::ptrdiff_t>(group_offset), .bits = 0 };
    }
}

template<std::endian Endian, details::fundamental T>
constexpr bool read_fundamental(T& value, std::span<const std::byte> raw) noexcept {
    if (raw.size() < sizeof(T)) {
        [[unlikely]] return false;
    }
    details::read_fundamental<Endian>(value, raw);
    return true;
}

template<std::endian Endian, details::fundamental T>
constexpr bool write_fundamental(const T& value, std::span<std::byte> raw) noexcept {
    if (raw.size() < sizeof(T)) {
        [[unlikely]] return false;
    }
    details::write_fundamental<Endian>(value, raw);
    return true;
}

template<std::endian Endian, details::fundamental T>
constexpr bool read_fundamental_batch(std::span<T> value, std::span<const std::byte> raw) noexcept {
    if (raw.size() < std::saturating_mul(sizeof(T), value.size())) {
        [[unlikely]] return false;
    }
    details::read_batch<Endian, 1>(value, raw);
    return true;
}

template<std::endian Endian, details::fundamental T>
constexpr bool write_fundamental_batch(std::span<const T> value, std::span<std::byte> raw) noexcept {
    if (raw.size() < std::saturating_mul(sizeof(T), value.size())) {
        [[unlikely]] return false;
    }
    details::write_batch<Endian, 1>(value, raw);
    return true;
}

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
    [[nodiscard]] static consteval std::size_t wire_size() noexcept {
        return details::layout_of<value_type, packed>.total_size;
    }

    template<std::meta::info Mem>
        requires details::member_accessible<value_type, Mem>
    [[nodiscard]] static consteval std::meta::member_offset offset_of() noexcept {
        return bpt::offset_of<Mem, value_type, packed>();
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

    [[nodiscard]] constexpr auto remained(std::size_t consume = 1) const noexcept {
        auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < std::saturating_mul(wire_size(), consume)) {
            [[unlikely]] return decltype(raw){};
        }
        return raw.subspan(wire_size() * consume);
    }

    [[nodiscard]] constexpr view_type consumed_view() const noexcept {
        auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < wire_size()) {
            [[unlikely]] return {};
        }
        return raw.subspan(0, wire_size());
    }

    template<typename Succeeded>
    [[nodiscard]] constexpr auto remained_view(std::size_t consume = 1) const noexcept
        -> rebind_type_view<Succeeded>
    {
        auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < std::saturating_mul(wire_size(), consume)) {
            [[unlikely]] return {};
        }
        return raw.subspan(wire_size() * consume);
    }

    [[nodiscard]] constexpr bool read(value_type& value) const noexcept {
        const auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < wire_size()) {
            [[unlikely]] return false;
        }
        details::read<endian, packed>(value, raw.subspan(0, wire_size()));
        return true;
    }

    [[nodiscard]] constexpr bool read(std::span<value_type> values) const noexcept {
        const auto raw = ((const view_type*)this)->buffer();
        if (raw.size() < std::saturating_mul(wire_size(), values.size())) {
            [[unlikely]] return false;
        }
        details::read_batch<endian, packed>(values, raw);
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

template<typename ValueType, std::endian Endian = std::endian::native, std::size_t Packed = 1uz>
    requires details::supported_type<ValueType, Packed>
class binary_view : private details::view_base<binary_view<ValueType, Endian, Packed>>
{
    using base = details::view_base<binary_view>;
public:
    using value_type = ValueType;
    using buffer_type = std::span<std::byte>;
    static constexpr std::endian endian = Endian;
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

    [[nodiscard]] constexpr bool write(const value_type& value) const noexcept {
        if (raw.size() < wire_size()) {
            [[unlikely]] return false;
        }
        details::write<endian, packed>(value, raw.subspan(0, wire_size()));
        return true;
    }

    [[nodiscard]] constexpr bool write(std::span<const value_type> values) const noexcept {
        if (raw.size() < std::saturating_mul(wire_size(), values.size())) {
            [[unlikely]] return false;
        }
        details::write_batch<endian, packed>(values, raw);
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

    [[nodiscard]]
    constexpr bool operator==(const binary_view& other) const noexcept {
        const auto lhs_buf = this->buffer();
        const auto rhs_buf = other.buffer();
        return lhs_buf.data() == rhs_buf.data() && lhs_buf.size() == rhs_buf.size();
    }

private:
    buffer_type raw;
};

template<typename ValueType, std::endian Endian = std::endian::native, std::size_t Packed = 1uz>
    requires details::supported_type<ValueType, Packed>
class readonly_binary_view : private details::view_base<readonly_binary_view<ValueType, Endian, Packed>>
{
    using base = details::view_base<readonly_binary_view>;
public:
    using value_type = ValueType;
    using buffer_type = std::span<const std::byte>;
    static constexpr std::endian endian = Endian;
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

    [[nodiscard]]
    constexpr bool operator==(const readonly_binary_view& other) const noexcept {
        const auto lhs_buf = this->buffer();
        const auto rhs_buf = other.buffer();
        return lhs_buf.data() == rhs_buf.data() && lhs_buf.size() == rhs_buf.size();
    }

private:
    buffer_type raw;
};

template<typename ValueType, std::endian Endian = std::endian::native, std::size_t Packed = 1uz>
    requires details::supported_type<ValueType, Packed>
class binary_input_iterator
{
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = ValueType;
    using reference = value_type;
    using difference_type = std::ptrdiff_t;

    using binary_view_type = readonly_binary_view<value_type, Endian, Packed>;

    constexpr binary_input_iterator() = default;
    constexpr binary_input_iterator(const binary_input_iterator&) = default;
    constexpr binary_input_iterator& operator=(const binary_input_iterator&) = default;

    constexpr binary_input_iterator(binary_view_type view) noexcept : view(view) {}

    [[nodiscard]] constexpr reference operator*() const noexcept {
        value_type value;
        bool res = view.read(value);
        (void) res; // TODO: assert
        return value;
    }

    constexpr binary_input_iterator& operator++() noexcept {
        view = view.template remained_view<value_type>();
        return *this;
    }

    constexpr binary_input_iterator operator++(int) noexcept {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }

    [[nodiscard]]
    constexpr binary_input_iterator operator+(difference_type n) const noexcept {
        auto tmp = *this;
        tmp.view = tmp.view.template remained_view<value_type>(n);
        return tmp;
    }

    constexpr binary_input_iterator& operator+=(difference_type n) noexcept {
        view = view.template remained_view<value_type>(n);
        return *this;
    }

    [[nodiscard]]
    constexpr bool operator==(const binary_input_iterator& other) const noexcept {
        return this->view == other.view;
    }

    [[nodiscard]]
    constexpr bool operator==(std::default_sentinel_t) const noexcept {
        return this->view.consumed().empty();
    }

private:
    binary_view_type view;
};

template<typename ValueType, std::endian Endian = std::endian::native, std::size_t Packed = 1uz>
    requires details::supported_type<ValueType, Packed>
class binary_output_iterator
{
public:
    using iterator_category = std::output_iterator_tag;
    using value_type = ValueType;
    using reference = void;
    using difference_type = std::ptrdiff_t;

    using binary_view_type = binary_view<value_type, Endian, Packed>;

    constexpr binary_output_iterator() = default;
    constexpr binary_output_iterator(const binary_output_iterator&) = default;
    constexpr binary_output_iterator& operator=(const binary_output_iterator&) = default;

    constexpr binary_output_iterator(binary_view_type view) noexcept : view(view) {}

    constexpr binary_output_iterator& operator=(const value_type& value) noexcept {
        bool res = view.write(value);
        (void) res; // TODO: assert
        return *this;
    }

    constexpr binary_output_iterator& operator*() noexcept { return *this; }

    constexpr binary_output_iterator& operator++() noexcept {
        view = view.template remained_view<value_type>();
        return *this;
    }

    constexpr binary_output_iterator operator++(int) noexcept {
        auto tmp = *this;
        ++(*this);
        return tmp;
    }

    [[nodiscard]]
    constexpr binary_output_iterator operator+(difference_type n) const noexcept {
        auto tmp = *this;
        tmp.view = tmp.view.template remained_view<value_type>(n);
        return tmp;
    }

    constexpr binary_output_iterator& operator+=(difference_type n) noexcept {
        view = view.template remained_view<value_type>(n);
        return *this;
    }

    [[nodiscard]]
    constexpr bool operator==(const binary_output_iterator& other) const noexcept {
        return this->view == other.view;
    }

    [[nodiscard]]
    constexpr bool operator==(std::default_sentinel_t) const noexcept {
        return this->view.consumed().empty();
    }

private:
    binary_view_type view;
};

template<typename ValueType, std::endian Endian = std::endian::native, std::size_t Packed = 1uz>
    requires details::supported_type<ValueType, Packed>
constexpr auto read_n(std::span<const std::byte> raw, std::size_t n) noexcept
    -> std::ranges::subrange<binary_input_iterator<ValueType, Endian, Packed>, std::default_sentinel_t>
{
    using view_type = readonly_binary_view<ValueType, Endian, Packed>;
    const auto end = std::saturating_mul(view_type::wire_size(), n);
    if (raw.size() < end) {
        [[unlikely]] return {};
    }
    view_type view(raw.subspan(0, end));
    return {
        binary_input_iterator<ValueType, Endian, Packed>(view),
        std::default_sentinel
    };
}

} // namespace bpt
