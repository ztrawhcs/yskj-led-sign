#include "TextProgram.h"
#include "AA55Packet.h"
#include "Commands.h"
#include "../config.h"
#include <mbedtls/sha1.h>

static void appendLE16(std::vector<uint8_t>& v, uint16_t val) {
    v.push_back(val & 0xFF);
    v.push_back((val >> 8) & 0xFF);
}

static void appendLE32(std::vector<uint8_t>& v, uint32_t val) {
    v.push_back(val & 0xFF);
    v.push_back((val >> 8) & 0xFF);
    v.push_back((val >> 16) & 0xFF);
    v.push_back((val >> 24) & 0xFF);
}

static void appendBytes(std::vector<uint8_t>& v, const uint8_t* data, size_t len) {
    v.insert(v.end(), data, data + len);
}

static void appendVarLen(std::vector<uint8_t>& v, size_t n) {
    auto vl = AA55::varLen(n);
    v.insert(v.end(), vl.begin(), vl.end());
}

std::vector<std::vector<uint8_t>> buildTextProgram(const String& text,
                                                     const TextProgramConfig& cfg) {
    uint32_t ts = (uint32_t)(millis() / 1000) + 1000000;
    uint8_t progIdx = cfg.programId - 1;

    // SHA1 resource hash
    String resStr = "pro" + String(cfg.programId) + "_text_" + text + "_" + String(ts);
    uint8_t sha1Hash[20];
    mbedtls_sha1((const uint8_t*)resStr.c_str(), resStr.length(), sha1Hash);

    // Animation mapping
    uint8_t aniType = 1;
    if (cfg.scroll == "left") aniType = 0;
    else if (cfg.scroll == "right") aniType = 27;
    else if (cfg.scroll == "up") aniType = 4;
    else if (cfg.scroll == "down") aniType = 5;
    else if (cfg.scroll == "static") aniType = 1;

    uint8_t speedByte = (cfg.speed > 0) ? cfg.speed - 1 : 0;
    uint8_t timeStay = (cfg.scroll == "static") ? 3 : 0;
    uint8_t playMode = (cfg.duration == 0) ? 0x00 : 0x01;
    uint16_t playCount = (cfg.duration > 0) ? cfg.duration : 3;

    // Build full TLV payload
    std::vector<uint8_t> payload;
    payload.reserve(512);

    // Resource hash (tag 0x35)
    payload.push_back(0x35);
    payload.push_back(0x19);
    payload.push_back(progIdx);
    appendBytes(payload, sha1Hash, 20);
    appendLE32(payload, ts);

    // Program header (tag 0x1C)
    uint8_t progHdr[] = {0x1C, 0x08, 0x00, 0x00, 0x00, 0x00, playMode, 0x00};
    appendBytes(payload, progHdr, sizeof(progHdr));
    appendLE16(payload, playCount);

    // Region (tag 0x0D + 0x1D)
    uint8_t region[] = {0x0D, 0x01, 0x00, 0x1D, 0x09};
    appendBytes(payload, region, sizeof(region));
    appendLE16(payload, 0);       // x
    appendLE16(payload, 0);       // y
    appendLE16(payload, SIGN_W);  // w
    appendLE16(payload, SIGN_H);  // h
    payload.push_back(0x00);      // bg_flag

    // Item prefix (save/prog/rect/item/animation)
    uint8_t prefix[] = {
        0x09, 0x01, 0x01,
        0x0C, 0x01, progIdx,
        0x0D, 0x01, 0x00,
        0x0E, 0x01, 0x00,
        0x14, 0x03, aniType, speedByte, timeStay
    };
    appendBytes(payload, prefix, sizeof(prefix));

    // Text item header (tag 0x11, type=0x06)
    uint8_t textHdr[] = {
        0x11, 0x0A,
        0x00, 0x00, 0x00,
        0x06,               // text_audio
        0x01,               // code
        cfg.fontFamily,
        cfg.fontSize,
        0x00,               // rotate
        0x00, 0x00          // interval
    };
    appendBytes(payload, textHdr, sizeof(textHdr));

    // Build text data with color and formatting
    std::vector<uint8_t> textData;
    textData.reserve(256);

    if (!cfg.segments.empty()) {
        // Multi-color segments
        auto& first = cfg.segments[0];
        uint8_t colorHdr[] = {0x00, first.r, first.g, first.b, 0x00, 0x00, 0x00};
        appendBytes(textData, colorHdr, sizeof(colorHdr));

        // Alignment
        if (cfg.scroll == "static") {
            uint8_t align[] = {0x03, 0x00, 0x01, 0x03, 0x01, 0x01};
            appendBytes(textData, align, sizeof(align));
        } else {
            uint8_t align[] = {0x03, 0x00, 0x00, 0x03, 0x01, 0x00};
            appendBytes(textData, align, sizeof(align));
        }

        // Font
        uint8_t font[] = {0x01, 0x01, cfg.fontSize, cfg.fontFamily};
        appendBytes(textData, font, sizeof(font));

        // First segment text
        const char* s = first.text.c_str();
        appendBytes(textData, (const uint8_t*)s, strlen(s));
        if (cfg.scroll != "static") {
            const char* pad = "          ";
            appendBytes(textData, (const uint8_t*)pad, 10);
        }

        // Remaining segments
        for (size_t i = 1; i < cfg.segments.size(); i++) {
            auto& seg = cfg.segments[i];
            uint8_t segColor[] = {0x00, seg.r, seg.g, seg.b, 0x00, 0x00, 0x00};
            appendBytes(textData, segColor, sizeof(segColor));
            const char* st = seg.text.c_str();
            appendBytes(textData, (const uint8_t*)st, strlen(st));
        }
    } else {
        // Single color
        uint8_t colorHdr[] = {0x00, cfg.r, cfg.g, cfg.b, 0x00, 0x00, 0x00};
        appendBytes(textData, colorHdr, sizeof(colorHdr));

        if (cfg.scroll == "static") {
            uint8_t align[] = {0x03, 0x00, 0x01, 0x03, 0x01, 0x01};
            appendBytes(textData, align, sizeof(align));
        } else {
            uint8_t align[] = {0x03, 0x00, 0x00, 0x03, 0x01, 0x00};
            appendBytes(textData, align, sizeof(align));
        }

        uint8_t font[] = {0x01, 0x01, cfg.fontSize, cfg.fontFamily};
        appendBytes(textData, font, sizeof(font));

        const char* s = text.c_str();
        appendBytes(textData, (const uint8_t*)s, strlen(s));
        if (cfg.scroll != "static") {
            const char* pad = "          ";
            appendBytes(textData, (const uint8_t*)pad, 10);
        }
    }

    // Chunk the text data (max 960 bytes per chunk)
    const size_t chunkMax = 960;
    size_t totalChunks = (textData.size() + chunkMax - 1) / chunkMax;

    for (size_t i = 0; i < totalChunks; i++) {
        size_t offset = i * chunkMax;
        size_t chunkLen = std::min(chunkMax, textData.size() - offset);

        // Chunk header (tag 0x12)
        payload.push_back(0x12);
        payload.push_back(0x07);
        appendLE16(payload, (uint16_t)totalChunks);
        appendLE16(payload, (uint16_t)i);
        appendLE16(payload, (uint16_t)chunkLen);
        payload.push_back(0x00);

        // Data (tag 0x13)
        payload.push_back(0x13);
        appendVarLen(payload, chunkLen);
        appendBytes(payload, textData.data() + offset, chunkLen);
    }

    // Dispatch (tag 0x18)
    uint8_t dispatch[] = {0x18, 0x06, 0x02, progIdx, 0x00, playMode};
    appendBytes(payload, dispatch, sizeof(dispatch));
    appendLE16(payload, playCount);

    // Split into AA55 packets
    std::vector<std::vector<uint8_t>> packets;

    // Packet 0: delete existing
    auto delPkt = Commands::deleteAll();
    packets.push_back(delPkt);

    // Remaining packets (max 480 bytes payload each)
    const size_t maxPayload = 480;
    size_t pos = 0;
    while (pos < payload.size()) {
        size_t chunk = std::min(maxPayload, payload.size() - pos);
        packets.push_back(AA55::buildPacket(AA55::nextSno(), payload.data() + pos, chunk));
        pos += chunk;
    }

    return packets;
}
