#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <print>
#include <ranges>
#include <system_error>

#include "binproto/core.hh"
#include "tests_util.hh"

// Test structs
struct Empty {
    static constexpr std::size_t answer = 42;
    constexpr bool foo() const noexcept { return true; }
};

struct Simple { std::int32_t a; std::int8_t b; std::int64_t c; };

struct Inherited : Empty, Simple {
    std::int16_t d;
    constexpr void bar() const noexcept {}
};

// Array member test structs
struct WithArray { std::int32_t head; std::uint16_t arr[3]; std::int8_t tail; };
struct WithStdArray { std::int32_t head; std::array<std::uint16_t, 3> arr; std::int8_t tail; };
struct Point { std::int16_t x; std::int16_t y; };
struct Grid { std::uint8_t n; Point pts[2]; };

struct Matrix {
    std::int32_t m[2][3];
    constexpr std::int32_t operator[](std::size_t i, std::size_t j) const noexcept { return m[i][j]; }
};

struct AfterByteArray { std::uint8_t pre; std::uint32_t m[3]; };

// New-API test structs
struct BitPair { std::uint8_t a : 3; std::uint8_t b : 5; };      // 1-byte group
struct WidePair { std::uint16_t x : 4; std::uint16_t y : 12; };  // 2-byte group
struct Chunk { std::uint32_t id; std::uint16_t seq; };           // 6 bytes, no groups

// supported_type / layout-accessibility test structs
struct HasPtr          { std::int32_t* p; };                 // pointer member: unsupported
struct HasConstMember  { const std::uint32_t x; };           // const member: unsupported
struct WithPrivate     { std::int32_t y; private: std::int32_t x; }; // private subobject

