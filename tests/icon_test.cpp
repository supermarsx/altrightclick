/**
 * @file icon_test.cpp
 * @brief Validate that the generated ICO contains expected sizes and payloads.
 */

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

struct IconEntry {
    uint8_t width;
    uint8_t height;
    uint8_t colorCount;
    uint8_t reserved;
    uint16_t planes;
    uint16_t bitCount;
    uint32_t bytesInRes;
    uint32_t imageOffset;
};

static uint16_t read16(const std::vector<uint8_t> &buf, size_t offset) {
    return static_cast<uint16_t>(buf[offset] | (buf[offset + 1] << 8));
}

static uint32_t read32(const std::vector<uint8_t> &buf, size_t offset) {
    return (static_cast<uint32_t>(buf[offset]) | (static_cast<uint32_t>(buf[offset + 1]) << 8) |
            (static_cast<uint32_t>(buf[offset + 2]) << 16) | (static_cast<uint32_t>(buf[offset + 3]) << 24));
}

static bool has_png_signature(const std::vector<uint8_t> &buf, size_t offset) {
    static const uint8_t sig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (offset + sizeof(sig) > buf.size())
        return false;
    for (size_t i = 0; i < sizeof(sig); ++i) {
        if (buf[offset + i] != sig[i])
            return false;
    }
    return true;
}

static bool has_bmp_header(const std::vector<uint8_t> &buf, size_t offset) {
    if (offset + 4 > buf.size())
        return false;
    uint32_t biSize = read32(buf, offset);
    return biSize == 40;
}

int main(int argc, char **argv) {
    const char *path = "build/x64/altrightclick.ico";
    if (argc > 1)
        path = argv[1];
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "failed to open " << path << "\n";
        return 2;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 6) {
        std::cerr << "file too small for ICO header\n";
        return 3;
    }
    uint16_t reserved = read16(data, 0);
    uint16_t type = read16(data, 2);
    uint16_t count = read16(data, 4);
    if (reserved != 0 || type != 1 || count == 0) {
        std::cerr << "invalid ICONDIR header\n";
        return 4;
    }
    size_t need = 6 + static_cast<size_t>(count) * 16;
    if (data.size() < need) {
        std::cerr << "file too small for directory entries\n";
        return 5;
    }
    std::vector<IconEntry> entries;
    entries.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        size_t off = 6 + static_cast<size_t>(i) * 16;
        IconEntry e{};
        e.width = data[off + 0];
        e.height = data[off + 1];
        e.colorCount = data[off + 2];
        e.reserved = data[off + 3];
        e.planes = read16(data, off + 4);
        e.bitCount = read16(data, off + 6);
        e.bytesInRes = read32(data, off + 8);
        e.imageOffset = read32(data, off + 12);
        entries.push_back(e);
    }
    std::vector<int> want = {256, 64, 48, 32, 16};
    for (int sz : want) {
        bool found = false;
        for (const auto &e : entries) {
            int w = (e.width == 0) ? 256 : e.width;
            if (w == sz) {
                found = true;
                if (e.bytesInRes == 0) {
                    std::cerr << "entry for " << sz << "px has zero length\n";
                    return 6;
                }
                size_t end = static_cast<size_t>(e.imageOffset) + static_cast<size_t>(e.bytesInRes);
                if (end > data.size()) {
                    std::cerr << "entry for " << sz << "px points outside file\n";
                    return 7;
                }
                if (sz == 256) {
                    if (!has_png_signature(data, e.imageOffset)) {
                        std::cerr << "256px entry missing PNG signature\n";
                        return 8;
                    }
                } else {
                    if (!has_bmp_header(data, e.imageOffset)) {
                        std::cerr << "entry for " << sz << "px missing BITMAPINFOHEADER\n";
                        return 9;
                    }
                }
                break;
            }
        }
        if (!found) {
            std::cerr << "missing size " << sz << "\n";
            return 10;
        }
    }
    std::cout << "ICO payload OK (" << entries.size() << " entries)\n";
    return 0;
}
