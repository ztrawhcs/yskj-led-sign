#include "Framebuffer.h"
#include "../protocol/AA55Packet.h"
#include <algorithm>

// ---------------------------------------------------------------------------
// Basic drawing
// ---------------------------------------------------------------------------

void Framebuffer::clear() {
    memset(_pixels, 0, sizeof(_pixels));
}

void Framebuffer::putPixel(int x, int y, RGB color) {
    if (x >= 0 && x < W && y >= 0 && y < H)
        _pixels[y * W + x] = color;
}

RGB Framebuffer::getPixel(int x, int y) const {
    if (x >= 0 && x < W && y >= 0 && y < H)
        return _pixels[y * W + x];
    return {0, 0, 0};
}

void Framebuffer::fillRect(int x0, int y0, int x1, int y1, RGB color) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = max(0, x0);
    y0 = max(0, y0);
    x1 = min(W - 1, x1);
    y1 = min(H - 1, y1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            _pixels[y * W + x] = color;
}

// ---------------------------------------------------------------------------
// rt_draw payload builders
// ---------------------------------------------------------------------------

std::vector<uint8_t> Framebuffer::_buildClearPayload() {
    // rt_draw type=1 (rectangle fill), black, full screen
    // Matches Python: 0x32, 0x0D, 0x01, R,G,B=0, type_rect=0x00,
    //   x0=0,y0=0, x1=95(0x5F),y1=21(0x15)  — all LE 16-bit
    return {
        0x32, 0x0D, 0x01,
        0x00, 0x00, 0x00,       // color = black
        0x00,                   // type_rect = filled
        0x00, 0x00,             // x0 = 0
        0x00, 0x00,             // y0 = 0
        (uint8_t)(W - 1), 0x00, // x1 = 95
        (uint8_t)(H - 1), 0x00  // y1 = 21
    };
}

std::vector<uint8_t> Framebuffer::_buildBitmapPayload(
    const uint8_t* bitmap, size_t bitmapLen, RGB color)
{
    // inner = 0x00 (type=bitmap) + R + G + B + x0_LE + y0_LE + x1_LE + y1_LE + bitmap_data
    std::vector<uint8_t> inner;
    inner.reserve(12 + bitmapLen);
    inner.push_back(0x00);      // type = bitmap
    inner.push_back(color.r);
    inner.push_back(color.g);
    inner.push_back(color.b);
    // x0 = 0
    inner.push_back(0x00); inner.push_back(0x00);
    // y0 = 0
    inner.push_back(0x00); inner.push_back(0x00);
    // x1 = 95
    inner.push_back((uint8_t)(W - 1)); inner.push_back(0x00);
    // y1 = 21
    inner.push_back((uint8_t)(H - 1)); inner.push_back(0x00);
    // bitmap data
    inner.insert(inner.end(), bitmap, bitmap + bitmapLen);

    // Wrap: 0x32 + var_len(inner.size()) + inner
    auto vl = AA55::varLen(inner.size());
    std::vector<uint8_t> payload;
    payload.reserve(1 + vl.size() + inner.size());
    payload.push_back(0x32);
    payload.insert(payload.end(), vl.begin(), vl.end());
    payload.insert(payload.end(), inner.begin(), inner.end());
    return payload;
}

// ---------------------------------------------------------------------------
// Color collection and quantization helpers
// ---------------------------------------------------------------------------

// Pack RGB into a 32-bit key for use as a map key
static uint32_t colorKey(RGB c) {
    return ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
}

static RGB keyToColor(uint32_t k) {
    return { (uint8_t)(k >> 16), (uint8_t)(k >> 8), (uint8_t)k };
}