int run_scalar_tests() {
    // --- Roundtrip test: Simple, pack(1), scalar members ---
    {
        std::array<std::byte, 64> storage{};
        bpt::binary_view<Simple, std::endian::native, 1> view(storage);

        Simple s{0x11223344, 0x55, 0x66778899AABBCCDDLL};

        if (!view.write<^^Simple::a>(s.a)) {
            std::println("write a failed");
            return 1;
        }
        if (!view.write<^^Simple::b>(s.b)) {
            std::println("write b failed");
            return 1;
        }
        if (!view.write<^^Simple::c>(s.c)) {
            std::println("write c failed");
            return 1;
        }

        Simple r{};
        if (!view.read<^^Simple::a>(r.a)) {
            std::println("read a failed");
            return 1;
        }
        if (!view.read<^^Simple::b>(r.b)) {
            std::println("read b failed");
            return 1;
        }
        if (!view.read<^^Simple::c>(r.c)) {
            std::println("read c failed");
            return 1;
        }

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[{:<15} {:<13}] a={:#x} b={:#x} c={:#x} | roundtrip {}",
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

        (void) view.write<^^Simple::a>(s.a);
        (void) view.write<^^Simple::b>(s.b);
        (void) view.write<^^Simple::c>(s.c);

        Simple r{};
        (void) view.read<^^Simple::a>(r.a);
        (void) view.read<^^Simple::b>(r.b);
        (void) view.read<^^Simple::c>(r.c);

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[{:<15} {:<13}] a={:#x} b={:#x} c={:#x} | roundtrip {}",
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
        (void) view.write<^^Inherited::a>(s.a);
        (void) view.write<^^Inherited::b>(s.b);
        (void) view.write<^^Inherited::c>(s.c);
        (void) view.write<^^Inherited::d>(s.d);

        Inherited r{};
        (void) view.read<^^Simple::a>(r.a);
        (void) view.read<^^Simple::b>(r.b);
        (void) view.read<^^Simple::c>(r.c);
        (void) view.read<^^Inherited::d>(r.d);

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c && s.d == r.d);
        std::println("[{:<15} {:<13}] a={:#x} b={:#x} c={:#x} d={:#x} | roundtrip {}",
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

        (void) view.write<^^Simple::a>(s.a);
        (void) view.write<^^Simple::b>(s.b);
        (void) view.write<^^Simple::c>(s.c);

        // On a little-endian host, the wire bytes should be byte-swapped.
        std::int32_t wire_a = std::bit_cast<std::int32_t>(std::array<std::byte, 4>{
            storage[0], storage[1], storage[2], storage[3]});
        bool wire_ok = (wire_a == 0x44332211);
        std::println("[{:<15} {:<13}] wire a={:#x} (expect {:#x}) | {}",
                     "Simple", "BE pack(1)",
                     (unsigned)wire_a, 0x44332211U, wire_ok ? "OK" : "FAIL");
        if (!wire_ok) return 1;

        Simple r{};
        (void) view.read<^^Simple::a>(r.a);
        (void) view.read<^^Simple::b>(r.b);
        (void) view.read<^^Simple::c>(r.c);

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[{:<15} {:<13}] a={:#x} b={:#x} c={:#x} | roundtrip {}",
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

        if (!view.write(s)) {
            std::println("write failed");
            return 1;
        }

        Simple r{};
        if (!view.read(r)) {
            std::println("read failed");
            return 1;
        }

        bool ok = (s.a == r.a && s.b == r.b && s.c == r.c);
        std::println("[{:<15} {:<13}] a={:#x} b={:#x} c={:#x} | roundtrip {}",
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
        if (!view.write(s)) return 1;

        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x44
                    && std::to_integer<unsigned>(storage[3]) == 0x11
                    && std::to_integer<unsigned>(storage[4]) == 0x12
                    && std::to_integer<unsigned>(storage[5]) == 0x11
                    && std::to_integer<unsigned>(storage[8]) == 0x66
                    && std::to_integer<unsigned>(storage[9]) == 0x55
                    && std::to_integer<unsigned>(storage[10]) == 0x77;

        WithArray r{};
        if (!view.read(r)) return 1;
        bool ok = wire_ok && r.head == 0x11223344
               && r.arr[0] == 0x1112 && r.arr[1] == 0x3344 && r.arr[2] == 0x5566
               && r.tail == 0x77;
        std::println("[{:<15} {:<13}] head={:#x} arr={:x} {:x} {:x} tail={:x} | {}",
                     "WithArray", "LE", (unsigned)r.head, r.arr[0], r.arr[1], r.arr[2],
                     (unsigned)r.tail, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- WithArray: big-endian, per-element byte swap ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<WithArray, std::endian::big, 1> view(storage);

        WithArray s{0x11223344, {0x1112, 0x3344, 0x5566}, 0x77};
        if (!view.write(s)) return 1;

        bool wire_ok = std::to_integer<unsigned>(storage[4]) == 0x11
                    && std::to_integer<unsigned>(storage[5]) == 0x12
                    && std::to_integer<unsigned>(storage[8]) == 0x55;

        WithArray r{};
        if (!view.read(r)) return 1;
        bool ok = wire_ok && r.head == 0x11223344 && r.arr[0] == 0x1112
               && r.arr[2] == 0x5566 && r.tail == 0x77;
        std::println("[{:<15} {:<13}] wire arr[0]={:02x}{:02x} | {}",
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
        if (!sub.write(arr)) return 1;

        std::uint16_t rr[3]{};
        if (!sub.read(rr)) return 1;
        bool ok = rr[0] == 0xAAAA && rr[1] == 0xBBBB && rr[2] == 0xCCCC
               && std::to_integer<unsigned>(storage[4]) == 0xAA;
        std::println("[{:<15} {:<13}] {:x} {:x} {:x} | {}",
                     "WithArray", "subview", rr[0], rr[1], rr[2], ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- WithStdArray: std::array member ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<WithStdArray, std::endian::little, 1> view(storage);

        WithStdArray s{0x11223344, {0x1112, 0x3344, 0x5566}, 0x77};
        if (!view.write(s)) return 1;

        WithStdArray r{};
        if (!view.read(r)) return 1;
        bool ok = r.head == 0x11223344 && r.arr[0] == 0x1112 && r.arr[1] == 0x3344
               && r.arr[2] == 0x5566 && r.tail == 0x77
               && std::to_integer<unsigned>(storage[4]) == 0x12;
        std::println("[{:<15} {:<13}] head={:#x} arr={:x} {:x} {:x} tail={:x} | {}",
                     "StdArray", "LE", (unsigned)r.head, r.arr[0], r.arr[1], r.arr[2],
                     (unsigned)r.tail, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Grid: array of structs ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<Grid, std::endian::little, 1> view(storage);

        Grid s{3, {{1, 2}, {3, 4}}};
        if (!view.write(s)) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 3
                    && std::to_integer<unsigned>(storage[1]) == 1
                    && std::to_integer<unsigned>(storage[5]) == 3;

        Grid r{};
        if (!view.read(r)) return 1;
        bool ok = wire_ok && r.n == 3
               && r.pts[0].x == 1 && r.pts[0].y == 2
               && r.pts[1].x == 3 && r.pts[1].y == 4;
        std::println("[{:<15} {:<13}] n={} pts=[({},{}) ({},{})] | {}",
                     "Grid", "LE", +r.n, r.pts[0].x, r.pts[0].y, r.pts[1].x, r.pts[1].y,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Matrix: multi-dimensional array member ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<Matrix, std::endian::little, 1> view(storage);

        Matrix s{{{1, 2, 3}, {4, 5, 6}}};
        if (!view.write(s)) return 1;
        Matrix r{};
        if (!view.read(r)) return 1;
        bool ok = r.m[0][0] == 1 && r.m[0][1] == 2 && r.m[0][2] == 3
               && r.m[1][0] == 4 && r.m[1][1] == 5 && r.m[1][2] == 6
               && std::to_integer<unsigned>(storage[0]) == 1
               && std::to_integer<unsigned>(storage[12]) == 4;
        std::println("[{:<15} {:<13}] [{:02x} {:02x} {:02x} | {:02x} {:02x} {:02x}] | {}",
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
        if (!view_le.write(s)) return 1;
        bool le_ok = std::to_integer<unsigned>(raw_le[0]) == 0x44
                  && std::to_integer<unsigned>(raw_le[15]) == 0xDD;

        std::array<std::byte, 32> raw_be{};
        bpt::binary_view<std::array<std::uint32_t, 4>, std::endian::big, 1> view_be(raw_be);
        if (!view_be.write(s)) return 1;
        bool be_ok = std::to_integer<unsigned>(raw_be[0]) == 0x11
                  && std::to_integer<unsigned>(raw_be[15]) == 0x00;

        std::array<std::uint32_t, 4> rl{}, rb{};
        if (!view_le.read(rl)) return 1;
        if (!view_be.read(rb)) return 1;
        bool ok = le_ok && be_ok && rl == s && rb == s;
        std::println("[{:<15} {:<13}] {:x} {:x} {:x} {:x} | {}",
                     "StdArray", "LE/BE", rl[0], rl[1], rl[2], rl[3], ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Simple, pack(2): alignment capped at 2 bytes ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<Simple, std::endian::little, 2> view(storage);

        Simple s{0x11223344, 0x55, 0x66778899AABBCCDDLL};
        if (!view.write(s)) return 1;
        // a@0-3, b@4, c aligned to 2 -> @6, total 14
        bool wire_ok = std::to_integer<unsigned>(storage[4]) == 0x55
                    && std::to_integer<unsigned>(storage[5]) == 0x00
                    && std::to_integer<unsigned>(storage[6]) == 0xDD;

        Simple r{};
        if (!view.read(r)) return 1;
        bool ok = wire_ok && s.a == r.a && s.b == r.b && s.c == r.c;
        std::println("[{:<15} {:<13}] a@0 b@4 c@6 total=14 | {}",
                     "Simple", "pack(2)", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- WithArray, pack(2): array member alignment ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<WithArray, std::endian::little, 2> view(storage);

        WithArray s{0x11223344, {0x1112, 0x3344, 0x5566}, 0x77};
        if (!view.write(s)) return 1;
        // head@0-3, arr@4-9, tail@10, total 12
        bool wire_ok = std::to_integer<unsigned>(storage[4]) == 0x12
                    && std::to_integer<unsigned>(storage[9]) == 0x55
                    && std::to_integer<unsigned>(storage[10]) == 0x77;

        WithArray r{};
        if (!view.read(r)) return 1;
        bool ok = wire_ok && r.head == 0x11223344 && r.arr[2] == 0x5566 && r.tail == 0x77;
        std::println("[{:<15} {:<13}] arr@4 tail@10 total=12 | {}",
                     "WithArray", "pack(2)", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- AfterByteArray, pack(2): scalar then array member ---
    {
        std::array<std::byte, 32> storage{};
        bpt::binary_view<AfterByteArray, std::endian::little, 2> view(storage);

        AfterByteArray s{0x55, {0x11111111, 0x22222222, 0x33333333}};
        if (!view.write(s)) return 1;
        // pre@0, m aligned to 2 -> @2, m[2]@10-13, total 14
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x55
                    && std::to_integer<unsigned>(storage[1]) == 0x00
                    && std::to_integer<unsigned>(storage[2]) == 0x11
                    && std::to_integer<unsigned>(storage[10]) == 0x33;

        AfterByteArray r{};
        if (!view.read(r)) return 1;
        bool ok = wire_ok && r.pre == 0x55 && r.m[0] == 0x11111111 && r.m[2] == 0x33333333;
        std::println("[{:<15} {:<13}] m@2 total=14 | {}",
                     "AfterByteArray", "pack(2)", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- layout<T, Packed>() / offset_of<Mem, T, Packed>() ---
    {
        const auto l_bits = bpt::layout<BitPair, 1>();
        bool ok = l_bits.size() == 2
               && l_bits[0].bytes == 0 && l_bits[0].bits == 0
               && l_bits[1].bytes == 0 && l_bits[1].bits == 3;

        const auto l_plain = bpt::layout<Simple, 1>();
        ok = ok && l_plain.size() == 3
             && l_plain[0].bytes == 0 && l_plain[0].bits == 0
             && l_plain[1].bytes == 4 && l_plain[1].bits == 0
             && l_plain[2].bytes == 5 && l_plain[2].bits == 0;

        const auto oa = bpt::offset_of<^^BitPair::a, BitPair, 1>();
        const auto ob = bpt::offset_of<^^BitPair::b, BitPair, 1>();
        ok = ok && oa.bytes == 0 && oa.bits == 0
             && ob.bytes == 0 && ob.bits == 3;

        const auto oc = bpt::offset_of<^^Simple::c, Simple, 1>();
        ok = ok && oc.bytes == 5 && oc.bits == 0;

        using BitView = bpt::binary_view<BitPair, std::endian::little, 1>;
        const auto vo = BitView::offset_of<^^BitPair::b>();
        ok = ok && vo.bytes == 0 && vo.bits == 3;

        std::println("[{:<15} {:<13}] layout/offset_of (bit+plain+view) | {}",
                     "API", "layout", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- read_fundamental / write_fundamental ---
    {
        std::array<std::byte, 8> buf{};
        std::uint32_t v = 0x11223344, r = 0;
        bool ok = bpt::write_fundamental<std::endian::big>(v, buf)
               && bpt::read_fundamental<std::endian::big>(r, buf) && r == v
               && std::to_integer<unsigned>(buf[0]) == 0x11
               && std::to_integer<unsigned>(buf[3]) == 0x44;

        std::uint32_t too_small_r = 0;
        const std::span<const std::byte> one = std::span<const std::byte>(buf).first(1);
        const std::span<std::byte> one_mut = std::span<std::byte>(buf).first(1);
        ok = ok && !bpt::read_fundamental<std::endian::big>(too_small_r, one)
             && !bpt::write_fundamental<std::endian::big>(v, one_mut);

        std::println("[{:<15} {:<13}] u32 BE roundtrip + bounds | {}",
                     "API", "fundamental", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- batch read/write (std::span of values) ---
    {
        // BitPair: 1-byte elements -> direct-copy fast path, both endians
        std::array<std::byte, 4> buf{};
        bpt::binary_view<BitPair, std::endian::little, 1> view(buf);
        const std::array<BitPair, 4> in{{ {5, 25}, {0, 0}, {7, 1}, {6, 2} }};
        if (!view.write(std::span<const BitPair>(in))) return 1;
        bool ok = std::to_integer<unsigned>(buf[0]) == 0xCD
               && std::to_integer<unsigned>(buf[1]) == 0x00
               && std::to_integer<unsigned>(buf[2]) == 0x0F
               && std::to_integer<unsigned>(buf[3]) == 0x16;

        std::array<BitPair, 4> out{};
        bpt::readonly_binary_view<BitPair, std::endian::little, 1> rview(buf);
        ok = ok && rview.read(std::span<BitPair>(out))
             && out[0].a == 5 && out[0].b == 25 && out[2].a == 7 && out[3].b == 2;

        // too few bytes for the whole batch
        std::array<BitPair, 5> more{};
        ok = ok && !rview.read(std::span<BitPair>(more));

        // WidePair: same object/wire size but has a bit-field group ->
        // direct-copy must be rejected, element-wise path used instead
        std::array<std::byte, 4> wbuf{};
        bpt::binary_view<WidePair, std::endian::native, 1> wview(wbuf);
        const std::array<WidePair, 2> win{{ {0xA, 0xABC}, {0x1, 0x2} }};
        if (!wview.write(std::span<const WidePair>(win))) return 1;
        ok = ok && std::to_integer<unsigned>(wbuf[0]) == 0xCA
             && std::to_integer<unsigned>(wbuf[1]) == 0xAB
             && std::to_integer<unsigned>(wbuf[2]) == 0x21
             && std::to_integer<unsigned>(wbuf[3]) == 0x00;

        std::array<WidePair, 2> wout{};
        bpt::readonly_binary_view<WidePair, std::endian::native, 1> wrview(wbuf);
        ok = ok && wrview.read(std::span<WidePair>(wout))
             && wout[0].x == 0xA && wout[0].y == 0xABC && wout[1].x == 0x1 && wout[1].y == 0x2;

        // Chunk: native endian, no groups, sizeof == wire size -> direct copy
        std::array<std::byte, 12> cbuf{};
        bpt::binary_view<Chunk, std::endian::native, 1> cview(cbuf);
        const std::array<Chunk, 2> cin{{ {0x11223344, 0x5566}, {0xAABBCCDD, 0x7788} }};
        if (!cview.write(std::span<const Chunk>(cin))) return 1;
        ok = ok && std::to_integer<unsigned>(cbuf[0]) == 0x44
             && std::to_integer<unsigned>(cbuf[3]) == 0x11
             && std::to_integer<unsigned>(cbuf[4]) == 0x66
             && std::to_integer<unsigned>(cbuf[6]) == 0xDD
             && std::to_integer<unsigned>(cbuf[9]) == 0xAA
             && std::to_integer<unsigned>(cbuf[10]) == 0x88
             && std::to_integer<unsigned>(cbuf[11]) == 0x77;

        std::array<Chunk, 2> cout{};
        bpt::readonly_binary_view<Chunk, std::endian::native, 1> crview(cbuf);
        ok = ok && crview.read(std::span<Chunk>(cout)) && cout[0].id == 0x11223344
             && cout[1].seq == 0x7788;

        // Chunk, big endian: batch must fall back to element-wise byte swap
        std::array<std::byte, 12> cbuf_be{};
        bpt::binary_view<Chunk, std::endian::big, 1> cview_be(cbuf_be);
        if (!cview_be.write(std::span<const Chunk>(cin))) return 1;
        ok = ok && std::to_integer<unsigned>(cbuf_be[0]) == 0x11
             && std::to_integer<unsigned>(cbuf_be[3]) == 0x44
             && std::to_integer<unsigned>(cbuf_be[4]) == 0x55
             && std::to_integer<unsigned>(cbuf_be[5]) == 0x66;

        std::array<Chunk, 2> cout_be{};
        bpt::readonly_binary_view<Chunk, std::endian::big, 1> crview_be(cbuf_be);
        ok = ok && crview_be.read(std::span<Chunk>(cout_be))
             && cout_be[0].id == 0x11223344 && cout_be[1].seq == 0x7788;

        std::println("[{:<15} {:<13}] batch direct-copy + group + BE | {}",
                     "API", "batch", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- consumed / remained / consumed_view / remained_view ---
    {
        using ChunkView = bpt::binary_view<Chunk, std::endian::little, 1>;
        static_assert(ChunkView::wire_size() == 6);

        std::array<std::byte, 16> buf{};
        ChunkView view(buf);
        bool ok = view.consumed().size() == 6
               && view.remained().size() == 10
               && view.remained(2).size() == 4
               && view.consumed_view().buffer().size() == 6
               && view.remained_view<Chunk>(1).buffer().size() == 10
               && view.remained_view<Chunk>(2).buffer().size() == 4
               && view.remained_view<Chunk>(3).buffer().size() == 0;

        // saturating count: no multiply overflow can pass the bounds check
        constexpr std::size_t huge = std::size_t{1} << 62; // 6 * 2^62 wraps to 0
        ok = ok && view.remained(huge).size() == 0
             && view.remained_view<Chunk>(huge).buffer().size() == 0;

        // too-small buffer: every split helper fails closed
        std::array<std::byte, 4> small{};
        ChunkView sv(small);
        ok = ok && sv.consumed().size() == 0
             && sv.remained().size() == 0
             && sv.consumed_view().buffer().size() == 0
             && sv.remained_view<Chunk>().buffer().size() == 0;

        std::println("[{:<15} {:<13}] splits + saturating count + fail-closed | {}",
                     "API", "consume", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- view equality (identity-based: same buffer pointer + size) ---
    {
        using ChunkView = bpt::binary_view<Chunk, std::endian::little, 1>;
        using ChunkROView = bpt::readonly_binary_view<Chunk, std::endian::little, 1>;

        std::array<std::byte, 8> a{};
        std::array<std::byte, 8> b{}; // same content, different buffer

        ChunkView va(a), vb(b);
        ChunkView vcopy(va);
        bool ok = va == vcopy;              // copies share the same buffer
        ok = ok && !(va == vb);             // identity, not content equality

        const std::span<std::byte> full(a);
        ChunkView vfull(full), vpart(full.first(4));
        ok = ok && !(vfull == vpart);       // same data, different span size

        ChunkView d1, d2;
        ok = ok && d1 == d2;                // empty views compare equal
        ok = ok && !(d1 == vfull);

        ChunkROView ra(a), ra2(a), rb(b);
        ok = ok && ra == ra2 && !(ra == rb);
        ChunkROView from_mut(va);           // readonly view built from mutable view
        ok = ok && from_mut == ra;

        std::println("[{:<15} {:<13}] identity + size, mut/ro | {}",
                     "API", "view == ", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- read: fixed-count element range ---
    {
        using InChunk = bpt::binary_input_iterator<Chunk, std::endian::little, 1>;
        static_assert(std::input_iterator<InChunk>);
        static_assert(std::forward_iterator<InChunk>);

        const auto raw = testutil::to_bytes<12>(
            {0x44, 0x33, 0x22, 0x11, 0x66, 0x55,   // Chunk{0x11223344, 0x5566}
             0xDD, 0xCC, 0xBB, 0xAA, 0x88, 0x77}); // Chunk{0xAABBCCDD, 0x7788}

        auto r = bpt::read<Chunk>(raw);
        static_assert(std::ranges::forward_range<decltype(r)>);

        bool ok = r.begin() != r.end();
        const Chunk c0 = *r.begin();
        ok = ok && c0.id == 0x11223344 && c0.seq == 0x5566;
        ok = ok && std::ranges::distance(r) == 2;

        auto i0 = r.begin();
        auto i1 = i0 + 1;                          // operator+ jumps one element
        const Chunk c1 = *i1;
        ok = ok && i1 != i0 && c1.id == 0xAABBCCDD && c1.seq == 0x7788;
        i0 += 1;
        ok = ok && i0 == i1;
        auto old = i1++;                           // post-increment keeps old position
        ok = ok && old == i0 && i1 == r.end() && i1 == std::default_sentinel;
        auto i2 = r.begin();
        i2 += 2;
        ok = ok && i2 == r.end();

        // big endian decodes byte-swapped wire values
        const auto raw_be = testutil::to_bytes<12>(
            {0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
             0xAA, 0xBB, 0xCC, 0xDD, 0x77, 0x88});
        auto rb = bpt::read<Chunk, std::endian::big, 1>(raw_be);
        const Chunk bc0 = *rb.begin();
        const Chunk bc1 = *(rb.begin() + 1);
        ok = ok && bc0.id == 0x11223344 && bc0.seq == 0x5566
             && bc1.id == 0xAABBCCDD && bc1.seq == 0x7788;

        // bit-field element type round-trips through read as well
        const auto bp = testutil::to_bytes<4>({0xCD, 0x00, 0x0F, 0x16});
        auto rbp = bpt::read<BitPair>(bp);
        const BitPair p0 = *rbp.begin();
        const BitPair p3 = *(rbp.begin() + 3);
        ok = ok && p0.a == 5 && p0.b == 25 && p3.a == 6 && p3.b == 2;

        std::println("[{:<15} {:<13}] count range + sentinel + endian + bits | {}",
                     "API", "read", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- binary_input_iterator ---
    {
        const auto raw = testutil::to_bytes<6>({0x44, 0x33, 0x22, 0x11, 0x66, 0x55});
        bpt::readonly_binary_view<Chunk, std::endian::little, 1> view(raw);
        using InChunk = bpt::binary_input_iterator<Chunk, std::endian::little, 1>;

        InChunk it(view);
        bool ok = it != std::default_sentinel;
        const Chunk c0 = *it;
        ok = ok && c0.id == 0x11223344 && c0.seq == 0x5566;

        InChunk end = it + 1;                        // exactly one element left
        ok = ok && end == std::default_sentinel;     // fully consumed -> sentinel
        ok = ok && it != end;
        ++it;
        ok = ok && it == end;                        // multipass: same position

        InChunk reset(view);
        InChunk old = reset++;                       // post-increment keeps old position
        ok = ok && old == InChunk(view) && reset == end;

        // default-constructed / empty-view iterator is immediately at the end
        InChunk dflt;
        bpt::readonly_binary_view<Chunk, std::endian::little, 1> empty;
        InChunk eit(empty);
        ok = ok && dflt == std::default_sentinel && eit == std::default_sentinel;

        // advancing past the end still compares as end
        InChunk past = it + 2;
        ok = ok && past == std::default_sentinel;

        // += skips elements without dereferencing them
        const auto raw2 = testutil::to_bytes<12>(
            {0x44, 0x33, 0x22, 0x11, 0x66, 0x55,
             0xDD, 0xCC, 0xBB, 0xAA, 0x88, 0x77});
        bpt::readonly_binary_view<Chunk, std::endian::little, 1> view2(raw2);
        InChunk it2(view2);
        it2 += 1;
        const Chunk c1 = *it2;
        ok = ok && c1.id == 0xAABBCCDD && c1.seq == 0x7788;

        std::println("[{:<15} {:<13}] deref/inc/+/sentinel | {}",
                     "API", "in-iter ", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- binary_output_iterator ---
    {
        using OutChunk = bpt::binary_output_iterator<Chunk, std::endian::little, 1>;
        static_assert(std::output_iterator<OutChunk, Chunk>);

        // sequential write with ++; exact-fit buffer ends at sentinel
        std::array<std::byte, 12> buf{};
        bpt::binary_view<Chunk, std::endian::little, 1> view(buf);
        OutChunk it(view);
        *it = Chunk{0x11223344, 0x5566};
        ++it;
        *it = Chunk{0xAABBCCDD, 0x7788};
        ++it;
        bool ok = it == std::default_sentinel;
        ok = ok && testutil::expect_bytes(std::span<const std::byte>(buf),
                     testutil::to_bytes<12>(
                         {0x44, 0x33, 0x22, 0x11, 0x66, 0x55,
                          0xDD, 0xCC, 0xBB, 0xAA, 0x88, 0x77}),
                     "API", "out-iter");

        // += skips elements without writing them
        std::array<std::byte, 12> buf2{};
        bpt::binary_view<Chunk, std::endian::little, 1> view2(buf2);
        OutChunk skip(view2);
        skip += 1;
        *skip = Chunk{0xAABBCCDD, 0x7788};
        ok = ok && skip == OutChunk(view2.remained_view<Chunk>(1)); // positioned at element 1
        ok = ok && testutil::expect_bytes(std::span<const std::byte>(buf2),
                     testutil::to_bytes<12>(
                         {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                          0xDD, 0xCC, 0xBB, 0xAA, 0x88, 0x77}),
                     "API", "out-skip");

        // std::copy drives the iterator to its sentinel
        const std::array<Chunk, 3> vals{{ {1, 2}, {3, 4}, {0x11223344, 0x5566} }};
        std::array<std::byte, 18> buf3{};
        bpt::binary_view<Chunk, std::endian::little, 1> view3(buf3);
        auto out = std::copy(vals.begin(), vals.end(), OutChunk(view3));
        ok = ok && out == std::default_sentinel;
        ok = ok && testutil::expect_bytes(std::span<const std::byte>(buf3),
                     testutil::to_bytes<18>(
                         {0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
                          0x03, 0x00, 0x00, 0x00, 0x04, 0x00,
                          0x44, 0x33, 0x22, 0x11, 0x66, 0x55}),
                     "API", "out-copy");

        // big-endian and bit-field value types
        std::array<std::byte, 6> buf_be{};
        bpt::binary_view<Chunk, std::endian::big, 1> view_be(buf_be);
        bpt::binary_output_iterator<Chunk, std::endian::big, 1> it_be(view_be);
        *it_be = Chunk{0x11223344, 0x5566};
        ++it_be;
        ok = ok && it_be == std::default_sentinel
             && testutil::expect_bytes(std::span<const std::byte>(buf_be),
                       testutil::to_bytes<6>({0x11, 0x22, 0x33, 0x44, 0x55, 0x66}),
                       "API", "out-be");

        std::array<std::byte, 2> bpbuf{};
        bpt::binary_view<BitPair, std::endian::little, 1> bpview(bpbuf);
        bpt::binary_output_iterator<BitPair, std::endian::little, 1> bpit(bpview);
        *bpit = BitPair{5, 25};
        ++bpit;
        *bpit = BitPair{0, 0};
        ok = ok && testutil::expect_bytes(std::span<const std::byte>(bpbuf),
                     testutil::to_bytes<2>({0xCD, 0x00}),
                     "API", "out-bit");

        std::println("[{:<15} {:<13}] write/++/+/copy/BE/bitfield | {}",
                     "API", "out-iter", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- supported_type concept / layout accessibility checks ---
    {
        static_assert(bpt::details::supported_type<Chunk, 1>);
        static_assert(bpt::details::supported_type<BitPair, 1>);
        static_assert(bpt::details::supported_type<std::uint32_t, 1>);
        static_assert(!bpt::details::supported_type<HasPtr, 1>);         // pointer member
        static_assert(!bpt::details::supported_type<HasConstMember, 1>); // const member
        static_assert(!bpt::details::supported_type<WithPrivate, 1>);    // private subobject

        bool ok = true;
        std::println("[{:<15} {:<13}] reject ptr/const/private | {}",
                     "API", "support ", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    return 0;
}
