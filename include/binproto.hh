#pragma once

#include "details/layout.hh"

namespace bpt {

namespace details {

template<typename Obj, std::meta::info Mem>
inline constexpr bool is_member_accessible = std::is_class_v<Obj>
    && (is_nonstatic_data_member(Mem) || is_base(Mem))
    && requires (Obj& v) { v.[:Mem:]; };

template<typename View>
struct bv_traits;

template<typename View, std::meta::info Mem>
using subview_return_type = bv_traits<View>::template rebind<typename [:type_of(Mem):]>;

template<typename Derived>
class subview_base
{
    using traits = bv_traits<Derived>;
public:
    template<std::meta::info Mem>
        requires details::is_member_accessible<typename traits::value_type, Mem>
              && (!is_bit_field(Mem))
    constexpr subview_return_type<Derived, Mem> subview() const noexcept {
        using value_type = traits::value_type;
        static constexpr auto endian = traits::endian;
        static constexpr auto packed = traits::packed;
        static constexpr auto offset
            = details::get_overall_offset_of_member(^^value_type, endian, packed, Mem);
        static constexpr auto member_size
            = details::layout_of<typename [:type_of(Mem):], endian, packed>.total_size;
        const auto raw = ((const Derived*)this)->buffer();
        if (raw.size() < offset + member_size) {
            return {};
        }
        return raw.subspan(offset, member_size);
    }
};

template<typename Derived>
class read_base
{
    using traits = bv_traits<Derived>;
public:
    constexpr std::error_code read(typename traits::value_type& value) const noexcept {
        using value_type = traits::value_type;
        static constexpr auto endian = traits::endian;
        static constexpr auto packed = traits::packed;
        static constexpr auto layout = details::layout_of<value_type, endian, packed>;
        const auto raw = ((const Derived*)this)->buffer();
        if (raw.size() < layout.total_size) {
            return std::make_error_code(std::errc::result_out_of_range);
        }
        details::read<endian, packed>(value, raw.subspan(0, layout.total_size));
        return {};
    }

    template<std::meta::info Mem>
        requires details::is_member_accessible<typename traits::value_type, Mem>
    constexpr std::error_code read(typename [:type_of(Mem):]& value) const noexcept {
        using value_type = traits::value_type;
        static constexpr auto endian = traits::endian;
        static constexpr auto packed = traits::packed;
        const auto& self = *((const Derived*)this);
        if constexpr (is_bit_field(Mem)) {
            static constexpr std::size_t offset
                = details::get_overall_offset_of_member(^^value_type, endian, packed, Mem);
            static constexpr std::size_t group_size
                = details::bit_field_group_width_of<Mem, endian, packed>;
            const auto raw = self.buffer();
            if (raw.size() < offset + group_size) {
                return std::make_error_code(std::errc::result_out_of_range);
            }
            details::read_sub_bits<Mem, endian, packed>(value, raw.subspan(offset, group_size));
            return {};
        } else {
            return self.template subview<Mem>().read(value);
        }
    }
};

} // namespace details

template<typename ValueType, std::endian GlobalEndian = std::endian::native, std::size_t Packed = 1uz>
class binary_view :
    private details::subview_base<binary_view<ValueType, GlobalEndian, Packed>>,
    private details::read_base<binary_view<ValueType, GlobalEndian, Packed>>
{
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

    using details::subview_base<binary_view>::subview;
    using details::read_base<binary_view>::read;

    constexpr std::error_code write(const value_type& value) const noexcept {
        static constexpr auto layout = details::layout_of<value_type, endian, packed>;
        if (raw.size() < layout.total_size) {
            return std::make_error_code(std::errc::result_out_of_range);
        }
        details::write<endian, packed>(value, raw.subspan(0, layout.total_size));
        return {};
    }

    template<std::meta::info Mem>
        requires details::is_member_accessible<value_type, Mem>
    constexpr std::error_code write(typename [:add_const(type_of(Mem)):]& value) const noexcept {
        if constexpr (is_bit_field(Mem)) {
            static constexpr std::size_t offset
                = details::get_overall_offset_of_member(^^value_type, endian, packed, Mem);
            static constexpr std::size_t group_size
                = details::bit_field_group_width_of<Mem, endian, packed>;
            if (raw.size() < offset + group_size) {
                return std::make_error_code(std::errc::result_out_of_range);
            }
            details::write_sub_bits<Mem, endian, packed>(value, raw.subspan(offset, group_size));
            return {};
        } else {
            return this->template subview<Mem>().write(value);
        }
    }

    buffer_type buffer() const noexcept { return raw; }

private:
    buffer_type raw;
};

template<typename ValueType, std::endian GlobalEndian = std::endian::native, std::size_t Packed = 1uz>
class readonly_binary_view :
    private details::subview_base<readonly_binary_view<ValueType, GlobalEndian, Packed>>,
    private details::read_base<readonly_binary_view<ValueType, GlobalEndian, Packed>>
{
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

    using details::subview_base<readonly_binary_view>::subview;
    using details::read_base<readonly_binary_view>::read;

    buffer_type buffer() const noexcept { return raw; }

private:
    buffer_type raw;
};

namespace details {

template<typename T, std::endian E, std::size_t P>
struct bv_traits<binary_view<T, E, P>> {
    using value_type = T;
    static constexpr std::endian endian = E;
    static constexpr std::size_t packed = P;
    template<typename U>
    using rebind = binary_view<U, E, P>;
};

template<typename T, std::endian E, std::size_t P>
struct bv_traits<readonly_binary_view<T, E, P>> {
    using value_type = T;
    static constexpr std::endian endian = E;
    static constexpr std::size_t packed = P;
    template<typename U>
    using rebind = readonly_binary_view<U, E, P>;
};

} // namespace bpt::details

} // namespace bpt
