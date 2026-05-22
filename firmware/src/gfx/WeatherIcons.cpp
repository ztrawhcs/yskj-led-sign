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

// rain: 13x10
static const char* ICON_RAIN[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#..........#.",
    "#############",
    ".###########.",
    ".............",
    ".#..#..#..#..",
    "..#..#..#..#.",
    ".#..#..#..#..",
};
static const int ICON_RAIN_ROWS = 10;
static const int ICON_RAIN_COLS = 13;

// snow: 13x10
static const char* ICON_SNOW[] = {
    "....####.....",
    "..##....##...",
    ".#........#..",
    "#..........#.",
    "#############",
    ".###########.",
    ".............",
    "..#...#...#..",
    "...#.#.#.#...",
    "..#...#...#..",
};
static const int ICON_SNOW_ROWS = 10;
static const int ICON_SNOW_COLS = 13;

// storm: 13x10
static const char* ICON_STORM[] = {
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
};
static const int ICON_STORM_ROWS = 10;
static const int ICON_STORM_COLS = 13;

// fog: 13x10
static const char* ICON_FOG[] = {
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
static const int ICON_FOG_ROWS = 10;
static const int ICON_FOG_COLS = 13;

// ---------------------------------------------------------------------------
// Icon lookup
// ---------------------------------------------------------------------------

struct IconDef {
    const char* name;
    const char** rows;
    int numRows;
    int numCols;
};

static const IconDef ALL_ICONS[] = {
    { "sun",   ICON_SUN,   ICON_SUN_ROWS,   ICON_SUN_COLS   },
    { "moon",  ICON_MOON,  ICON_MOON_ROWS,  ICON_MOON_COLS  },
    { "cloud", ICON_CLOUD, ICON_CLOUD_ROWS, ICON_CLOUD_COLS },
    { "rain",  ICON_RAIN,  ICON_RAIN_ROWS,  ICON_RAIN_COLS  },
    { "snow",  ICON_SNOW,  ICON_SNOW_ROWS,  ICON_SNOW_COLS  },
    { "storm", ICON_STORM, ICON_STORM_ROWS, ICON_STORM_COLS },
    { "fog",   ICON_FOG,   ICON_FOG_ROWS,   ICON_FOG_COLS   },
};
static const int NUM_ICONS = sizeof(ALL_ICONS) / sizeof(ALL_ICONS[0]);

static const IconDef* findIcon(const char* name) {
    for (int i = 0; i < NUM_ICONS; i++) {
        if (strcmp(ALL_ICONS[i].name, name) == 0)
            return &ALL_ICONS[i];
    }
    // Fall back to cloud
    return &ALL_ICONS[2];
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WeatherIcons::draw(Framebuffer& fb, const char* iconName,
                        int x, int y, RGB color)
{
    const IconDef* icon = findIcon(iconName);
    for (int row = 0; row < icon->numRows; row++) {
        const char* rowStr = icon->rows[row];
        for (int col = 0; col < icon->numCols && rowStr[col]; col++) {
            if (rowStr[col] == '#') {
                fb.putPixel(x + col, y + row, color);
            }
        }
    }
}

int WeatherIcons::iconWidth(const char* iconName) {
    return findIcon(iconName)->numCols;
}

int WeatherIcons::iconHeight(const char* iconName) {
    return findIcon(iconName)->numRows;
}
