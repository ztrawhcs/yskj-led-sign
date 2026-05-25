#include "ClockWeatherMode.h"
#include "../gfx/PixelFont.h"
#include "../gfx/WeatherIcons.h"
#include "../gfx/GifEncoder.h"
#include "../gfx/WeatherParticles.h"
#include "../protocol/TextProgram.h"
#include "../protocol/AA55Packet.h"
#include "../protocol/ProgramUpload.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

ClockWeatherMode clockMode;

static const int VISIBLE_H = 16;

// ---------------------------------------------------------------------------
// Font data — 4 base fonts, each 10 digits
// ---------------------------------------------------------------------------

// Font 0: Standard 3x5
static const uint8_t FONT0[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}
};

// Font 1: Rounded 4x5
static const uint8_t FONT1[10][5] = {
    {6,9,9,9,6},{2,6,2,2,7},{6,1,6,8,15},{6,1,6,1,6},{9,9,15,1,1},
    {15,8,14,1,6},{6,8,14,9,6},{15,1,2,4,4},{6,9,6,9,6},{6,9,7,1,6}
};

// Font 2: Block 4x5
static const uint8_t FONT2[10][5] = {
    {15,9,9,9,15},{6,2,2,2,15},{15,1,15,8,15},{15,1,15,1,15},{9,9,15,1,1},
    {15,8,15,1,15},{15,8,15,9,15},{15,1,3,2,2},{15,9,15,9,15},{15,9,15,1,15}
};

// Font 3: Detailed 5x7
static const uint8_t FONT3[10][7] = {
    {14,17,17,17,17,17,14},{4,12,4,4,4,4,14},{14,17,1,6,8,16,31},
    {14,17,1,6,1,17,14},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12}
};

static const int FONT_W[] = {3, 4, 4, 5};
static const int FONT_H[] = {5, 5, 5, 7};

// 5x7 'F' glyph for temp display
static const uint8_t FONT3_F[7] = {31,16,16,30,16,16,16};

// 7x7 weather icons
static const uint8_t ICON7_SUN[7]   = {0x14,0x08,0x3E,0x5D,0x3E,0x08,0x14};
static const uint8_t ICON7_MOON[7]  = {0x1C,0x3C,0x3C,0x3C,0x3C,0x3C,0x1C};
static const uint8_t ICON7_CLOUD[7] = {0x00,0x1C,0x22,0x41,0x41,0x3E,0x00};
static const uint8_t ICON7_RAIN[7]  = {0x1C,0x3E,0x7F,0x00,0x2A,0x15,0x2A};
static const uint8_t ICON7_SNOW[7]  = {0x1C,0x3E,0x7F,0x00,0x2A,0x14,0x2A};
static const uint8_t ICON7_STORM[7] = {0x1C,0x3E,0x7F,0x08,0x18,0x3C,0x08};
static const uint8_t ICON7_FOG[7]   = {0x7F,0x00,0x3E,0x00,0x7F,0x00,0x3E};

static void drawIcon7(Framebuffer& fb, const char* icon, int x, int y, RGB color) {
    const uint8_t* data = ICON7_CLOUD;
    if (strcmp(icon, "sun") == 0) data = ICON7_SUN;
    else if (strcmp(icon, "moon") == 0) data = ICON7_MOON;
    else if (strcmp(icon, "rain") == 0) data = ICON7_RAIN;
    else if (strcmp(icon, "snow") == 0) data = ICON7_SNOW;
    else if (strcmp(icon, "storm") == 0) data = ICON7_STORM;
    else if (strcmp(icon, "fog") == 0) data = ICON7_FOG;
    for (int row = 0; row < 7; row++)
        for (int col = 0; col < 7; col++)
            if (data[row] & (1 << (6 - col)))
                fb.putPixel(x + col, y + row, color);
}

static int drawTempStr(Framebuffer& fb, const char* str, int x, int y, RGB color) {
    int cx = x;
    for (int i = 0; str[i]; i++) {
        if (i > 0) cx += 1;
        if (str[i] >= '0' && str[i] <= '9') {
            const uint8_t* g = FONT3[str[i] - '0'];
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++)
                    if (g[r] & (1 << (4 - c)))
                        fb.putPixel(cx + c, y + r, color);
            cx += 5;
        } else if (str[i] == 'F') {
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++)
                    if (FONT3_F[r] & (1 << (4 - c)))
                        fb.putPixel(cx + c, y + r, color);
            cx += 5;
        } else if (str[i] == '-') {
            for (int c = 1; c < 4; c++)
                fb.putPixel(cx + c, y + 3, color);
            cx += 5;
        }
    }
    return cx - x;
}

static int tempStrWidth(const char* str) {
    int w = 0;
    for (int i = 0; str[i]; i++) {
        if (i > 0) w += 1;
        w += 5;
    }
    return w;
}

// ---------------------------------------------------------------------------
// Init, settings persistence
// ---------------------------------------------------------------------------

void ClockWeatherMode::begin(SignBLE* ble) {
    _ble = ble;
    _lastUpdate = 0;
    _lastWeatherFetch = 0;
    _lastForecastFlash = 0;
    _lastNewsFetch = 0;
    _lastWatchdog = 0;
    _forceRedraw = true;
    _lastMinuteSent = -1;
    _newsCount = 0;
    loadSettings();
}

void ClockWeatherMode::loadSettings() {
    Preferences prefs;
    prefs.begin("clock", true);
    _clockLayout = prefs.getInt("layout", 0);
    _fontId = prefs.getInt("fontId", 0);
    _fontAA = prefs.getBool("fontAA", false);
    _cTimeX = prefs.getInt("ctX", 5);
    _cTimeY = prefs.getInt("ctY", 0);
    _cTimeScale = prefs.getInt("ctS", 3);
    _cTempX = prefs.getInt("cpX", 65);
    _cTempY = prefs.getInt("cpY", 5);
    _cIconX = prefs.getInt("ciX", 65);
    _cIconY = prefs.getInt("ciY", 0);
    _cIconSize = prefs.getInt("ciSz", 0);
    _cTimeColor.r = prefs.getUChar("tcR", 50);
    _cTimeColor.g = prefs.getUChar("tcG", 130);
    _cTimeColor.b = prefs.getUChar("tcB", 255);
    _rssUrl = prefs.getString("rssUrl", "");
    _calendarUrl = prefs.getString("calUrl", "");
    _proxyUrl = prefs.getString("proxyUrl", "");
    _animStyle = prefs.getInt("animStyle", 1);
    prefs.end();
    Serial.printf("[Clock] Loaded: layout=%d font=%d aa=%d animStyle=%d color=#%02x%02x%02x\n",
        _clockLayout, _fontId, _fontAA, _animStyle, _cTimeColor.r, _cTimeColor.g, _cTimeColor.b);
    if (_rssUrl.length() > 0)
        Serial.printf("[Clock] RSS: %s\n", _rssUrl.c_str());
    if (_proxyUrl.length() > 0)
        Serial.printf("[Clock] Proxy: %s\n", _proxyUrl.c_str());
}

void ClockWeatherMode::saveSettings() {
    Preferences prefs;
    prefs.begin("clock", false);
    prefs.putInt("layout", _clockLayout);
    prefs.putInt("fontId", _fontId);
    prefs.putBool("fontAA", _fontAA);
    prefs.putInt("ctX", _cTimeX);
    prefs.putInt("ctY", _cTimeY);
    prefs.putInt("ctS", _cTimeScale);
    prefs.putInt("cpX", _cTempX);
    prefs.putInt("cpY", _cTempY);
    prefs.putInt("ciX", _cIconX);
    prefs.putInt("ciY", _cIconY);
    prefs.putInt("ciSz", _cIconSize);
    prefs.putUChar("tcR", _cTimeColor.r);
    prefs.putUChar("tcG", _cTimeColor.g);
    prefs.putUChar("tcB", _cTimeColor.b);
    prefs.end();
}

void ClockWeatherMode::forceUpdate() {
    _forceRedraw = true;
    _clearTextOnNext = true;
    _lastMinuteSent = -1;
}

void ClockWeatherMode::showForecast() {
    _forecastRequested = true;
}

void ClockWeatherMode::setClockLayout(int layout) {
    if (layout < 0) layout = 0;
    if (layout > 3) layout = 3;
    _clockLayout = layout;
    _forceRedraw = true;
    _iconRegionValid = false;
    saveSettings();
}

void ClockWeatherMode::setCustomPositions(int timeX, int timeY, int timeScale,
                                           int tempX, int tempY,
                                           int iconX, int iconY, int iconSize) {
    _cTimeX = timeX; _cTimeY = timeY;
    _cTimeScale = max(2, min(3, timeScale));
    _cTempX = tempX; _cTempY = tempY;
    _cIconX = iconX; _cIconY = iconY;
    _cIconSize = (iconSize == 1) ? 1 : 0;
    _forceRedraw = true;
    saveSettings();
}

void ClockWeatherMode::setFont(int fontId, bool aa) {
    if (fontId < 0) fontId = 0;
    if (fontId > 3) fontId = 3;
    _fontId = fontId;
    _fontAA = aa;
    _forceRedraw = true;
    saveSettings();
}

void ClockWeatherMode::setTimeColor(uint8_t r, uint8_t g, uint8_t b) {
    _cTimeColor = {r, g, b};
    _forceRedraw = true;
    saveSettings();
}

// ---------------------------------------------------------------------------
// Timer / Stopwatch
// ---------------------------------------------------------------------------

void ClockWeatherMode::startCountdown(int totalSeconds) {
    _timerMode = TIMER_COUNTDOWN;
    _timerDurationMs = (unsigned long)totalSeconds * 1000UL;
    _timerStartMs = millis();
    _timerPausedElapsed = 0;
    _timerRunning = true;
    _timerFinishedFlash = false;
    _timerFlashCount = 0;
    _forceRedraw = true;
    stopGifProgram();
    Serial.printf("[Timer] Countdown started: %ds\n", totalSeconds);
}

void ClockWeatherMode::startStopwatch() {
    _timerMode = TIMER_STOPWATCH;
    _timerStartMs = millis();
    _timerPausedElapsed = 0;
    _timerDurationMs = 0;
    _timerRunning = true;
    _timerFinishedFlash = false;
    _timerFlashCount = 0;
    _forceRedraw = true;
    stopGifProgram();
    Serial.println("[Timer] Stopwatch started");
}

void ClockWeatherMode::pauseTimer() {
    if (!_timerRunning) return;
    _timerPausedElapsed += millis() - _timerStartMs;
    _timerRunning = false;
    _forceRedraw = true;
    Serial.println("[Timer] Paused");
}

void ClockWeatherMode::resumeTimer() {
    if (_timerRunning || _timerMode == TIMER_NONE) return;
    _timerStartMs = millis();
    _timerRunning = true;
    _forceRedraw = true;
    Serial.println("[Timer] Resumed");
}

void ClockWeatherMode::resetTimer() {
    _timerMode = TIMER_NONE;
    _timerRunning = false;
    _timerPausedElapsed = 0;
    _timerFinishedFlash = false;
    _forceRedraw = true;
    Serial.println("[Timer] Reset, back to clock");
}

int ClockWeatherMode::timerElapsedSec() const {
    unsigned long elapsed = _timerPausedElapsed;
    if (_timerRunning) elapsed += millis() - _timerStartMs;
    return (int)(elapsed / 1000);
}

int ClockWeatherMode::timerRemainingSec() const {
    if (_timerMode != TIMER_COUNTDOWN) return 0;
    unsigned long elapsed = _timerPausedElapsed;
    if (_timerRunning) elapsed += millis() - _timerStartMs;
    if (elapsed >= _timerDurationMs) return 0;
    return (int)((_timerDurationMs - elapsed) / 1000);
}

// ---------------------------------------------------------------------------
// News
// ---------------------------------------------------------------------------

void ClockWeatherMode::setRssUrl(const String& url) {
    _rssUrl = url;
    Preferences prefs;
    prefs.begin("clock", false);
    prefs.putString("rssUrl", url);
    prefs.end();
    Serial.printf("[News] RSS URL set: %s\n", url.c_str());
}

void ClockWeatherMode::triggerNewsFetch() {
    _lastNewsFetch = 0;
}

void ClockWeatherMode::setCalendarUrl(const String& url) {
    _calendarUrl = url;
    Preferences prefs;
    prefs.begin("clock", false);
    prefs.putString("calUrl", url);
    prefs.end();
    _lastCalFetch = 0;
    Serial.printf("[Cal] URL set: %s\n", url.c_str());
}

void ClockWeatherMode::setProxyUrl(const String& url) {
    _proxyUrl = url;
    Preferences prefs;
    prefs.begin("clock", false);
    prefs.putString("proxyUrl", url);
    prefs.end();
    Serial.printf("[Notif] Proxy URL set: %s\n", url.c_str());
}

