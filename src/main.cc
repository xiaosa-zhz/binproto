#include <print>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <system_error>
#include <utility>

#include "binproto.hh"

// Test structs
struct Empty {};
struct Simple { std::int32_t a; std::int8_t b; std::int64_t c; };
struct Inherited : Empty, Simple { std::int16_t d; };

// Bit-field test structs
struct Flags { std::uint8_t a : 3; std::uint8_t b : 5; };
struct Wide { std::uint16_t x : 4; std::uint16_t y : 12; };
struct SignedBit { int x : 3; };
struct BoolBits { bool f : 1; bool g : 1; };
enum class E : unsigned char { A = 1, B = 2, C = 3 };
enum class SE : signed char { N = -1, P = 1 };
struct EnumBits { E e : 2; SE se : 2; };
struct MixedBits { std::uint32_t plain; std::uint8_t a : 3; std::uint8_t b : 5; };
struct Terminated { std::uint8_t a : 3; std::uint8_t b : 5; std::uint8_t : 0; std::uint32_t c; };
struct BitBase { std::uint8_t a : 3; };
struct BitDerived : BitBase { std::uint8_t b : 5; };
struct U64Bits { std::uint64_t a : 32; std::uint64_t b : 32; };

static_assert(subobjects_of(^^Terminated, std::meta::access_context::current()).size() == 4);

