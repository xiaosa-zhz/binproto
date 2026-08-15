#pragma once

#include <string_view>

namespace bpt {

class error
{
public:
    enum class code {
        ok = 0,
        buffer_too_small,
        extent_not_set,
        alternative_not_set,
        extent_too_large,
        alternative_not_found,
    };

    using enum code;

    constexpr error() = default;
    constexpr error(const error&) = default;
    constexpr error& operator=(const error&) = default;
    constexpr error(code c) noexcept : c(c) {}

    constexpr std::string_view message() const noexcept {
        switch (c) {
            case ok:                    return "no error";
            case buffer_too_small:      return "provided buffer size is smaller than needed wire size";
            case extent_not_set:        return "access dynamic array member or member after it without set_extent";
            case alternative_not_set:   return "access alternative member or member after it without set_alternative";
            case extent_too_large:      return "set_extent called with extent larger than maximum extent";
            case alternative_not_found: return "set_alternative called with alternative not found in the union";
        }
        return "unknown error";
    }

    explicit constexpr operator bool() const noexcept { return c != ok; }
    friend constexpr bool operator==(error lhs, error rhs) = default;
    friend constexpr bool operator==(error lhs, code rhs) noexcept { return lhs.c == rhs; }

private:
    code c = ok;
};

} // namespace bpt
