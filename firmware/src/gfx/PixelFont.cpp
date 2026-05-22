#include "PixelFont.h"

// ---------------------------------------------------------------------------
// 3x5 pixel-art glyphs
// Each glyph is 5 rows. Each row is a byte whose low 3 bits represent
// the 3 columns (bit2 = leftmost, bit0 = rightmost).
//
// Example: '0' row 0 = ### = 0b111 = 7
// ---------------------------------------------------------------------------

// Glyph width (most are 3, colon is 1, space is 2, '.' is 1)
static const int GLYPH_W_DEFAULT = 3;
static const int GLYPH_SPACING = 1;  // 1px gap between characters

struct Glyph {
    uint8_t rows[5];
    int width;
};

// clang-format off
static const Glyph GLYPHS[] = {
    // '0'  ###  # #  # #  # #  ###
    { { 0b111, 0b101, 0b101, 0b101, 0b111 }, 3 },
    // '1'   #   ##    #    #   ###
    { { 0b010, 0b110, 0b010, 0b010, 0b111 }, 3 },
    // '2'  ###    #  ###  #    ###
    { { 0b111, 0b001, 0b111, 0b100, 0b111 }, 3 },
    // '3'  ###    #  ###    #  ###
    { { 0b111, 0b001, 0b111, 0b001, 0b111 }, 3 },
    // '4'  # #  # #  ###    #    #
    { { 0b101, 0b101, 0b111, 0b001, 0b001 }, 3 },
    // '5'  ###  #    ###    #  ###
    { { 0b111, 0b100, 0b111, 0b001, 0b111 }, 3 },
    // '6'  ###  #    ###  # #  ###
    { { 0b111, 0b100, 0b111, 0b101, 0b111 }, 3 },
    // '7'  ###    #    #    #    #
    { { 0b111, 0b001, 0b001, 0b001, 0b001 }, 3 },
    // '8'  ###  # #  ###  # #  ###
    { { 0b111, 0b101, 0b111, 0b101, 0b111 }, 3 },
    // '9'  ###  # #  ###    #  ###
    { { 0b111, 0b101, 0b111, 0b001, 0b111 }, 3 },
};

// Colon: 1px wide, dots at row 1 and row 3
static const Glyph GLYPH_COLON = { { 0b0, 0b1, 0b0, 0b1, 0b0 }, 1 };

// Space: 2px wide, all blank
static const Glyph GLYPH_SPACE = { { 0, 0, 0, 0, 0 }, 2 };

// Dash/minus: 3px wide, bar at row 2
static const Glyph GLYPH_DASH = { { 0b000, 0b000, 0b111, 0b000, 0b000 }, 3 };

// Period: 1px wide, dot at row 4
static const Glyph GLYPH_PERIOD = { { 0b0, 0b0, 0b0, 0b0, 0b1 }, 1 };

// Degree: 3px wide, small circle at top
static const Glyph GLYPH_DEGREE = { { 0b010, 0b101, 0b010, 0b000, 0b000 }, 3 };

