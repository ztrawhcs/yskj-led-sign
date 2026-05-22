#include "AA55Packet.h"

namespace AA55 {

static uint16_t _sno = 0;

uint16_t nextSno() {
    return ++_sno;
}

std::vector<uint8_t> varLen(size_t n) {
    if (n < 128)
        return {(uint8_t)n};
    if (n < 256)
        return {0x81, (uint8_t)n};
    return {0x82, (uint8_t)(n & 0xFF), (uint8_t)((n >> 8) & 0xFF)};
}

std::vector<uint8_t> buildPacket(uint16_t sno, const uint8_t* payload, size_t len,
                                  uint8_t flags, uint8_t cmdType) {
    bool hasChecksum = (flags & 0x80) != 0;
    size_t bodyLen = 2 + 1 + 1 + len;
    size_t totalLen = bodyLen + (hasChecksum ? 2 : 0);

    std::vector<uint8_t> pkt;
    pkt.reserve(6 + bodyLen + 2);

    // Header
    pkt.push_back(0xAA);
    pkt.push_back(0x55);
    pkt.push_back(0xFF);
    pkt.push_back(0xFF);

    // Length (2 bytes LE)
    pkt.push_back(totalLen & 0xFF);
    pkt.push_back((totalLen >> 8) & 0xFF);

    // SNO (2 bytes LE)
    pkt.push_back(sno & 0xFF);
    pkt.push_back((sno >> 8) & 0xFF);

    // Flags + cmd_type
    pkt.push_back(flags);
    pkt.push_back(cmdType);

    // Payload
    pkt.insert(pkt.end(), payload, payload + len);

    // Checksum
    if (hasChecksum) {
        uint16_t sum = 0;
        for (auto b : pkt) sum += b;
        pkt.push_back(sum & 0xFF);
        pkt.push_back((sum >> 8) & 0xFF);
    }

    return pkt;
}

}
