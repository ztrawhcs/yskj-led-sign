#pragma once
#include <Arduino.h>

struct RGB;

class GifEncoder {
public:
    bool begin(uint8_t* outputBuf, size_t bufSize, int width, int height,
               const RGB* palette, int numColors, int frameDelayCs = 15);
    bool addFrame(const uint8_t* pixelIndices);
    size_t finish();

private:
    uint8_t* _buf;
    size_t _bufSize;
    size_t _pos;
    int _w, _h;
    int _numColors;
    int _minCodeSize;
    int _frameDelayCs;

    void writeByte(uint8_t b);
    void writeLE16(uint16_t v);
    void writeBytes(const uint8_t* data, size_t len);
    bool writeLZW(const uint8_t* pixels, int count);

    // Bit-packing state for LZW sub-blocks
    uint32_t _bitAccum;
    int _bitsInAccum;
    uint8_t _subBlock[256];
    int _subBlockLen;
    size_t _subBlockSizePos;

    void startSubBlocks();
    void writeBits(uint32_t code, int nbits);
    void flushSubBlock();
    void endSubBlocks();
};
