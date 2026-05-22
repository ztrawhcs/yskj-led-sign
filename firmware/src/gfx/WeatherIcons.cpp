#include "WeatherIcons.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Weather icon pixel art — copied from sign.py WEATHER_ICON_PX.
// Each icon is an array of C-string rows where '#' = pixel on, '.' = off.
// ---------------------------------------------------------------------------

// sun: 13x13
static const char* ICON_SUN[] = {
    "......#......",
    "..#...#...#..",
    "...#.....#...",
    "....#####....",
    "...##...##...",
    "#.##.....##.#",
    "###.......###",
    "#.##.....##.#",
    "...##...##...",
    "....#####....",
    "...#.....#...",
    "..#...#...#..",
    "......#......",
};
static const int ICON_SUN_ROWS = 13;
static const int ICON_SUN_COLS = 13;

// moon: 13x13
static const char* ICON_MOON[] = {
    "....#####....",
    "...###..##...",
    "..###....##..",
    ".####.....#..",
    ".####.....#..",
    "######.......",
    "######.......",
    "######.......",
    ".####.....#..",
    ".####.....#..",
    "..###....##..",
    "...###..##...",
    "....#####....",
};
static const int ICON_MOON_ROWS = 13;
static const int ICON_MOON_COLS = 13;

// cloud: 13x10
static const char* ICON_CLOUD[] = {
    ".............",
    "....####.....",
    "..##....##...",
    ".#........#..",
    ".#........#..",
    "#..........#.",
    "#..........#.",
    ".############",
    "..##########.",
    ".............",
};
static const int ICON_CLOUD_ROWS = 10;
static const int ICON_CLOUD_COLS = 13;

// rain: 13x13 — 3 animation frames, drops fall from cloud
static const char* ICON_RAIN_0[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#..........#.",
    "#############",
    ".###########.",
    ".............",
    ".#..#..#..#..",
    ".............",
    "..#..#..#..#.",
    ".............",
    ".#..#..#..#..",
    ".............",
};
static const char* ICON_RAIN_1[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#..........#.",
    "#############",
    ".###########.",
    ".............",
    ".............",
    ".#..#..#..#..",
    ".............",
    "..#..#..#..#.",
    ".............",
    ".#..#..#..#..",
};
static const char* ICON_RAIN_2[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#..........#.",
    "#############",
    ".###########.",
    ".............",
    "..#..#..#..#.",
    ".............",
    ".#..#..#..#..",
    ".............",
    "..#..#..#..#.",
    ".............",
};
static const char** ICON_RAIN_FRAMES[] = { ICON_RAIN_0, ICON_RAIN_1, ICON_RAIN_2 };
static const int ICON_RAIN_ROWS = 13;
static const int ICON_RAIN_COLS = 13;
static const int ICON_RAIN_NFRAMES = 3;

// snow: 13x13 — 3 animation frames, flakes drift down
static const char* ICON_SNOW_0[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#..........#.",
    "#############",
    ".###########.",
    ".............",
    "..#...#...#..",
    ".............",
    "....#...#....",
    ".............",
    ".#...#...#...",
    ".............",
};
static const char* ICON_SNOW_1[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#..........#.",
    "#############",
    ".###########.",
    ".............",
    ".............",
    "...#...#...#.",
    ".............",
    ".#...#...#...",
    ".............",
    "..#...#...#..",
};
static const char* ICON_SNOW_2[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#..........#.",
    "#############",
    ".###########.",
    ".............",
    ".#...#...#...",
    ".............",
    "..#...#...#..",
    ".............",
    "...#...#...#.",
    ".............",
};
static const char** ICON_SNOW_FRAMES[] = { ICON_SNOW_0, ICON_SNOW_1, ICON_SNOW_2 };
static const int ICON_SNOW_ROWS = 13;
static const int ICON_SNOW_COLS = 13;
static const int ICON_SNOW_NFRAMES = 3;

// storm: 13x13 — 3 animation frames, lightning flickers
static const char* ICON_STORM_0[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#############",
    ".###########.",
    "......##.....",
    ".....##......",
    "....####.....",
    "......##.....",
    ".......#.....",
    ".............",
    ".............",
    ".............",
};
static const char* ICON_STORM_1[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#############",
    ".###########.",
    ".............",
    ".#..#..#..#..",
    ".............",
    "..#..#..#..#.",
    ".............",
    ".............",
    ".............",
    ".............",
};
static const char* ICON_STORM_2[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#############",
    ".###########.",
    ".....###.....",
    "....###......",
    "...#####.....",
    ".....###.....",
    "......##.....",
    ".......#.....",
    ".............",
    ".............",
};
static const char** ICON_STORM_FRAMES[] = { ICON_STORM_0, ICON_STORM_1, ICON_STORM_2 };
static const int ICON_STORM_ROWS = 13;
static const int ICON_STORM_COLS = 13;
static const int ICON_STORM_NFRAMES = 3;