void ClockWeatherMode::showNotification(const String& text, uint8_t r, uint8_t g, uint8_t b) {
    if (_timerMode != TIMER_NONE) return;
    scrollHeadline(text);
}

// ---------------------------------------------------------------------------
// News dedup (must be before loop)
// ---------------------------------------------------------------------------

static String _shownHeadlines[10];
static int _shownCount = 0;

static bool alreadyShown(const String& title) {
    String lower = title;
    lower.toLowerCase();
    for (int i = 0; i < _shownCount; i++) {
        String prev = _shownHeadlines[i];
        prev.toLowerCase();
        if (lower.length() >= 30 && prev.length() >= 30 &&
            lower.substring(0, 30) == prev.substring(0, 30))
            return true;
        if (lower == prev) return true;
    }
    return false;
}

static void markShown(const String& title) {
    if (_shownCount < 10)
        _shownHeadlines[_shownCount++] = title;
}

// ---------------------------------------------------------------------------
// Main loop — animation-aware timing
// ---------------------------------------------------------------------------

void ClockWeatherMode::loop() {
    unsigned long now = millis();

    // Test hold — freeze display updates
    if (_testHoldUntil > 0 && now < _testHoldUntil) return;
    if (_testHoldUntil > 0) {
        _testHoldUntil = 0;
        _forceRedraw = true;
        _hasPrevFrame = false;
        Serial.println("[Test] Hold expired, resuming normal display");
    }

    // NWS alerts via Cloudflare Worker proxy (avoids HTTPS/TLS on ESP32)
    if (WiFi.status() == WL_CONNECTED &&
        (now - _lastAlertFetch > 300000 || (_lastAlertFetch == 0 && _lastWeatherFetch > 0))) {
        fetchWeatherAlerts();
        _lastAlertFetch = now;
    }

    // Weather fetch doesn't need BLE — run it over WiFi regardless
    if (WiFi.status() == WL_CONNECTED &&
        (_lastWeatherFetch == 0 || now - _lastWeatherFetch > 120000)) {
        fetchWeather();
        _lastWeatherFetch = now;
    }

    if (!_ble || !_ble->isReady()) return;

    // News disabled
    bool newsHours = false;

    // Calendar fetch every 5 minutes (from Google Apps Script)
    if (_calendarUrl.length() > 0 && (now - _lastCalFetch > 300000 || _lastCalFetch == 0)) {
        fetchCalendar();
        _lastCalFetch = now;
    }

    // NWS alerts fetched earlier in loop (before weather) for max heap

    // Notification proxy poll every 30 seconds (only if proxy configured)
    if (_proxyUrl.length() > 0 && (now - _lastProxyPoll > 30000 || _lastProxyPoll == 0)) {
        fetchNotifications();
        _lastProxyPoll = now;
    }

    // Badge reminder: weekdays 7:30-9:00am, every 2 minutes
    struct tm badgeTm;
    if (getLocalTime(&badgeTm)) {
        bool badgeTime = badgeTm.tm_wday >= 1 && badgeTm.tm_wday <= 5 &&
            ((badgeTm.tm_hour == 7 && badgeTm.tm_min >= 30) ||
             (badgeTm.tm_hour == 8));
        if (badgeTime) {
            static unsigned long lastBadge = 0;
            if (now - lastBadge > 120000 || lastBadge == 0) {
                stopGifProgram();
                Serial.println("[Badge] Showing badge reminder via rt_draw");
                static Framebuffer bfb;
                bfb.clear();
                RGB orange = {255, 140, 0};
                const char* line1 = "Don't Forget";
                const char* line2 = "Your Badge!!!";
                int w1 = PixelFont::stringWidth(line1);
                int w2 = PixelFont::stringWidth(line2);
                PixelFont::drawString(bfb, line1, max(0, (Framebuffer::W - w1) / 2), 1, orange);
                PixelFont::drawString(bfb, line2, max(0, (Framebuffer::W - w2) / 2), 9, orange);
                sendFrame(bfb, 2);
                delay(4000);
                sendFrame(bfb, 2);
                delay(4000);
                _forceRedraw = true;
                _hasPrevFrame = false;
                lastBadge = millis();
            }
        }
    }

    // Timer/stopwatch mode takes priority
    if (_timerMode != TIMER_NONE) {
        if (_forceRedraw || now - _timerLastRender >= 1000) {
            renderTimer();
            _timerLastRender = now;
            _forceRedraw = false;
        }
        return;
    }

    if (now - _lastWatchdog > 600000) {
        _lastWatchdog = now;
        _hasPrevFrame = false;
        _forceRedraw = true;
    }

    // Scroll content every 5 minutes: calendar always, news only once per fetch
    bool hasUnshownNews = newsHours && _currentNewsIdx < _newsCount;
    if ((hasUnshownNews || _calEventCount > 0) && _lastNewsScroll > 0 &&
        now - _lastNewsScroll > 300000) {
        if (hasUnshownNews) {
            scrollHeadline(_newsQueue[_currentNewsIdx]);
            markShown(_newsQueue[_currentNewsIdx]);
            _currentNewsIdx++;
        } else if (_calEventCount > 0) {
            static int calIdx = 0;
            scrollHeadline(_calEvents[calIdx % _calEventCount]);
            calIdx++;
        }
        _lastNewsScroll = now;
    }
    if ((hasUnshownNews || _calEventCount > 0) && _lastNewsScroll == 0)
        _lastNewsScroll = now;

    // Weather alerts: show static for 5s every 5 minutes while active
    if (_alertCount > 0 && (now - _lastAlertShow > 300000 || _lastAlertShow == 0)) {
        stopGifProgram();
        static Framebuffer afb;
        afb.clear();
        RGB border = {255, 255, 0};
        RGB alertColor = {255, 0, 0};
        for (int x = 0; x < Framebuffer::W; x++) { afb.putPixel(x, 0, border); afb.putPixel(x, VISIBLE_H-1, border); }
        for (int y = 0; y < VISIBLE_H; y++) { afb.putPixel(0, y, border); afb.putPixel(Framebuffer::W-1, y, border); }
        int ay = (VISIBLE_H - _alertCount * 7) / 2;
        if (ay < 2) ay = 2;
        for (int i = 0; i < _alertCount && ay + 6 <= VISIBLE_H - 1; i++) {
            int tw = PixelFont::stringWidth(_weatherAlerts[i].c_str());
            int tx = max(2, (Framebuffer::W - tw) / 2);
            PixelFont::drawString(afb, _weatherAlerts[i].c_str(), tx, ay, alertColor);
            ay += 7;
        }
        sendFrame(afb, 4);
        _alertUntil = now + 5000;
        _lastAlertShow = now;
        _hasPrevFrame = false;
    }
    if (_alertUntil > 0 && now < _alertUntil) {
        static unsigned long lastAlertResend = 0;
        if (now - lastAlertResend > 2000) {
            static Framebuffer afb2;
            afb2.clear();
            RGB border = {255, 255, 0};
            RGB alertColor = {255, 0, 0};
            for (int x = 0; x < Framebuffer::W; x++) { afb2.putPixel(x, 0, border); afb2.putPixel(x, VISIBLE_H-1, border); }
            for (int y = 0; y < VISIBLE_H; y++) { afb2.putPixel(0, y, border); afb2.putPixel(Framebuffer::W-1, y, border); }
            int ay = (VISIBLE_H - _alertCount * 7) / 2;
            if (ay < 2) ay = 2;
            for (int i = 0; i < _alertCount && ay + 6 <= VISIBLE_H - 1; i++) {
                int tw = PixelFont::stringWidth(_weatherAlerts[i].c_str());
                int tx = max(2, (Framebuffer::W - tw) / 2);
                PixelFont::drawString(afb2, _weatherAlerts[i].c_str(), tx, ay, alertColor);
                ay += 7;
            }
            sendFrame(afb2, 4);
            lastAlertResend = now;
        }
        return;
    }
    _alertUntil = 0;

    // UV burn time warning — Type 1 skin, every 30 min when UV >= 3
    static unsigned long _lastUvAlert = 0;
    if (_weather.uvIndex >= 3.0f && _weather.isDay &&
        (_lastUvAlert == 0 || now - _lastUvAlert > 1800000)) {
        stopGifProgram();
        int burnMin = (int)(200.0f / (_weather.uvIndex * 2.5f));
        if (burnMin < 1) burnMin = 1;
        static Framebuffer uvfb;
        uvfb.clear();
        RGB uvColor = _weather.uvIndex >= 8 ? RGB{255, 0, 0} :
                      _weather.uvIndex >= 6 ? RGB{255, 80, 0} :
                                              RGB{255, 180, 0};
        char uvLine1[24], uvLine2[24];
        snprintf(uvLine1, sizeof(uvLine1), "UV %.0f", _weather.uvIndex);
        snprintf(uvLine2, sizeof(uvLine2), "Burn: %d min", burnMin);
        int w1 = PixelFont::stringWidth(uvLine1);
        int w2 = PixelFont::stringWidth(uvLine2);
        PixelFont::drawString(uvfb, uvLine1, max(0, (Framebuffer::W - w1) / 2), 1, uvColor);
        PixelFont::drawString(uvfb, uvLine2, max(0, (Framebuffer::W - w2) / 2), 9, uvColor);
        sendFrame(uvfb, 4);
        delay(4000);
        sendFrame(uvfb, 4);
        delay(1000);
        _lastUvAlert = now;
        _forceRedraw = true;
        _hasPrevFrame = false;
    }

    if (_forecastRequested && _weather.hourlyCount >= 24) {
        _forecastRequested = false;
        renderForecastFlash();
        _lastForecastFlash = millis();
        return;
    }

    if (_weather.hourlyCount >= 24 && _lastForecastFlash > 0 &&
        now - _lastForecastFlash > 300000) {
        renderForecastFlash();
        _lastForecastFlash = millis();
        return;
    }
    if (_lastForecastFlash == 0) _lastForecastFlash = now + 150000;

    const char* ic = _weather.icon.c_str();
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    int currentMinute = timeinfo.tm_hour * 60 + timeinfo.tm_min;

    bool wantAnim = WeatherParticles::needsAnimation(ic);
    DisplayMode wantMode = DISPLAY_RT_DRAW;
    if (wantAnim) wantMode = (_animStyle == 1) ? DISPLAY_SPLIT : DISPLAY_GIF_PROGRAM;

    // Mode transitions
    if (wantMode != _displayMode) {
        Serial.printf("[Clock] Switching to %s mode\n",
            wantMode == DISPLAY_SPLIT ? "split" :
            wantMode == DISPLAY_GIF_PROGRAM ? "GIF" : "rt_draw");
        _displayMode = wantMode;
        _gifFailCount = 0;
        _forceRedraw = true;
        _hasPrevFrame = false;
        _splitGifActive = false;
    }

    if (_displayMode == DISPLAY_SPLIT) {
        int hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        bool needGifUpload = !_splitGifActive || _forceRedraw
            || _weather.temp != _splitGifTemp
            || _weather.icon != _splitGifIcon
            || hour12 != _splitLastHour;

        if (needGifUpload) {
            // Calculate layout to find weather region
            int fid = _fontId;
            int fw = FONT_W[fid];
            int scale = 2;
            if (fid == 3) scale = 2;
            int digitW = fw * scale;
            auto digitAdv = [&](char c) -> int {
                return (c == '1') ? max((int)scale, (fw - 1) * scale) : digitW;
            };
            char hBuf[4];
            snprintf(hBuf, sizeof(hBuf), "%d", hour12);
            int hLen = strlen(hBuf);
            char mBuf[4];
            snprintf(mBuf, sizeof(mBuf), "%02d", timeinfo.tm_min);
            int colonLPad = max(1, scale - 2);
            int colonRPad = scale;
            int dotSize = scale;
            int digitGap = scale;
            int hourAdv = 0;
            for (int i = 0; i < hLen; i++) {
                if (i > 0) hourAdv += digitGap;
                hourAdv += digitAdv(hBuf[i]);
            }
            int minAdv = digitAdv(mBuf[0]) + digitGap + digitAdv(mBuf[1]);
            int timeW = hourAdv + colonLPad + dotSize + colonRPad + minAdv;
            int ampmW = PixelFont::stringWidth(timeinfo.tm_hour >= 12 ? "PM" : "AM");
            int clockPortionW = timeW + 2 + ampmW + 8; // ampmGap + padding
            int gifX = max(clockPortionW, (int)Framebuffer::W / 2);
            int gifW = Framebuffer::W - gifX;

            bool ok = generateSplitWeatherGif(gifX, gifW);
            if (ok) {
                _splitGifActive = true;
                _splitGifTemp = _weather.temp;
                _splitGifIcon = _weather.icon;
                _splitGifX = gifX;
                _splitGifW = gifW;
                _splitLastHour = hour12;
                _hasPrevFrame = false;
                _forceRedraw = true;
                Serial.printf("[Split] Weather GIF at x=%d w=%d\n", gifX, gifW);
            }
        }
        // rt_draw the clock (left portion) with minute-only updates
        if (_forceRedraw || currentMinute != _lastMinuteSent) {
            if (timeinfo.tm_sec <= 2 || _forceRedraw) {
                renderClock();
                _lastMinuteSent = currentMinute;
                _forceRedraw = false;
            }
        }

    } else if (_displayMode == DISPLAY_GIF_PROGRAM) {
        if (_forceRedraw || currentMinute != _lastMinuteSent) {
            if (timeinfo.tm_sec <= 5 || _forceRedraw) {
                bool ok = generateAndUploadGif();
                if (!ok) {
                    _gifFailCount++;
                    Serial.printf("[Clock] GIF upload failed (%d/%d)\n",
                        _gifFailCount, MAX_GIF_FAILS);
                    if (_gifFailCount >= MAX_GIF_FAILS) {
                        Serial.println("[Clock] GIF failed too many times, falling back");
                        _displayMode = DISPLAY_RT_DRAW;
                        _hasPrevFrame = false;
                        renderClock();
                        _lastMinuteSent = currentMinute;
                    }
                } else {
                    _gifFailCount = 0;
                    _lastMinuteSent = currentMinute;
                }
                _forceRedraw = false;
            }
        }
    } else {
        if (!_forceRedraw && currentMinute == _lastMinuteSent) return;
        if (!_forceRedraw && timeinfo.tm_sec > 2) return;
        renderClock();
        _lastMinuteSent = currentMinute;
        _forceRedraw = false;
    }
}