int main() {
    std::println("Hello, World!");

    // --- Roundtrip test: Simple, pack(1), scalar members ---
    {
        std::array<std::byte, 64> storage{};
        bpt::binary_view<Simple, std::endian::native, 1> view(storage);

        Simple s{0x11223344, 0x55, 0x66778899AABBCCDDLL};

        if (auto ec = view.write<^^Simple::a>(s.a); ec) {
            std::println("write a failed: {}", ec.message());
            return 1;
        }
        if (auto ec = view.write<^^Simple::b>(s.b); ec) {
            std::println("write b failed: {}", ec.message());
            return 1;
        }
        if (auto ec = view.write<^^Simple::c>(s.c); ec) {
            std::println("write c failed: {}", ec.message());
            return 1;
        }

        Simple r{};
        if (auto ec = view.read<^^Simple::a>(r.a); ec) {
            std::println("read a failed: {}", ec.message());
            return 1;
        }
        if (auto ec = view.read<^^Simple::b>(r.b); ec) {
            std::println("read b failed: {}", ec.message());
            return 1;
        }
        if (auto ec = view.read<^^Simple::c>(r.c); ec) {
            std::println("read c failed: {}", ec.message());
            return 1;
        }

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[Simple  pack(1)] a={:#x} b={:#x} c={:#x} | roundtrip {}",
                     (unsigned)r.a, (unsigned)r.b, (unsigned long long)r.c,
                     ok ? "OK" : "FAIL");

        // Verify packed offsets: a@0, b@4, c@5 (pack 1)
        static constexpr auto layout = bpt::details::generate_member_offset_table(^^Simple, std::endian::native, 1);
        static_assert(layout.offsets[0].offset == 0);
        static_assert(layout.offsets[1].offset == 4);
        static_assert(layout.offsets[2].offset == 5);
        static_assert(layout.total_size == 13);

        if (!ok) return 1;
    }

    // --- Roundtrip test: Simple, pack(8) (natural alignment) ---
    {
        std::array<std::byte, 64> storage{};
        bpt::binary_view<Simple, std::endian::native, 8> view(storage);

        Simple s{0x77889900, 0x42, 0x123456789ABCDEF0LL};

        view.write<^^Simple::a>(s.a);
        view.write<^^Simple::b>(s.b);
        view.write<^^Simple::c>(s.c);

        Simple r{};
        view.read<^^Simple::a>(r.a);
        view.read<^^Simple::b>(r.b);
        view.read<^^Simple::c>(r.c);

        auto v = std::views::iota(0uz, 16uz);
        v.begin()[2];

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[Simple  pack(8)] a={:#x} b={:#x} c={:#x} | roundtrip {}",
                     (unsigned)r.a, (unsigned)r.b, (unsigned long long)r.c,
                     ok ? "OK" : "FAIL");

        // pack(8): natural layout -> a@0, b@4, c@8, total 16
        static constexpr auto layout8 = bpt::details::generate_member_offset_table(^^Simple, std::endian::native, 8);
        static_assert(layout8.offsets[0].offset == 0);
        static_assert(layout8.offsets[1].offset == 4);
        static_assert(layout8.offsets[2].offset == 8);
        static_assert(layout8.total_size == 16);

        if (!ok) return 1;
    }

    // --- Roundtrip test: Inherited, pack(1) ---
    {
        std::array<std::byte, 64> storage{};
        bpt::binary_view<Inherited, std::endian::native, 1> view(storage);

        Inherited s{{}, {0x11223344, 0x55, 0x66778899AABBCCDDLL}, 0x7788};

        // write inherited members via base reflection
        view.write<^^Inherited::a>(s.a);
        view.write<^^Inherited::b>(s.b);
        view.write<^^Inherited::c>(s.c);
        view.write<^^Inherited::d>(s.d);

        Inherited r{};
        view.read<^^Simple::a>(r.a);
        view.read<^^Simple::b>(r.b);
        view.read<^^Simple::c>(r.c);
        view.read<^^Inherited::d>(r.d);

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c && s.d == r.d);
        std::println("[Inherit pack(1)] a={:#x} b={:#x} c={:#x} d={:#x} | roundtrip {}",
                     (unsigned)r.a, (unsigned)r.b, (unsigned long long)r.c,
                     (unsigned)r.d, ok ? "OK" : "FAIL");

        if (!ok) return 1;
    }

    // --- Roundtrip test: Simple, big-endian, pack(1) ---
    // Verifies endian conversion on arithmetic members.
    {
        std::array<std::byte, 64> storage{};
        bpt::binary_view<Simple, std::endian::big, 1> view(storage);

        Simple s{0x11223344, 0x55, 0x66778899AABBCCDDLL};

        view.write<^^Simple::a>(s.a);
        view.write<^^Simple::b>(s.b);
        view.write<^^Simple::c>(s.c);

        // On a little-endian host, the wire bytes should be byte-swapped.
        std::int32_t wire_a = std::bit_cast<std::int32_t>(std::array<std::byte, 4>{
            storage[0], storage[1], storage[2], storage[3]});
        bool wire_ok = (wire_a == 0x44332211);
        std::println("[Simple  BE pack(1)] wire a={:#x} (expect {:#x}) | {}",
                     (unsigned)wire_a, 0x44332211U, wire_ok ? "OK" : "FAIL");
        if (!wire_ok) return 1;

        Simple r{};
        view.read<^^Simple::a>(r.a);
        view.read<^^Simple::b>(r.b);
        view.read<^^Simple::c>(r.c);

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[Simple  BE pack(1)] a={:#x} b={:#x} c={:#x} | roundtrip {}",
                     (unsigned)r.a, (unsigned)r.b, (unsigned long long)r.c,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Roundtrip test: Simple, big-endian, pack(1) ---
    // Verifies direct read/write of the entire struct
    {
        std::array<std::byte, 64> storage{};
        bpt::binary_view<Simple, std::endian::big, 1> view(storage);

        Simple s{0x11223344, 0x55, 0x66778899AABBCCDDLL};

        if (auto ec = view.write(s); ec) {
            std::println("write failed: {}", ec.message());
            return 1;
        }

        Simple r{};
        if (auto ec = view.read(r); ec) {
            std::println("read failed: {}", ec.message());
            return 1;
        }

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[Simple  BE pack(1)] a={:#x} b={:#x} c={:#x} | roundtrip {}",
                     (unsigned)r.a, (unsigned)r.b, (unsigned long long)r.c,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Bit-field layout checks ---
    {
        static constexpr auto flags_layout = bpt::details::generate_member_offset_table(^^Flags, std::endian::little, 1);
        static_assert(flags_layout.total_size == 1);
        static_assert(flags_layout.offsets[0].offset == 0 && flags_layout.offsets[0].bit_offset == 0);
        static_assert(flags_layout.offsets[0].group_bit_width == 8);
        static_assert(flags_layout.offsets[1].offset == 0 && flags_layout.offsets[1].bit_offset == 3);
        static_assert(flags_layout.offsets[1].group_bit_width == 8);

        static constexpr auto wide_layout = bpt::details::generate_member_offset_table(^^Wide, std::endian::little, 1);
        static_assert(wide_layout.total_size == 2);
        static_assert(wide_layout.offsets[1].offset == 0 && wide_layout.offsets[1].bit_offset == 4);
        static_assert(wide_layout.offsets[1].group_bit_width == 16);

        static constexpr auto mixed_layout = bpt::details::generate_member_offset_table(^^MixedBits, std::endian::little, 1);
        static_assert(mixed_layout.total_size == 5);
        static_assert(mixed_layout.offsets[1].offset == 4 && mixed_layout.offsets[1].bit_offset == 0);
        static_assert(mixed_layout.offsets[2].offset == 4 && mixed_layout.offsets[2].bit_offset == 3);

        static constexpr auto term_layout = bpt::details::generate_member_offset_table(^^Terminated, std::endian::little, 1);
        static_assert(term_layout.total_size == 5);
        // Unnamed zero-width bit-fields are not reflected as subobjects;
        // offsets[2] is c, packed right after the one-byte group at pack(1).
        static_assert(term_layout.offsets[2].offset == 1);

        static constexpr auto derived_layout = bpt::details::generate_member_offset_table(^^BitDerived, std::endian::little, 1);
        static_assert(derived_layout.total_size == 2);
        static_assert(derived_layout.offsets[1].offset == 1 && derived_layout.offsets[1].bit_offset == 0);

        static constexpr auto u64_layout = bpt::details::generate_member_offset_table(^^U64Bits, std::endian::little, 1);
        static_assert(u64_layout.total_size == 8);
        static_assert(u64_layout.offsets[1].bit_offset == 32);
        static_assert(u64_layout.offsets[1].group_bit_width == 64);
    }

    // --- Flags: single-byte group, wire format + whole-struct roundtrip ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<Flags, std::endian::little, 1> view(storage);

        Flags f{5, 25};
        if (auto ec = view.write(f); ec) {
            std::println("flags LE write failed: {}", ec.message());
            return 1;
        }
        // a=0b101 in bits 0..2, b=0b11001 in bits 3..7 -> 0b11001101
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0xCD;
        std::println("[Flags   LE pack(1)] wire={:#04x} {}", std::to_integer<unsigned>(storage[0]),
                     wire_ok ? "OK" : "FAIL");
        if (!wire_ok) return 1;

        Flags r{};
        if (auto ec = view.read(r); ec) {
            std::println("flags LE read failed: {}", ec.message());
            return 1;
        }
        bool ok = r.a == 5 && r.b == 25;
        std::println("[Flags   LE pack(1)] a={} b={} | roundtrip {}", +r.a, +r.b, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Flags: big-endian wire format ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<Flags, std::endian::big, 1> view(storage);

        Flags f{5, 25};
        if (auto ec = view.write(f); ec) return 1;
        // MSB-first packing: a=0b101 in bits 5..7, b=0b11001 in bits 0..4 -> 0b10111001
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0xB9;
        std::println("[Flags   BE pack(1)] wire={:#04x} {}", std::to_integer<unsigned>(storage[0]),
                     wire_ok ? "OK" : "FAIL");
        if (!wire_ok) return 1;

        Flags r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = r.a == 5 && r.b == 25;
        std::println("[Flags   BE pack(1)] a={} b={} | roundtrip {}", +r.a, +r.b, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Flags: member-wise read/write must not clobber the sibling field ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<Flags, std::endian::little, 1> view(storage);

        if (auto ec = view.write<^^Flags::a>(std::uint8_t{5}); ec) return 1;
        if (auto ec = view.write<^^Flags::b>(std::uint8_t{25}); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0xCD;

        std::uint8_t a = 0, b = 0;
        if (auto ec = view.read<^^Flags::a>(a); ec) return 1;
        if (auto ec = view.read<^^Flags::b>(b); ec) return 1;
        bool ok = wire_ok && a == 5 && b == 25;
        std::println("[Flags   LE member ] wire={:#04x} a={} b={} | {}",
                     std::to_integer<unsigned>(storage[0]), a, b, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Wide: multi-byte group, both endians ---
    {
        std::array<std::byte, 4> raw_le{};
        bpt::binary_view<Wide, std::endian::little, 1> view_le(raw_le);
        Wide w{0xA, 0xABC};
        if (auto ec = view_le.write(w); ec) return 1;
        // LE: group value 0xABCA stored LSB-first
        bool le_ok = std::to_integer<unsigned>(raw_le[0]) == 0xCA
                  && std::to_integer<unsigned>(raw_le[1]) == 0xAB;

        std::array<std::byte, 4> raw_be{};
        bpt::binary_view<Wide, std::endian::big, 1> view_be(raw_be);
        if (auto ec = view_be.write(w); ec) return 1;
        // BE: x in the top 4 bits of the first byte, y follows MSB-first
        bool be_ok = std::to_integer<unsigned>(raw_be[0]) == 0xAA
                  && std::to_integer<unsigned>(raw_be[1]) == 0xBC;

        Wide rl{}, rb{};
        if (auto ec = view_le.read(rl); ec) return 1;
        if (auto ec = view_be.read(rb); ec) return 1;
        bool ok = le_ok && be_ok
               && rl.x == 0xA && rl.y == 0xABC
               && rb.x == 0xA && rb.y == 0xABC;
        std::println("[Wide  LE/BE pack(1)] LE={:02x}{:02x} BE={:02x}{:02x} | {}",
                     std::to_integer<unsigned>(raw_le[0]), std::to_integer<unsigned>(raw_le[1]),
                     std::to_integer<unsigned>(raw_be[0]), std::to_integer<unsigned>(raw_be[1]),
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- SignedBit: negative values must sign-extend on read ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<SignedBit, std::endian::little, 1> view(storage);

        bool ok = true;
        for (int v : {-3, -1, 2}) {
            SignedBit s{v};
            if (auto ec = view.write(s); ec) return 1;
            SignedBit r{};
            if (auto ec = view.read(r); ec) return 1;
            int m = 0;
            if (auto ec = view.read<^^SignedBit::x>(m); ec) return 1;
            ok = ok && r.x == v && m == v;
        }
        std::println("[SignedBit LE     ] x in {{-3,-1,2}} | roundtrip {}", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- EnumBits: scoped enums, signed underlying sign-extension ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<EnumBits, std::endian::little, 1> view(storage);

        EnumBits s{E::B, SE::N};
        if (auto ec = view.write(s); ec) return 1;
        // e=0b10 in bits 0..1, se=-1 as 0b11 in bits 2..3 -> 0b1110
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x0E;

        EnumBits r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.e == E::B && r.se == SE::N;
        std::println("[EnumBits LE      ] wire={:#04x} e={} se={} | {}",
                     std::to_integer<unsigned>(storage[0]), std::to_underlying(r.e),
                     (int)std::to_underlying(r.se), ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- BoolBits ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<BoolBits, std::endian::little, 1> view(storage);

        BoolBits s{true, false};
        if (auto ec = view.write(s); ec) return 1;
        BoolBits r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = std::to_integer<unsigned>(storage[0]) == 0x01 && r.f && !r.g;
        bool rf = r.f, rg = r.g; // bit-fields cannot bind to println's forwarding references
        std::println("[BoolBits LE      ] wire={:#04x} f={} g={} | {}",
                     std::to_integer<unsigned>(storage[0]), rf, rg, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- MixedBits: bit-field group at a non-zero byte offset ---
    {
        std::array<std::byte, 8> storage{};
        bpt::binary_view<MixedBits, std::endian::little, 1> view(storage);

        MixedBits s{0x11223344, 5, 25};
        if (auto ec = view.write(s); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[4]) == 0xCD;

        MixedBits r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.plain == 0x11223344 && r.a == 5 && r.b == 25;
        std::println("[Mixed   LE pack(1)] plain={:#x} a={} b={} | {}", r.plain, +r.a, +r.b,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Terminated: zero-width bit-field separator is skipped ---
    {
        std::array<std::byte, 8> storage{};
        bpt::binary_view<Terminated, std::endian::little, 1> view(storage);

        Terminated t{5, 25, 0x12345678};
        if (auto ec = view.write(t); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0xCD;

        Terminated r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 5 && r.b == 25 && r.c == 0x12345678;
        std::println("[Terminated LE   ] a={} b={} c={:#x} | {}", +r.a, +r.b, r.c,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- BitDerived: bit-field inherited from a base class ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<BitDerived, std::endian::little, 1> view(storage);

        BitDerived s{{5}, 25};
        if (auto ec = view.write(s); ec) return 1;

        BitDerived r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = r.a == 5 && r.b == 25;

        // member-wise access to the inherited bit-field
        std::uint8_t a = 0;
        if (auto ec = view.read<^^BitDerived::a>(a); ec) return 1;
        if (auto ec = view.write<^^BitDerived::b>(std::uint8_t{17}); ec) return 1;
        std::uint8_t b = 0;
        if (auto ec = view.read<^^BitDerived::b>(b); ec) return 1;
        ok = ok && a == 5 && b == 17;
        std::println("[Derived  LE      ] a={} b={} | {}", +r.a, +b, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- U64Bits: a full 64-bit group, both endians ---
    {
        std::array<std::byte, 8> raw_le{};
        bpt::binary_view<U64Bits, std::endian::little, 1> view_le(raw_le);
        U64Bits v{0x89ABCDEF, 0x01234567};
        if (auto ec = view_le.write(v); ec) return 1;
        bool le_ok = std::to_integer<unsigned>(raw_le[0]) == 0xEF
                  && std::to_integer<unsigned>(raw_le[3]) == 0x89
                  && std::to_integer<unsigned>(raw_le[4]) == 0x67
                  && std::to_integer<unsigned>(raw_le[7]) == 0x01;

        std::array<std::byte, 8> raw_be{};
        bpt::binary_view<U64Bits, std::endian::big, 1> view_be(raw_be);
        if (auto ec = view_be.write<^^U64Bits::a>(v.a); ec) return 1;
        if (auto ec = view_be.write<^^U64Bits::b>(v.b); ec) return 1;
        bool be_ok = std::to_integer<unsigned>(raw_be[0]) == 0x89
                  && std::to_integer<unsigned>(raw_be[3]) == 0xEF
                  && std::to_integer<unsigned>(raw_be[4]) == 0x01
                  && std::to_integer<unsigned>(raw_be[7]) == 0x67;

        U64Bits rl{}, rb{};
        if (auto ec = view_le.read(rl); ec) return 1;
        if (auto ec = view_be.read(rb); ec) return 1;
        bool ok = le_ok && be_ok
               && rl.a == 0x89ABCDEF && rl.b == 0x01234567
               && rb.a == 0x89ABCDEF && rb.b == 0x01234567;
        std::println("[U64Bits LE/BE    ] 64-bit group | {}", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Bounds checking on bit-field access ---
    {
        std::array<std::byte, 0> storage{};
        bpt::binary_view<Flags, std::endian::little, 1> view(storage);

        Flags f{};
        std::uint8_t a = 0;
        bool ok = view.read(f) == std::errc::result_out_of_range
               && view.read<^^Flags::a>(a) == std::errc::result_out_of_range
               && view.write(f) == std::errc::result_out_of_range
               && view.write<^^Flags::a>(std::uint8_t{1}) == std::errc::result_out_of_range;
        std::println("[Flags   bounds   ] empty buffer | {}", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    std::println("All roundtrip tests passed!");
    return 0;
}
