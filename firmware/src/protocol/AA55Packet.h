#pragma once
#include <Arduino.h>
#include <vector>

namespace AA55 {

std::vector<uint8_t> buildPacket(uint16_t sno, const uint8_t* payload, size_t len,
                                  uint8_t flags = 0xC1, uint8_t cmdType = 2);

std::vector<uint8_t> varLen(size_t n);

uint16_t nextSno();

}