static int colorDistSq(RGB a, RGB b) {
    int dr = (int)a.r - b.r;
    int dg = (int)a.g - b.g;
    int db = (int)a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

// ---------------------------------------------------------------------------
// buildRtDrawPackets — main entry point
// ---------------------------------------------------------------------------

struct ColorEntry {
    uint32_t key;
    int count;
};

std::vector<std::vector<uint8_t>> Framebuffer::buildRtDrawPackets(int maxColors) {
    // 1) Collect non-black pixels grouped by color
    //    Use a flat array approach to avoid std::map heap overhead.
    //    With maxColors typically <=12, we expect few unique colors.
    static const int MAX_UNIQUE = 256;
    ColorEntry entries[MAX_UNIQUE];
    int numUnique = 0;

    for (int i = 0; i < W * H; i++) {
        RGB c = _pixels[i];
        if (c.isBlack()) continue;
        uint32_t k = colorKey(c);
        // Linear search — fine for small color counts
        int idx = -1;
        for (int j = 0; j < numUnique; j++) {
            if (entries[j].key == k) { idx = j; break; }
        }
        if (idx >= 0) {
            entries[idx].count++;
        } else if (numUnique < MAX_UNIQUE) {
            entries[numUnique] = { k, 1 };
            numUnique++;
        }
    }

    if (numUnique == 0) {
        // All black — just send clear
        auto clearPayload = _buildClearPayload();
        auto pkt = AA55::buildPacket(AA55::nextSno(),
                                      clearPayload.data(), clearPayload.size(),
                                      0xC1, 2);
        return { pkt };
    }

    // 2) If too many unique colors, keep the top maxColors by pixel count
    //    and reassign every remaining pixel to its nearest kept color.
    //    Build a remapping table: remap[original_key] = kept_key
    //    We do this with parallel arrays since we're on embedded.

    // Sort entries by count descending
    std::sort(entries, entries + numUnique,
              [](const ColorEntry& a, const ColorEntry& b) {
                  return a.count > b.count;
              });

    int keepCount = min(numUnique, maxColors);

    // Build a remap: for colors beyond keepCount, find nearest kept color
    // keptKeys[0..keepCount-1] are the ones we keep
    // We'll build the final bitmap per kept color directly from pixel scan

    // Precompute kept colors for distance comparison
    RGB keptColors[256];  // maxColors is small, but MAX_UNIQUE is the bound
    for (int i = 0; i < keepCount; i++)
        keptColors[i] = keyToColor(entries[i].key);

    // If quantization needed, build a remap lookup for the excess colors
    // Map: excess color key -> index into keptColors
    struct RemapEntry { uint32_t key; int keptIdx; };
    RemapEntry remap[MAX_UNIQUE];
    int remapCount = 0;
    bool needRemap = (numUnique > maxColors);

    if (needRemap) {
        for (int i = keepCount; i < numUnique; i++) {
            RGB src = keyToColor(entries[i].key);
            int bestIdx = 0;
            int bestDist = colorDistSq(src, keptColors[0]);
            for (int j = 1; j < keepCount; j++) {
                int d = colorDistSq(src, keptColors[j]);
                if (d < bestDist) { bestDist = d; bestIdx = j; }
            }
            remap[remapCount++] = { entries[i].key, bestIdx };
        }
    }

    // 3) Build one bitmap per kept color
    static const int ROW_BYTES = (W + 7) / 8;  // 12
    static const int BITMAP_SIZE = ROW_BYTES * H; // 264

    // Allocate bitmaps for each kept color (on stack for small maxColors)
    // Use a flat buffer: keptBitmaps[colorIdx * BITMAP_SIZE + byte]
    static uint8_t bitmapBuf[12 * BITMAP_SIZE]; // 12 * 264 = 3168 bytes
    memset(bitmapBuf, 0, keepCount * BITMAP_SIZE);

    int keptCounts[12] = {};  // pixel count per kept color (for sorting)

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            RGB c = _pixels[y * W + x];
            if (c.isBlack()) continue;

            uint32_t k = colorKey(c);

            // Determine which kept color index this pixel maps to
            int ci = -1;
            for (int j = 0; j < keepCount; j++) {
                if (entries[j].key == k) { ci = j; break; }
            }
            if (ci < 0 && needRemap) {
                // Look up in remap
                for (int j = 0; j < remapCount; j++) {
                    if (remap[j].key == k) { ci = remap[j].keptIdx; break; }
                }
            }
            if (ci < 0) continue;  // shouldn't happen

            // Set the bit in the bitmap: MSB first, 12 bytes per row
            int byteIdx = y * ROW_BYTES + x / 8;
            int bitIdx = 7 - (x % 8);
            bitmapBuf[ci * BITMAP_SIZE + byteIdx] |= (1 << bitIdx);
            keptCounts[ci]++;
        }
    }

    // 4) Build packets
    std::vector<std::vector<uint8_t>> packets;
    packets.reserve(1 + keepCount);

    // Clear packet first
    {
        auto clearPayload = _buildClearPayload();
        packets.push_back(AA55::buildPacket(AA55::nextSno(),
                                             clearPayload.data(),
                                             clearPayload.size(),
                                             0xC1, 2));
    }

    // Sort kept colors by pixel count descending (largest layer first)
    int order[12];
    for (int i = 0; i < keepCount; i++) order[i] = i;
    std::sort(order, order + keepCount,
              [&](int a, int b) { return keptCounts[a] > keptCounts[b]; });

    // Build one packet per color layer
    for (int oi = 0; oi < keepCount; oi++) {
        int ci = order[oi];
        if (keptCounts[ci] == 0) continue;

        auto payload = _buildBitmapPayload(
            bitmapBuf + ci * BITMAP_SIZE, BITMAP_SIZE, keptColors[ci]);

        packets.push_back(AA55::buildPacket(AA55::nextSno(),
                                             payload.data(),
                                             payload.size(),
                                             0xC1, 2));
    }

    return packets;
}
