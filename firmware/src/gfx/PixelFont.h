#pragma once
#include "Framebuffer.h"

namespace PixelFont {

/// Draw a string at (x,y) using 3x5 pixel-art glyphs.
/// Supported characters: 0-9, ':', 'U', 'V', 'F', ' ', '-', '.', degree sign.
/// Returns total width drawn (in pixels).
int drawString(Framebuffer& fb, const char* str, int x, int y, RGB color);

/// Calculate the pixel width of a string without drawing it.
int stringWidth(const char* str);

}
