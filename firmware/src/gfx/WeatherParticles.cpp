#include "WeatherParticles.h"
#include <string.h>

namespace WeatherParticles {

static uint32_t particleHash(int seed, int idx) {
    uint32_t h = (uint32_t)(seed * 31 + idx) * 2654435761u;
    h ^= h >> 13;
    h *= 1597334677u;
    return h >> 16;
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

static const RGB RAIN_COLORS[] = {
    {8,  18, 50},
    {12, 28, 75},
    {18, 40, 100},
    {25, 55, 130},
    {30, 65, 150},
    {40, 80, 170},
};
static const int NUM_RAIN_COLORS = 6;

void getParticleColors(ParticleType type, RGB* colors, int* count) {
    *count = 0;
    switch (type) {
        case RAIN:
            for (int i = 0; i < NUM_RAIN_COLORS && *count < 8; i++)
                colors[(*count)++] = RAIN_COLORS[i];
            break;
        case SNOW:
            colors[(*count)++] = {25, 28, 45};
            colors[(*count)++] = {40, 45, 60};
            colors[(*count)++] = {55, 60, 75};
            break;
        case STORM:
            for (int i = 0; i < NUM_RAIN_COLORS && *count < 7; i++)
                colors[(*count)++] = RAIN_COLORS[i];
            colors[(*count)++] = {80, 80, 40};
            break;
        default:
            break;
    }
}

static void drawRain(Framebuffer& fb, int frameIdx, int numFrames, int seed) {
    const int NUM_DROPS = 50;
    int H = Framebuffer::VISIBLE_H;
    int W = Framebuffer::W;

    for (int d = 0; d < NUM_DROPS; d++) {
        // Vary visibility per frame
        uint32_t vis = particleHash(frameIdx * 13 + seed + 503, d * 3 + 7);
        if ((vis % 7) < 2) continue;

        // Stratified X: divide screen into NUM_DROPS zones, jitter within zone
        float zoneW = (float)(W - 2) / NUM_DROPS;
        int zoneStart = 1 + (int)(d * zoneW);
        int jitter = particleHash(seed + 42, d) % (int)(zoneW + 1);
        int baseX = zoneStart + jitter;
        if (baseX >= W - 1) baseX = W - 2;

        int baseY = particleHash(seed + 77, d) % H;
        int speed = 2 + (particleHash(seed + 99, d) % 4);

        // Wind drift per drop
        int windBias = (particleHash(seed + 333, d) % 5) - 2;
        int drift = windBias + ((int)(particleHash(frameIdx * 3 + seed + 200, d) % 3) - 1);

        int y = (baseY + frameIdx * speed) % H;
        int x = baseX + drift;
        if (x < 1) x = 1;
        if (x >= W - 1) x = W - 1;

        // Pick color — varies per drop, shimmers per frame
        uint32_t colorSeed = particleHash(seed + 155, d);
        int baseColorIdx = colorSeed % NUM_RAIN_COLORS;
        int shimmer = ((int)(particleHash(frameIdx * 17 + seed + 700, d) % 5)) - 2;
        int colorIdx = baseColorIdx + shimmer;
        if (colorIdx < 0) colorIdx = 0;
        if (colorIdx >= NUM_RAIN_COLORS) colorIdx = NUM_RAIN_COLORS - 1;

        RGB clr = RAIN_COLORS[colorIdx];

        if (fb.getPixel(x, y).isBlack())
            fb.putPixel(x, y, clr);

        if (speed >= 3) {
            int y2 = (y + 1) % H;
            int dimIdx = colorIdx - 1;
            if (dimIdx < 0) dimIdx = 0;
            RGB dimmer = RAIN_COLORS[dimIdx];
            if (y2 < H && fb.getPixel(x, y2).isBlack())
                fb.putPixel(x, y2, dimmer);
        }
        if (speed >= 5) {
            int y3 = (y + 2) % H;
            int faintIdx = colorIdx - 2;
            if (faintIdx < 0) faintIdx = 0;
            RGB faint = RAIN_COLORS[faintIdx];
            if (y3 < H && fb.getPixel(x, y3).isBlack())
                fb.putPixel(x, y3, faint);
        }
    }
}

static void drawSnow(Framebuffer& fb, int frameIdx, int numFrames, int seed) {
    const int NUM_FLAKES = 30;
    int H = Framebuffer::VISIBLE_H;
    int W = Framebuffer::W;

    for (int f = 0; f < NUM_FLAKES; f++) {
        uint32_t vis = particleHash(frameIdx * 11 + seed + 600, f);
        if ((vis % 5) == 0) continue;

        float zoneW = (float)(W - 4) / NUM_FLAKES;
        int baseX = 2 + (int)(f * zoneW) + (particleHash(seed + 13, f) % (int)(zoneW + 1));
        if (baseX >= W - 2) baseX = W - 3;
        int baseY = particleHash(seed + 57, f) % H;
        int speed = 1 + (particleHash(seed + 31, f) % 2);
        int drift = ((frameIdx + particleHash(seed + 71, f)) % 5) - 2;

        int y = (baseY + frameIdx * speed) % H;
        int x = baseX + drift;
        if (x < 0) x += W;
        if (x >= W) x -= W;

        static const RGB SNOW_COLORS[] = {
            {25, 28, 45}, {40, 45, 60}, {55, 60, 75}
        };
        int ci = particleHash(seed + 88, f) % 3;
        RGB clr = SNOW_COLORS[ci];
        if (fb.getPixel(x, y).isBlack())
            fb.putPixel(x, y, clr);
    }
}

static void drawStorm(Framebuffer& fb, int frameIdx, int numFrames, int seed) {
    drawRain(fb, frameIdx, numFrames, seed);
    int H = Framebuffer::VISIBLE_H;
    int W = Framebuffer::W;

    for (int d = 0; d < 25; d++) {
        int x = (particleHash(seed + 150, d) % (W - 4)) + 2;
        int y = (particleHash(seed + 170, d) % H + frameIdx * 4) % H;
        int ci = particleHash(seed + 190, d) % NUM_RAIN_COLORS;
        RGB extra = RAIN_COLORS[ci];
        if (fb.getPixel(x, y).isBlack())
            fb.putPixel(x, y, extra);
    }

    bool flash = (frameIdx == 2) || (frameIdx == 7 && numFrames >= 10);
    if (flash) {
        RGB fl = {80, 80, 40};
        for (int i = 0; i < 10; i++) {
            int x = particleHash(200 + frameIdx + seed, i) % W;
            int y = particleHash(300 + frameIdx + seed, i) % H;
            if (fb.getPixel(x, y).isBlack())
                fb.putPixel(x, y, fl);
        }
    }
}

RGB renderParticles(Framebuffer& fb, ParticleType type, int frameIdx, int numFrames, int seed) {
    switch (type) {
        case RAIN:
            drawRain(fb, frameIdx, numFrames, seed);
            return {25, 55, 130};
        case SNOW:
            drawSnow(fb, frameIdx, numFrames, seed);
            return {40, 45, 60};
        case STORM:
            drawStorm(fb, frameIdx, numFrames, seed);
            return {25, 55, 130};
        default:
            return {0, 0, 0};
    }
}

} // namespace WeatherParticles
