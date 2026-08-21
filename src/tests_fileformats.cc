#include <array>
#include <cstddef>
#include <cstdint>
#include <print>
#include <span>

#include "binproto/core.hh"
#include "tests_util.hh"

// Real-world on-disk file formats, verified against byte sequences derived
// from the published specifications (Windows BMP, RIFF/WAVE, System V ABI ELF64).

// BITMAPFILEHEADER as declared with `#pragma pack(2)` in the Windows SDK:
// the 32-bit fields sit at unaligned offsets 2 and 10.
struct BitmapFileHeader {
    std::uint16_t type;        // 'BM' = 0x4D42
    std::uint32_t size;
    std::uint16_t reserved1;
    std::uint16_t reserved2;
    std::uint32_t off_bits;
};

// BITMAPINFOHEADER (40 bytes).
struct BitmapInfoHeader {
    std::uint32_t size;
    std::int32_t width;
    std::int32_t height;
    std::uint16_t planes;
    std::uint16_t bit_count;
    std::uint32_t compression;       // BI_RGB = 0
    std::uint32_t size_image;
    std::int32_t x_pels_per_meter;
    std::int32_t y_pels_per_meter;
    std::uint32_t clr_used;
    std::uint32_t clr_important;
};

// A complete BMP header: file header at 0, info header at 14.
struct BmpFile {
    BitmapFileHeader file_header;
    BitmapInfoHeader info_header;
};

// WavFmtChunk: RIFF "fmt " chunk of a PCM wave file.
struct WavFmtChunk {
    std::array<std::uint8_t, 4> chunk_id;   // "fmt "
    std::uint32_t chunk_size;               // 16 for PCM
    std::uint16_t audio_format;             // 1 = PCM
    std::uint16_t num_channels;
    std::uint32_t sample_rate;
    std::uint32_t byte_rate;
    std::uint16_t block_align;
    std::uint16_t bits_per_sample;
};

// The canonical 44-byte PCM WAV header.
struct WavFile {
    std::array<std::uint8_t, 4> riff_id;    // "RIFF"
    std::uint32_t riff_size;                // 36 + data_size
    std::array<std::uint8_t, 4> wave_id;    // "WAVE"
    WavFmtChunk fmt_chunk;
    std::array<std::uint8_t, 4> data_id;    // "data"
    std::uint32_t data_size;
};

enum class ElfClass : std::uint8_t { none = 0, bits32 = 1, bits64 = 2 };
enum class ElfData : std::uint8_t { none = 0, little = 1, big = 2 };
enum class OsAbi : std::uint8_t { sysv = 0 };
enum class ElfType : std::uint16_t {
    none = 0, relocatable = 1, executable = 2, dynamic = 3, core = 4,
};
enum class ElfMachine : std::uint16_t {
    none = 0, sparcv9 = 43, x86_64 = 62, aarch64 = 183,
};

// e_ident[16] of the ELF64 header (System V ABI).
struct ElfIdent {
    std::array<std::uint8_t, 4> magic;   // 0x7F 'E' 'L' 'F'
    ElfClass ei_class;
    ElfData ei_data;
    std::uint8_t ei_version;
    OsAbi ei_osabi;
    std::uint8_t ei_abiversion;
    std::array<std::uint8_t, 7> pad;
};

// ELF64 file header (System V ABI, figure 5.3): exactly 64 bytes.
struct Elf64Header {
    ElfIdent ident;
    ElfType type;
    ElfMachine machine;
    std::uint32_t version;
    std::uint64_t entry;
    std::uint64_t phoff;
    std::uint64_t shoff;
    std::uint32_t flags;
    std::uint16_t ehsize;
    std::uint16_t phentsize;
    std::uint16_t phnum;
    std::uint16_t shentsize;
    std::uint16_t shnum;
    std::uint16_t shstrndx;
};

