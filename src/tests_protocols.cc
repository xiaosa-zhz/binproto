#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>
#include <utility>

#include "binproto/core.hh"
#include "tests_util.hh"

// Real-world network protocol headers, verified against wire bytes derived
// from the published specifications (RFC 751/768/791/1035/5905/9293, IEEE 802.3).

// Ethernet II frame header (IEEE 802.3).
struct EthernetHeader {
    std::array<std::uint8_t, 6> dest_mac;
    std::array<std::uint8_t, 6> src_mac;
    std::uint16_t ethertype;
};

// IPv4 header (RFC 791), without options.
struct Ipv4Header {
    std::uint8_t version : 4;
    std::uint8_t ihl : 4;
    std::uint8_t dscp : 6;
    std::uint8_t ecn : 2;
    std::uint16_t total_length;
    std::uint16_t identification;
    std::uint16_t flags : 3;
    std::uint16_t fragment_offset : 13;
    std::uint8_t ttl;
    std::uint8_t protocol;
    std::uint16_t header_checksum;
    std::array<std::uint8_t, 4> src_address;
    std::array<std::uint8_t, 4> dest_address;
};

// TCP header (RFC 9293), without options.
struct TcpHeader {
    std::uint16_t source_port;
    std::uint16_t dest_port;
    std::uint32_t sequence_number;
    std::uint32_t acknowledgment_number;
    std::uint8_t data_offset : 4;
    std::uint8_t reserved : 3;
    std::uint8_t ns : 1;
    std::uint8_t cwr : 1;
    std::uint8_t ece : 1;
    std::uint8_t urg : 1;
    std::uint8_t ack_flag : 1;
    std::uint8_t psh : 1;
    std::uint8_t rst : 1;
    std::uint8_t syn : 1;
    std::uint8_t fin : 1;
    std::uint16_t window;
    std::uint16_t checksum;
    std::uint16_t urgent_pointer;
};

// UDP header (RFC 768).
struct UdpHeader {
    std::uint16_t source_port;
    std::uint16_t dest_port;
    std::uint16_t length;
    std::uint16_t checksum;
};

// DNS message header (RFC 1035 section 4.1.1). The flag bits form one
// consecutive 16-bit group: QR OPCODE AA TC RD RA Z RCODE, MSB first.
struct DnsHeader {
    std::uint16_t id;
    std::uint16_t qr : 1;
    std::uint16_t opcode : 4;
    std::uint16_t aa : 1;
    std::uint16_t tc : 1;
    std::uint16_t rd : 1;
    std::uint16_t ra : 1;
    std::uint16_t zero : 3;
    std::uint16_t rcode : 4;
    std::uint16_t qdcount;
    std::uint16_t ancount;
    std::uint16_t nscount;
    std::uint16_t arcount;
};

// NTPv4 header (RFC 5905), mode fields packed into the first byte,
// signed 8-bit precision, and fixed-width timestamps.
struct NtpHeader {
    std::uint8_t li : 2;
    std::uint8_t vn : 3;
    std::uint8_t mode : 3;
    std::uint8_t stratum;
    std::uint8_t poll;
    std::int8_t precision;
    std::uint32_t root_delay;
    std::uint32_t root_dispersion;
    std::uint32_t reference_id;
    std::uint32_t reference_ts_sec;
    std::uint32_t reference_ts_frac;
    std::uint32_t origin_ts_sec;
    std::uint32_t origin_ts_frac;
    std::uint32_t receive_ts_sec;
    std::uint32_t receive_ts_frac;
    std::uint32_t transmit_ts_sec;
    std::uint32_t transmit_ts_frac;
};

// A complete Ethernet frame carrying an options-less IPv4/UDP/DNS message:
// the exact byte layout of a real DNS query datagram on the wire.
struct UdpDnsFrame {
    EthernetHeader ethernet;
    Ipv4Header ipv4;
    UdpHeader udp;
    DnsHeader dns;
};