// fog: 13x10 — 2 animation frames, lines shift
static const char* ICON_FOG_0[] = {
    ".............",
    "#############",
    ".............",
    ".###########.",
    ".............",
    "#############",
    ".............",
    ".###########.",
    ".............",
    ".............",
};
static const char* ICON_FOG_1[] = {
    ".............",
    ".###########.",
    ".............",
    "#############",
    ".............",
    ".###########.",
    ".............",
    "#############",
    ".............",
    ".............",
};
static const char** ICON_FOG_FRAMES[] = { ICON_FOG_0, ICON_FOG_1 };
static const int ICON_FOG_ROWS = 10;
static const int ICON_FOG_COLS = 13;
static const int ICON_FOG_NFRAMES = 2;

// ---------------------------------------------------------------------------
// Icon lookup
// ---------------------------------------------------------------------------

struct IconDef {
    const char* name;
    const char*** frames;   // array of frame pointers (each frame is array of row strings)
    int numFrames;
    int numRows;
    int numCols;
};

static const char** ICON_SUN_FRAMES[] = { ICON_SUN };
static const char** ICON_MOON_FRAMES[] = { ICON_MOON };
static const char** ICON_CLOUD_FRAMES[] = { ICON_CLOUD };

static const IconDef ALL_ICONS[] = {
    { "sun",   ICON_SUN_FRAMES,   1,                    ICON_SUN_ROWS,   ICON_SUN_COLS   },
    { "moon",  ICON_MOON_FRAMES,  1,                    ICON_MOON_ROWS,  ICON_MOON_COLS  },
    { "cloud", ICON_CLOUD_FRAMES, 1,                    ICON_CLOUD_ROWS, ICON_CLOUD_COLS },
    { "rain",  ICON_RAIN_FRAMES,  ICON_RAIN_NFRAMES,    ICON_RAIN_ROWS,  ICON_RAIN_COLS  },
    { "snow",  ICON_SNOW_FRAMES,  ICON_SNOW_NFRAMES,    ICON_SNOW_ROWS,  ICON_SNOW_COLS  },
    { "storm", ICON_STORM_FRAMES, ICON_STORM_NFRAMES,   ICON_STORM_ROWS, ICON_STORM_COLS },
    { "fog",   ICON_FOG_FRAMES,   ICON_FOG_NFRAMES,     ICON_FOG_ROWS,   ICON_FOG_COLS   },
};
static const int NUM_ICONS = sizeof(ALL_ICONS) / sizeof(ALL_ICONS[0]);

static const IconDef* findIcon(const char* name) {
    for (int i = 0; i < NUM_ICONS; i++) {
        if (strcmp(ALL_ICONS[i].name, name) == 0)
            return &ALL_ICONS[i];
    }
    return &ALL_ICONS[2];
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static void drawIconFrame(Framebuffer& fb, const IconDef* icon, int x, int y, RGB color, int frame) {
    int f = frame % icon->numFrames;
    const char** rows = icon->frames[f];
    for (int row = 0; row < icon->numRows; row++) {
        const char* rowStr = rows[row];
        for (int col = 0; col < icon->numCols && rowStr[col]; col++) {
            if (rowStr[col] == '#')
                fb.putPixel(x + col, y + row, color);
        }
    }
}

void WeatherIcons::draw(Framebuffer& fb, const char* iconName,
                        int x, int y, RGB color)
{
    drawIconFrame(fb, findIcon(iconName), x, y, color, 0);
}

void WeatherIcons::drawFrame(Framebuffer& fb, const char* iconName,
                             int x, int y, RGB color, int frame)
{
    drawIconFrame(fb, findIcon(iconName), x, y, color, frame);
}

int WeatherIcons::animFrameCount(const char* iconName) {
    return findIcon(iconName)->numFrames;
}

int WeatherIcons::iconWidth(const char* iconName) {
    return findIcon(iconName)->numCols;
}

int WeatherIcons::iconHeight(const char* iconName) {
    return findIcon(iconName)->numRows;
}