int run_fileformat_tests() {
    // --- BITMAPFILEHEADER under pack(2): unaligned 32-bit fields ---
    {
        using View = bpt::binary_view<BitmapFileHeader, std::endian::little, 2>;
        static_assert(View::wire_size() == 14);

        static constexpr std::uint8_t golden_raw[] = {
            0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);

        const BitmapFileHeader fh{
            .type = 0x4D42, .size = 58, .reserved1 = 0, .reserved2 = 0, .off_bits = 54,
        };

        std::array<std::byte, 14> storage{};
        if (!View(storage).write(fh)) return 1;

        // bfSize lives at the unaligned offset 2 — the reason the Windows
        // SDK declares this struct inside #pragma pack(2).
        const bool wire_ok = std::to_integer<unsigned>(storage[2]) == 0x3A
                          && std::to_integer<unsigned>(storage[6]) == 0x00
                          && std::to_integer<unsigned>(storage[10]) == 0x36;
        if (!testutil::expect_bytes(storage, golden, "BMP filehdr", "pack(2)")) return 1;

        BitmapFileHeader r{};
        if (!View(storage).read(r)) return 1;
        const bool ok = wire_ok && r.type == 0x4D42 && r.size == 58 && r.off_bits == 54;
        std::println("[{:<15} {:<13}] type={:#06x} size={} off={:<3} | {}",
                     "BMP filehdr", "LE pack(2)", r.type, r.size, r.off_bits,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Complete 54-byte BMP header: pack(2) composite ---
    {
        using View = bpt::binary_view<BmpFile, std::endian::little, 2>;
        static_assert(View::wire_size() == 54);
        static_assert(bpt::binary_view<BitmapInfoHeader, std::endian::little, 2>::wire_size() == 40);

        static constexpr std::uint8_t golden_raw[] = {
            // BITMAPFILEHEADER: BM, size 58, reserved, pixel data at 54
            0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
            // BITMAPINFOHEADER: 40 bytes, 1x1 24bpp BI_RGB bottom-up
            0x28, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
            0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);

        const BmpFile bmp{
            .file_header = {.type = 0x4D42, .size = 58, .reserved1 = 0, .reserved2 = 0, .off_bits = 54},
            .info_header = {
                .size = 40, .width = 1, .height = 1,
                .planes = 1, .bit_count = 24,
                .compression = 0, .size_image = 4,
                .x_pels_per_meter = 2835, .y_pels_per_meter = 2835,
                .clr_used = 0, .clr_important = 0,
            },
        };

        std::array<std::byte, 54> storage{};
        const bpt::binary_view<BmpFile, std::endian::little, 2> view(storage);
        if (!view.write(bmp)) return 1;
        if (!testutil::expect_bytes(storage, golden, "BMP", "LE write")) return 1;

        // the info header must start right after the file header at offset 14
        const auto ih_view = view.subview<^^BmpFile::info_header>();
        std::uint32_t ih_size = 0;
        if (!ih_view.read<^^BitmapInfoHeader::size>(ih_size)) return 1;

        BmpFile r{};
        if (!view.read(r)) return 1;
        const bool ok = ih_size == 40 && r.info_header.width == 1 && r.info_header.height == 1
                     && r.info_header.bit_count == 24 && r.file_header.size == 58
                     && r.info_header.x_pels_per_meter == 2835;
        std::println("[{:<15} {:<13}] {}x{} bpp={} ih@14 total=54 | {}", "BMP", "LE pack(2)",
                     r.info_header.width, r.info_header.height, r.info_header.bit_count,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- Canonical 44-byte PCM WAV header ---
    {
        using View = bpt::binary_view<WavFile, std::endian::little, 1>;
        static_assert(View::wire_size() == 44);

        static constexpr std::uint8_t golden_raw[] = {
            0x52, 0x49, 0x46, 0x46, 0xAC, 0x58, 0x01, 0x00, 0x57, 0x41, 0x56, 0x45,
            0x66, 0x6D, 0x74, 0x20, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00,
            0x44, 0xAC, 0x00, 0x00, 0x10, 0xB1, 0x02, 0x00, 0x04, 0x00, 0x10, 0x00,
            0x64, 0x61, 0x74, 0x61, 0x88, 0x58, 0x01, 0x00,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);
        static constexpr std::uint32_t data_size = 88200;   // half a second of CD audio

        const WavFile wav{
            .riff_id = {'R', 'I', 'F', 'F'},
            .riff_size = 36 + data_size,
            .wave_id = {'W', 'A', 'V', 'E'},
            .fmt_chunk = {
                .chunk_id = {'f', 'm', 't', ' '},
                .chunk_size = 16, .audio_format = 1, .num_channels = 2,
                .sample_rate = 44100, .byte_rate = 176400,
                .block_align = 4, .bits_per_sample = 16,
            },
            .data_id = {'d', 'a', 't', 'a'},
            .data_size = data_size,
        };

        std::array<std::byte, 44> storage{};
        if (!View(storage).write(wav)) return 1;
        if (!testutil::expect_bytes(storage, golden, "WAV", "LE write")) return 1;

        WavFile r{};
        if (!bpt::readonly_binary_view<WavFile, std::endian::little, 1>(golden).read(r)) {
            return 1;
        }
        const bool ok = r.riff_id == std::array<std::uint8_t, 4>{'R', 'I', 'F', 'F'}
                     && r.riff_size == 88236 && r.fmt_chunk.sample_rate == 44100
                     && r.fmt_chunk.byte_rate == 176400 && r.fmt_chunk.block_align == 4
                     && r.fmt_chunk.bits_per_sample == 16 && r.data_size == data_size;
        std::println("[{:<15} {:<13}] pcm {}Hz x{} {}bit rate={} | {}", "WAV", "RIFF",
                     r.fmt_chunk.sample_rate, r.fmt_chunk.num_channels,
                     r.fmt_chunk.bits_per_sample, r.fmt_chunk.byte_rate,
                     ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- ELF64 header, little-endian (x86-64 executable) ---
    {
        using View = bpt::binary_view<Elf64Header, std::endian::little, 1>;
        static_assert(View::wire_size() == 64);
        static_assert(bpt::binary_view<ElfIdent, std::endian::little, 1>::wire_size() == 16);

        static constexpr std::uint8_t golden_raw[] = {
            0x7F, 0x45, 0x4C, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x3E, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x10, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x88, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x38, 0x00, 0x03, 0x00, 0x40, 0x00,
            0x0E, 0x00, 0x0D, 0x00,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);

        const Elf64Header eh{
            .ident = {
                .magic = {0x7F, 'E', 'L', 'F'},
                .ei_class = ElfClass::bits64, .ei_data = ElfData::little,
                .ei_version = 1, .ei_osabi = OsAbi::sysv, .ei_abiversion = 0,
                .pad = {},
            },
            .type = ElfType::executable,
            .machine = ElfMachine::x86_64,
            .version = 1,
            .entry = 0x401000,
            .phoff = 64,
            .shoff = 10888,
            .flags = 0,
            .ehsize = 64, .phentsize = 56, .phnum = 3,
            .shentsize = 64, .shnum = 14, .shstrndx = 13,
        };

        std::array<std::byte, 64> storage{};
        if (!View(storage).write(eh)) return 1;
        if (!testutil::expect_bytes(storage, golden, "ELF64 LE", "write")) return 1;

        Elf64Header r{};
        if (!bpt::readonly_binary_view<Elf64Header, std::endian::little, 1>(golden).read(r)) {
            return 1;
        }
        const bool ok = r.ident.magic == std::array<std::uint8_t, 4>{0x7F, 'E', 'L', 'F'}
                     && r.ident.ei_class == ElfClass::bits64
                     && r.ident.ei_data == ElfData::little
                     && r.type == ElfType::executable && r.machine == ElfMachine::x86_64
                     && r.entry == 0x401000 && r.phoff == 64 && r.shoff == 10888
                     && r.ehsize == 64 && r.phentsize == 56 && r.shentsize == 64;
        std::println("[{:<15} {:<13}] class={} machine={} entry={:#x} | {}", "ELF64 LE",
                     "SysV ABI", std::to_underlying(r.ident.ei_class),
                     std::to_underlying(r.machine), r.entry, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    // --- ELF64 header, big-endian: same struct re-targeted at SPARC V9 ---
    {
        using View = bpt::binary_view<Elf64Header, std::endian::big, 1>;
        static_assert(View::wire_size() == 64);

        static constexpr std::uint8_t golden_raw[] = {
            0x7F, 0x45, 0x4C, 0x46, 0x02, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x2B, 0x00, 0x00, 0x00, 0x01,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x38, 0x00, 0x02, 0x00, 0x40,
            0x00, 0x0A, 0x00, 0x09,
        };
        static constexpr auto golden = testutil::to_bytes(golden_raw);

        const Elf64Header eh{
            .ident = {
                .magic = {0x7F, 'E', 'L', 'F'},
                .ei_class = ElfClass::bits64, .ei_data = ElfData::big,
                .ei_version = 1, .ei_osabi = OsAbi::sysv, .ei_abiversion = 0,
                .pad = {},
            },
            .type = ElfType::executable,
            .machine = ElfMachine::sparcv9,
            .version = 1,
            .entry = 0x100000,
            .phoff = 64,
            .shoff = 0x1F00,
            .flags = 0,
            .ehsize = 64, .phentsize = 56, .phnum = 2,
            .shentsize = 64, .shnum = 10, .shstrndx = 9,
        };

        std::array<std::byte, 64> storage{};
        if (!View(storage).write(eh)) return 1;
        if (!testutil::expect_bytes(storage, golden, "ELF64 BE", "write")) return 1;

        Elf64Header r{};
        if (!bpt::readonly_binary_view<Elf64Header, std::endian::big, 1>(golden).read(r)) {
            return 1;
        }
        const bool ok = r.ident.ei_data == ElfData::big
                     && r.machine == ElfMachine::sparcv9 && r.type == ElfType::executable
                     && r.entry == 0x100000 && r.shoff == 0x1F00
                     && r.phnum == 2 && r.shstrndx == 9;
        std::println("[{:<15} {:<13}] machine={} entry={:#x} | {}", "ELF64 BE", "SPARCV9",
                     std::to_underlying(r.machine), r.entry, ok ? "OK" : "FAIL");
        if (!ok) return 1;
    }

    return 0;
}