// Internet checksum (RFC 1071): ones' complement of the ones' complement sum
// of consecutive big-endian 16-bit words.
constexpr std::uint16_t inet_checksum(std::span<const std::byte> data) noexcept {
    std::uint32_t sum = 0;
    const auto even_size = data.size() & ~std::size_t{1};
    for (std::size_t i = 0; i < even_size; i += 2) {
        const auto word = static_cast<std::uint16_t>(
            (std::to_integer<unsigned>(data[i]) << 8) | std::to_integer<unsigned>(data[i + 1]));
        sum += word;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<std::uint16_t>(~sum);
}

// Pseudo-header checksum used by TCP and UDP over IPv4.
constexpr std::uint16_t pseudo_header_checksum(std::span<const std::byte> src,
                                               std::span<const std::byte> dest,
                                               std::uint8_t protocol_number,
                                               std::span<const std::byte> segment) {
    std::array<std::byte, 12> pseudo{};
    for (std::size_t i = 0; i < 4; ++i) {
        pseudo[i] = src[i];
        pseudo[4 + i] = dest[i];
    }
    pseudo[9] = std::byte{protocol_number};
    const auto length = static_cast<std::uint16_t>(segment.size());
    pseudo[10] = static_cast<std::byte>(length >> 8);
    pseudo[11] = static_cast<std::byte>(length & 0xFF);
    // concatenate by summing in two passes: sum(pseudo words) then fold with segment
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < 12; i += 2) {
        sum += static_cast<std::uint16_t>((std::to_integer<unsigned>(pseudo[i]) << 8)
                                          | std::to_integer<unsigned>(pseudo[i + 1]));
    }
    const auto even_size = segment.size() & ~std::size_t{1};
    for (std::size_t i = 0; i < even_size; i += 2) {
        sum += static_cast<std::uint16_t>((std::to_integer<unsigned>(segment[i]) << 8)
                                          | std::to_integer<unsigned>(segment[i + 1]));
    }
    if (segment.size() & 1) {
        sum += static_cast<std::uint16_t>(std::to_integer<unsigned>(segment.back()) << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<std::uint16_t>(~sum);
}

int run_protocol_tests() {
    // --- Ethernet II header: broadcast ARP frame ---
    {
        using View = bpt::binary_view<EthernetHeader, std::endian::big, 1>;
        static_assert(View::wire_size() == 14);

        static constexpr std::uint8_t golden_raw[] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x54, 0xE1,
            0xAD, 0x87, 0x92, 0x0E, 0x08, 0x06,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);

        const EthernetHeader frame{
            .dest_mac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
            .src_mac = {0x54, 0xE1, 0xAD, 0x87, 0x92, 0x0E},
            .ethertype = 0x0806,
        };

        std::array<std::byte, 14> storage{};
        if (!View(storage).write(frame)) return 1;
        if (!testutil::expect_bytes(storage, golden, "Ethernet", "BE write")) return 1;

        EthernetHeader r{};
        if (!bpt::readonly_binary_view<EthernetHeader, std::endian::big, 1>(golden).read(r)) {
            return 1;
        }
        const bool ok = r.dest_mac == frame.dest_mac && r.src_mac == frame.src_mac
                     && r.ethertype == 0x0806;
        std::println("[{:<15} {:<13}] type={:#06x} | {}", "Ethernet", "BE",
                     r.ethertype, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- IPv4 header (RFC 791): the classic example datagram ---
    // 45 00 00 73 00 00 40 00 40 11 b8 61 c0 a8 00 01 c0 a8 00 c7
    // version=4 ihl=5 tos=0 len=115 id=0 df frag=0 ttl=64 proto=udp cksum=b861
    // 192.168.0.1 -> 192.168.0.199
    {
        using View = bpt::binary_view<Ipv4Header, std::endian::big, 1>;
        static_assert(View::wire_size() == 20);

        static constexpr std::uint8_t golden_raw[] = {
            0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
            0xB8, 0x61, 0xC0, 0xA8, 0x00, 0x01, 0xC0, 0xA8, 0x00, 0xC7,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);

        const Ipv4Header packet{
            .version = 4, .ihl = 5, .dscp = 0, .ecn = 0,
            .total_length = 115, .identification = 0,
            .flags = 0b010, .fragment_offset = 0,
            .ttl = 64, .protocol = 17, .header_checksum = 0xB861,
            .src_address = {192, 168, 0, 1},
            .dest_address = {192, 168, 0, 199},
        };

        std::array<std::byte, 20> storage{};
        if (!View(storage).write(packet)) return 1;
        if (!testutil::expect_bytes(storage, golden, "IPv4", "BE write")) return 1;

        // RFC 1071 validity property: a correct checksum makes the whole
        // header sum to all-ones.
        bool wire_ok = inet_checksum(storage) == 0;

        Ipv4Header r{};
        if (!bpt::readonly_binary_view<Ipv4Header, std::endian::big, 1>(golden).read(r)) {
            return 1;
        }
        const bool ok = wire_ok && r.version == 4 && r.ihl == 5 && r.dscp == 0 && r.ecn == 0
                     && r.total_length == 115 && r.identification == 0
                     && r.flags == 0b010 && r.fragment_offset == 0
                     && r.ttl == 64 && r.protocol == 17 && r.header_checksum == 0xB861
                     && r.src_address == std::array<std::uint8_t, 4>{192, 168, 0, 1}
                     && r.dest_address == std::array<std::uint8_t, 4>{192, 168, 0, 199};
        std::println("[{:<15} {:<13}] ttl={} {}:{}->{} len={} cksum={:#06x} | {}",
                     "IPv4", "RFC 791", +r.ttl, +r.version, +r.ihl, +r.protocol,
                     r.total_length, r.header_checksum, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- IPv4 bit-fields written member-wise must land on spec offsets ---
    {
        std::array<std::byte, 20> storage{};
        bpt::binary_view<Ipv4Header, std::endian::big, 1> view(storage);

        if (!view.write<^^Ipv4Header::version>(std::uint8_t{4})) return 1;
        if (!view.write<^^Ipv4Header::ihl>(std::uint8_t{5})) return 1;
        if (!view.write<^^Ipv4Header::flags>(std::uint16_t{0b010})) return 1;
        if (!view.write<^^Ipv4Header::fragment_offset>(std::uint16_t{0x1234})) return 1;

        // version|ihl share byte 0; flags|frag share the 16-bit word at bytes 6..7
        // word = (flags 0b010 << 13) | frag 0x1234 = 0x5234
        const bool ok = std::to_integer<unsigned>(storage[0]) == 0x45
                     && std::to_integer<unsigned>(storage[6]) == 0x52
                     && std::to_integer<unsigned>(storage[7]) == 0x34;
        std::println("[{:<15} {:<13}] ver/ihl@0 flags/frag@6 | {}", "IPv4", "BE member",
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;

        std::uint16_t frag = 0;
        if (!view.read<^^Ipv4Header::fragment_offset>(frag)) return 1;
        if (frag != 0x1234) return 1;
    }

    // --- TCP header (RFC 9293): a canonical SYN with pseudo-header checksum ---
    {
        using View = bpt::binary_view<TcpHeader, std::endian::big, 1>;
        static_assert(View::wire_size() == 20);

        static constexpr std::uint8_t golden_raw[] = {
            0xD4, 0x31, 0x01, 0xBB, 0x1A, 0x2B, 0x3C, 0x4D, 0x00, 0x00,
            0x00, 0x00, 0x50, 0x02, 0xFA, 0xF0, 0x05, 0x31, 0x00, 0x00,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);
        static constexpr std::uint16_t expected_checksum = 0x0531;

        constexpr std::array<std::uint8_t, 4> local{192, 168, 1, 10};
        constexpr std::array<std::uint8_t, 4> remote{192, 168, 1, 1};

        const TcpHeader syn{
            .source_port = 54321, .dest_port = 443,
            .sequence_number = 0x1A2B3C4D, .acknowledgment_number = 0,
            .data_offset = 5, .reserved = 0, .ns = 0,
            .cwr = 0, .ece = 0, .urg = 0, .ack_flag = 0,
            .psh = 0, .rst = 0, .syn = 1, .fin = 0,
            .window = 64240, .checksum = expected_checksum, .urgent_pointer = 0,
        };

        std::array<std::byte, 20> storage{};
        if (!View(storage).write(syn)) return 1;
        if (!testutil::expect_bytes(storage, golden, "TCP", "BE write")) return 1;

        // Independently recompute the checksum from raw bytes via the IPv4
        // pseudo-header and compare against the value on the wire.
        auto segment = storage;
        segment[16] = std::byte{0};
        segment[17] = std::byte{0};
        const auto recomputed = pseudo_header_checksum(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(local.data()), 4),
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(remote.data()), 4),
            6, segment);
        const bool checksum_ok = recomputed == expected_checksum;

        TcpHeader r{};
        if (!bpt::readonly_binary_view<TcpHeader, std::endian::big, 1>(golden).read(r)) {
            return 1;
        }
        const bool ok = checksum_ok && r.source_port == 54321 && r.dest_port == 443
                     && r.sequence_number == 0x1A2B3C4D && r.data_offset == 5
                     && r.syn == 1 && r.fin == 0 && r.rst == 0 && r.ack_flag == 0
                     && r.window == 64240 && r.checksum == expected_checksum;
        std::println("[{:<15} {:<13}] {}->{} syn={} win={} cksum={:#06x} | {}",
                     "TCP", "RFC 9293", r.source_port, r.dest_port, +r.syn,
                     r.window, r.checksum, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- UDP header (RFC 768) ---
    {
        using View = bpt::binary_view<UdpHeader, std::endian::big, 1>;
        static_assert(View::wire_size() == 8);

        static constexpr std::uint8_t golden_raw[] = {
            0xCA, 0x6C, 0x00, 0x35, 0x00, 0x29, 0x54, 0x0B,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);

        const UdpHeader hdr{
            .source_port = 51820, .dest_port = 53, .length = 41, .checksum = 0x540B,
        };

        std::array<std::byte, 8> storage{};
        if (!View(storage).write(hdr)) return 1;
        if (!testutil::expect_bytes(storage, golden, "UDP", "BE write")) return 1;

        UdpHeader r{};
        if (!bpt::readonly_binary_view<UdpHeader, std::endian::big, 1>(golden).read(r)) {
            return 1;
        }
        const bool ok = r.source_port == 51820 && r.dest_port == 53
                     && r.length == 41 && r.checksum == 0x540B;
        std::println("[{:<15} {:<13}] {}->{} len={} | {}", "UDP", "RFC 768",
                     r.source_port, r.dest_port, r.length, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- DNS message header (RFC 1035): query and response flag words ---
    {
        using View = bpt::binary_view<DnsHeader, std::endian::big, 1>;
        static_assert(View::wire_size() == 12);

        static constexpr std::uint8_t query_raw[] = {
            0x13, 0x37, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        static constexpr std::uint8_t response_raw[] = {
            0x13, 0x37, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
        };
        static constexpr auto query_golden = testutil::to_bytes(query_raw);
        static constexpr auto response_golden = testutil::to_bytes(response_raw);

        // recursive query: RD set, QDCOUNT=1
        const DnsHeader query{
            .id = 0x1337, .qr = 0, .opcode = 0, .aa = 0, .tc = 0, .rd = 1,
            .ra = 0, .zero = 0, .rcode = 0,
            .qdcount = 1, .ancount = 0, .nscount = 0, .arcount = 0,
        };
        std::array<std::byte, 12> storage{};
        if (!View(storage).write(query)) return 1;
        if (!testutil::expect_bytes(storage, query_golden, "DNS query", "BE write")) return 1;

        // response: QR+RD+RA set, RCODE=0, QDCOUNT=1 ANCOUNT=1
        DnsHeader r{};
        if (!bpt::readonly_binary_view<DnsHeader, std::endian::big, 1>(response_golden).read(r)) {
            return 1;
        }
        const bool read_ok = r.id == 0x1337 && r.qr == 1 && r.opcode == 0 && r.rd == 1
                          && r.ra == 1 && r.rcode == 0 && r.qdcount == 1 && r.ancount == 1;

        const DnsHeader response{
            .id = 0x1337, .qr = 1, .opcode = 0, .aa = 0, .tc = 0, .rd = 1,
            .ra = 1, .zero = 0, .rcode = 0,
            .qdcount = 1, .ancount = 1, .nscount = 0, .arcount = 0,
        };
        std::array<std::byte, 12> storage2{};
        if (!View(storage2).write(response)) return 1;
        const bool write_ok = testutil::expect_bytes(storage2, response_golden, "DNS reply", "BE write");

        const bool ok = read_ok && write_ok;
        std::println("[{:<15} {:<13}] qr={} rd={} ra={} rcode={} an={} | {}",
                     "DNS", "RFC 1035", +r.qr, +r.rd, +r.ra, +r.rcode,
                     r.ancount, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- NTPv4 header (RFC 5905): server response with signed precision ---
    {
        using View = bpt::binary_view<NtpHeader, std::endian::big, 1>;
        static_assert(View::wire_size() == 48);

        static constexpr std::uint8_t golden_raw[] = {
            0x24, 0x02, 0x0A, 0xE9, 0x00, 0x00, 0x01, 0x86, 0x00, 0x00, 0x02, 0xBC,
            0x47, 0x50, 0x53, 0x00, 0xEA, 0x06, 0x0D, 0x00, 0x20, 0x00, 0x00, 0x00,
            0xEA, 0x06, 0x0C, 0xF0, 0x00, 0x00, 0x00, 0x00, 0xEA, 0x06, 0x0D, 0x00,
            0x30, 0x00, 0x00, 0x00, 0xEA, 0x06, 0x0D, 0x00, 0x40, 0x00, 0x00, 0x00,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);

        const NtpHeader server{
            .li = 0, .vn = 4, .mode = 4,
            .stratum = 2, .poll = 10, .precision = -23,
            .root_delay = 0x00000186, .root_dispersion = 0x000002BC,
            .reference_id = 0x47505300,   // 'GPS\0'
            .reference_ts_sec = 0xEA060D00, .reference_ts_frac = 0x20000000,
            .origin_ts_sec = 0xEA060CF0, .origin_ts_frac = 0x00000000,
            .receive_ts_sec = 0xEA060D00, .receive_ts_frac = 0x30000000,
            .transmit_ts_sec = 0xEA060D00, .transmit_ts_frac = 0x40000000,
        };

        std::array<std::byte, 48> storage{};
        if (!View(storage).write(server)) return 1;
        if (!testutil::expect_bytes(storage, golden, "NTPv4", "BE write")) return 1;

        NtpHeader r{};
        if (!bpt::readonly_binary_view<NtpHeader, std::endian::big, 1>(golden).read(r)) {
            return 1;
        }
        const bool ok = r.li == 0 && r.vn == 4 && r.mode == 4 && r.stratum == 2
                     && r.poll == 10 && r.precision == -23
                     && r.reference_id == 0x47505300
                     && r.transmit_ts_frac == 0x40000000;
        std::println("[{:<15} {:<13}] vn={} mode={} stratum={} precision={} ref='GPS' | {}",
                     "NTPv4", "RFC 5905", +r.vn, +r.mode, +r.stratum, +r.precision,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Full stack: Ethernet + IPv4 + UDP + DNS as one packed BE struct ---
    {
        using FrameView = bpt::binary_view<UdpDnsFrame, std::endian::big, 1>;
        static_assert(FrameView::wire_size() == 54);

        static constexpr std::uint8_t golden_raw[] = {
            // ethernet: a4:bb:6d:12:3f:90 -> 54:e1:ad:87:92:0e, IPv4
            0xA4, 0xBB, 0x6D, 0x12, 0x3F, 0x90, 0x54, 0xE1, 0xAD, 0x87, 0x92, 0x0E,
            0x08, 0x00,
            // ipv4: 192.168.1.10 -> 192.168.1.1, 61-byte datagram, cksum 0xc71b
            0x45, 0x00, 0x00, 0x3D, 0x30, 0x39, 0x00, 0x00, 0x40, 0x11,
            0xC7, 0x1B, 0xC0, 0xA8, 0x01, 0x0A, 0xC0, 0xA8, 0x01, 0x01,
            // udp: 51820 -> 53, length 41, cksum 0x540b
            0xCA, 0x6C, 0x00, 0x35, 0x00, 0x29, 0x54, 0x0B,
            // dns: query header, QDCOUNT=1
            0x13, 0x37, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            // ...followed by the variable-length question section, which lies
            // beyond wire_size(): "\03www\07example\03com\0" A IN
            0x03, 0x77, 0x77, 0x77, 0x07, 0x65, 0x78, 0x61, 0x6D, 0x70, 0x6C, 0x65,
            0x03, 0x63, 0x6F, 0x6D, 0x00, 0x00, 0x01, 0x00, 0x01,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);
        // only the modeled 54-byte header part must be produced by write()
        static constexpr auto header_golden = [] {
            std::array<std::byte, 54> out{};
            std::ranges::copy(std::span<const std::byte>(golden).first(out.size()),
                              out.begin());
            return out;
        }();

        const UdpDnsFrame frame{
            .ethernet = {
                .dest_mac = {0xA4, 0xBB, 0x6D, 0x12, 0x3F, 0x90},
                .src_mac = {0x54, 0xE1, 0xAD, 0x87, 0x92, 0x0E},
                .ethertype = 0x0800,
            },
            .ipv4 = {
                .version = 4, .ihl = 5, .dscp = 0, .ecn = 0,
                .total_length = 61, .identification = 0x3039,
                .flags = 0, .fragment_offset = 0,
                .ttl = 64, .protocol = 17, .header_checksum = 0xC71B,
                .src_address = {192, 168, 1, 10},
                .dest_address = {192, 168, 1, 1},
            },
            .udp = {
                .source_port = 51820, .dest_port = 53,
                .length = 41, .checksum = 0x540B,
            },
            .dns = {
                .id = 0x1337, .qr = 0, .opcode = 0, .aa = 0, .tc = 0, .rd = 1,
                .ra = 0, .zero = 0, .rcode = 0,
                .qdcount = 1, .ancount = 0, .nscount = 0, .arcount = 0,
            },
        };

        // buffer larger than wire_size(): trailing question section untouched
        std::array<std::byte, sizeof(golden_raw)> storage{};
        if (!FrameView(storage).write(frame)) return 1;
        if (!testutil::expect_bytes(storage, header_golden, "Eth/IP/UDP/DNS", "BE write")) {
            return 1;
        }
        bool tail_untouched = true;
        for (std::size_t i = FrameView::wire_size(); i < storage.size(); ++i) {
            tail_untouched = tail_untouched && storage[i] == std::byte{0};
        }

        // recompute both checksums from the written bytes, independently of
        // any layout knowledge
        const auto ip_span = std::span<const std::byte>(storage).subspan(14, 20);
        auto ip_copy_storage = std::array<std::byte, 20>{};
        std::ranges::copy(ip_span, ip_copy_storage.begin());
        ip_copy_storage[10] = std::byte{0};
        ip_copy_storage[11] = std::byte{0};
        const bool ip_ck_ok = inet_checksum(ip_copy_storage) == 0xC71B;

        // udp segment: 8-byte header + 12-byte dns header + 21-byte question;
        // the header part comes from the written buffer, while the variable-
        // length tail comes from the golden capture
        auto udp_seg_copy = std::array<std::byte, 41>{};
        std::ranges::copy(std::span<const std::byte>(storage).subspan(34, 20),
                          udp_seg_copy.begin());
        std::ranges::copy(std::span<const std::byte>(golden).subspan(54),
                          udp_seg_copy.begin() + 20);
        udp_seg_copy[6] = std::byte{0};
        udp_seg_copy[7] = std::byte{0};
        const auto ip_src_span = std::span<const std::byte>(storage).subspan(26, 4);
        const auto ip_dst_span = std::span<const std::byte>(storage).subspan(30, 4);
        const bool udp_ck_ok = pseudo_header_checksum(
            ip_src_span, ip_dst_span, 17, udp_seg_copy) == 0x540B;

        // parse the golden capture back through layered subviews
        bpt::readonly_binary_view<UdpDnsFrame, std::endian::big, 1> frame_view(golden);
        UdpDnsFrame parsed{};
        if (!frame_view.read(parsed)) return 1;

        std::uint16_t dns_id = 0, udp_sport = 0;
        const auto udp_view = frame_view.subview<^^UdpDnsFrame::udp>();
        if (!udp_view.read<^^UdpHeader::source_port>(udp_sport)) return 1;
        const auto dns_view = frame_view.subview<^^UdpDnsFrame::dns>();
        if (!dns_view.read<^^DnsHeader::id>(dns_id)) return 1;

        const bool ok = tail_untouched && ip_ck_ok && udp_ck_ok
                     && parsed.ipv4.total_length == 61 && parsed.udp.length == 41
                     && parsed.dns.qdcount == 1 && parsed.ethernet.ethertype == 0x0800
                     && udp_sport == 51820 && dns_id == 0x1337;
        std::println("[{:<15} {:<13}] eth@0 ip@14 udp@34 dns@42 total=54 | {}",
                     "Eth/IP/UDP/DNS", "stacked", ok ? "OK" : "FAIL");
        if (!ok) {
            std::println("    tail={} ip_ck={} udp_ck={} len={}/{} qd={} etype={:#x} sport={} dns_id={:#x}",
                         tail_untouched, ip_ck_ok, udp_ck_ok,
                         parsed.ipv4.total_length, parsed.udp.length,
                         parsed.dns.qdcount, parsed.ethernet.ethertype, udp_sport, dns_id);
            return 1;
        }
    }

    return 0;
}
