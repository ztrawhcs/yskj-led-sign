#include "ProgramUpload.h"
#include "AA55Packet.h"
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

std::vector<std::vector<uint8_t>> buildGifProgram(
    const uint8_t* gifData, size_t gifLen, uint8_t programId) {

    uint32_t ts = (uint32_t)(millis() / 1000) + 1000000;
    uint8_t progIdx = programId - 1;

    // SHA1 resource hash
    String resStr = "pro" + String(programId) + "_gif_" + String(ts);
    uint8_t sha1Hash[20];
    mbedtls_sha1((const uint8_t*)resStr.c_str(), resStr.length(), sha1Hash);

    // Build elements in forward order, then arrange wire order
    // Element 0: Program header
    std::vector<uint8_t> progHdr;
    progHdr.reserve(40);
    uint8_t delCmd[] = {0x08, 0x02, 0x00, progIdx};
    appendBytes(progHdr, delCmd, 4);
    uint8_t saveProg[] = {0x09, 0x01, 0x01, 0x0C, 0x01, progIdx};
    appendBytes(progHdr, saveProg, 6);
    // Tag 0x1C: play_mode=0 (loop), count=0 (infinite), sync=0
    uint8_t progTag[] = {0x1C, 0x06, 0x00, 0x00};
    appendBytes(progHdr, progTag, 4);
    appendLE16(progHdr, 0);  // play_count: 0 = infinite loop
    appendLE16(progHdr, 0);  // time_sync
    // Region
    uint8_t rectTag[] = {0x0D, 0x01, 0x00, 0x1D, 0x09};
    appendBytes(progHdr, rectTag, 5);
    appendLE16(progHdr, 0);       // x
    appendLE16(progHdr, 0);       // y
    appendLE16(progHdr, SIGN_W);  // w
    appendLE16(progHdr, SIGN_H);  // h
    progHdr.push_back(0x00);      // bg_flag

    // Element 1: Item header
    std::vector<uint8_t> itemHdr;
    itemHdr.reserve(24);
    uint8_t itemPrefix[] = {
        0x09, 0x01, 0x01,
        0x0C, 0x01, progIdx,
        0x0D, 0x01, 0x00,
        0x0E, 0x01, 0x00,
        0x14, 0x03, 0x01, 0x00, 0xFF,  // animation: static(1), speed=0, stay=255
        0x11, 0x04, 0x00, 0x01, 0x00, 0x0A  // type=graphic(0x0A)
    };
    appendBytes(itemHdr, itemPrefix, sizeof(itemPrefix));

    // Elements 2..N+1: Data chunks
    const size_t chunkMax = 960;
    size_t totalChunks = (gifLen + chunkMax - 1) / chunkMax;

    std::vector<std::vector<uint8_t>> chunkElems;
    chunkElems.reserve(totalChunks);

    for (size_t i = 0; i < totalChunks; i++) {
        size_t offset = i * chunkMax;
        size_t chunkLen = gifLen - offset;
        if (chunkLen > chunkMax) chunkLen = chunkMax;

        std::vector<uint8_t> elem;
        elem.reserve(chunkLen + 30);
        uint8_t chunkPrefix[] = {
            0x09, 0x01, 0x01,
            0x0C, 0x01, progIdx,
            0x0D, 0x01, 0x00,
            0x0E, 0x01, 0x00
        };
        appendBytes(elem, chunkPrefix, sizeof(chunkPrefix));
        // Chunk header tag 0x12
        elem.push_back(0x12);
        elem.push_back(0x07);
        appendLE16(elem, (uint16_t)totalChunks);
        appendLE16(elem, (uint16_t)i);
        appendLE16(elem, (uint16_t)chunkMax); // always 960 per JS R()
        elem.push_back(0x00);
        // Data tag 0x13
        elem.push_back(0x13);
        appendVarLen(elem, chunkLen);
        appendBytes(elem, gifData + offset, chunkLen);

        chunkElems.push_back(std::move(elem));
    }

    // Element N+2: Dispatch
    std::vector<uint8_t> dispatch = {0x18, 0x04, 0x02, progIdx, 0x00, 0xFF};

    // Element N+3: Resource hash
    std::vector<uint8_t> resHash;
    resHash.reserve(28);
    resHash.push_back(0x35);
    resHash.push_back(0x19);
    resHash.push_back(progIdx);
    appendBytes(resHash, sha1Hash, 20);
    appendLE32(resHash, ts);

    // Wire order (matching JS de() reverse iteration):
    // prog_hdr, item_hdr, chunk_(N-1)...chunk_0, dispatch, res_hash
    std::vector<std::vector<uint8_t>> wireOrder;
    wireOrder.push_back(progHdr);
    wireOrder.push_back(itemHdr);
    for (int i = (int)chunkElems.size() - 1; i >= 0; i--)
        wireOrder.push_back(chunkElems[i]);
    wireOrder.push_back(dispatch);
    wireOrder.push_back(resHash);

    // Each element becomes its own AA55 packet (total > 997 bytes)
    std::vector<std::vector<uint8_t>> packets;
    packets.reserve(wireOrder.size());
    for (auto& elem : wireOrder) {
        packets.push_back(AA55::buildPacket(AA55::nextSno(),
            elem.data(), elem.size(), 0xC1, 2));
    }

    return packets;
}
