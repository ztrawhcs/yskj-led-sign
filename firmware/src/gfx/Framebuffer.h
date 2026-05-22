#pragma once
#include <Arduino.h>
#include <vector>

struct RGB {
    uint8_t r, g, b;

    bool operator==(const RGB& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const RGB& o) const { return !(*this == o); }
    bool isBlack() const { return r == 0 && g == 0 && b == 0; }
};

class Framebuffer {
public:
    static const int W = 96;
    static const int H = 22;
    static const int VISIBLE_H = 16;

    void clear();
    void putPixel(int x, int y, RGB color);
    RGB getPixel(int x, int y) const;
    void fillRect(int x0, int y0, int x1, int y1, RGB color);

    /// Build rt_draw packets from framebuffer content.
    /// Returns a vector of AA55 packets: one clear packet followed by one
    /// bitmap layer per unique non-black color, sorted largest-layer-first.
    std::vector<std::vector<uint8_t>> buildRtDrawPackets(int maxColors = 12);

    /// Build rt_draw packets for a sub-region only.
    std::vector<std::vector<uint8_t>> buildRegionPackets(
        int rx0, int ry0, int rx1, int ry1, int maxColors = 2);

    /// Build a region-clear payload (black rect fill for sub-region).
    static std::vector<uint8_t> buildRegionClear(int x0, int y0, int x1, int y1);

private:
    RGB _pixels[W * H] = {};

    // Internal: build the rt_draw clear payload
    static std::vector<uint8_t> _buildClearPayload();

    // Internal: build a region-clear payload
    static std::vector<uint8_t> _buildRegionClearPayload(int x0, int y0, int x1, int y1);

    // Internal: build an rt_draw bitmap payload for one color layer
    static std::vector<uint8_t> _buildBitmapPayload(
        const uint8_t* bitmap, size_t bitmapLen, RGB color);

    // Internal: build a region bitmap payload
    static std::vector<uint8_t> _buildRegionBitmapPayload(
        const uint8_t* bitmap, size_t bitmapLen, RGB color,
        int x0, int y0, int x1, int y1);
};
