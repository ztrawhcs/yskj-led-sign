#include "GifEncoder.h"
#include "Framebuffer.h"
#include <string.h>

static const int HASH_SIZE = 2039;
static int16_t s_hashPrefix[HASH_SIZE];
static uint8_t s_hashSuffix[HASH_SIZE];
static int16_t s_hashCode[HASH_SIZE];

void GifEncoder::writeByte(uint8_t b) {
    if (_pos < _bufSize) _buf[_pos++] = b;
}

void GifEncoder::writeLE16(uint16_t v) {
    writeByte(v & 0xFF);
    writeByte((v >> 8) & 0xFF);
}

void GifEncoder::writeBytes(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len && _pos < _bufSize; i++)
        _buf[_pos++] = data[i];
}

bool GifEncoder::begin(uint8_t* outputBuf, size_t bufSize, int width, int height,
                       const RGB* palette, int numColors, int frameDelayCs) {
    _buf = outputBuf;
    _bufSize = bufSize;
    _pos = 0;
    _w = width;
    _h = height;
    _frameDelayCs = frameDelayCs;

    // Pad numColors to next power of 2 (min 4 for GIF)
    int gctSize = 2;
    int gctBits = 1;
    while (gctSize < numColors || gctSize < 4) {
        gctSize *= 2;
        gctBits++;
    }
    _numColors = gctSize;
    _minCodeSize = gctBits;

    // GIF89a header
    writeBytes((const uint8_t*)"GIF89a", 6);

    // Logical Screen Descriptor
    writeLE16(width);
    writeLE16(height);
    uint8_t packed = 0x80 | ((gctBits - 1) << 4) | (gctBits - 1);
    writeByte(packed);
    writeByte(0); // bg color index
    writeByte(0); // pixel aspect ratio

    // Global Color Table
    for (int i = 0; i < gctSize; i++) {
        if (i < numColors) {
            writeByte(palette[i].r);
            writeByte(palette[i].g);
            writeByte(palette[i].b);
        } else {
            writeByte(0); writeByte(0); writeByte(0);
        }
    }

    // Netscape Application Extension (infinite loop)
    writeByte(0x21); // extension
    writeByte(0xFF); // application
    writeByte(11);   // block size
    writeBytes((const uint8_t*)"NETSCAPE2.0", 11);
    writeByte(3);    // sub-block size
    writeByte(1);    // loop sub-block id
    writeLE16(0);    // loop count 0 = infinite
    writeByte(0);    // block terminator

    return _pos < _bufSize;
}

bool GifEncoder::addFrame(const uint8_t* pixelIndices) {
    // Graphic Control Extension
    writeByte(0x21); // extension
    writeByte(0xF9); // graphic control
    writeByte(4);    // block size
    writeByte(0x04); // disposal=1 (do not dispose), no transparency
    writeLE16(_frameDelayCs);
    writeByte(0);    // transparent color index (unused)
    writeByte(0);    // block terminator

    // Image Descriptor
    writeByte(0x2C);
    writeLE16(0);    // left
    writeLE16(0);    // top
    writeLE16(_w);
    writeLE16(_h);
    writeByte(0);    // no local color table

    // LZW Minimum Code Size
    writeByte(_minCodeSize);

    // LZW compressed data
    if (!writeLZW(pixelIndices, _w * _h))
        return false;

    // Block terminator
    writeByte(0);

    return _pos < _bufSize;
}

size_t GifEncoder::finish() {
    writeByte(0x3B); // GIF trailer
    return _pos;
}

void GifEncoder::startSubBlocks() {
    _bitAccum = 0;
    _bitsInAccum = 0;
    _subBlockLen = 0;
}

void GifEncoder::writeBits(uint32_t code, int nbits) {
    _bitAccum |= (code << _bitsInAccum);
    _bitsInAccum += nbits;
    while (_bitsInAccum >= 8) {
        _subBlock[_subBlockLen++] = _bitAccum & 0xFF;
        _bitAccum >>= 8;
        _bitsInAccum -= 8;
        if (_subBlockLen == 255) flushSubBlock();
    }
}

void GifEncoder::flushSubBlock() {
    if (_subBlockLen > 0) {
        writeByte((uint8_t)_subBlockLen);
        writeBytes(_subBlock, _subBlockLen);
        _subBlockLen = 0;
    }
}

void GifEncoder::endSubBlocks() {
    if (_bitsInAccum > 0) {
        _subBlock[_subBlockLen++] = _bitAccum & 0xFF;
        _bitAccum = 0;
        _bitsInAccum = 0;
    }
    flushSubBlock();
}

bool GifEncoder::writeLZW(const uint8_t* pixels, int count) {
    if (count == 0) return true;

    int clearCode = 1 << _minCodeSize;
    int eoiCode = clearCode + 1;
    int nextCode = clearCode + 2;
    int codeSize = _minCodeSize + 1;
    int maxCode = (1 << codeSize) - 1;

    memset(s_hashPrefix, 0xFF, sizeof(s_hashPrefix));

    startSubBlocks();

    writeBits(clearCode, codeSize);

    int prefix = pixels[0];

    for (int i = 1; i < count; i++) {
        int suffix = pixels[i];

        // Hash lookup for (prefix, suffix)
        int hash = ((prefix << 8) ^ suffix) % HASH_SIZE;
        if (hash < 0) hash += HASH_SIZE;

        int found = -1;
        for (int probe = 0; probe < HASH_SIZE; probe++) {
            int slot = (hash + probe) % HASH_SIZE;
            if (s_hashPrefix[slot] == -1) break; // empty
            if (s_hashPrefix[slot] == prefix && s_hashSuffix[slot] == suffix) {
                found = slot;
                break;
            }
        }

        if (found >= 0) {
            prefix = s_hashCode[found];
        } else {
            writeBits(prefix, codeSize);

            if (nextCode <= 4095) {
                int slot = hash;
                while (s_hashPrefix[slot] != -1)
                    slot = (slot + 1) % HASH_SIZE;

                s_hashPrefix[slot] = prefix;
                s_hashSuffix[slot] = suffix;
                s_hashCode[slot] = nextCode;

                if (nextCode > maxCode && codeSize < 12) {
                    codeSize++;
                    maxCode = (1 << codeSize) - 1;
                }
                nextCode++;
            } else {
                writeBits(clearCode, codeSize);
                nextCode = clearCode + 2;
                codeSize = _minCodeSize + 1;
                maxCode = (1 << codeSize) - 1;
                memset(s_hashPrefix, 0xFF, sizeof(s_hashPrefix));
            }

            prefix = suffix;
        }

        if (_pos >= _bufSize - 300) return false;
    }

    writeBits(prefix, codeSize);
    writeBits(eoiCode, codeSize);
    endSubBlocks();

    return _pos < _bufSize;
}
