#include "WeatherParticles.h"
#include <string.h>

namespace WeatherParticles {

static uint32_t particleHash(int seed, int idx) {
    return ((uint32_t)(seed * 31 + idx) * 2654435761u) >> 16;
}

ParticleType typeFromIcon(const char* icon) {
    if (strcmp(icon, "rain") == 0) return RAIN;
    if (strcmp(icon, "snow") == 0) return SNOW;
    if (strcmp(icon, "storm") == 0) return STORM;
    return NONE;
}

bool needsAnimation(const char* icon) {
    return typeFromIcon(icon) != NONE;
}

void getParticleColors(ParticleType type, RGB* colors, int* count) {
    *count = 0;
    switch (type) {
        case RAIN:
            colors[(*count)++] = {20, 45, 110};
            colors[(*count)++] = {10, 25, 70};
            break;
        case SNOW:
            colors[(*count)++] = {40, 45, 60};
            colors[(*count)++] = {20, 25, 40};
            break;
        case STORM:
            colors[(*count)++] = {20, 45, 110};
            colors[(*count)++] = {80, 80, 40};
            break;
        default:
            break;
    }
}

static void drawRain(Framebuffer& fb, int frameIdx, int numFrames) {
    RGB bright = {20, 45, 110};
    RGB dim = {10, 25, 70};
    const int NUM_DROPS = 35;
    int H = Framebuffer::VISIBLE_H;
    int W = Framebuffer::W;

    for (int d = 0; d < NUM_DROPS; d++) {
        // Skip some drops per frame for less uniform look
        uint32_t vis = particleHash(frameIdx * 7 + 500, d);
        if ((vis % 5) == 0) continue;

        int baseX = (particleHash(42, d) % (W - 4)) + 2;
        int baseY = particleHash(77, d) % H;
        int speed = 2 + (particleHash(99, d) % 3);
        int y = (baseY + frameIdx * speed) % H;
        int x = baseX + ((particleHash(frameIdx + 200, d) % 3) - 1);
        if (x < 0) x = 0;
        if (x >= W) x = W - 1;

        RGB clr = (d % 3 == 0) ? dim : bright;
        if (fb.getPixel(x, y).isBlack())
            fb.putPixel(x, y, clr);
        int y2 = y + 1;
        if (y2 < H && fb.getPixel(x, y2).isBlack())
            fb.putPixel(x, y2, clr);
    }
}

static void drawSnow(Framebuffer& fb, int frameIdx, int numFrames) {
    RGB bright = {40, 45, 60};
    RGB dim = {20, 25, 40};
    const int NUM_FLAKES = 30;
    int H = Framebuffer::VISIBLE_H;
    int W = Framebuffer::W;

    for (int f = 0; f < NUM_FLAKES; f++) {
        uint32_t vis = particleHash(frameIdx * 11 + 600, f);
        if ((vis % 5) == 0) continue;

        int baseX = (particleHash(13, f) % (W - 4)) + 2;
        int baseY = particleHash(57, f) % H;
        int speed = 1 + (particleHash(31, f) % 2);
        int drift = ((frameIdx + particleHash(71, f)) % 5) - 2;

        int y = (baseY + frameIdx * speed) % H;
        int x = baseX + drift;
        if (x < 0) x += W;
        if (x >= W) x -= W;

        RGB clr = (f % 2 == 0) ? bright : dim;
        if (fb.getPixel(x, y).isBlack())
            fb.putPixel(x, y, clr);
    }
}

static void drawStorm(Framebuffer& fb, int frameIdx, int numFrames) {
    // Heavy rain base — extra drops on top of normal rain
    drawRain(fb, frameIdx, numFrames);
    RGB extra = {15, 35, 90};
    int H = Framebuffer::VISIBLE_H;
    int W = Framebuffer::W;
    for (int d = 0; d < 20; d++) {
        int x = (particleHash(150, d) % (W - 4)) + 2;
        int y = (particleHash(170, d) % H + frameIdx * 4) % H;
        if (fb.getPixel(x, y).isBlack())
            fb.putPixel(x, y, extra);
    }

    // Lightning flash on frame 2
    if (frameIdx == 2 || frameIdx == 3) {
        RGB flash = {80, 80, 40};
        for (int i = 0; i < 8; i++) {
            int x = particleHash(200 + frameIdx, i) % W;
            int y = particleHash(300 + frameIdx, i) % H;
            if (fb.getPixel(x, y).isBlack())
                fb.putPixel(x, y, flash);
        }
    }
}

RGB renderParticles(Framebuffer& fb, ParticleType type, int frameIdx, int numFrames) {
    switch (type) {
        case RAIN:
            drawRain(fb, frameIdx, numFrames);
            return {20, 45, 110};
        case SNOW:
            drawSnow(fb, frameIdx, numFrames);
            return {40, 45, 60};
        case STORM:
            drawStorm(fb, frameIdx, numFrames);
            return {20, 45, 110};
        default:
            return {0, 0, 0};
    }
}

} // namespace WeatherParticles
