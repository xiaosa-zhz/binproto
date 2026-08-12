#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <print>
#include <system_error>

#include "binproto.hh"

// Test structs
struct Empty {};
struct Simple { std::int32_t a; std::int8_t b; std::int64_t c; };
struct Inherited : Empty, Simple { std::int16_t d; };

// Array member test structs
struct WithArray { std::int32_t head; std::uint16_t arr[3]; std::int8_t tail; };
struct WithStdArray { std::int32_t head; std::array<std::uint16_t, 3> arr; std::int8_t tail; };
struct Point { std::int16_t x; std::int16_t y; };
struct Grid { std::uint8_t n; Point pts[2]; };
struct Matrix { std::int32_t m[2][3]; };

int run_scalar_tests() {
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
        std::println("[{:<10} {:<13}] a={:#x} b={:#x} c={:#x} | roundtrip {}",
                     "Simple", "pack(1)",
                     (unsigned)r.a, (unsigned)r.b, (unsigned long long)r.c,
                     ok ? "OK" : "FAIL");

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

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[{:<10} {:<13}] a={:#x} b={:#x} c={:#x} | roundtrip {}",
                     "Simple", "pack(8)",
                     (unsigned)r.a, (unsigned)r.b, (unsigned long long)r.c,
                     ok ? "OK" : "FAIL");

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
        std::println("[{:<10} {:<13}] a={:#x} b={:#x} c={:#x} d={:#x} | roundtrip {}",
                     "Inherit", "pack(1)",
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
        std::println("[{:<10} {:<13}] wire a={:#x} (expect {:#x}) | {}",
                     "Simple", "BE pack(1)",
                     (unsigned)wire_a, 0x44332211U, wire_ok ? "OK" : "FAIL");
        if (!wire_ok) return 1;

        Simple r{};
        view.read<^^Simple::a>(r.a);
        view.read<^^Simple::b>(r.b);
        view.read<^^Simple::c>(r.c);

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[{:<10} {:<13}] a={:#x} b={:#x} c={:#x} | roundtrip {}",
                     "Simple", "BE pack(1)",
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
        std::println("[{:<10} {:<13}] a={:#x} b={:#x} c={:#x} | roundtrip {}",
                     "Simple", "BE struct",
                     (unsigned)r.a, (unsigned)r.b, (unsigned long long)r.c,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- WithArray: bounded array member, little-endian ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<WithArray, std::endian::little, 1> view(storage);

        WithArray s{0x11223344, {0x1112, 0x3344, 0x5566}, 0x77};
        if (auto ec = view.write(s); ec) return 1;

        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x44
                    && std::to_integer<unsigned>(storage[3]) == 0x11
                    && std::to_integer<unsigned>(storage[4]) == 0x12
                    && std::to_integer<unsigned>(storage[5]) == 0x11
                    && std::to_integer<unsigned>(storage[8]) == 0x66
                    && std::to_integer<unsigned>(storage[9]) == 0x55
                    && std::to_integer<unsigned>(storage[10]) == 0x77;

        WithArray r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.head == 0x11223344
               && r.arr[0] == 0x1112 && r.arr[1] == 0x3344 && r.arr[2] == 0x5566
               && r.tail == 0x77;
        std::println("[{:<10} {:<13}] head={:#x} arr={:x} {:x} {:x} tail={:x} | {}",
                     "WithArray", "LE", (unsigned)r.head, r.arr[0], r.arr[1], r.arr[2],
                     (unsigned)r.tail, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- WithArray: big-endian, per-element byte swap ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<WithArray, std::endian::big, 1> view(storage);

        WithArray s{0x11223344, {0x1112, 0x3344, 0x5566}, 0x77};
        if (auto ec = view.write(s); ec) return 1;

        bool wire_ok = std::to_integer<unsigned>(storage[4]) == 0x11
                    && std::to_integer<unsigned>(storage[5]) == 0x12
                    && std::to_integer<unsigned>(storage[8]) == 0x55;

        WithArray r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.head == 0x11223344 && r.arr[0] == 0x1112
               && r.arr[2] == 0x5566 && r.tail == 0x77;
        std::println("[{:<10} {:<13}] wire arr[0]={:02x}{:02x} | {}",
                     "WithArray", "BE", std::to_integer<unsigned>(storage[4]),
                     std::to_integer<unsigned>(storage[5]), ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- WithArray: subview on the array member ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<WithArray, std::endian::little, 1> view(storage);

        std::uint16_t arr[3]{0xAAAA, 0xBBBB, 0xCCCC};
        auto sub = view.subview<^^WithArray::arr>();
        if (auto ec = sub.write(arr); ec) return 1;

        std::uint16_t rr[3]{};
        if (auto ec = sub.read(rr); ec) return 1;
        bool ok = rr[0] == 0xAAAA && rr[1] == 0xBBBB && rr[2] == 0xCCCC
               && std::to_integer<unsigned>(storage[4]) == 0xAA;
        std::println("[{:<10} {:<13}] {:x} {:x} {:x} | {}",
                     "WithArray", "subview", rr[0], rr[1], rr[2], ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- WithStdArray: std::array member ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<WithStdArray, std::endian::little, 1> view(storage);

        WithStdArray s{0x11223344, {0x1112, 0x3344, 0x5566}, 0x77};
        if (auto ec = view.write(s); ec) return 1;

        WithStdArray r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = r.head == 0x11223344 && r.arr[0] == 0x1112 && r.arr[1] == 0x3344
               && r.arr[2] == 0x5566 && r.tail == 0x77
               && std::to_integer<unsigned>(storage[4]) == 0x12;
        std::println("[{:<10} {:<13}] head={:#x} arr={:x} {:x} {:x} tail={:x} | {}",
                     "StdArray", "LE", (unsigned)r.head, r.arr[0], r.arr[1], r.arr[2],
                     (unsigned)r.tail, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Grid: array of structs ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<Grid, std::endian::little, 1> view(storage);

        Grid s{3, {{1, 2}, {3, 4}}};
        if (auto ec = view.write(s); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 3
                    && std::to_integer<unsigned>(storage[1]) == 1
                    && std::to_integer<unsigned>(storage[5]) == 3;

        Grid r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.n == 3
               && r.pts[0].x == 1 && r.pts[0].y == 2
               && r.pts[1].x == 3 && r.pts[1].y == 4;
        std::println("[{:<10} {:<13}] n={} pts=[({},{}) ({},{})] | {}",
                     "Grid", "LE", +r.n, r.pts[0].x, r.pts[0].y, r.pts[1].x, r.pts[1].y,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Matrix: multi-dimensional array member ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<Matrix, std::endian::little, 1> view(storage);

        Matrix s{{{1, 2, 3}, {4, 5, 6}}};
        if (auto ec = view.write(s); ec) return 1;
        Matrix r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = r.m[0][0] == 1 && r.m[0][1] == 2 && r.m[0][2] == 3
               && r.m[1][0] == 4 && r.m[1][1] == 5 && r.m[1][2] == 6
               && std::to_integer<unsigned>(storage[0]) == 1
               && std::to_integer<unsigned>(storage[12]) == 4;
        std::println("[{:<10} {:<13}] [{:02x} {:02x} {:02x} | {:02x} {:02x} {:02x}] | {}",
                     "Matrix", "LE",
                     (unsigned)r.m[0][0], (unsigned)r.m[0][1], (unsigned)r.m[0][2],
                     (unsigned)r.m[1][0], (unsigned)r.m[1][1], (unsigned)r.m[1][2],
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Top-level std::array as value type, both endians ---
    {
        std::array<std::byte, 32> raw_le{};
        bpt::binary_view<std::array<std::uint32_t, 4>, std::endian::little, 1> view_le(raw_le);
        std::array<std::uint32_t, 4> s{0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00};
        if (auto ec = view_le.write(s); ec) return 1;
        bool le_ok = std::to_integer<unsigned>(raw_le[0]) == 0x44
                  && std::to_integer<unsigned>(raw_le[15]) == 0xDD;

        std::array<std::byte, 32> raw_be{};
        bpt::binary_view<std::array<std::uint32_t, 4>, std::endian::big, 1> view_be(raw_be);
        if (auto ec = view_be.write(s); ec) return 1;
        bool be_ok = std::to_integer<unsigned>(raw_be[0]) == 0x11
                  && std::to_integer<unsigned>(raw_be[15]) == 0x00;

        std::array<std::uint32_t, 4> rl{}, rb{};
        if (auto ec = view_le.read(rl); ec) return 1;
        if (auto ec = view_be.read(rb); ec) return 1;
        bool ok = le_ok && be_ok && rl == s && rb == s;
        std::println("[{:<10} {:<13}] {:x} {:x} {:x} {:x} | {}",
                     "StdArray", "LE/BE", rl[0], rl[1], rl[2], rl[3], ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    return 0;
}