static const Glyph GLYPH_A = { { 0b111, 0b101, 0b111, 0b101, 0b101 }, 3 };
static const Glyph GLYPH_B = { { 0b110, 0b101, 0b110, 0b101, 0b110 }, 3 };
static const Glyph GLYPH_C = { { 0b111, 0b100, 0b100, 0b100, 0b111 }, 3 };
static const Glyph GLYPH_D = { { 0b110, 0b101, 0b101, 0b101, 0b110 }, 3 };
static const Glyph GLYPH_E = { { 0b111, 0b100, 0b110, 0b100, 0b111 }, 3 };
static const Glyph GLYPH_F = { { 0b111, 0b100, 0b110, 0b100, 0b100 }, 3 };
static const Glyph GLYPH_G = { { 0b111, 0b100, 0b101, 0b101, 0b111 }, 3 };
static const Glyph GLYPH_H = { { 0b101, 0b101, 0b111, 0b101, 0b101 }, 3 };
static const Glyph GLYPH_I = { { 0b111, 0b010, 0b010, 0b010, 0b111 }, 3 };
static const Glyph GLYPH_J = { { 0b001, 0b001, 0b001, 0b101, 0b111 }, 3 };
static const Glyph GLYPH_K = { { 0b101, 0b110, 0b100, 0b110, 0b101 }, 3 };
static const Glyph GLYPH_L = { { 0b100, 0b100, 0b100, 0b100, 0b111 }, 3 };
static const Glyph GLYPH_M = { { 0b10001, 0b11011, 0b10101, 0b10001, 0b10001 }, 5 };
static const Glyph GLYPH_N = { { 0b1001, 0b1101, 0b1011, 0b1001, 0b1001 }, 4 };
static const Glyph GLYPH_O = { { 0b111, 0b101, 0b101, 0b101, 0b111 }, 3 };
static const Glyph GLYPH_P = { { 0b111, 0b101, 0b111, 0b100, 0b100 }, 3 };
static const Glyph GLYPH_Q = { { 0b111, 0b101, 0b101, 0b111, 0b001 }, 3 };
static const Glyph GLYPH_R = { { 0b111, 0b101, 0b111, 0b110, 0b101 }, 3 };
static const Glyph GLYPH_S = { { 0b111, 0b100, 0b111, 0b001, 0b111 }, 3 };
static const Glyph GLYPH_T = { { 0b111, 0b010, 0b010, 0b010, 0b010 }, 3 };
static const Glyph GLYPH_U = { { 0b101, 0b101, 0b101, 0b101, 0b111 }, 3 };
static const Glyph GLYPH_V = { { 0b101, 0b101, 0b101, 0b010, 0b010 }, 3 };
static const Glyph GLYPH_W = { { 0b10001, 0b10001, 0b10101, 0b11011, 0b10001 }, 5 };
static const Glyph GLYPH_X = { { 0b101, 0b101, 0b010, 0b101, 0b101 }, 3 };
static const Glyph GLYPH_Y = { { 0b101, 0b101, 0b010, 0b010, 0b010 }, 3 };
static const Glyph GLYPH_Z = { { 0b111, 0b001, 0b010, 0b100, 0b111 }, 3 };
static const Glyph GLYPH_EXCL = { { 0b1, 0b1, 0b1, 0b0, 0b1 }, 1 };
static const Glyph GLYPH_APOS = { { 0b1, 0b1, 0b0, 0b0, 0b0 }, 1 };
// clang-format on

static const Glyph* ALPHA_TABLE[] = {
    &GLYPH_A, &GLYPH_B, &GLYPH_C, &GLYPH_D, &GLYPH_E, &GLYPH_F,
    &GLYPH_G, &GLYPH_H, &GLYPH_I, &GLYPH_J, &GLYPH_K, &GLYPH_L,
    &GLYPH_M, &GLYPH_N, &GLYPH_O, &GLYPH_P, &GLYPH_Q, &GLYPH_R,
    &GLYPH_S, &GLYPH_T, &GLYPH_U, &GLYPH_V, &GLYPH_W, &GLYPH_X,
    &GLYPH_Y, &GLYPH_Z,
};

static const Glyph* getGlyph(char ch) {
    if (ch >= '0' && ch <= '9') return &GLYPHS[ch - '0'];
    if (ch >= 'A' && ch <= 'Z') return ALPHA_TABLE[ch - 'A'];
    if (ch >= 'a' && ch <= 'z') return ALPHA_TABLE[ch - 'a'];
    if (ch == ':')  return &GLYPH_COLON;
    if (ch == ' ')  return &GLYPH_SPACE;
    if (ch == '-')  return &GLYPH_DASH;
    if (ch == '.')  return &GLYPH_PERIOD;
    if (ch == '!')  return &GLYPH_EXCL;
    if (ch == '\'') return &GLYPH_APOS;
    if ((uint8_t)ch == 0xB0 || ch == '*') return &GLYPH_DEGREE;
    return &GLYPH_SPACE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int PixelFont::stringWidth(const char* str) {
    int w = 0;
    bool first = true;
    for (const char* p = str; *p; p++) {
        const Glyph* g = getGlyph(*p);
        if (!g) continue;
        if (!first) w += GLYPH_SPACING;
        w += g->width;
        first = false;
    }
    return w;
}

int PixelFont::drawString(Framebuffer& fb, const char* str, int x, int y, RGB color) {
    int cx = x;
    bool first = true;
    for (const char* p = str; *p; p++) {
        const Glyph* g = getGlyph(*p);
        if (!g) continue;
        if (!first) cx += GLYPH_SPACING;

        // Draw the glyph
        for (int row = 0; row < 5; row++) {
            uint8_t bits = g->rows[row];
            for (int col = 0; col < g->width; col++) {
                // bit (width-1-col) corresponds to column col
                if (bits & (1 << (g->width - 1 - col))) {
                    fb.putPixel(cx + col, y + row, color);
                }
            }
        }
        cx += g->width;
        first = false;
    }
    return cx - x;  // total width drawn
}
