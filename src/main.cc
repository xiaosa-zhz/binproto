#include <print>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "binproto.hh"

// Test structs
struct Empty {};
struct Simple { std::int32_t a; std::int8_t b; std::int64_t c; };
struct Inherited : Empty, Simple { std::int16_t d; };

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

    std::println("All roundtrip tests passed!");
    return 0;
}