// ---------------------------------------------------------------------------
// Weather fetch
// ---------------------------------------------------------------------------

static const char* owmToIcon(int id, const char* owmIcon) {
    bool night = (owmIcon[2] == 'n');
    if (id >= 200 && id < 300) return "storm";
    if (id >= 300 && id < 400) return "rain";
    if (id >= 500 && id < 600) return "rain";
    if (id >= 600 && id < 700) return "snow";
    if (id >= 700 && id < 800) return "fog";
    if (id == 800) return night ? "moon" : "sun";
    return "cloud";
}

void ClockWeatherMode::fetchWeather() {
    bool gotCurrent = false;

    // Try OpenWeatherMap for current conditions (skip if key previously failed)
    static bool owmKeyValid = true;
    if (owmKeyValid) {
        HTTPClient http;
        String url = "http://api.openweathermap.org/data/2.5/weather?"
                     "lat=" + String(WEATHER_LAT, 3) +
                     "&lon=" + String(WEATHER_LON, 3) +
                     "&appid=" OPENWEATHER_KEY
                     "&units=imperial";
        http.begin(url);
        http.setTimeout(8000);
        int code = http.GET();
        Serial.printf("[Weather-OWM] HTTP %d\n", code);
        if (code == 200) {
            String payload = http.getString();
            JsonDocument doc;
            if (!deserializeJson(doc, payload)) {
                JsonVariant tempVal = doc["main"]["temp"];
                if (!tempVal.isNull()) {
                    _weather.temp = (int)tempVal.as<float>();
                } else {
                    Serial.println("[Weather-OWM] temp field missing, keeping previous value");
                }
                _weather.humidity = doc["main"]["humidity"] | 0;
                int wxId = doc["weather"][0]["id"] | 800;
                const char* owmIcon = doc["weather"][0]["icon"] | "01d";
                _weather.code = wxId;
                _weather.isDay = (owmIcon[2] != 'n');
                _weather.icon = owmToIcon(wxId, owmIcon);
                _weather.valid = true;
                gotCurrent = true;
                Serial.printf("[Weather-OWM] %dF %d%% %s (id=%d)\n",
                    _weather.temp, _weather.humidity, _weather.icon.c_str(), wxId);
            }
        } else if (code == 401) {
            Serial.println("[Weather-OWM] Key invalid, disabling until reboot");
            owmKeyValid = false;
        }
        http.end();
    }

    // Open-Meteo: hourly forecast + current conditions (primary if OWM disabled)
    {
        HTTPClient http;
        String url = "http://api.open-meteo.com/v1/forecast?"
                     "latitude=" + String(WEATHER_LAT, 3) +
                     "&longitude=" + String(WEATHER_LON, 3) +
                     "&hourly=temperature_2m,uv_index,weather_code,precipitation_probability"
                     "&daily=temperature_2m_max,temperature_2m_min"
                     "&current=temperature_2m,weather_code,is_day,relative_humidity_2m,uv_index"
                     "&temperature_unit=fahrenheit"
                     "&timezone=America%2FNew_York"
                     "&forecast_days=2";
        http.begin(url);
        http.setTimeout(10000);
        int code = http.GET();
        Serial.printf("[Weather-Meteo] HTTP %d\n", code);
        if (code == 200) {
            String payload = http.getString();
            JsonDocument doc;
            if (!deserializeJson(doc, payload)) {
                // Always grab UV from Open-Meteo (OWM doesn't provide it)
                _weather.uvIndex = doc["current"]["uv_index"] | 0.0f;

                // Open-Meteo is better at precipitation detection than OWM —
                // override icon if Meteo sees rain/snow but OWM didn't
                int meteoWmo = doc["current"]["weather_code"] | 0;
                if (gotCurrent && meteoWmo >= 50) {
                    String meteoIcon = wmoToIcon(meteoWmo, _weather.isDay);
                    if ((meteoIcon == "rain" || meteoIcon == "snow") &&
                        _weather.icon != "rain" && _weather.icon != "snow") {
                        Serial.printf("[Weather-Meteo] Overriding OWM icon '%s' → '%s' (wmo=%d)\n",
                            _weather.icon.c_str(), meteoIcon.c_str(), meteoWmo);
                        _weather.icon = meteoIcon;
                        _weather.code = meteoWmo;
                    }
                }

                if (!gotCurrent) {
                    JsonVariant tempVal = doc["current"]["temperature_2m"];
                    if (!tempVal.isNull()) {
                        _weather.temp = (int)tempVal.as<float>();
                    } else {
                        Serial.println("[Weather-Meteo] temperature_2m missing, keeping previous value");
                    }
                    _weather.humidity = doc["current"]["relative_humidity_2m"] | 0;
                    int wmo = doc["current"]["weather_code"] | 0;
                    _weather.isDay = doc["current"]["is_day"] | 1;
                    _weather.icon = wmoToIcon(wmo, _weather.isDay);
                    _weather.code = wmo;
                    _weather.valid = true;
                    gotCurrent = true;
                    Serial.printf("[Weather-Meteo] %dF %d%% UV%.1f %s (wmo=%d)\n",
                        _weather.temp, _weather.humidity, _weather.uvIndex,
                        _weather.icon.c_str(), wmo);
                }

                JsonArray temps = doc["hourly"]["temperature_2m"];
                JsonArray uvs = doc["hourly"]["uv_index"];
                JsonArray wmos = doc["hourly"]["weather_code"];
                JsonArray probs = doc["hourly"]["precipitation_probability"];
                // Today: indices 0-23
                _weather.hourlyCount = 0;
                float hi = -999, lo = 999;
                for (int i = 0; i < 24 && i < (int)temps.size(); i++) {
                    float t = temps[i].as<float>();
                    _weather.hourly[i] = t;
                    if (t > hi) hi = t;
                    if (t < lo) lo = t;
                    if (i < (int)uvs.size())
                        _weather.hourlyUV[i] = uvs[i].as<float>();
                    else
                        _weather.hourlyUV[i] = 0;
                    if (i < (int)wmos.size())
                        _weather.hourlyWMO[i] = wmos[i].as<int>();
                    else
                        _weather.hourlyWMO[i] = 0;
                    if (i < (int)probs.size())
                        _weather.hourlyPrecipProb[i] = probs[i].as<int>();
                    else
                        _weather.hourlyPrecipProb[i] = 0;
                    _weather.hourlyCount++;
                }
                // Use daily max/min from API (more accurate than hourly-derived)
                JsonArray dailyMax = doc["daily"]["temperature_2m_max"];
                JsonArray dailyMin = doc["daily"]["temperature_2m_min"];
                if (dailyMax.size() > 0 && dailyMin.size() > 0) {
                    _weather.tempHigh = dailyMax[0].as<float>();
                    _weather.tempLow = dailyMin[0].as<float>();
                } else {
                    _weather.tempHigh = hi;
                    _weather.tempLow = lo;
                }
                // Clamp so hi/lo never contradict current observation
                if (_weather.temp > _weather.tempHigh) _weather.tempHigh = _weather.temp;
                if (_weather.temp < _weather.tempLow) _weather.tempLow = _weather.temp;

                // Tomorrow: indices 24-47
                _weather.tomorrowCount = 0;
                for (int i = 24; i < 48 && i < (int)temps.size(); i++) {
                    int j = i - 24;
                    float t = temps[i].as<float>();
                    _weather.tomorrowHourly[j] = t;
                    if (i < (int)wmos.size())
                        _weather.tomorrowWMO[j] = wmos[i].as<int>();
                    else
                        _weather.tomorrowWMO[j] = 0;
                    if (i < (int)probs.size())
                        _weather.tomorrowPrecipProb[j] = probs[i].as<int>();
                    else
                        _weather.tomorrowPrecipProb[j] = 0;
                    _weather.tomorrowCount++;
                }
                if (dailyMax.size() > 1 && dailyMin.size() > 1) {
                    _weather.tomorrowHigh = dailyMax[1].as<float>();
                    _weather.tomorrowLow = dailyMin[1].as<float>();
                } else {
                    float tHi = -999, tLo = 999;
                    for (int j = 0; j < _weather.tomorrowCount; j++) {
                        if (_weather.tomorrowHourly[j] > tHi) tHi = _weather.tomorrowHourly[j];
                        if (_weather.tomorrowHourly[j] < tLo) tLo = _weather.tomorrowHourly[j];
                    }
                    _weather.tomorrowHigh = tHi;
                    _weather.tomorrowLow = tLo;
                }
            }
        }
        http.end();
    }

    if (!gotCurrent)
        Serial.println("[Weather] Both APIs failed, keeping previous values");
}

// ---------------------------------------------------------------------------
// Clock rendering
// ---------------------------------------------------------------------------

void ClockWeatherMode::renderClock() {
    if (_clearTextOnNext) {
        TextProgramConfig blank;
        blank.scroll = "static";
        blank.fontSize = 16;
        blank.duration = 1;
        TextSegment seg;
        seg.text = " ";
        seg.r = 0; seg.g = 0; seg.b = 0;
        blank.segments.push_back(seg);
        auto pkts = buildTextProgram("", blank);
        for (auto& pkt : pkts) { _ble->send(pkt); delay(100); }
        delay(300);
        _clearTextOnNext = false;
        _hasPrevFrame = false;
    }

    struct tm ti;
    if (!getLocalTime(&ti)) return;
    int hour12 = ti.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;

    bool minuteOnly = _hasPrevFrame && _minRegionValid && !_forceRedraw
        && hour12 == _lastHourSent
        && _weather.temp == _lastTempSent
        && _weather.icon == _lastIconSent;

    static Framebuffer fb;
    // Save previous region bounds before drawClockFace overwrites them
    _prevMinRegionX0 = _minRegionX0;
    _prevMinRegionX1 = _minRegionX1;
    _prevMinOnesX0 = _minOnesX0;
    fb.clear();
    drawClockFace(fb);

    if (minuteOnly) {
        int minTens = ti.tm_min / 10;
        bool tensChanged = (minTens != _lastMinTens);
        _lastMinTens = minTens;
        // Union current and previous frame regions to cover proportional width changes
        int curX0 = tensChanged ? _minRegionX0 : _minOnesX0;
        int prevX0 = tensChanged ? _prevMinRegionX0 : _prevMinOnesX0;
        int rx0 = max(0, min(curX0, prevX0));
        int ry0 = max(0, _minRegionY0);
        int rx1 = min((int)Framebuffer::W - 1, max(_minRegionX1, _prevMinRegionX1));
        int ry1 = min((int)Framebuffer::VISIBLE_H - 1, _minRegionY1);
        sendRegion(fb, rx0, ry0, rx1, ry1, 4);
        Serial.printf("[Clock] Minute update %d:%02d region [%d,%d]-[%d,%d]%s\n",
            hour12, ti.tm_min, rx0, ry0, rx1, ry1, tensChanged ? " (tens)" : "");
    } else if (_hasPrevFrame) {
        int dx0 = Framebuffer::W, dy0 = Framebuffer::VISIBLE_H, dx1 = -1, dy1 = -1;
        for (int y = 0; y < Framebuffer::VISIBLE_H; y++) {
            for (int x = 0; x < Framebuffer::W; x++) {
                if (fb.getPixel(x, y) != _prevFrame.getPixel(x, y)) {
                    if (x < dx0) dx0 = x;
                    if (x > dx1) dx1 = x;
                    if (y < dy0) dy0 = y;
                    if (y > dy1) dy1 = y;
                }
            }
        }
        if (dx1 >= 0) {
            auto clearPayload = Framebuffer::buildRegionClear(dx0, dy0, dx1, dy1);
            auto clearPkt = AA55::buildPacket(AA55::nextSno(),
                clearPayload.data(), clearPayload.size(), 0xC1, 2);
            _ble->send(clearPkt);
            delay(50);
            auto allPackets = fb.buildRtDrawPackets(12);
            for (size_t i = 1; i < allPackets.size(); i++) {
                _ble->send(allPackets[i]);
                if (i < allPackets.size() - 1) delay(50);
            }
        }
        Serial.printf("[Clock] Full update %d:%02d%s %dF %s\n",
            hour12, ti.tm_min, ti.tm_hour >= 12 ? "PM" : "AM",
            _weather.temp, _weather.icon.c_str());
    } else {
        sendFrame(fb, 12);
        Serial.printf("[Clock] Initial frame %d:%02d%s %dF %s\n",
            hour12, ti.tm_min, ti.tm_hour >= 12 ? "PM" : "AM",
            _weather.temp, _weather.icon.c_str());
    }

    _prevFrame = fb;
    _hasPrevFrame = true;
    _lastHourSent = hour12;
    _lastTempSent = _weather.temp;
    _lastIconSent = _weather.icon;
}

