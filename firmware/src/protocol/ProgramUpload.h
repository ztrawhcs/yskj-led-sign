#pragma once
#include <Arduino.h>
#include <vector>

std::vector<std::vector<uint8_t>> buildGifProgram(
    const uint8_t* gifData, size_t gifLen, uint8_t programId = 1);
