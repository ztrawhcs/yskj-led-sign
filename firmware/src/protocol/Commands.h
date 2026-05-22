#pragma once
#include "AA55Packet.h"

namespace Commands {

inline std::vector<uint8_t> powerOn() {
    uint8_t payload[] = {0x04, 0x02, 0x00, 0x01};
    return AA55::buildPacket(AA55::nextSno(), payload, sizeof(payload));
}

inline std::vector<uint8_t> powerOff() {
    uint8_t payload[] = {0x04, 0x02, 0x00, 0x00};
    return AA55::buildPacket(AA55::nextSno(), payload, sizeof(payload));
}

inline std::vector<uint8_t> setBrightness(uint8_t level) {
    uint8_t payload[] = {0x06, 0x02, 0x00, level};
    return AA55::buildPacket(AA55::nextSno(), payload, sizeof(payload));
}

inline std::vector<uint8_t> deleteAll() {
    uint8_t payload[] = {0x08, 0x02, 0x00, 0xFF};
    return AA55::buildPacket(AA55::nextSno(), payload, sizeof(payload));
}

inline std::vector<uint8_t> getDeviceInfo() {
    uint8_t payload[] = {0x03, 0x01, 0x00};
    return AA55::buildPacket(AA55::nextSno(), payload, sizeof(payload), 0xC1, 3);
}

inline std::vector<uint8_t> initParamDev() {
    uint8_t payload[] = {0x0A, 0x00, 0x1B, 0x00, 0x04, 0x00, 0x06, 0x00};
    return AA55::buildPacket(AA55::nextSno(), payload, sizeof(payload), 0xC1, 3);
}

inline std::vector<uint8_t> initConfig() {
    uint8_t payload[] = {0x05, 0x09, 0x1A, 0x00, 0x05, 0x13, 0x16, 0x16, 0x08, 0x19, 0x03};
    return AA55::buildPacket(AA55::nextSno(), payload, sizeof(payload));
}

inline std::vector<uint8_t> initFontQuery() {
    uint8_t payload[] = {0x36, 0x00};
    return AA55::buildPacket(AA55::nextSno(), payload, sizeof(payload), 0xC1, 3);
}

}