// ---------------------------------------------------------------------------
// Font-aware scaled digit drawing
// ---------------------------------------------------------------------------

static void drawScaledDigit(Framebuffer& fb, int digit, int x, int y,
                            int fontId, int scale, RGB color, bool aa) {
    int fw = FONT_W[fontId];
    int fh = FONT_H[fontId];
    const uint8_t* glyph;

    if (fontId == 3)
        glyph = FONT3[digit];
    else if (fontId == 2)
        glyph = FONT2[digit];
    else if (fontId == 1)
        glyph = FONT1[digit];
    else
        glyph = FONT0[digit];

    for (int row = 0; row < fh; row++) {
        for (int col = 0; col < fw; col++) {
            if (glyph[row] & (1 << (fw - 1 - col))) {
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        fb.putPixel(x + col * scale + sx, y + row * scale + sy, color);
            }
        }
    }

    // Edge AA — optional dim outline around each lit block
    if (aa && scale >= 2) {
        RGB dim = {(uint8_t)(color.r / 2), (uint8_t)(color.g / 2), (uint8_t)(color.b / 2)};
        for (int row = 0; row < fh; row++) {
            for (int col = 0; col < fw; col++) {
                bool on = glyph[row] & (1 << (fw - 1 - col));
                if (!on) continue;
                bool rOff = (col == fw - 1) || !(glyph[row] & (1 << (fw - 2 - col)));
                bool lOff = (col == 0) || !(glyph[row] & (1 << (fw - col)));
                bool bOff = (row == fh - 1) || !(glyph[row + 1] & (1 << (fw - 1 - col)));
                bool tOff = (row == 0) || !(glyph[row - 1] & (1 << (fw - 1 - col)));
                if (rOff) {
                    int px = x + (col + 1) * scale;
                    for (int sy = 0; sy < scale; sy++)
                        fb.putPixel(px, y + row * scale + sy, dim);
                }
                if (lOff) {
                    int px = x + col * scale - 1;
                    if (px >= 0)
                        for (int sy = 0; sy < scale; sy++)
                            fb.putPixel(px, y + row * scale + sy, dim);
                }
                if (bOff) {
                    int py = y + (row + 1) * scale;
                    for (int sx = 0; sx < scale; sx++)
                        fb.putPixel(x + col * scale + sx, py, dim);
                }
                if (tOff) {
                    int py = y + row * scale - 1;
                    if (py >= 0)
                        for (int sx = 0; sx < scale; sx++)
                            fb.putPixel(x + col * scale + sx, py, dim);
                }
            }
        }
    }

    // Diagonal smoothing — always on at scale >= 2 to fix staircase jaggies
    if (scale >= 2) {
        RGB mid = {(uint8_t)(color.r * 2 / 3), (uint8_t)(color.g * 2 / 3), (uint8_t)(color.b * 2 / 3)};
        RGB lo  = {(uint8_t)(color.r / 3), (uint8_t)(color.g / 3), (uint8_t)(color.b / 3)};
        for (int row = 0; row < fh - 1; row++) {
            for (int col = 0; col < fw - 1; col++) {
                bool tl = glyph[row]     & (1 << (fw - 1 - col));
                bool tr = glyph[row]     & (1 << (fw - 1 - (col + 1)));
                bool bl = glyph[row + 1] & (1 << (fw - 1 - col));
                bool br = glyph[row + 1] & (1 << (fw - 1 - (col + 1)));

                // Down-right diagonal: TL on, BR on, TR off, BL off
                if (tl && br && !tr && !bl) {
                    int rx = x + (col + 1) * scale;
                    int ry = y + row * scale + scale - 1;
                    int lx = x + col * scale + scale - 1;
                    int ly = y + (row + 1) * scale;
                    fb.putPixel(rx, ry, mid);
                    fb.putPixel(lx, ly, mid);
                    if (scale > 2) {
                        fb.putPixel(rx + 1, ry, lo);
                        fb.putPixel(lx, ly + 1, lo);
                    }
                }

                // Down-left diagonal: TR on, BL on, TL off, BR off
                if (tr && bl && !tl && !br) {
                    int lx = x + (col + 1) * scale - 1;
                    int ry = y + row * scale + scale - 1;
                    int rx = x + col * scale + scale;
                    int ly = y + (row + 1) * scale;
                    fb.putPixel(lx, ry, mid);
                    fb.putPixel(rx, ly, mid);
                    if (scale > 2) {
                        fb.putPixel(lx - 1, ry, lo);
                        fb.putPixel(rx, ly + 1, lo);
                    }
                }

                // Concave corners (3 of 4 ON) — only at narrow 1-pixel steps
                // Skip if the "bar" of the L extends beyond the 2x2 block
                int s = scale;

                // .X / XX — top-left OFF
                if (!tl && tr && bl && br) {
                    bool lExt = (col > 0)    && (glyph[row+1] & (1 << (fw-1-(col-1))));
                    bool rExt = (col+2 < fw) && (glyph[row+1] & (1 << (fw-1-(col+2))));
                    if (!lExt && !rExt)
                        fb.putPixel(x + col*s + s-1, y + row*s + s-1, lo);
                }
                // X. / XX — top-right OFF
                if (tl && !tr && bl && br) {
                    bool lExt = (col > 0)    && (glyph[row+1] & (1 << (fw-1-(col-1))));
                    bool rExt = (col+2 < fw) && (glyph[row+1] & (1 << (fw-1-(col+2))));
                    if (!lExt && !rExt)
                        fb.putPixel(x + (col+1)*s, y + row*s + s-1, lo);
                }
                // XX / .X — bottom-left OFF
                if (tl && tr && !bl && br) {
                    bool lExt = (col > 0)    && (glyph[row] & (1 << (fw-1-(col-1))));
                    bool rExt = (col+2 < fw) && (glyph[row] & (1 << (fw-1-(col+2))));
                    if (!lExt && !rExt)
                        fb.putPixel(x + col*s + s-1, y + (row+1)*s, lo);
                }
                // XX / X. — bottom-right OFF
                if (tl && tr && bl && !br) {
                    bool lExt = (col > 0)    && (glyph[row] & (1 << (fw-1-(col-1))));
                    bool rExt = (col+2 < fw) && (glyph[row] & (1 << (fw-1-(col+2))));
                    if (!lExt && !rExt)
                        fb.putPixel(x + (col+1)*s, y + (row+1)*s, lo);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Draw clock face — all layouts
// ---------------------------------------------------------------------------

void ClockWeatherMode::drawClockFace(Framebuffer& fb) {
    struct tm ti;
    if (!getLocalTime(&ti)) return;

    int hour12 = ti.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;

    int fid = _fontId;
    int fw = FONT_W[fid];
    int fh = FONT_H[fid];
    int scale = (_clockLayout == 0) ? 2 : 3;
    if (fid == 3) scale = 2; // detailed 5x7 at scale 2 = 10x14
    int digitW = fw * scale;
    int digitH = fh * scale;
    bool aa = _fontAA;

    RGB timeColor = _cTimeColor;
    RGB tempClr = tempColor(_weather.temp);
    uint8_t iconR, iconG, iconB;
    iconColor(_weather.icon.c_str(), iconR, iconG, iconB);
    RGB iconClr = {iconR, iconG, iconB};

    char hourBuf[4], minBuf[4], ampmBuf[4];
    snprintf(hourBuf, sizeof(hourBuf), "%d", hour12);
    snprintf(minBuf, sizeof(minBuf), "%02d", ti.tm_min);
    snprintf(ampmBuf, sizeof(ampmBuf), "%s", ti.tm_hour >= 12 ? "PM" : "AM");

    char tempFBuf[8];
    if (_weather.valid)
        snprintf(tempFBuf, sizeof(tempFBuf), "%dF", _weather.temp);
    else
        snprintf(tempFBuf, sizeof(tempFBuf), "--");
    int tempW = tempStrWidth(tempFBuf);
    int iw7 = 7; // 7x7 icon width
    int ih7 = 7; // 7x7 icon height

    // Calculate time width
    int hLen = strlen(hourBuf);
    int colonLPad = max(1, scale - 2);   // tighter on left
    int colonRPad = scale;                // more space on right
    int dotSize = scale;                  // bigger colon dots
    int digitGap = scale;                 // inter-digit spacing

    // Proportional advance: "1" is narrower since its glyph is mostly one column
    auto digitAdv = [&](char c) -> int {
        return (c == '1') ? max((int)scale, (fw - 1) * scale) : digitW;
    };

    // Compute timeW from actual digit widths
    int hourAdv = 0;
    for (int i = 0; i < hLen; i++) {
        if (i > 0) hourAdv += digitGap;
        hourAdv += digitAdv(hourBuf[i]);
    }
    int minAdv = digitAdv(minBuf[0]) + digitGap + digitAdv(minBuf[1]);
    int timeW = hourAdv + colonLPad + dotSize + colonRPad + minAdv;

    // Shared: draw time digits + colon, returns ending x
    // Also records minute digit bounding box for partial updates
    auto drawTimeFn = [&](int startX, int startY) -> int {
        int cx = startX;
        for (int i = 0; i < hLen; i++) {
            if (i > 0) cx += digitGap;
            drawScaledDigit(fb, hourBuf[i] - '0', cx, startY, fid, scale, timeColor, aa);
            cx += digitAdv(hourBuf[i]);
        }
        // Colon: tight left, roomy right, bigger dots
        cx += colonLPad;
        int colonX = cx;
        int centerY = startY + digitH / 2;
        int dotOff = max(2, digitH / 4);
        for (int dy = 0; dy < dotSize; dy++)
            for (int dx = 0; dx < dotSize; dx++) {
                fb.putPixel(colonX + dx, centerY - dotOff + dy, timeColor);
                fb.putPixel(colonX + dx, centerY + dotOff - 1 + dy, timeColor);
            }
        cx += dotSize + colonRPad;
        _minRegionX0 = cx;
        _minRegionY0 = startY - (aa && scale >= 2 ? 1 : 0);
        for (int i = 0; i < 2; i++) {
            drawScaledDigit(fb, minBuf[i] - '0', cx, startY, fid, scale, timeColor, aa);
            cx += digitAdv(minBuf[i]);
            if (i == 0) { _minOnesX0 = cx + digitGap; cx += digitGap; }
        }
        _minRegionX1 = cx + (aa && scale >= 2 ? 1 : 0);
        _minRegionY1 = startY + digitH + (scale >= 2 ? 1 : 0);
        _minRegionValid = true;
        return cx;
    };

    int gap = 3;

    if (_clockLayout == 0) {
        bool animMode = _displayMode == DISPLAY_GIF_PROGRAM;
        bool splitMode = _displayMode == DISPLAY_SPLIT;
        int ampmW = PixelFont::stringWidth(ampmBuf);

        if (splitMode) {
            // Split mode: only draw clock + AM/PM, centered in left portion
            int leftW = _splitGifActive ? _splitGifX : Framebuffer::W / 2;
            int ampmGap = 2;
            int clockTotal = timeW + ampmGap + ampmW;
            int xStart = max(0, (leftW - clockTotal) / 2);
            int ty = max(0, (VISIBLE_H - digitH) / 2);

            int cx = drawTimeFn(xStart, ty);
            int ampmY = ty + digitH - 5;
            PixelFont::drawString(fb, ampmBuf, cx + ampmGap, ampmY, timeColor);
            _iconRegionValid = false;

        } else if (animMode) {
            int animGap = 4;
            char tempLine[8];
            snprintf(tempLine, sizeof(tempLine), "%dF", _weather.temp);
            int tempLineW = tempStrWidth(tempLine);

            // Find when precipitation ends
            char endLine[10] = "";
            struct tm now_tm;
            if (getLocalTime(&now_tm) && _weather.hourlyCount >= 24) {
                int curHour = now_tm.tm_hour;
                int clearHour = -1;
                for (int h = curHour + 1; h < 24; h++) {
                    int wmo = _weather.hourlyWMO[h];
                    bool precip = (wmo >= 51 && wmo <= 67) || (wmo >= 71 && wmo <= 77) ||
                                  (wmo >= 80 && wmo <= 86) || (wmo >= 95 && wmo <= 99);
                    if (!precip) { clearHour = h; break; }
                }
                if (clearHour >= 0) {
                    int h12 = clearHour % 12;
                    if (h12 == 0) h12 = 12;
                    snprintf(endLine, sizeof(endLine), "Til %d%s",
                        h12, clearHour >= 12 ? "P" : "A");
                } else {
                    snprintf(endLine, sizeof(endLine), "All day");
                }
            }
            int endLineW = (endLine[0]) ? PixelFont::stringWidth(endLine) : 0;
            int dropAndTextW = endLine[0] ? (5 + endLineW) : 0;
            int weatherW = max(tempLineW, dropAndTextW);
            int ampmGap = 2;
            int total = timeW + ampmGap + ampmW + animGap + weatherW;
            int xStart = max(0, (Framebuffer::W - total) / 2);
            int ty = max(0, (VISIBLE_H - digitH) / 2);

            int cx = drawTimeFn(xStart, ty);
            int ampmY = ty + digitH - 5;
            PixelFont::drawString(fb, ampmBuf, cx + ampmGap, ampmY, timeColor);
            cx += ampmGap + ampmW;

            int wx = cx + animGap;
            drawTempStr(fb, tempLine, wx, 1, tempClr);
            if (endLine[0]) {
                RGB drop = {60, 120, 255};
                int dx = wx, dy = 9;
                fb.putPixel(dx+1, dy,   drop);
                fb.putPixel(dx,   dy+1, drop);
                fb.putPixel(dx+1, dy+1, drop);
                fb.putPixel(dx+2, dy+1, drop);
                fb.putPixel(dx,   dy+2, drop);
                fb.putPixel(dx+1, dy+2, drop);
                fb.putPixel(dx+2, dy+2, drop);
                fb.putPixel(dx,   dy+3, drop);
                fb.putPixel(dx+1, dy+3, drop);
                fb.putPixel(dx+2, dy+3, drop);
                fb.putPixel(dx+1, dy+4, drop);
                int textX = wx + 5;
                RGB brightClr = {(uint8_t)min(255, tempClr.r * 3 / 4 + 60),
                                 (uint8_t)min(255, tempClr.g * 3 / 4 + 60),
                                 (uint8_t)min(255, tempClr.b * 3 / 4 + 60)};
                PixelFont::drawString(fb, endLine, textX, 9, brightClr);
            }
            _iconRegionValid = false;
        } else {
            // Normal mode: icon + temp
            int normalGap = 6;
            int bigIconW = WeatherIcons::iconWidth(_weather.icon.c_str());
            int bigIconH = WeatherIcons::iconHeight(_weather.icon.c_str());
            int ampmGap = 2;
            int total = timeW + ampmGap + ampmW + normalGap + bigIconW + 3 + tempW;
            int xStart = max(0, (Framebuffer::W - total) / 2);
            int ty = max(0, (VISIBLE_H - digitH) / 2);

            int cx = drawTimeFn(xStart, ty);
            int ampmY = ty + digitH - 5;
            PixelFont::drawString(fb, ampmBuf, cx + ampmGap, ampmY, timeColor);
            cx += ampmGap + ampmW;

            int ix = cx + normalGap;
            int iy = max(0, (VISIBLE_H - bigIconH) / 2);
            WeatherIcons::drawFrame(fb, _weather.icon.c_str(), ix, iy, iconClr, _animFrame);
            _iconRx0 = ix; _iconRy0 = iy;
            _iconRx1 = ix + bigIconW - 1; _iconRy1 = iy + bigIconH - 1;
            _iconRegionValid = true;
            drawTempStr(fb, tempFBuf, ix + bigIconW + 3, max(0, (VISIBLE_H - 7) / 2), tempClr);
        }

    } else if (_clockLayout == 1) {
        int ty = max(0, (VISIBLE_H - digitH) / 2);
        int iy = max(0, (VISIBLE_H - ih7) / 2);
        drawIcon7(fb, _weather.icon.c_str(), 1, iy, iconClr);
        _iconRx0 = 1; _iconRy0 = iy;
        _iconRx1 = 1 + iw7 - 1; _iconRy1 = iy + ih7 - 1;
        _iconRegionValid = true;
        int timeStart = max(0, (Framebuffer::W - timeW) / 2);
        drawTimeFn(timeStart, ty);
        drawTempStr(fb, tempFBuf, Framebuffer::W - tempW - 1,
                    max(0, (VISIBLE_H - 7) / 2), tempClr);

    } else if (_clockLayout == 2) {
        // Stacked weather right-aligned, time centered in remaining space
        int ampmW = PixelFont::stringWidth(ampmBuf);
        int weatherW = max(tempW, iw7);
        int weatherX = Framebuffer::W - weatherW - 1;
        int timeArea = weatherX - gap;
        int totalTime = timeW + 2 + ampmW;
        int xStart = max(0, (timeArea - totalTime) / 2);
        int ty = max(0, (VISIBLE_H - digitH) / 2);

        int cx = drawTimeFn(xStart, ty);
        int ampmY = ty + digitH - 5;
        PixelFont::drawString(fb, ampmBuf, cx + 2, ampmY, timeColor);

        int tempX = weatherX + (weatherW - tempW) / 2;
        drawTempStr(fb, tempFBuf, tempX, 0, tempClr);
        int iconX = weatherX + (weatherW - iw7) / 2;
        drawIcon7(fb, _weather.icon.c_str(), iconX, 9, iconClr);
        _iconRx0 = iconX; _iconRy0 = 9;
        _iconRx1 = iconX + iw7 - 1; _iconRy1 = 9 + ih7 - 1;
        _iconRegionValid = true;

    } else {
        // Layout 3: Custom
        int cScale = _cTimeScale;
        int cFw = FONT_W[fid];
        int cDigitW = cFw * cScale;
        int cDigitH = FONT_H[fid] * cScale;
        RGB cTimeClr = _cTimeColor;

        int cpx = _cTimeX;
        for (int i = 0; i < hLen; i++) {
            if (i > 0) cpx += cScale;
            drawScaledDigit(fb, hourBuf[i] - '0', cpx, _cTimeY, fid, cScale, cTimeClr, aa);
            cpx += cDigitW;
        }
        int cColonPad = max(1, cScale - 1);
        int cDotSz = max(1, cScale - 1);
        cpx += cColonPad;
        int cCenterY = _cTimeY + cDigitH / 2;
        int cDotOff = max(2, cDigitH / 4);
        for (int dy = 0; dy < cDotSz; dy++)
            for (int dx = 0; dx < cDotSz; dx++) {
                fb.putPixel(cpx+dx, cCenterY-cDotOff+dy, cTimeClr);
                fb.putPixel(cpx+dx, cCenterY+cDotOff-1+dy, cTimeClr);
            }
        cpx += cDotSz + cColonPad;
        for (int i = 0; i < 2; i++) {
            drawScaledDigit(fb, minBuf[i] - '0', cpx, _cTimeY, fid, cScale, cTimeClr, aa);
            cpx += cDigitW;
            if (i == 0) cpx += cScale;
        }
        int ampmY = _cTimeY + cDigitH - 5;
        PixelFont::drawString(fb, ampmBuf, cpx + 1, ampmY, cTimeClr);

        drawTempStr(fb, tempFBuf, _cTempX, _cTempY, tempClr);

        if (_cIconSize == 0) {
            drawIcon7(fb, _weather.icon.c_str(), _cIconX, _cIconY, iconClr);
        } else {
            int ciy = _cIconY;
            if (ciy + ih7 > VISIBLE_H) ciy = VISIBLE_H - ih7;
            drawIcon7(fb, _weather.icon.c_str(), _cIconX, ciy, iconClr);
        }
    }
}

// ---------------------------------------------------------------------------
// Weather particles
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Forecast flash
// ---------------------------------------------------------------------------

void ClockWeatherMode::renderForecastFlash() {
    if (_weather.hourlyCount < 24) return;

    // Render forecast into framebuffer
    static Framebuffer fb;
    fb.clear();
    drawForecastFullscreen(fb);

    // Build a palette from the forecast frame
    RGB palette[16];
    int numColors = 0;
    // Always include black as index 0
    palette[numColors++] = {0, 0, 0};

    // Collect unique colors (simple nearest-match with limited palette)
    static uint8_t indexBuf[Framebuffer::W * Framebuffer::H];
    for (int y = 0; y < Framebuffer::H; y++) {
        for (int x = 0; x < Framebuffer::W; x++) {
            RGB px = fb.getPixel(x, y);
            if (px.isBlack()) {
                indexBuf[y * Framebuffer::W + x] = 0;
                continue;
            }
            int bestIdx = 0;
            int bestDist = 999999;
            for (int c = 0; c < numColors; c++) {
                int dr = (int)px.r - palette[c].r;
                int dg = (int)px.g - palette[c].g;
                int db = (int)px.b - palette[c].b;
                int d = dr*dr + dg*dg + db*db;
                if (d < bestDist) { bestDist = d; bestIdx = c; }
            }
            if (bestDist > 1000 && numColors < 16) {
                bestIdx = numColors;
                palette[numColors++] = px;
            }
            indexBuf[y * Framebuffer::W + x] = bestIdx;
        }
    }

    // Encode as single-frame GIF
    static uint8_t gifBuf[8192];
    GifEncoder gif;
    if (!gif.begin(gifBuf, sizeof(gifBuf), Framebuffer::W, Framebuffer::H,
                   palette, numColors, 100)) {  // 1000cs = 10s display
        Serial.println("[Forecast] GIF encoder init failed");
        return;
    }
    if (!gif.addFrame(indexBuf)) {
        Serial.println("[Forecast] GIF frame failed");
        return;
    }
    size_t gifSize = gif.finish();

    // Upload as full-screen GIF to program slot 1 (replaces weather GIF)
    auto packets = buildGifProgram(gifBuf, gifSize, 1, 0, 0, Framebuffer::W, Framebuffer::H);
    for (size_t i = 0; i < packets.size(); i++) {
        _ble->send(packets[i]);
        delay(80);
    }
    Serial.printf("[Forecast] Uploaded as GIF (%zu bytes, %d colors)\n", gifSize, numColors);

    // Hold for 10 seconds, then replace forecast GIF with black 1x1
    delay(10000);

    static const uint8_t BLACK_GIF[] = {
        0x47,0x49,0x46,0x38,0x39,0x61,
        0x01,0x00,0x01,0x00,0x80,0x00,0x00,
        0x00,0x00,0x00, 0x00,0x00,0x00,
        0x2C,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,
        0x02,0x02,0x44,0x01,0x00,
        0x3B
    };
    auto clearPkts = buildGifProgram(BLACK_GIF, sizeof(BLACK_GIF), 1, 0, 0, 1, 1);
    for (size_t i = 0; i < clearPkts.size(); i++) {
        _ble->send(clearPkts[i]);
        delay(80);
    }
    delay(200);
    Serial.println("[Forecast] Cleared forecast GIF");

    _splitGifActive = false;
    _splitGifIcon = "";
    _splitGifTemp = -999;
    _forceRedraw = true;
    _hasPrevFrame = false;
}

void ClockWeatherMode::drawForecastFullscreen(Framebuffer& fb) {
    struct tm ti;
    bool haveTime = getLocalTime(&ti);
    // After 6PM, show tomorrow's forecast instead of today's
    bool showTomorrow = haveTime && ti.tm_hour >= 20 && _weather.tomorrowCount >= 24;

    float* hourlyData = showTomorrow ? _weather.tomorrowHourly : _weather.hourly;
    int* precipProb = showTomorrow ? _weather.tomorrowPrecipProb : _weather.hourlyPrecipProb;
    float tLow = showTomorrow ? _weather.tomorrowLow : _weather.tempLow;
    float tHigh = showTomorrow ? _weather.tomorrowHigh : _weather.tempHigh;
    float tRange = max(1.0f, tHigh - tLow);

    for (int x = 0; x < Framebuffer::W; x++) {
        float hourF = (float)x / Framebuffer::W * 24.0f;
        int h0 = (int)hourF;
        int h1 = min(h0 + 1, 23);
        float fracH = hourF - h0;
        float tempVal = hourlyData[h0] * (1.0f - fracH) + hourlyData[h1] * fracH;
        float frac = (tempVal - tLow) / tRange;
        int row = VISIBLE_H - 1 - (int)round(frac * (VISIBLE_H - 1));
        RGB c = tempColor((int)tempVal);
        RGB dim = {(uint8_t)(c.r / 4), (uint8_t)(c.g / 4), (uint8_t)(c.b / 4)};
        for (int fy = row; fy < VISIBLE_H; fy++)
            fb.putPixel(x, fy, (fy == row) ? c : dim);

        float probVal = precipProb[h0] * (1.0f - fracH) + precipProb[min(h0 + 1, 23)] * fracH;
        if (probVal > 1.0f) {
            float pf = probVal / 100.0f;
            uint8_t rb = (uint8_t)(30 + pf * 170);
            uint8_t rg = (uint8_t)(pf * pf * 80);
            uint8_t rr = (uint8_t)(pf * pf * 40);
            // Blend with the graph fill color underneath
            RGB under = dim;
            RGB rain1 = {(uint8_t)(under.r + (rr - under.r) * pf),
                         (uint8_t)(under.g + (rg - under.g) * pf),
                         (uint8_t)(under.b + (rb - under.b) * pf)};
            RGB rain2 = {rr, rg, rb};
            fb.putPixel(x, VISIBLE_H - 2, rain1);
            fb.putPixel(x, VISIBLE_H - 1, rain2);
        }
    }

    if (haveTime) {
        if (!showTomorrow) {
            // Draw "now" marker for today's forecast
            float pxPerHour = (float)Framebuffer::W / 24.0f;
            int nowX = (int)(ti.tm_hour * pxPerHour + pxPerHour / 2);
            RGB white = {255, 255, 255};
            for (int y = 0; y < VISIBLE_H; y++)
                if (nowX >= 0 && nowX < Framebuffer::W)
                    fb.putPixel(nowX, y, white);
        }

        // Label: current time + temp for today, "Tmrw" + hi/lo for tomorrow
        char labelBuf[16];
        RGB labelColor;
        if (showTomorrow) {
            snprintf(labelBuf, sizeof(labelBuf), "Tmrw %d/%d",
                (int)_weather.tomorrowHigh, (int)_weather.tomorrowLow);
            labelColor = {180, 140, 255};
        } else {
            int h12 = ti.tm_hour % 12;
            if (h12 == 0) h12 = 12;
            snprintf(labelBuf, sizeof(labelBuf), "%d:%02d", h12, ti.tm_min);
            labelColor = {50, 130, 255};
        }
        int tw = PixelFont::stringWidth(labelBuf);

        uint8_t ir, ig, ib;
        iconColor(_weather.icon.c_str(), ir, ig, ib);
        RGB ic = {ir, ig, ib};

        char tempBuf[8];
        snprintf(tempBuf, sizeof(tempBuf), "%d", _weather.temp);
        RGB tc = tempColor(_weather.temp);
        int tempW2 = PixelFont::stringWidth(tempBuf);

        int iconW = 5;
        int labelW = tw + 2 + iconW + 2 + tempW2;
        fb.fillRect(0, 0, labelW + 2, 5, {0, 0, 0});

        int cx = 1;
        PixelFont::drawString(fb, labelBuf, cx, 0, labelColor);
        cx += tw + 2;

        static const char* TINY_SUN[]  ={"  #  ","# # #"," ### ","# # #","  #  "};
        static const char* TINY_MOON[] ={" ##  ","  ## ","  ## ","  ## "," ##  "};
        static const char* TINY_CLOUD[]={"     "," ### ","#####","#####"," ### "};
        static const char* TINY_RAIN[] ={" ### ","#####"," # # ","# # #","     "};
        static const char* TINY_SNOW[] ={" # # ","  #  ","# # #","  #  "," # # "};
        static const char* TINY_STORM[]={"  #  "," ##  ","#### "," ##  ","#    "};
        static const char* TINY_FOG[]  ={"#####","     ","#####","     ","#####"};

        const char** tinyIcon = TINY_CLOUD;
        const char* iname = _weather.icon.c_str();
        if (strcmp(iname,"sun")==0) tinyIcon = TINY_SUN;
        else if (strcmp(iname,"moon")==0) tinyIcon = TINY_MOON;
        else if (strcmp(iname,"rain")==0) tinyIcon = TINY_RAIN;
        else if (strcmp(iname,"snow")==0) tinyIcon = TINY_SNOW;
        else if (strcmp(iname,"storm")==0) tinyIcon = TINY_STORM;
        else if (strcmp(iname,"fog")==0) tinyIcon = TINY_FOG;

        for (int ry = 0; ry < 5; ry++)
            for (int rx = 0; rx < 5 && tinyIcon[ry][rx]; rx++)
                if (tinyIcon[ry][rx] == '#')
                    fb.putPixel(cx + rx, ry, ic);
        cx += 7;
        PixelFont::drawString(fb, tempBuf, cx, 0, tc);
    }

    char hiBuf[8];
    snprintf(hiBuf, sizeof(hiBuf), "%d", (int)tHigh);
    RGB hiC = tempColor((int)tHigh);
    int hiW = PixelFont::stringWidth(hiBuf);
    fb.fillRect(Framebuffer::W - hiW - 2, 0, Framebuffer::W - 1, 5, {0, 0, 0});
    PixelFont::drawString(fb, hiBuf, Framebuffer::W - hiW - 1, 0, hiC);

    char loBuf[8];
    snprintf(loBuf, sizeof(loBuf), "%d", (int)tLow);
    RGB loC = tempColor((int)tLow);
    int loW = PixelFont::stringWidth(loBuf);
    fb.fillRect(Framebuffer::W - loW - 2, VISIBLE_H - 8, Framebuffer::W - 1, VISIBLE_H - 3, {0, 0, 0});
    PixelFont::drawString(fb, loBuf, Framebuffer::W - loW - 1, VISIBLE_H - 7, loC);

    // UV peak label — placed under highest-temp section (most space below graph line)
    float peakUV = 0;
    for (int h = 0; h < 24; h++)
        if (_weather.hourlyUV[h] > peakUV) peakUV = _weather.hourlyUV[h];
    if (peakUV >= 1.0f) {
        RGB uvColor = {180, 0, 255};
        char uvBuf[4];
        snprintf(uvBuf, sizeof(uvBuf), "%d", (int)peakUV);
        int uvNumW = PixelFont::stringWidth(uvBuf);
        int fullW = 4 + 4 + 1 + uvNumW;

        float pxPerHour = (float)Framebuffer::W / 24.0f;
        int labelWidthHours = (int)ceil((float)(fullW + 4) / pxPerHour);
        if (labelWidthHours < 3) labelWidthHours = 3;
        float bestAvg = -9999;
        int bestH = 0;
        for (int h = 0; h <= 24 - labelWidthHours; h++) {
            float sum = 0;
            for (int j = h; j < h + labelWidthHours; j++)
                sum += _weather.hourly[j];
            float avg = sum / labelWidthHours;
            if (avg > bestAvg) { bestAvg = avg; bestH = h; }
        }
        int lx = (int)((bestH + labelWidthHours / 2.0f) * pxPerHour) - fullW / 2;
        lx = max(0, min(Framebuffer::W - fullW - 1, lx));
        int ly = VISIBLE_H - 8;

        fb.fillRect(lx - 1, ly - 1, lx + fullW, ly + 5, {0, 0, 0});
        int pcx = lx;
        pcx += PixelFont::drawString(fb, "U", pcx, ly, uvColor);
        pcx += 1;
        pcx += PixelFont::drawString(fb, "V", pcx, ly, uvColor);
        pcx += 2;
        PixelFont::drawString(fb, uvBuf, pcx, ly, uvColor);
    }
}

// ---------------------------------------------------------------------------
// Timer rendering
// ---------------------------------------------------------------------------

void ClockWeatherMode::renderTimer() {
    if (_timerMode == TIMER_COUNTDOWN) {
        int remaining = timerRemainingSec();
        if (remaining <= 0 && _timerRunning) {
            _timerRunning = false;
            _timerFinishedFlash = true;
            _timerFlashCount = 0;
            Serial.println("[Timer] Countdown finished!");
        }
        if (_timerFinishedFlash) {
            _timerFlashCount++;
            if (_timerFlashCount > 10) {
                resetTimer();
                return;
            }
            static Framebuffer fb;
            fb.clear();
            if (_timerFlashCount % 2 == 0) {
                RGB green = {0, 255, 0};
                drawTempStr(fb, "0", 40, 4, green);
            } else {
                RGB red = {255, 0, 0};
                PixelFont::drawString(fb, "TIME!", 30, 5, red);
            }
            sendFrame(fb, 4);
            return;
        }
    }

    static Framebuffer fb;
    fb.clear();
    drawTimerFace(fb);
    sendFrame(fb, 2);
}

void ClockWeatherMode::drawTimerFace(Framebuffer& fb) {
    int totalSec;
    if (_timerMode == TIMER_COUNTDOWN) {
        totalSec = timerRemainingSec();
    } else {
        totalSec = timerElapsedSec();
    }

    int hours = totalSec / 3600;
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;

    bool showHours = (hours > 0) || (_timerMode == TIMER_COUNTDOWN && _timerDurationMs >= 3600000);
    int fid = _fontId;
    int scale = 2;
    if (fid == 3) scale = 2;
    int fw = FONT_W[fid];
    int fh = FONT_H[fid];
    int digitW = fw * scale;
    int digitH = fh * scale;
    bool aa = _fontAA;

    RGB color = _timerRunning ? _cTimeColor : RGB{100, 100, 100};
    if (_timerMode == TIMER_COUNTDOWN) {
        int remaining = timerRemainingSec();
        if (remaining <= 10 && remaining > 0)
            color = {255, 50, 0};
        else if (remaining <= 30)
            color = {255, 180, 0};
    }

    int colonPad = max(1, scale - 1);
    int dotSize = max(1, scale - 1);
    int pairW = 2 * digitW + scale;
    int colonW = colonPad + dotSize + colonPad;
    int totalW;
    if (showHours)
        totalW = pairW + colonW + pairW + colonW + pairW;
    else
        totalW = pairW + colonW + pairW;

    int xStart = max(0, (Framebuffer::W - totalW) / 2);
    int ty = max(0, (16 - digitH) / 2);
    int cx = xStart;

    auto drawPair = [&](int val) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02d", val);
        drawScaledDigit(fb, buf[0] - '0', cx, ty, fid, scale, color, aa);
        cx += digitW + scale;
        drawScaledDigit(fb, buf[1] - '0', cx, ty, fid, scale, color, aa);
        cx += digitW;
    };

    auto drawColon = [&]() {
        cx += colonPad;
        int centerY = ty + digitH / 2;
        int dotOff = max(2, digitH / 4);
        for (int dy = 0; dy < dotSize; dy++)
            for (int dx = 0; dx < dotSize; dx++) {
                fb.putPixel(cx + dx, centerY - dotOff + dy, color);
                fb.putPixel(cx + dx, centerY + dotOff - 1 + dy, color);
            }
        cx += dotSize + colonPad;
    };

    if (showHours) {
        drawPair(hours);
        drawColon();
    }
    drawPair(mins);
    drawColon();
    drawPair(secs);

    // Label at bottom
    RGB dim = {60, 60, 60};
    const char* label = _timerMode == TIMER_COUNTDOWN ? "COUNTDOWN" : "STOPWATCH";
    if (!_timerRunning && _timerMode != TIMER_NONE)
        label = "PAUSED";
    int lw = PixelFont::stringWidth(label);
    if (ty + digitH + 6 <= 16)
        PixelFont::drawString(fb, label, max(0, (Framebuffer::W - lw) / 2), ty + digitH + 1, dim);
}

// ---------------------------------------------------------------------------
// News RSS fetch
// ---------------------------------------------------------------------------

static bool headlineMatches(const String& title) {
    String lower = title;
    lower.toLowerCase();
    // Reject opinion/editorial content
    static const char* reject[] = {
        "opinion:", "op-ed", "column:", "editorial:", "analysis:",
        "review:", "commentary:", "perspective:",
        nullptr
    };
    for (int i = 0; reject[i]; i++)
        if (lower.indexOf(reject[i]) >= 0) return false;

    // Supreme Court/SCOTUS requires an action word — not speculation
    if (lower.indexOf("supreme court") >= 0 || lower.indexOf("scotus") >= 0) {
        static const char* scotusAction[] = {
            "overturns", "overturned", "upheld", "upholds",
            "strikes down", "5-4", "6-3", "7-2", "9-0",
            "unanimous",
            nullptr
        };
        for (int i = 0; scotusAction[i]; i++)
            if (lower.indexOf(scotusAction[i]) >= 0) return true;
        return false;
    }

    static const char* keywords[] = {
        "doj", "department of justice", "attorney general",
        "indictment", "impeach", "martial law",
        "executive order",
        "overturned", "struck down", "upheld",
        "mass shooting", "shooter", "assassination", "assassinated",
        "bombing", "terrorist", "declare war", "invasion", "invades",
        "earthquake", "hurricane", "tornado", "wildfire",
        "recall", "fda", "cdc", "pandemic",
        "killed", "dead", "explosion",
        "protest", "riot", "emergency",
        nullptr
    };
    for (int i = 0; keywords[i]; i++)
        if (lower.indexOf(keywords[i]) >= 0) return true;
    return false;
}

static const char* NEWS_QUERIES[] = {
    "supreme+court+OR+congress+OR+white+house",
    "shooting+OR+earthquake+OR+hurricane+OR+explosion",
    nullptr
};

void ClockWeatherMode::fetchNews() {
    _newsCount = 0;
    _currentNewsIdx = 0;

    for (int q = 0; NEWS_QUERIES[q] && _newsCount < 5; q++) {
        HTTPClient http;
        String url = String("https://news.google.com/rss/search?q=") +
            NEWS_QUERIES[q] + "&hl=en-US&gl=US&ceid=US:en&when=1d";
        http.begin(url);
        http.setTimeout(10000);
        http.addHeader("User-Agent", "ESP32-Sign/1.0");
        int code = http.GET();
        Serial.printf("[News] Fetch q=%s → %d\n", NEWS_QUERIES[q], code);

        if (code == 200) {
            String body = http.getString();
            int pos = 0;
            while (_newsCount < 5) {
                int itemStart = body.indexOf("<item", pos);
                if (itemStart < 0) break;
                int titleStart = body.indexOf("<title>", itemStart);
                if (titleStart < 0) break;
                titleStart += 7;
                int titleEnd = body.indexOf("</title>", titleStart);
                if (titleEnd < 0) break;
                String title = body.substring(titleStart, titleEnd);
                title.replace("<![CDATA[", "");
                title.replace("]]>", "");
                // Strip " - Source" suffix from Google News titles
                int dashIdx = title.lastIndexOf(" - ");
                if (dashIdx > 20) title = title.substring(0, dashIdx);
                title.trim();

                if (title.length() > 0 && title.length() < 150 && headlineMatches(title)) {
                    bool dup = false;
                    for (int i = 0; i < _newsCount; i++)
                        if (_newsQueue[i] == title) { dup = true; break; }
                    if (!dup && alreadyShown(title)) dup = true;
                    if (!dup) {
                        _newsQueue[_newsCount++] = title;
                        Serial.printf("[News] MATCH #%d: %s\n", _newsCount, title.c_str());
                    }
                }
                pos = titleEnd;
            }
        }
        http.end();
        if (NEWS_QUERIES[q + 1]) delay(500);
    }
    Serial.printf("[News] Total: %d matching headlines\n", _newsCount);
}

// ---------------------------------------------------------------------------
// Calendar fetch (Google Apps Script)
// ---------------------------------------------------------------------------

void ClockWeatherMode::fetchCalendar() {
    if (_calendarUrl.length() == 0) return;

    HTTPClient http;
    http.begin(_calendarUrl);
    http.setTimeout(10000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = http.GET();
    Serial.printf("[Cal] Fetch → %d\n", code);

    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("[Cal] JSON error: %s\n", err.c_str());
            http.end();
            return;
        }

        JsonArray events = doc["events"].as<JsonArray>();
        _calEventCount = 0;
        for (JsonObject ev : events) {
            if (_calEventCount >= 5) break;
            String title = ev["title"] | "";
            String timeStr = ev["time"] | "";
            if (title.length() > 0) {
                _calEvents[_calEventCount] = timeStr + " " + title;
                Serial.printf("[Cal] #%d: %s %s\n", _calEventCount + 1,
                    timeStr.c_str(), title.c_str());
                _calEventCount++;
            }
        }
        Serial.printf("[Cal] Got %d events\n", _calEventCount);
    }
    http.end();
}

// ---------------------------------------------------------------------------
// Notification proxy fetch
// ---------------------------------------------------------------------------

void ClockWeatherMode::fetchNotifications() {
    if (_proxyUrl.length() == 0) return;

    HTTPClient http;
    http.begin(_proxyUrl + "/notifications");
    http.setTimeout(5000);
    int code = http.GET();

    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("[Notif] JSON error: %s\n", err.c_str());
            http.end();
            return;
        }

        JsonArray items = doc["items"].as<JsonArray>();
        _notifCount = 0;
        for (JsonObject item : items) {
            if (_notifCount >= 8) break;
            String text = item["text"] | "";
            bool urgent = item["urgent"] | false;
            if (text.length() > 0) {
                _notifications[_notifCount++] = text;
                if (urgent && _timerMode == TIMER_NONE) {
                    scrollHeadline(text);
                }
            }
        }
        Serial.printf("[Notif] Got %d notifications\n", _notifCount);
    }
    http.end();
}

// ---------------------------------------------------------------------------
// NWS Weather Alerts
// ---------------------------------------------------------------------------

void ClockWeatherMode::fetchWeatherAlerts() {
    HTTPClient http;
    String url = "http://nws-alerts-proxy.matthew-s-schwartz.workers.dev/"
                 "?lat=" + String(WEATHER_LAT, 3) +
                 "&lon=" + String(WEATHER_LON, 3);
    http.begin(url);
    http.setTimeout(10000);
    int code = http.GET();
    Serial.printf("[Alerts] Fetch → %d\n", code);

    _alertCount = 0;

    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            JsonArray alerts = doc["alerts"].as<JsonArray>();
            for (JsonVariant a : alerts) {
                if (_alertCount >= 3) break;
                String event = a.as<String>();
                if (event.length() > 0) {
                    _weatherAlerts[_alertCount++] = event;
                    Serial.printf("[Alerts] Active: %s\n", event.c_str());
                }
            }
        }
    }
    http.end();
    Serial.printf("[Alerts] Total active: %d\n", _alertCount);
}

