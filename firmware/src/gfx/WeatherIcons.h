#pragma once
#include "Framebuffer.h"

namespace WeatherIcons {

/// Draw a named weather icon at (x, y) with the given color.
/// Valid names: "sun", "moon", "cloud", "rain", "snow", "storm", "fog".
/// Unknown names fall back to "cloud".
void draw(Framebuffer& fb, const char* iconName, int x, int y, RGB color);

/// Draw animation frame for animated icons (rain, snow, storm, fog).
/// frame cycles 0..animFrameCount()-1. Non-animated icons ignore frame.
void drawFrame(Framebuffer& fb, const char* iconName, int x, int y, RGB color, int frame);

/// Number of animation frames for this icon (1 = static).
int animFrameCount(const char* iconName);

/// Returns the width (in pixels) of the named icon.
int iconWidth(const char* iconName);

/// Returns the height (in pixels) of the named icon.
int iconHeight(const char* iconName);

}
