#include "lc_test.h"

#include "core/image_write.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

std::uint32_t ReadU32BigEndian(const std::vector<std::uint8_t>& d, std::size_t at) {
    return (std::uint32_t{d[at]} << 24) | (std::uint32_t{d[at + 1]} << 16) | (std::uint32_t{d[at + 2]} << 8) | d[at + 3];
}

std::span<const std::uint8_t> Bytes(const char* text) {
    return {reinterpret_cast<const std::uint8_t*>(text), std::strlen(text)};
}

// Decodes a zlib stream made only of stored blocks (what EncodePng emits).
bool InflateStored(std::span<const std::uint8_t> z, std::vector<std::uint8_t>& out) {
    if (z.size() < 6 || z[0] != 0x78) return false;
    if (((z[0] << 8) | z[1]) % 31 != 0) return false;
    std::size_t i = 2;
    bool final = false;
    while (!final) {
        if (i + 5 > z.size()) return false;
        const std::uint8_t header = z[i++];
        if ((header & 0x06) != 0) return false;  // BTYPE must be 00.
        final = (header & 0x01) != 0;
        const std::uint16_t len = static_cast<std::uint16_t>(z[i] | (z[i + 1] << 8));
        const std::uint16_t nlen = static_cast<std::uint16_t>(z[i + 2] | (z[i + 3] << 8));
        i += 4;
        if (static_cast<std::uint16_t>(~len) != nlen) return false;
        if (i + len > z.size()) return false;
        out.insert(out.end(), z.begin() + i, z.begin() + i + len);
        i += len;
    }
    if (i + 4 != z.size()) return false;
    return ReadU32BigEndian(std::vector<std::uint8_t>(z.begin(), z.end()), i) == lc::Adler32(out);
}

}  // namespace

LC_TEST(image_crc32_and_adler32_known_values) {
    LC_CHECK_EQ(lc::Crc32(Bytes("123456789")), 0xCBF43926u);
    LC_CHECK_EQ(lc::Adler32(Bytes("Wikipedia")), 0x11E60398u);
    LC_CHECK_EQ(lc::Crc32({}), 0u);
    LC_CHECK_EQ(lc::Adler32({}), 1u);
}

LC_TEST(image_png_structure_and_payload) {
    lc::ImageRgba8 img;
    img.width = 2;
    img.height = 2;
    img.pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 10, 20, 30, 40};
    const std::vector<std::uint8_t> png = lc::EncodePng(img);

    static const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    LC_REQUIRE(png.size() > 8 + 25 + 12);
    LC_CHECK(std::memcmp(png.data(), kSig, 8) == 0);

    // Walk the chunks, checking every CRC.
    std::size_t at = 8;
    std::vector<std::string> types;
    std::vector<std::uint8_t> idat;
    while (at + 12 <= png.size()) {
        const std::uint32_t len = ReadU32BigEndian(png, at);
        const std::string type(png.begin() + at + 4, png.begin() + at + 8);
        LC_REQUIRE(at + 12 + len <= png.size());
        const std::uint32_t crc = lc::Crc32(std::span<const std::uint8_t>(png.data() + at + 4, len + 4));
        LC_CHECK_EQ(crc, ReadU32BigEndian(png, at + 8 + len));
        types.push_back(type);
        if (type == "IHDR") {
            LC_CHECK_EQ(len, 13u);
            LC_CHECK_EQ(ReadU32BigEndian(png, at + 8), 2u);
            LC_CHECK_EQ(ReadU32BigEndian(png, at + 12), 2u);
            LC_CHECK_EQ(png[at + 16], 8);  // Bit depth.
            LC_CHECK_EQ(png[at + 17], 6);  // RGBA.
        }
        if (type == "IDAT") {
            idat.insert(idat.end(), png.begin() + at + 8, png.begin() + at + 8 + len);
        }
        at += 12 + len;
    }
    LC_CHECK_EQ(at, png.size());
    LC_REQUIRE(types.size() == 3);
    LC_CHECK_EQ(types[0], std::string("IHDR"));
    LC_CHECK_EQ(types[1], std::string("IDAT"));
    LC_CHECK_EQ(types[2], std::string("IEND"));

    std::vector<std::uint8_t> raw;
    LC_REQUIRE(InflateStored(idat, raw));
    const std::vector<std::uint8_t> expected = {0, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 255, 255, 10, 20, 30, 40};
    LC_CHECK(raw == expected);
}

LC_TEST(image_png_large_image_uses_multiple_stored_blocks) {
    lc::ImageRgba8 img;
    img.width = 300;
    img.height = 100;  // 300*4+1 = 1201 bytes per row * 100 = 120100 raw bytes > 65535.
    img.pixels.assign(static_cast<std::size_t>(img.width) * img.height * 4, 7);
    const std::vector<std::uint8_t> png = lc::EncodePng(img);
    std::size_t at = 8;
    std::vector<std::uint8_t> idat;
    while (at + 12 <= png.size()) {
        const std::uint32_t len = ReadU32BigEndian(png, at);
        const std::string type(png.begin() + at + 4, png.begin() + at + 8);
        if (type == "IDAT") idat.insert(idat.end(), png.begin() + at + 8, png.begin() + at + 8 + len);
        at += 12 + len;
    }
    std::vector<std::uint8_t> raw;
    LC_REQUIRE(InflateStored(idat, raw));
    LC_CHECK_EQ(raw.size(), std::size_t{120100});
    LC_CHECK_EQ(raw[0], 0);
    LC_CHECK_EQ(raw[1], 7);
    LC_CHECK_EQ(raw[1201], 0);
}

LC_TEST(image_pfm_header_and_bottom_up_rows) {
    lc::ImageRgbF32 img;
    img.width = 2;
    img.height = 2;
    img.pixels = {1, 2, 3, 4, 5, 6,      // top row
                  7, 8, 9, 10, 11, 12};  // bottom row
    const std::vector<std::uint8_t> pfm = lc::EncodePfm(img);
    const std::string header = "PF\n2 2\n-1.0\n";
    LC_REQUIRE(pfm.size() == header.size() + 12 * sizeof(float));
    LC_CHECK(std::memcmp(pfm.data(), header.data(), header.size()) == 0);
    float first[3];
    std::memcpy(first, pfm.data() + header.size(), sizeof(first));
    LC_CHECK_NEAR(first[0], 7, 0);  // Bottom image row is stored first.
    LC_CHECK_NEAR(first[2], 9, 0);
    float last[3];
    std::memcpy(last, pfm.data() + pfm.size() - sizeof(last), sizeof(last));
    LC_CHECK_NEAR(last[2], 6, 0);
}