// ---------------------------------------------------------------------------
// Headline scrolling
// ---------------------------------------------------------------------------

void ClockWeatherMode::scrollHeadline(const String& text) {
    stopGifProgram();
    // Give plenty of time for full scroll — sign at speed 10 with fontSize 12
    // is roughly 0.4s per character. Duration must exceed scroll time.
    int scrollSec = max((int)(text.length() * 0.45f), 10);
    int waitMs = scrollSec * 1000;

    TextProgramConfig cfg;
    cfg.scroll = "left";
    cfg.speed = 10;
    cfg.fontSize = 12;
    cfg.fontFamily = 0x00;
    cfg.duration = scrollSec + 10;

    TextSegment seg;
    seg.text = text + "          ";
    seg.r = 255; seg.g = 100; seg.b = 0;
    cfg.segments.push_back(seg);

    auto pkts = buildTextProgram("", cfg);
    for (auto& pkt : pkts) { _ble->send(pkt); delay(150); }
    delay(waitMs);

    TextProgramConfig blank;
    blank.scroll = "static";
    blank.fontSize = 16;
    TextSegment blankSeg;
    blankSeg.text = " ";
    blankSeg.r = 0; blankSeg.g = 0; blankSeg.b = 0;
    blank.segments.push_back(blankSeg);
    auto blankPkts = buildTextProgram("", blank);
    for (auto& pkt : blankPkts) { _ble->send(pkt); delay(150); }
    delay(300);
    _forceRedraw = true;
    _hasPrevFrame = false;
}

