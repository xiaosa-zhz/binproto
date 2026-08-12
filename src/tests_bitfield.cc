#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <print>
#include <system_error>
#include <utility>

#include "binproto.hh"

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

// Extreme bit-field edge cases
struct AnonPad { std::uint8_t a : 3; std::uint8_t : 4; std::uint8_t b : 5; };      // anon non-zero-width inside a group
struct AnonLead { std::uint8_t : 2; std::uint8_t a : 6; };                          // anon non-zero-width at group start
struct MultiZero { std::uint8_t a : 3; std::uint8_t : 0; std::uint8_t b : 5; std::uint8_t : 0; std::uint8_t c : 3; };
struct ConsecZero { std::uint8_t a : 3; std::uint8_t : 0; std::uint8_t : 0; std::uint8_t b : 5; };
struct TailZero { std::uint8_t a : 3; std::uint8_t b : 5; std::uint8_t : 0; };
struct BitBase2 { std::uint8_t a : 3; std::uint8_t : 2; };
struct BitDerived2 : BitBase2 { std::uint8_t b : 5; };                              // inheritance + anon bit-field in base
struct DeepDerived : BitBase2 { std::uint8_t : 0; std::uint8_t c : 3; std::uint32_t d; }; // inheritance + zero-width + anon + scalar

// Group restart alignment: a zero-width bit-field (or a preceding member) must
// align the next bit-field group to the declared type of its first bit-field,
// capped by the pack alignment.
struct RestartAligned { std::uint8_t a : 3; std::uint8_t : 0; std::uint32_t b : 5; };
struct AfterMember { std::uint8_t pre; std::uint32_t b : 5; };
struct AfterMember16 { std::uint8_t pre; std::uint16_t b : 5; };

