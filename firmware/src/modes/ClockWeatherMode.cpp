#include "ClockWeatherMode.h"
#include "../gfx/PixelFont.h"
#include "../gfx/WeatherIcons.h"
#include "../protocol/TextProgram.h"
#include "../protocol/AA55Packet.h"
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
    prefs.end();
    Serial.printf("[Clock] Loaded: layout=%d font=%d aa=%d color=#%02x%02x%02x\n",
        _clockLayout, _fontId, _fontAA, _cTimeColor.r, _cTimeColor.g, _cTimeColor.b);
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
    if (_weather.hourlyCount >= 24) {
        renderForecastFlash();
        _forceRedraw = true;
    }
}

void ClockWeatherMode::setClockLayout(int layout) {
    if (layout < 0) layout = 0;
    if (layout > 3) layout = 3;
    _clockLayout = layout;
    _forceRedraw = true;
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
// Main loop — animation-aware timing
// ---------------------------------------------------------------------------

void ClockWeatherMode::loop() {
    if (!_ble || !_ble->isReady()) return;

    unsigned long now = millis();

    if (_lastWeatherFetch == 0 || now - _lastWeatherFetch > 120000) {
        fetchWeather();
        _lastWeatherFetch = now;
    }

    // News fetch every 15 minutes, only 7am-9pm
    struct tm newsTime;
    bool newsHours = getLocalTime(&newsTime) && newsTime.tm_hour >= 7 && newsTime.tm_hour < 21;
    if (newsHours && (now - _lastNewsFetch > 900000 || _lastNewsFetch == 0)) {
        fetchNews();
        _lastNewsFetch = now;
    }

    // Calendar fetch every 5 minutes (from Google Apps Script)
    if (_calendarUrl.length() > 0 && (now - _lastCalFetch > 300000 || _lastCalFetch == 0)) {
        fetchCalendar();
        _lastCalFetch = now;
    }

    // NWS weather alerts every 5 minutes
    if (now - _lastAlertFetch > 300000 || _lastAlertFetch == 0) {
        fetchWeatherAlerts();
        _lastAlertFetch = now;
    }

    // Notification proxy poll every 30 seconds (only if proxy configured)
    if (_proxyUrl.length() > 0 && (now - _lastProxyPoll > 30000 || _lastProxyPoll == 0)) {
        fetchNotifications();
        _lastProxyPoll = now;
    }

    // Badge reminder: weekdays 7:30-8:30am, every 5 minutes
    bool badgeTime = newsTime.tm_wday >= 1 && newsTime.tm_wday <= 5 &&
        ((newsTime.tm_hour == 7 && newsTime.tm_min >= 30) ||
         (newsTime.tm_hour == 8 && newsTime.tm_min < 30));
    if (badgeTime) {
        static unsigned long lastBadge = 0;
        if (now - lastBadge > 300000 || lastBadge == 0) {
            scrollHeadline("Don't Forget Your Badge!!!");
            lastBadge = now;
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

    if (now - _lastWatchdog > 120000) {
        _lastWatchdog = now;
        _forceRedraw = true;
    }

    // Scroll content every 5 minutes: calendar always, news only 7am-9pm
    bool hasNews = newsHours && _newsCount > 0;
    if ((hasNews || _calEventCount > 0) && _lastNewsScroll > 0 &&
        now - _lastNewsScroll > 300000) {
        if (_calEventCount > 0 && (!hasNews || _currentNewsIdx % 2 == 0)) {
            int calIdx = (_currentNewsIdx / 2) % _calEventCount;
            scrollHeadline(_calEvents[calIdx]);
        } else if (hasNews) {
            int newsIdx = (_currentNewsIdx / 2) % _newsCount;
            scrollHeadline(_newsQueue[newsIdx]);
        }
        _currentNewsIdx++;
        _lastNewsScroll = now;
    }
    if ((hasNews || _calEventCount > 0) && _lastNewsScroll == 0)
        _lastNewsScroll = now;

    // Weather alerts: show static for 5s every 5 minutes while active
    if (_alertCount > 0 && (now - _lastAlertShow > 300000 || _lastAlertShow == 0)) {
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

    if (_weather.hourlyCount >= 24 && _lastForecastFlash > 0 &&
        now - _lastForecastFlash > 300000) {
        renderForecastFlash();
        _lastForecastFlash = now;
        _forceRedraw = true;
        return;
    }
    if (_lastForecastFlash == 0) _lastForecastFlash = now;

    if (_forecastUntil > 0 && now < _forecastUntil) {
        // Re-send forecast frame every 2s to keep it visible
        static unsigned long lastForecastResend = 0;
        if (now - lastForecastResend > 2000) {
            static Framebuffer ffb;
            ffb.clear();
            drawForecastFullscreen(ffb);
            sendFrame(ffb, 12);
            lastForecastResend = now;
        }
        return;
    }
    _forecastUntil = 0;

    _animActive = false;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    int currentMinute = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    if (!_forceRedraw && currentMinute == _lastMinuteSent) return;
    if (!_forceRedraw && timeinfo.tm_sec > 2) return;

    renderClock();
    _lastMinuteSent = currentMinute;
    _forceRedraw = false;
}

// ---------------------------------------------------------------------------
// Weather fetch
// ---------------------------------------------------------------------------

void ClockWeatherMode::fetchWeather() {
    HTTPClient http;
    String url = "https://api.open-meteo.com/v1/forecast?"
                 "latitude=" + String(WEATHER_LAT, 3) +
                 "&longitude=" + String(WEATHER_LON, 3) +
                 "&current=temperature_2m,weather_code,is_day"
                 "&hourly=temperature_2m,uv_index"
                 "&temperature_unit=fahrenheit"
                 "&timezone=America%2FNew_York"
                 "&forecast_days=1";

    http.begin(url);
    http.setTimeout(10000);
    int code = http.GET();

    Serial.printf("[Weather] HTTP code: %d\n", code);
    if (code == 200) {
        String payload = http.getString();
        Serial.printf("[Weather] Response: %d bytes\n", payload.length());

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Serial.printf("[Weather] JSON error: %s\n", err.c_str());
            http.end();
            return;
        }

        _weather.temp = (int)doc["current"]["temperature_2m"].as<float>();
        _weather.code = doc["current"]["weather_code"] | 0;
        _weather.isDay = doc["current"]["is_day"] | 1;
        _weather.icon = wmoToIcon(_weather.code, _weather.isDay);

        JsonArray temps = doc["hourly"]["temperature_2m"];
        JsonArray uvs = doc["hourly"]["uv_index"];
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
            _weather.hourlyCount++;
        }
        _weather.tempHigh = hi;
        _weather.tempLow = lo;

        Serial.printf("[Weather] %dF %s (code %d, %s) H:%.0f L:%.0f\n",
            _weather.temp, _weather.icon.c_str(), _weather.code,
            _weather.isDay ? "day" : "night", hi, lo);
    } else {
        Serial.printf("[Weather] Fetch failed: %d\n", code);
    }
    http.end();
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
    }

    static Framebuffer fb;
    fb.clear();
    drawClockFace(fb);
    if (_animActive) drawParticles(fb);
    sendFrame(fb, 12);

    struct tm ti;
    getLocalTime(&ti);
    Serial.printf("[Clock] %d:%02d%s %dF %s font=%d\n",
        ti.tm_hour > 12 ? ti.tm_hour - 12 : (ti.tm_hour == 0 ? 12 : ti.tm_hour),
        ti.tm_min, ti.tm_hour >= 12 ? "PM" : "AM",
        _weather.temp, _weather.icon.c_str(), _fontId);
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
    snprintf(tempFBuf, sizeof(tempFBuf), "%dF", _weather.temp);
    int tempW = tempStrWidth(tempFBuf);
    int iw7 = 7; // 7x7 icon width
    int ih7 = 7; // 7x7 icon height

    // Calculate time width
    int hLen = strlen(hourBuf);
    int colonLPad = max(1, scale - 2);   // tighter on left
    int colonRPad = scale;                // more space on right
    int dotSize = scale;                  // bigger colon dots
    int timeW = hLen * digitW + (hLen - 1) * scale  // hour digits + spacing
                + colonLPad + dotSize + colonRPad     // colon area (asymmetric: tight left, roomy right)
                + 2 * digitW + scale;                 // minute digits + spacing

    // Shared: draw time digits + colon, returns ending x
    auto drawTimeFn = [&](int startX, int startY) -> int {
        int cx = startX;
        for (int i = 0; i < hLen; i++) {
            if (i > 0) cx += scale;
            drawScaledDigit(fb, hourBuf[i] - '0', cx, startY, fid, scale, timeColor, aa);
            cx += digitW;
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
        for (int i = 0; i < 2; i++) {
            drawScaledDigit(fb, minBuf[i] - '0', cx, startY, fid, scale, timeColor, aa);
            cx += digitW;
            if (i == 0) cx += scale;
        }
        return cx;
    };

    int gap = 3;

    if (_clockLayout == 0) {
        int ampmW = PixelFont::stringWidth(ampmBuf);
        int bigIconW = WeatherIcons::iconWidth(_weather.icon.c_str());
        int bigIconH = WeatherIcons::iconHeight(_weather.icon.c_str());
        int total = timeW + 1 + ampmW + gap + bigIconW + 3 + tempW;
        int xStart = max(0, (Framebuffer::W - total) / 2);
        int ty = max(0, (VISIBLE_H - digitH) / 2);

        int cx = drawTimeFn(xStart, ty);
        int ampmY = ty + digitH - 5;
        PixelFont::drawString(fb, ampmBuf, cx + 1, ampmY, timeColor);
        cx += 1 + ampmW;

        int ix = cx + gap;
        int iy = max(0, (VISIBLE_H - bigIconH) / 2);
        WeatherIcons::draw(fb, _weather.icon.c_str(), ix, iy, iconClr);
        drawTempStr(fb, tempFBuf, ix + bigIconW + 3, max(0, (VISIBLE_H - 7) / 2), tempClr);

    } else if (_clockLayout == 1) {
        int ty = max(0, (VISIBLE_H - digitH) / 2);
        int iy = max(0, (VISIBLE_H - ih7) / 2);
        drawIcon7(fb, _weather.icon.c_str(), 1, iy, iconClr);
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
        int totalTime = timeW + 1 + ampmW;
        int xStart = max(0, (timeArea - totalTime) / 2);
        int ty = max(0, (VISIBLE_H - digitH) / 2);

        int cx = drawTimeFn(xStart, ty);
        int ampmY = ty + digitH - 5;
        PixelFont::drawString(fb, ampmBuf, cx + 1, ampmY, timeColor);

        int tempX = weatherX + (weatherW - tempW) / 2;
        drawTempStr(fb, tempFBuf, tempX, 0, tempClr);
        int iconX = weatherX + (weatherW - iw7) / 2;
        drawIcon7(fb, _weather.icon.c_str(), iconX, 9, iconClr);

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

void ClockWeatherMode::updateParticles() {
    const char* ic = _weather.icon.c_str();
    bool isRain = (strcmp(ic,"rain")==0 || strcmp(ic,"storm")==0);
    bool isSnow = strcmp(ic,"snow")==0;
    bool isSun = strcmp(ic,"sun")==0;
    bool isFog = strcmp(ic,"fog")==0;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!_particles[i].active) {
            // Spawn new particle
            _particles[i].active = true;
            _particles[i].x = random(0, Framebuffer::W);
            _particles[i].y = -1;
            if (isRain) {
                _particles[i].vy = 1.5f + random(0, 100) / 100.0f;
                _particles[i].vx = -0.3f;
            } else if (isSnow) {
                _particles[i].vy = 0.5f + random(0, 50) / 100.0f;
                _particles[i].vx = (random(0, 100) - 50) / 100.0f;
            } else if (isSun) {
                _particles[i].x = random(0, 10);
                _particles[i].y = random(0, VISIBLE_H);
                _particles[i].vx = 0.5f;
                _particles[i].vy = 0.3f;
            } else if (isFog) {
                _particles[i].x = -1;
                _particles[i].y = random(0, VISIBLE_H);
                _particles[i].vx = 0.3f + random(0, 30) / 100.0f;
                _particles[i].vy = 0;
            }
            break; // spawn one per frame
        }

        // Move existing particles
        _particles[i].x += _particles[i].vx;
        _particles[i].y += _particles[i].vy;

        if (_particles[i].y >= VISIBLE_H || _particles[i].y < -2 ||
            _particles[i].x >= Framebuffer::W || _particles[i].x < -2) {
            _particles[i].active = false;
        }
    }
}

void ClockWeatherMode::drawParticles(Framebuffer& fb) {
    const char* ic = _weather.icon.c_str();
    bool isRain = (strcmp(ic,"rain")==0 || strcmp(ic,"storm")==0);
    bool isSnow = strcmp(ic,"snow")==0;
    bool isSun = strcmp(ic,"sun")==0;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!_particles[i].active) continue;
        int px = (int)_particles[i].x;
        int py = (int)_particles[i].y;
        if (px < 0 || px >= Framebuffer::W || py < 0 || py >= VISIBLE_H) continue;

        if (isRain) {
            RGB c = {20, 40, 120};
            fb.putPixel(px, py, c);
            if (py + 1 < VISIBLE_H) fb.putPixel(px, py + 1, {10, 20, 60});
        } else if (isSnow) {
            fb.putPixel(px, py, {60, 60, 80});
        } else if (isSun) {
            fb.putPixel(px, py, {60, 50, 0});
        } else {
            // fog
            fb.putPixel(px, py, {30, 30, 35});
            if (px + 1 < Framebuffer::W) fb.putPixel(px + 1, py, {20, 20, 25});
        }
    }

    // Storm: occasional lightning flash
    if (strcmp(ic,"storm")==0 && random(0, 15) == 0) {
        int lx = random(0, Framebuffer::W - 3);
        RGB flash = {80, 80, 100};
        for (int y = 0; y < VISIBLE_H; y++)
            fb.putPixel(lx + random(0, 3), y, flash);
    }
}