// ---------------------------------------------------------------------------
// Send frame
// ---------------------------------------------------------------------------

void ClockWeatherMode::stopGifProgram() {
    if (!_splitGifActive && _displayMode == DISPLAY_RT_DRAW) return;

    // Upload a 1x1 single-frame black GIF to program slot 1 in a tiny off-screen region
    // This replaces the weather GIF without triggering the built-in car demo
    // (deleting the program triggers the demo)
    static const uint8_t BLACK_GIF[] = {
        0x47,0x49,0x46,0x38,0x39,0x61, // GIF89a
        0x01,0x00,0x01,0x00,0x80,0x00,0x00, // 1x1, 1 color
        0x00,0x00,0x00, 0x00,0x00,0x00, // black palette
        0x2C,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00, // image descriptor
        0x02,0x02,0x44,0x01,0x00, // LZW min 2, data
        0x3B // trailer
    };
    auto pkts = buildGifProgram(BLACK_GIF, sizeof(BLACK_GIF), 1, 0, 0, 1, 1);
    for (size_t i = 0; i < pkts.size(); i++) {
        _ble->send(pkts[i]);
        delay(80);
    }
    delay(200);

    _splitGifActive = false;
    _hasPrevFrame = false;
    _forceRedraw = true;
    Serial.println("[GIF] Replaced GIF with blank for overlay content");
}

