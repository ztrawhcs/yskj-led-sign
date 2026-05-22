#pragma once
#include "Framebuffer.h"

namespace WeatherParticles {

enum ParticleType { NONE = 0, RAIN, SNOW, STORM };

ParticleType typeFromIcon(const char* icon);
bool needsAnimation(const char* icon);
RGB renderParticles(Framebuffer& fb, ParticleType type, int frameIdx, int numFrames);
void getParticleColors(ParticleType type, RGB* colors, int* count);

}
