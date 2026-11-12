#include "bmp.h"

#include <cstdio>

namespace {
void put16(std::vector<std::uint8_t>& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}
}  // namespace

bool writeBmp24(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgb) {
    if (width <= 0 || height <= 0 || rgb.size() != static_cast<std::size_t>(width) * height * 3) return false;
    const std::uint32_t rowBytes = (static_cast<std::uint32_t>(width) * 3 + 3u) & ~3u;
    const std::uint32_t pixelBytes = rowBytes * static_cast<std::uint32_t>(height);

    std::vector<std::uint8_t> out;
    out.reserve(54 + pixelBytes);
    out.push_back('B');
    out.push_back('M');
    put32(out, 54 + pixelBytes);
    put32(out, 0);
    put32(out, 54);
    put32(out, 40);  // BITMAPINFOHEADER
    put32(out, static_cast<std::uint32_t>(width));
    put32(out, static_cast<std::uint32_t>(height));  // positive: rows are stored bottom-up
    put16(out, 1);
    put16(out, 24);
    put32(out, 0);  // BI_RGB
    put32(out, pixelBytes);
    put32(out, 2835);
    put32(out, 2835);
    put32(out, 0);
    put32(out, 0);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = rgb.data() + static_cast<std::size_t>(y) * width * 3;
        for (int x = 0; x < width; ++x) {  // BMP stores BGR
            out.push_back(row[x * 3 + 2]);
            out.push_back(row[x * 3 + 1]);
            out.push_back(row[x * 3 + 0]);
        }
        for (std::uint32_t pad = static_cast<std::uint32_t>(width) * 3; pad < rowBytes; ++pad) out.push_back(0);
    }

    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    return std::fclose(f) == 0 && ok;
}