void ClockWeatherMode::sendFrame(Framebuffer& fb, int maxColors) {
    auto packets = fb.buildRtDrawPackets(maxColors);
    for (size_t i = 0; i < packets.size(); i++) {
        _ble->send(packets[i]);
        if (i < packets.size() - 1) delay(50);
    }
}

void ClockWeatherMode::sendTestFrame(Framebuffer& fb) {
    sendFrame(fb, 2);
    _forceRedraw = true;
    _hasPrevFrame = false;
}

void ClockWeatherMode::sendRegion(Framebuffer& fb, int x0, int y0, int x1, int y1, int maxColors) {
    auto packets = fb.buildRegionPackets(x0, y0, x1, y1, maxColors);
    for (size_t i = 0; i < packets.size(); i++) {
        _ble->send(packets[i]);
        if (i < packets.size() - 1) delay(50);
    }
}

// ---------------------------------------------------------------------------
// GIF animation
// ---------------------------------------------------------------------------

static int gifColorDist(RGB a, RGB b) {
    int dr = (int)a.r - b.r;
    int dg = (int)a.g - b.g;
    int db = (int)a.b - b.b;
    return dr*dr + dg*dg + db*db;
}

void ClockWeatherMode::buildAnimPalette(RGB* palette, int* numColors) {
    int n = 0;
    palette[n++] = {0, 0, 0}; // black background
    palette[n++] = _cTimeColor;
    // AA dim variants
    palette[n++] = {(uint8_t)(_cTimeColor.r/2), (uint8_t)(_cTimeColor.g/2), (uint8_t)(_cTimeColor.b/2)};
    palette[n++] = {(uint8_t)(_cTimeColor.r*2/3), (uint8_t)(_cTimeColor.g*2/3), (uint8_t)(_cTimeColor.b*2/3)};
    palette[n++] = {(uint8_t)(_cTimeColor.r/3), (uint8_t)(_cTimeColor.g/3), (uint8_t)(_cTimeColor.b/3)};
    palette[n++] = tempColor(_weather.temp);
    uint8_t ir, ig, ib;
    iconColor(_weather.icon.c_str(), ir, ig, ib);
    palette[n++] = {ir, ig, ib};

    // Particle colors
    RGB pColors[8];
    int pCount = 0;
    WeatherParticles::getParticleColors(
        WeatherParticles::typeFromIcon(_weather.icon.c_str()), pColors, &pCount);
    for (int i = 0; i < pCount && n < 16; i++) {
        bool dup = false;
        for (int j = 0; j < n; j++)
            if (palette[j] == pColors[i]) { dup = true; break; }
        if (!dup) palette[n++] = pColors[i];
    }
    *numColors = n;
}