int run_bitfield_tests() {
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
        std::println("[{:<15} {:<13}] wire={:#04x} {}",
                     "Flags", "LE pack(1)", std::to_integer<unsigned>(storage[0]),
                     wire_ok ? "OK" : "FAIL");
        if (!wire_ok) return 1;

        Flags r{};
        if (auto ec = view.read(r); ec) {
            std::println("flags LE read failed: {}", ec.message());
            return 1;
        }
        bool ok = r.a == 5 && r.b == 25;
        std::println("[{:<15} {:<13}] a={} b={} | roundtrip {}",
                     "Flags", "LE pack(1)", +r.a, +r.b, ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] wire={:#04x} {}",
                     "Flags", "BE pack(1)", std::to_integer<unsigned>(storage[0]),
                     wire_ok ? "OK" : "FAIL");
        if (!wire_ok) return 1;

        Flags r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = r.a == 5 && r.b == 25;
        std::println("[{:<15} {:<13}] a={} b={} | roundtrip {}",
                     "Flags", "BE pack(1)", +r.a, +r.b, ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] wire={:#04x} a={} b={} | {}",
                     "Flags", "LE member", std::to_integer<unsigned>(storage[0]), a, b,
                     ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] LE={:02x}{:02x} BE={:02x}{:02x} | {}",
                     "Wide", "LE/BE pack(1)",
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
        std::println("[{:<15} {:<13}] x in {{-3,-1,2}} | roundtrip {}",
                     "SignedBit", "LE", ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] wire={:#04x} e={} se={} | {}",
                     "EnumBits", "LE", std::to_integer<unsigned>(storage[0]), std::to_underlying(r.e),
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
        std::println("[{:<15} {:<13}] wire={:#04x} f={} g={} | {}",
                     "BoolBits", "LE", std::to_integer<unsigned>(storage[0]), rf, rg,
                     ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] plain={:#x} a={} b={} | {}",
                     "Mixed", "LE pack(1)", r.plain, +r.a, +r.b, ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] a={} b={} c={:#x} | {}",
                     "Terminated", "LE", +r.a, +r.b, r.c, ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] a={} b={} | {}",
                     "Derived", "LE", +r.a, +b, ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] 64-bit group | {}",
                     "U64Bits", "LE/BE", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- AnonPad: anonymous non-zero-width bit-field, 12-bit group, both endians ---
    {
        std::array<std::byte, 4> raw_le{};
        bpt::binary_view<AnonPad, std::endian::little, 1> view_le(raw_le);
        AnonPad s{5, 25};
        if (auto ec = view_le.write(s); ec) return 1;
        // group value = a | (b << 7) = 5 | (25 << 7) = 0xC85, LSB-first
        bool le_ok = std::to_integer<unsigned>(raw_le[0]) == 0x85
                  && std::to_integer<unsigned>(raw_le[1]) == 0x0C;

        std::array<std::byte, 4> raw_be{};
        bpt::binary_view<AnonPad, std::endian::big, 1> view_be(raw_be);
        if (auto ec = view_be.write(s); ec) return 1;
        // MSB-first, group left-aligned in the window: a in bits 15-13, b in bits 8-4 -> 0xA190
        bool be_ok = std::to_integer<unsigned>(raw_be[0]) == 0xA1
                  && std::to_integer<unsigned>(raw_be[1]) == 0x90;

        AnonPad rl{}, rb{};
        if (auto ec = view_le.read(rl); ec) return 1;
        if (auto ec = view_be.read(rb); ec) return 1;
        bool ok = le_ok && be_ok && rl.a == 5 && rl.b == 25 && rb.a == 5 && rb.b == 25;

        // member-wise access to a bit-field after an anonymous one
        std::uint8_t mb = 0;
        if (auto ec = view_le.read<^^AnonPad::b>(mb); ec) return 1;
        ok = ok && mb == 25;
        std::println("[{:<15} {:<13}] LE={:02x}{:02x} BE={:02x}{:02x} a={} b={} | {}",
                     "AnonPad", "LE/BE",
                     std::to_integer<unsigned>(raw_le[0]), std::to_integer<unsigned>(raw_le[1]),
                     std::to_integer<unsigned>(raw_be[0]), std::to_integer<unsigned>(raw_be[1]),
                     +rl.a, +rl.b, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- AnonLead: anonymous bit-field at group start ---
    {
        std::array<std::byte, 4> raw_le{};
        bpt::binary_view<AnonLead, std::endian::little, 1> view_le(raw_le);
        AnonLead s{0x2A};
        if (auto ec = view_le.write(s); ec) return 1;
        // a(6 bits) shifted left by the 2 leading anonymous bits -> 0xA8
        bool wire_ok = std::to_integer<unsigned>(raw_le[0]) == 0xA8;

        std::array<std::byte, 4> raw_be{};
        bpt::binary_view<AnonLead, std::endian::big, 1> view_be(raw_be);
        if (auto ec = view_be.write(s); ec) return 1;
        // MSB-first, left-aligned: a(6 bits) at group bits 2-7 -> byte bits 0-5 = 0x2A
        wire_ok = wire_ok && std::to_integer<unsigned>(raw_be[0]) == 0x2A;

        AnonLead rl{}, rb{};
        if (auto ec = view_le.read(rl); ec) return 1;
        if (auto ec = view_be.read(rb); ec) return 1;
        bool ok = wire_ok && rl.a == 0x2A && rb.a == 0x2A;
        std::println("[{:<15} {:<13}] wire={:#04x} a={} | {}",
                     "AnonLead", "LE/BE", std::to_integer<unsigned>(raw_le[0]), +rl.a,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- MultiZero: multiple zero-width bit-fields, three independent groups ---
    {
        std::array<std::byte, 4> raw_le{};
        bpt::binary_view<MultiZero, std::endian::little, 1> view_le(raw_le);
        MultiZero s{5, 25, 3};
        if (auto ec = view_le.write(s); ec) return 1;
        bool le_ok = std::to_integer<unsigned>(raw_le[0]) == 0x05
                  && std::to_integer<unsigned>(raw_le[1]) == 0x19
                  && std::to_integer<unsigned>(raw_le[2]) == 0x03;

        std::array<std::byte, 4> raw_be{};
        bpt::binary_view<MultiZero, std::endian::big, 1> view_be(raw_be);
        if (auto ec = view_be.write(s); ec) return 1;
        // BE groups are left-aligned in their byte: a<<5 | b<<3 | c<<5
        bool be_ok = std::to_integer<unsigned>(raw_be[0]) == 0xA0
                  && std::to_integer<unsigned>(raw_be[1]) == 0xC8
                  && std::to_integer<unsigned>(raw_be[2]) == 0x60;

        MultiZero rl{}, rb{};
        if (auto ec = view_le.read(rl); ec) return 1;
        if (auto ec = view_be.read(rb); ec) return 1;
        bool ok = le_ok && be_ok
               && rl.a == 5 && rl.b == 25 && rl.c == 3
               && rb.a == 5 && rb.b == 25 && rb.c == 3;

        // member-wise access in each split group
        std::uint8_t ma = 0, mb = 0, mc = 0;
        if (auto ec = view_le.read<^^MultiZero::a>(ma); ec) return 1;
        if (auto ec = view_le.read<^^MultiZero::b>(mb); ec) return 1;
        if (auto ec = view_le.read<^^MultiZero::c>(mc); ec) return 1;
        ok = ok && ma == 5 && mb == 25 && mc == 3;
        std::println("[{:<15} {:<13}] {:02x} {:02x} {:02x} | {}",
                     "MultiZero", "LE/BE",
                     std::to_integer<unsigned>(raw_le[0]), std::to_integer<unsigned>(raw_le[1]),
                     std::to_integer<unsigned>(raw_le[2]), ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- ConsecZero: consecutive zero-width bit-fields ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<ConsecZero, std::endian::little, 1> view(storage);

        ConsecZero s{5, 25};
        if (auto ec = view.write(s); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x05
                    && std::to_integer<unsigned>(storage[1]) == 0x19;

        ConsecZero r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 5 && r.b == 25;
        std::println("[{:<15} {:<13}] {:02x} {:02x} | {}",
                     "ConsecZero", "LE",
                     std::to_integer<unsigned>(storage[0]), std::to_integer<unsigned>(storage[1]),
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- TailZero: trailing zero-width bit-field adds no size ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<TailZero, std::endian::little, 1> view(storage);

        TailZero s{5, 25};
        if (auto ec = view.write(s); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0xCD;

        TailZero r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 5 && r.b == 25;
        std::println("[{:<15} {:<13}] wire={:#04x} | {}",
                     "TailZero", "LE", std::to_integer<unsigned>(storage[0]),
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- BitDerived2: bit-field inherited from a base that has an anonymous bit-field ---
    {
        std::array<std::byte, 4> storage{};
        bpt::binary_view<BitDerived2, std::endian::little, 1> view(storage);

        BitDerived2 s{{5}, 25};
        if (auto ec = view.write(s); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x05
                    && std::to_integer<unsigned>(storage[1]) == 0x19;

        BitDerived2 r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 5 && r.b == 25;

        // member-wise access to the inherited and the derived bit-field
        std::uint8_t a = 0, b = 0;
        if (auto ec = view.read<^^BitDerived2::a>(a); ec) return 1;
        if (auto ec = view.read<^^BitDerived2::b>(b); ec) return 1;
        if (auto ec = view.write<^^BitDerived2::b>(std::uint8_t{17}); ec) return 1;
        std::uint8_t b2 = 0;
        if (auto ec = view.read<^^BitDerived2::b>(b2); ec) return 1;
        ok = ok && a == 5 && b == 25 && b2 == 17;
        std::println("[{:<15} {:<13}] {:02x} {:02x} a={} b={} b'={} | {}",
                     "Derived2", "LE",
                     std::to_integer<unsigned>(storage[0]), std::to_integer<unsigned>(storage[1]),
                     a, b, b2, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- DeepDerived: inheritance + zero-width + anonymous bit-field + scalar ---
    {
        std::array<std::byte, 8> storage{};
        bpt::binary_view<DeepDerived, std::endian::little, 1> view(storage);

        DeepDerived s{{5}, 3, 0x12345678};
        if (auto ec = view.write(s); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x05
                    && std::to_integer<unsigned>(storage[1]) == 0x03
                    && std::to_integer<unsigned>(storage[2]) == 0x78
                    && std::to_integer<unsigned>(storage[5]) == 0x12;

        DeepDerived r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 5 && r.c == 3 && r.d == 0x12345678;

        // member-wise access to a member inside a base, after zero-width/anon fields
        std::uint32_t d = 0;
        if (auto ec = view.read<^^DeepDerived::d>(d); ec) return 1;
        if (auto ec = view.write<^^DeepDerived::d>(std::uint32_t{0xDEADBEEF}); ec) return 1;
        std::uint32_t d2 = 0;
        if (auto ec = view.read<^^DeepDerived::d>(d2); ec) return 1;
        ok = ok && d == 0x12345678 && d2 == 0xDEADBEEF;
        std::println("[{:<15} {:<13}] {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} a={} c={} d={:#x} d'={:#x} | {}",
                     "Deep", "LE",
                     std::to_integer<unsigned>(storage[0]), std::to_integer<unsigned>(storage[1]),
                     std::to_integer<unsigned>(storage[2]), std::to_integer<unsigned>(storage[3]),
                     std::to_integer<unsigned>(storage[4]), std::to_integer<unsigned>(storage[5]),
                     +r.a, +r.c, d, d2, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- RestartAligned pack(8): zero-width terminated group restarts on an aligned byte ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<RestartAligned, std::endian::little, 8> view(storage);

        RestartAligned s{5, 31};
        if (auto ec = view.write(s); ec) return 1;
        // a's uint8_t unit occupies byte 0; b's uint32_t unit must start at byte 4
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x05
                    && std::to_integer<unsigned>(storage[4]) == 0x1F;

        RestartAligned r{};
        if (auto ec = view.read(r); ec) return 1;

        // member-wise access across the aligned boundary
        std::uint32_t mb = 0;
        if (auto ec = view.read<^^RestartAligned::b>(mb); ec) return 1;
        bool ok = wire_ok && r.a == 5 && r.b == 31 && mb == 31;
        std::println("[{:<15} {:<13}] a@0 b@4 wire={:02x} {:02x} | {}",
                     "RestartAligned", "LE pack(8)",
                     std::to_integer<unsigned>(storage[0]), std::to_integer<unsigned>(storage[4]),
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- RestartAligned pack(1): same struct, alignment capped at 1 byte ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<RestartAligned, std::endian::little, 1> view(storage);

        RestartAligned s{5, 31};
        if (auto ec = view.write(s); ec) return 1;
        // pack(1) caps the unit alignment to 1 -> b immediately follows at byte 1
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x05
                    && std::to_integer<unsigned>(storage[1]) == 0x1F;

        RestartAligned r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 5 && r.b == 31;
        std::println("[{:<15} {:<13}] a@0 b@1 wire={:02x} {:02x} | {}",
                     "RestartAligned", "LE pack(1)",
                     std::to_integer<unsigned>(storage[0]), std::to_integer<unsigned>(storage[1]),
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- RestartAligned pack(8): big-endian roundtrip through the aligned unit ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<RestartAligned, std::endian::big, 8> view(storage);

        RestartAligned s{2, 17};
        if (auto ec = view.write(s); ec) return 1;
        // BE groups are left-aligned in their unit byte: a<<5 = 0x40, b<<3 = 0x88
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x40
                    && std::to_integer<unsigned>(storage[4]) == 0x88;

        RestartAligned r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 2 && r.b == 17;
        std::println("[{:<15} {:<13}] wire={:02x} {:02x} | {}",
                     "RestartAligned", "BE pack(8)",
                     std::to_integer<unsigned>(storage[0]), std::to_integer<unsigned>(storage[4]),
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- AfterMember pack(8): group after a scalar member starts on an aligned byte ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<AfterMember, std::endian::little, 8> view(storage);

        AfterMember s{0x55, 31};
        if (auto ec = view.write(s); ec) return 1;
        // pre occupies byte 0; b's uint32_t unit must start at byte 4, not byte 1
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x55
                    && std::to_integer<unsigned>(storage[1]) == 0x00
                    && std::to_integer<unsigned>(storage[4]) == 0x1F;

        AfterMember r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.pre == 0x55 && r.b == 31;
        std::println("[{:<15} {:<13}] pre@0 b@4 wire={:02x} {:02x} {:02x} | {}",
                     "AfterMember", "LE pack(8)",
                     std::to_integer<unsigned>(storage[0]), std::to_integer<unsigned>(storage[1]),
                     std::to_integer<unsigned>(storage[4]), ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- RestartAligned pack(2): unit alignment capped at 2 bytes ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<RestartAligned, std::endian::little, 2> view(storage);

        RestartAligned s{5, 31};
        if (auto ec = view.write(s); ec) return 1;
        // a@0; b's uint32_t unit aligned to min(2,4)=2 -> @2, total 4
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x05
                    && std::to_integer<unsigned>(storage[1]) == 0x00
                    && std::to_integer<unsigned>(storage[2]) == 0x1F;

        RestartAligned r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 5 && r.b == 31;
        std::println("[{:<15} {:<13}] a@0 b@2 total=4 | {}",
                     "RestartAligned", "LE pack(2)", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- RestartAligned pack(2): big-endian roundtrip ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<RestartAligned, std::endian::big, 2> view(storage);

        RestartAligned s{2, 17};
        if (auto ec = view.write(s); ec) return 1;
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x40
                    && std::to_integer<unsigned>(storage[2]) == 0x88;

        RestartAligned r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.a == 2 && r.b == 17;
        std::println("[{:<15} {:<13}] wire={:02x} {:02x} | {}",
                     "RestartAligned", "BE pack(2)",
                     std::to_integer<unsigned>(storage[0]), std::to_integer<unsigned>(storage[2]),
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- AfterMember pack(2): scalar then uint32_t bit-field unit ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<AfterMember, std::endian::little, 2> view(storage);

        AfterMember s{0x55, 31};
        if (auto ec = view.write(s); ec) return 1;
        // pre@0; b aligned to 2 -> @2, total 4
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x55
                    && std::to_integer<unsigned>(storage[2]) == 0x1F;

        AfterMember r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.pre == 0x55 && r.b == 31;
        std::println("[{:<15} {:<13}] pre@0 b@2 total=4 | {}",
                     "AfterMember", "LE pack(2)", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- AfterMember16 pack(2): uint16_t bit-field unit after a scalar ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<AfterMember16, std::endian::little, 2> view(storage);

        AfterMember16 s{0x55, 17};
        if (auto ec = view.write(s); ec) return 1;
        // uint16_t unit aligned to min(2,2)=2 -> @2
        bool wire_ok = std::to_integer<unsigned>(storage[0]) == 0x55
                    && std::to_integer<unsigned>(storage[2]) == 0x11;

        AfterMember16 r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.pre == 0x55 && r.b == 17;
        std::println("[{:<15} {:<13}] pre@0 b@2 total=4 | {}",
                     "AfterMember16", "LE pack(2)", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- MixedBits pack(2): bit-field group after uint32_t scalar ---
    {
        std::array<std::byte, 16> storage{};
        bpt::binary_view<MixedBits, std::endian::little, 2> view(storage);

        MixedBits s{0x11223344, 5, 25};
        if (auto ec = view.write(s); ec) return 1;
        // plain@0-3; uint8_t group aligned to 1 -> @4, total 6
        bool wire_ok = std::to_integer<unsigned>(storage[4]) == 0xCD;

        MixedBits r{};
        if (auto ec = view.read(r); ec) return 1;
        bool ok = wire_ok && r.plain == 0x11223344 && r.a == 5 && r.b == 25;
        std::println("[{:<15} {:<13}] group@4 total=6 | {}",
                     "Mixed", "LE pack(2)", ok ? "OK" : "FAIL");
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
        std::println("[{:<15} {:<13}] empty buffer | {}",
                     "Flags", "bounds", ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    return 0;
}
