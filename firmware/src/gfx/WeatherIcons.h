#pragma once
#include "Framebuffer.h"

namespace WeatherIcons {

/// Draw a named weather icon at (x, y) with the given color.
/// Valid names: "sun", "moon", "cloud", "rain", "snow", "storm", "fog".
/// Unknown names fall back to "cloud".
void draw(Framebuffer& fb, const char* iconName, int x, int y, RGB color);

/// Returns the width (in pixels) of the named icon.
int iconWidth(const char* iconName);

/// Returns the height (in pixels) of the named icon.
int iconHeight(const char* iconName);

}