bool ClockWeatherMode::generateAndUploadGif() {
    const int NUM_FRAMES = 12;
    const int FRAME_DELAY_CS = 12; // 150ms per frame

    RGB palette[16];
    int numColors = 0;
    buildAnimPalette(palette, &numColors);

    static uint8_t gifBuf[12288];
    GifEncoder gif;
    if (!gif.begin(gifBuf, sizeof(gifBuf), Framebuffer::W, Framebuffer::H,
                   palette, numColors, FRAME_DELAY_CS)) {
        Serial.println("[GIF] Encoder init failed");
        return false;
    }

    static Framebuffer fb;
    static uint8_t indexBuf[Framebuffer::W * Framebuffer::H];

    auto ptype = WeatherParticles::typeFromIcon(_weather.icon.c_str());

    for (int f = 0; f < NUM_FRAMES; f++) {
        fb.clear();
        drawClockFace(fb);
        WeatherParticles::renderParticles(fb, ptype, f, NUM_FRAMES, (int)(millis() / 1000));

        // Quantize to palette
        for (int y = 0; y < Framebuffer::H; y++) {
            for (int x = 0; x < Framebuffer::W; x++) {
                RGB px = fb.getPixel(x, y);
                int bestIdx = 0;
                if (!px.isBlack()) {
                    int bestDist = gifColorDist(px, palette[0]);
                    for (int c = 1; c < numColors; c++) {
                        int d = gifColorDist(px, palette[c]);
                        if (d < bestDist) { bestDist = d; bestIdx = c; }
                    }
                }
                indexBuf[y * Framebuffer::W + x] = bestIdx;
            }
        }

        if (!gif.addFrame(indexBuf)) {
            Serial.printf("[GIF] Frame %d failed (buffer full)\n", f);
            return false;
        }
    }

    size_t gifSize = gif.finish();
    Serial.printf("[GIF] %d frames, %zu bytes\n", NUM_FRAMES, gifSize);

    auto packets = buildGifProgram(gifBuf, gifSize);
    Serial.printf("[GIF] Uploading %d packets\n", (int)packets.size());

    for (size_t i = 0; i < packets.size(); i++) {
        _ble->send(packets[i]);
        delay(100);
    }

    Serial.println("[GIF] Upload complete");
    return true;
}

bool ClockWeatherMode::generateSplitWeatherGif(int gifX, int gifW) {
    const int GIF_H = Framebuffer::H;
    const int NUM_FRAMES = 12;
    const int FRAME_DELAY_CS = 12;

    RGB palette[16];
    int numColors = 0;
    buildAnimPalette(palette, &numColors);

    static uint8_t gifBuf[8192];
    GifEncoder gif;
    if (!gif.begin(gifBuf, sizeof(gifBuf), gifW, GIF_H, palette, numColors, FRAME_DELAY_CS)) {
        Serial.println("[Split-GIF] Encoder init failed");
        return false;
    }

    // Weather content to bake into GIF
    RGB tempClr = tempColor(_weather.temp);
    char tempLine[8];
    snprintf(tempLine, sizeof(tempLine), "%dF", _weather.temp);
    int tempLineW = tempStrWidth(tempLine);

    char endLine[10] = "";
    struct tm now_tm;
    if (getLocalTime(&now_tm) && _weather.hourlyCount >= 24) {
        int curHour = now_tm.tm_hour;
        int clearHour = -1;
        for (int h = curHour + 1; h < 24; h++) {
            int wmo = _weather.hourlyWMO[h];
            bool precip = (wmo >= 51 && wmo <= 67) || (wmo >= 71 && wmo <= 77) ||
                          (wmo >= 80 && wmo <= 86) || (wmo >= 95 && wmo <= 99);
            if (!precip) { clearHour = h; break; }
        }
        if (clearHour >= 0) {
            int h12 = clearHour % 12;
            if (h12 == 0) h12 = 12;
            snprintf(endLine, sizeof(endLine), "Til %d%s",
                h12, clearHour >= 12 ? "P" : "A");
        } else {
            snprintf(endLine, sizeof(endLine), "All day");
        }
    }
    int endLineW = endLine[0] ? PixelFont::stringWidth(endLine) : 0;

    auto ptype = WeatherParticles::typeFromIcon(_weather.icon.c_str());
    static Framebuffer fb;
    static uint8_t indexBuf[48 * 22]; // max gifW * GIF_H

    for (int f = 0; f < NUM_FRAMES; f++) {
        fb.clear();

        // Draw weather content at absolute positions, will extract gifX region
        int wx = gifX + 3; // left margin within GIF (skip column 0 to avoid edge artifacts)
        drawTempStr(fb, tempLine, wx, 1, tempClr);
        if (endLine[0]) {
            RGB drop = {60, 120, 255};
            int dx = wx, dy = 9;
            fb.putPixel(dx+1, dy,   drop);
            fb.putPixel(dx,   dy+1, drop);
            fb.putPixel(dx+1, dy+1, drop);
            fb.putPixel(dx+2, dy+1, drop);
            fb.putPixel(dx,   dy+2, drop);
            fb.putPixel(dx+1, dy+2, drop);
            fb.putPixel(dx+2, dy+2, drop);
            fb.putPixel(dx,   dy+3, drop);
            fb.putPixel(dx+1, dy+3, drop);
            fb.putPixel(dx+2, dy+3, drop);
            fb.putPixel(dx+1, dy+4, drop);
            int textX = wx + 5;
            RGB brightClr = {(uint8_t)min(255, tempClr.r * 3 / 4 + 60),
                             (uint8_t)min(255, tempClr.g * 3 / 4 + 60),
                             (uint8_t)min(255, tempClr.b * 3 / 4 + 60)};
            PixelFont::drawString(fb, endLine, textX, 9, brightClr);
        }

        // Rain particles across the whole screen, we extract just our region
        WeatherParticles::renderParticles(fb, ptype, f, NUM_FRAMES, (int)(millis() / 1000));

        // Clear first column to avoid edge artifact at GIF boundary
        for (int y = 0; y < GIF_H; y++)
            fb.putPixel(gifX, y, {0, 0, 0});

        // Extract the GIF region and quantize
        for (int y = 0; y < GIF_H; y++) {
            for (int x = 0; x < gifW; x++) {
                RGB px = fb.getPixel(gifX + x, y);
                int bestIdx = 0;
                if (!px.isBlack()) {
                    int bestDist = gifColorDist(px, palette[0]);
                    for (int c = 1; c < numColors; c++) {
                        int d = gifColorDist(px, palette[c]);
                        if (d < bestDist) { bestDist = d; bestIdx = c; }
                    }
                }
                indexBuf[y * gifW + x] = bestIdx;
            }
        }

        if (!gif.addFrame(indexBuf)) {
            Serial.printf("[Split-GIF] Frame %d failed\n", f);
            return false;
        }
    }

    size_t gifSize = gif.finish();
    Serial.printf("[Split-GIF] %dx%d at x=%d, %d frames, %zu bytes\n",
        gifW, GIF_H, gifX, NUM_FRAMES, gifSize);

    auto packets = buildGifProgram(gifBuf, gifSize, 1, gifX, 0, gifW, GIF_H);
    for (size_t i = 0; i < packets.size(); i++) {
        _ble->send(packets[i]);
        delay(100);
    }
    return true;
}

void ClockWeatherMode::setAnimStyle(int style) {
    _animStyle = (style == 0) ? 0 : 1;
    _forceRedraw = true;
    _hasPrevFrame = false;
    _splitGifActive = false;
    _displayMode = DISPLAY_RT_DRAW; // force re-evaluation
    Preferences prefs;
    prefs.begin("clock", false);
    prefs.putInt("animStyle", _animStyle);
    prefs.end();
    Serial.printf("[Clock] Animation style set to %s\n",
        _animStyle == 1 ? "split" : "full");
}

bool ClockWeatherMode::testRegionalGif() {
    const int GIF_W = 30, GIF_H = Framebuffer::H;
    const int GIF_X = Framebuffer::W - GIF_W;
    const int NUM_FRAMES = 12;
    const int FRAME_DELAY_CS = 12;

    // Freeze display updates for 15 seconds
    _testHoldUntil = millis() + 15000;
    Serial.println("[Test] Starting regional GIF test (15s hold)");

    // Palette: black + rain particle colors
    RGB palette[16];
    int numColors = 0;
    palette[numColors++] = {0, 0, 0};

    auto ptype = WeatherParticles::typeFromIcon("rain");
    RGB pColors[8];
    int pCount = 0;
    WeatherParticles::getParticleColors(ptype, pColors, &pCount);
    for (int i = 0; i < pCount && numColors < 16; i++)
        palette[numColors++] = pColors[i];

    static uint8_t gifBuf[4096];
    GifEncoder gif;
    if (!gif.begin(gifBuf, sizeof(gifBuf), GIF_W, GIF_H, palette, numColors, FRAME_DELAY_CS)) {
        Serial.println("[Test] GIF encoder init failed");
        _testHoldUntil = 0;
        return false;
    }

    static Framebuffer fb;
    static uint8_t indexBuf[30 * 22];

    for (int f = 0; f < NUM_FRAMES; f++) {
        fb.clear();
        WeatherParticles::renderParticles(fb, ptype, f, NUM_FRAMES, (int)(millis() / 1000));

        for (int y = 0; y < GIF_H; y++) {
            for (int x = 0; x < GIF_W; x++) {
                RGB px = fb.getPixel(GIF_X + x, y);
                int bestIdx = 0;
                if (!px.isBlack()) {
                    int bestDist = gifColorDist(px, palette[0]);
                    for (int c = 1; c < numColors; c++) {
                        int d = gifColorDist(px, palette[c]);
                        if (d < bestDist) { bestDist = d; bestIdx = c; }
                    }
                }
                indexBuf[y * GIF_W + x] = bestIdx;
            }
        }

        if (!gif.addFrame(indexBuf)) {
            Serial.printf("[Test] Frame %d failed\n", f);
            _testHoldUntil = 0;
            return false;
        }
    }

    size_t gifSize = gif.finish();
    Serial.printf("[Test] Regional GIF: %dx%d at (%d,0), %zu bytes\n",
        GIF_W, GIF_H, GIF_X, gifSize);

    // Step 1: Clear the entire screen
    static Framebuffer blankFb;
    blankFb.clear();
    sendFrame(blankFb, 1);
    Serial.println("[Test] Screen cleared");
    delay(1000);

    // Step 2: Upload the regional rain GIF to the right side
    auto packets = buildGifProgram(gifBuf, gifSize, 1, GIF_X, 0, GIF_W, GIF_H);
    Serial.printf("[Test] Uploading %d GIF packets (region %d,0 %dx%d)\n",
        (int)packets.size(), GIF_X, GIF_W, GIF_H);
    for (size_t i = 0; i < packets.size(); i++) {
        _ble->send(packets[i]);
        delay(100);
    }
    Serial.println("[Test] Regional GIF uploaded, waiting 3s to see if it renders...");
    delay(3000);

    // Step 3: rt_draw the clock on the left side only
    static Framebuffer clockFb;
    clockFb.clear();
    drawClockFace(clockFb);
    sendRegion(clockFb, 0, 0, GIF_X - 1, VISIBLE_H - 1, 12);
    Serial.println("[Test] rt_draw sent to left region. Watch for 15s!");

    _hasPrevFrame = false;
    _forceRedraw = true;
    return true;
}

// ---------------------------------------------------------------------------
// Color helpers
// ---------------------------------------------------------------------------

RGB ClockWeatherMode::tempColor(int tempF) {
    if (tempF <= 32) return {0, 100, 255};
    if (tempF <= 50) return {0, 200, 255};
    if (tempF <= 65) return {0, 255, 100};
    if (tempF <= 80) return {180, 255, 0};
    if (tempF <= 90) return {255, 140, 0};
    return {255, 40, 0};
}

const char* ClockWeatherMode::iconColor(const char* icon, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (strcmp(icon, "sun") == 0) { r=255; g=255; b=0; }
    else if (strcmp(icon, "moon") == 0) { r=180; g=180; b=255; }
    else if (strcmp(icon, "cloud") == 0) { r=200; g=200; b=200; }
    else if (strcmp(icon, "fog") == 0) { r=140; g=140; b=140; }
    else if (strcmp(icon, "rain") == 0) { r=50; g=100; b=255; }
    else if (strcmp(icon, "snow") == 0) { r=220; g=230; b=255; }
    else if (strcmp(icon, "storm") == 0) { r=255; g=50; b=255; }
    else { r=200; g=200; b=200; }
    return icon;
}

const char* ClockWeatherMode::wmoToIcon(int code, bool isDay) {
    switch (code) {
    case 0: case 1: return isDay ? "sun" : "moon";
    case 2: case 3: return "cloud";
    case 45: case 48: return "fog";
    case 51: case 53: case 55: case 61: case 63: case 65:
    case 80: case 81: case 82: return "rain";
    case 71: case 73: case 75: case 77: case 85: case 86: return "snow";
    case 95: case 96: case 99: return "storm";
    default: return "cloud";
    }
}