// ---------------------------------------------------------------------------
// Forecast flash
// ---------------------------------------------------------------------------

void ClockWeatherMode::renderForecastFlash() {
    if (_weather.hourlyCount < 24) return;

    static Framebuffer fb;
    fb.clear();
    drawForecastFullscreen(fb);
    sendFrame(fb, 12);

    Serial.println("[Clock] Forecast flash (15s)");
    _forecastUntil = millis() + FORECAST_FLASH_DURATION_MS;
}

void ClockWeatherMode::drawForecastFullscreen(Framebuffer& fb) {
    float tLow = _weather.tempLow;
    float tHigh = _weather.tempHigh;
    float tRange = max(1.0f, tHigh - tLow);

    for (int x = 0; x < Framebuffer::W; x++) {
        float hourF = (float)x / Framebuffer::W * 24.0f;
        int h0 = (int)hourF;
        int h1 = min(h0 + 1, 23);
        float fracH = hourF - h0;
        float tempVal = _weather.hourly[h0] * (1.0f - fracH) + _weather.hourly[h1] * fracH;
        float frac = (tempVal - tLow) / tRange;
        int row = VISIBLE_H - 1 - (int)round(frac * (VISIBLE_H - 1));
        RGB c = tempColor((int)tempVal);
        RGB dim = {(uint8_t)(c.r / 4), (uint8_t)(c.g / 4), (uint8_t)(c.b / 4)};
        for (int fy = row; fy < VISIBLE_H; fy++)
            fb.putPixel(x, fy, (fy == row) ? c : dim);
    }

    struct tm ti;
    if (getLocalTime(&ti)) {
        float pxPerHour = (float)Framebuffer::W / 24.0f;
        int nowX = (int)(ti.tm_hour * pxPerHour + pxPerHour / 2);
        RGB white = {255, 255, 255};
        for (int y = 0; y < VISIBLE_H; y++)
            if (nowX >= 0 && nowX < Framebuffer::W)
                fb.putPixel(nowX, y, white);

        char timeBuf[8];
        int h12 = ti.tm_hour % 12;
        if (h12 == 0) h12 = 12;
        snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", h12, ti.tm_min);
        RGB timeBlue = {50, 130, 255};
        int tw = PixelFont::stringWidth(timeBuf);

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
        PixelFont::drawString(fb, timeBuf, cx, 0, timeBlue);
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
    snprintf(hiBuf, sizeof(hiBuf), "%d", (int)_weather.tempHigh);
    RGB hiC = tempColor((int)_weather.tempHigh);
    int hiW = PixelFont::stringWidth(hiBuf);
    fb.fillRect(Framebuffer::W - hiW - 2, 0, Framebuffer::W - 1, 5, {0, 0, 0});
    PixelFont::drawString(fb, hiBuf, Framebuffer::W - hiW - 1, 0, hiC);

    char loBuf[8];
    snprintf(loBuf, sizeof(loBuf), "%d", (int)_weather.tempLow);
    RGB loC = tempColor((int)_weather.tempLow);
    int loW = PixelFont::stringWidth(loBuf);
    fb.fillRect(Framebuffer::W - loW - 2, VISIBLE_H - 6, Framebuffer::W - 1, VISIBLE_H - 1, {0, 0, 0});
    PixelFont::drawString(fb, loBuf, Framebuffer::W - loW - 1, VISIBLE_H - 5, loC);

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
        int ly = VISIBLE_H - 6;

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
    static const char* keywords[] = {
        "supreme court", "scotus",
        "doj", "department of justice", "attorney general",
        "indictment", "impeach", "martial law",
        "executive order", "constitutional",
        "overturned", "struck down", "upheld",
        "mass shooting", "shooter", "assassination", "assassinated",
        "bombing", "terrorist", "declare war", "invasion",
        "breaking:", "just in:", "urgent:",
        nullptr
    };
    for (int i = 0; keywords[i]; i++)
        if (lower.indexOf(keywords[i]) >= 0) return true;
    return false;
}

static const char* NEWS_QUERIES[] = {
    "supreme+court+OR+scotus+OR+DOJ+OR+indictment",
    "breaking+news+shooting+OR+assassination+OR+bombing",
    nullptr
};

void ClockWeatherMode::fetchNews() {
    _newsCount = 0;

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
                    if (!dup) {
                        _newsQueue[_newsCount++] = title;
                        Serial.printf("[News] MATCH #%d: %s\n", _newsCount, title.c_str());
                    }
                }
                pos = titleEnd;
            }
        }
        http.end();
        if (q + 1 < 3 && NEWS_QUERIES[q + 1]) delay(500);
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
    String url = "https://api.weather.gov/alerts/active?point=" +
        String(WEATHER_LAT, 3) + "," + String(WEATHER_LON, 3) +
        "&status=actual&urgency=Immediate,Expected";
    http.begin(url);
    http.setTimeout(10000);
    http.addHeader("User-Agent", "ESP32-Sign/1.0");
    http.addHeader("Accept", "application/geo+json");
    int code = http.GET();
    Serial.printf("[Alerts] Fetch → %d\n", code);

    _alertCount = 0;

    if (code == 200) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (!err) {
            JsonArray features = doc["features"].as<JsonArray>();
            for (JsonObject f : features) {
                if (_alertCount >= 3) break;
                String event = f["properties"]["event"] | "";
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
    int scrollTime = max((int)(text.length() * 0.35f), 8);

    TextProgramConfig cfg;
    cfg.scroll = "left";
    cfg.speed = 8;
    cfg.fontSize = 14;
    cfg.fontFamily = 0x00;
    cfg.duration = scrollTime;

    TextSegment seg;
    seg.text = text + "          ";
    seg.r = 255; seg.g = 100; seg.b = 0;
    cfg.segments.push_back(seg);

    auto pkts = buildTextProgram("", cfg);
    for (auto& pkt : pkts) { _ble->send(pkt); delay(150); }
    delay(scrollTime * 1000);

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
}

// ---------------------------------------------------------------------------
// Send frame
// ---------------------------------------------------------------------------

void ClockWeatherMode::sendFrame(Framebuffer& fb, int maxColors) {
    auto packets = fb.buildRtDrawPackets(maxColors);
    for (size_t i = 0; i < packets.size(); i++) {
        _ble->send(packets[i]);
        if (i < packets.size() - 1) delay(50);
    }
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
