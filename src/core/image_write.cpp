#include "core/image_write.h"

#include <array>
#include <cstring>
#include <string>

namespace lc {

namespace {

std::array<std::uint32_t, 256> MakeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        table[n] = c;
    }
    return table;
}

void PutU32BigEndian(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void PutU16LittleEndian(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void AppendChunk(std::vector<std::uint8_t>& out, const char type[4], std::span<const std::uint8_t> data) {
    PutU32BigEndian(out, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> crcInput;
    crcInput.reserve(4 + data.size());
    crcInput.insert(crcInput.end(), type, type + 4);
    crcInput.insert(crcInput.end(), data.begin(), data.end());
    out.insert(out.end(), crcInput.begin(), crcInput.end());
    PutU32BigEndian(out, Crc32(crcInput));
}

// zlib stream containing only stored (type 0) deflate blocks: no compression, always valid.
std::vector<std::uint8_t> ZlibStored(std::span<const std::uint8_t> raw) {
    std::vector<std::uint8_t> z;
    z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    z.push_back(0x78);  // CMF: deflate, 32 KiB window.
    z.push_back(0x01);  // FLG: no preset dictionary, fastest; (0x78 * 256 + 0x01) % 31 == 0.
    constexpr std::size_t kMaxBlock = 65535;
    std::size_t offset = 0;
    do {
        const std::size_t len = std::min(kMaxBlock, raw.size() - offset);
        const bool final = offset + len >= raw.size();
        z.push_back(final ? 0x01 : 0x00);  // BFINAL in bit 0, BTYPE = 00 (stored).
        PutU16LittleEndian(z, static_cast<std::uint16_t>(len));
        PutU16LittleEndian(z, static_cast<std::uint16_t>(~len));
        z.insert(z.end(), raw.begin() + offset, raw.begin() + offset + len);
        offset += len;
    } while (offset < raw.size());
    PutU32BigEndian(z, Adler32(raw));
    return z;
}

}  // namespace

std::uint32_t Crc32(std::span<const std::uint8_t> data) {
    static const std::array<std::uint32_t, 256> table = MakeCrcTable();
    std::uint32_t c = 0xFFFFFFFFu;
    for (const std::uint8_t b : data) {
        c = table[(c ^ b) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

std::uint32_t Adler32(std::span<const std::uint8_t> data) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    // Process in runs small enough that the sums cannot overflow before the modulo.
    std::size_t i = 0;
    while (i < data.size()) {
        const std::size_t end = std::min(data.size(), i + 5552);
        for (; i < end; ++i) {
            a += data[i];
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}

std::vector<std::uint8_t> EncodePng(const ImageRgba8& image) {
    const std::size_t rowBytes = static_cast<std::size_t>(image.width) * 4;
    std::vector<std::uint8_t> raw;
    raw.reserve((rowBytes + 1) * image.height);
    for (std::uint32_t y = 0; y < image.height; ++y) {
        raw.push_back(0);  // Filter type 0 (None) for this scanline.
        const std::uint8_t* row = image.pixels.data() + static_cast<std::size_t>(y) * rowBytes;
        raw.insert(raw.end(), row, row + rowBytes);
    }

    std::vector<std::uint8_t> png;
    static constexpr std::uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    png.insert(png.end(), std::begin(kSignature), std::end(kSignature));

    std::vector<std::uint8_t> ihdr;
    PutU32BigEndian(ihdr, image.width);
    PutU32BigEndian(ihdr, image.height);
    ihdr.push_back(8);  // Bit depth.
    ihdr.push_back(6);  // Colour type: RGBA.
    ihdr.push_back(0);  // Compression method.
    ihdr.push_back(0);  // Filter method.
    ihdr.push_back(0);  // No interlace.
    AppendChunk(png, "IHDR", ihdr);

    const std::vector<std::uint8_t> idat = ZlibStored(raw);
    AppendChunk(png, "IDAT", idat);
    AppendChunk(png, "IEND", {});
    return png;
}

std::vector<std::uint8_t> EncodePfm(const ImageRgbF32& image) {
    const std::string header = "PF\n" + std::to_string(image.width) + " " + std::to_string(image.height) + "\n-1.0\n";
    std::vector<std::uint8_t> out(header.begin(), header.end());
    const std::size_t rowFloats = static_cast<std::size_t>(image.width) * 3;
    out.reserve(out.size() + rowFloats * image.height * sizeof(float));
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const std::uint32_t srcRow = image.height - 1 - y;  // PFM stores the bottom row first.
        const float* row = image.pixels.data() + static_cast<std::size_t>(srcRow) * rowFloats;
        const std::size_t offset = out.size();
        out.resize(offset + rowFloats * sizeof(float));
        std::memcpy(out.data() + offset, row, rowFloats * sizeof(float));
    }
    return out;
}

}  // namespace lc
