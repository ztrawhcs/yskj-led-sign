#pragma once
#include <Arduino.h>
#include <vector>

struct TextSegment {
    String text;
    uint8_t r, g, b;
};

struct TextProgramConfig {
    uint8_t fontSize = 16;
    String scroll = "left";
    uint8_t speed = 10;
    uint16_t duration = 0;
    uint8_t programId = 1;
    uint8_t r = 255, g = 255, b = 255;
    uint8_t fontFamily = 0x00;
    std::vector<TextSegment> segments;
};

std::vector<std::vector<uint8_t>> buildTextProgram(const String& text,
                                                     const TextProgramConfig& cfg);
