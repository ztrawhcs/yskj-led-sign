#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "../ble/SignBLE.h"
#include "../gfx/Framebuffer.h"
#include "../config.h"
#include <vector>

struct WeatherData {
    int temp = 70;
    int humidity = 0;
    float uvIndex = 0;
    String icon = "cloud";
    int code = 0;
    bool isDay = true;
    float hourly[24] = {};
    float hourlyUV[24] = {};
    int hourlyWMO[24] = {};
    int hourlyCount = 0;
    float tempHigh = 0;
    float tempLow = 0;
};

class ClockWeatherMode {
public:
    void begin(SignBLE* ble);
    void loop();
    void forceUpdate();
    void showForecast();
    void setClockLayout(int layout);
    int getClockLayout() const { return _clockLayout; }
    void setCustomPositions(int timeX, int timeY, int timeScale,
                            int tempX, int tempY,
                            int iconX, int iconY, int iconSize);
    void setFont(int fontId, bool aa);
    int getFontId() const { return _fontId; }
    bool getFontAA() const { return _fontAA; }
    int customTimeX() const { return _cTimeX; }
    int customTimeY() const { return _cTimeY; }
    int customTimeScale() const { return _cTimeScale; }
    int customTempX() const { return _cTempX; }
    int customTempY() const { return _cTempY; }
    int customIconX() const { return _cIconX; }
    int customIconY() const { return _cIconY; }
    int customIconSize() const { return _cIconSize; }
    int currentTemp() const { return _weather.temp; }
    int currentHumidity() const { return _weather.humidity; }
    float currentUV() const { return _weather.uvIndex; }
    const String& currentIcon() const { return _weather.icon; }
    void setWeatherIcon(const String& icon) { _weather.icon = icon; _forceRedraw = true; _lastWeatherFetch = millis(); }
    void setTimeColor(uint8_t r, uint8_t g, uint8_t b);
    void sendTestFrame(Framebuffer& fb);
    uint8_t timeColorR() const { return _cTimeColor.r; }
    uint8_t timeColorG() const { return _cTimeColor.g; }
    uint8_t timeColorB() const { return _cTimeColor.b; }

    // Timer / Stopwatch
    enum TimerMode { TIMER_NONE = 0, TIMER_COUNTDOWN = 1, TIMER_STOPWATCH = 2 };
    void startCountdown(int totalSeconds);
    void startStopwatch();
    void pauseTimer();
    void resumeTimer();
    void resetTimer();
    TimerMode timerMode() const { return _timerMode; }
    bool timerRunning() const { return _timerRunning; }
    int timerRemainingSec() const;
    int timerElapsedSec() const;

    // News headlines
    void setRssUrl(const String& url);
    String getRssUrl() const { return _rssUrl; }
    void triggerNewsFetch();

    // Calendar (Google Apps Script)
    void setCalendarUrl(const String& url);
    String getCalendarUrl() const { return _calendarUrl; }
    int calendarEventCount() const { return _calEventCount; }
    const String& calendarEvent(int i) const { return _calEvents[i]; }

    // Notifications (from proxy)
    void setProxyUrl(const String& url);
    String getProxyUrl() const { return _proxyUrl; }
    void showNotification(const String& text, uint8_t r = 255, uint8_t g = 200, uint8_t b = 0);
    int notifCount() const { return _notifCount; }
    const String& notifAt(int i) const { return _notifications[i]; }

private:
    SignBLE* _ble = nullptr;
    unsigned long _lastUpdate = 0;
    unsigned long _lastWeatherFetch = 0;
    unsigned long _lastForecastFlash = 0;
    unsigned long _lastNewsFetch = 0;
    unsigned long _lastWatchdog = 0;
    WeatherData _weather;
    bool _forceRedraw = true;
    bool _clearTextOnNext = false;
    unsigned long _forecastUntil = 0;
    int _lastMinuteSent = -1;
    int _clockLayout = 0;  // 0=normal, 1=large-statusbar, 2=large-stacked, 3=custom
    int _fontId = 0;       // 0=standard, 1=rounded, 2=digital, 3=detailed5x7
    bool _fontAA = false;  // anti-aliasing

    // Custom layout positions
    int _cTimeX = 5, _cTimeY = 0, _cTimeScale = 3;
    int _cTempX = 65, _cTempY = 5;
    int _cIconX = 65, _cIconY = 0;
    int _cIconSize = 0; // 0=tiny 5x5, 1=large 13x13
    RGB _cTimeColor = {50, 130, 255};
    RGB _cTempColorOverride = {0, 0, 0}; // {0,0,0} = auto from temp value

    // Weather animation
    unsigned long _lastAnimFrame = 0;
    bool _animActive = false;
    int _animFrame = 0;
    // Cached icon region for partial updates
    int _iconRx0 = 0, _iconRy0 = 0, _iconRx1 = 0, _iconRy1 = 0;
    bool _iconRegionValid = false;
    // Previous frame for diff-based updates
    Framebuffer _prevFrame;
    bool _hasPrevFrame = false;

    // GIF animation mode
    enum DisplayMode { DISPLAY_RT_DRAW = 0, DISPLAY_GIF_PROGRAM = 1 };
    DisplayMode _displayMode = DISPLAY_RT_DRAW;
    int _gifFailCount = 0;
    static const int MAX_GIF_FAILS = 3;

    // Timer / Stopwatch state
    TimerMode _timerMode = TIMER_NONE;
    unsigned long _timerStartMs = 0;
    unsigned long _timerDurationMs = 0;
    unsigned long _timerPausedElapsed = 0;
    bool _timerRunning = false;
    unsigned long _timerLastRender = 0;
    bool _timerFinishedFlash = false;
    int _timerFlashCount = 0;

    // News headlines
    String _newsQueue[5];
    int _newsCount = 0;
    String _rssUrl;
    int _currentNewsIdx = 0;
    unsigned long _lastNewsScroll = 0;

    // Calendar
    String _calendarUrl;
    String _calEvents[5];
    int _calEventCount = 0;
    unsigned long _lastCalFetch = 0;

    // Notifications from proxy
    String _proxyUrl;
    String _notifications[8];
    int _notifCount = 0;
    unsigned long _lastProxyPoll = 0;

    // NWS weather alerts
    String _weatherAlerts[3];
    int _alertCount = 0;
    unsigned long _lastAlertFetch = 0;
    unsigned long _lastAlertShow = 0;
    unsigned long _alertUntil = 0;

    void saveSettings();
    void loadSettings();

    void fetchWeather();
    void fetchNews();
    void fetchCalendar();
    void fetchNotifications();
    void fetchWeatherAlerts();
    void renderClock();
    void renderTimer();
    void renderForecastFlash();
    void scrollHeadline(const String& text);

    // Rendering helpers
    void drawClockFace(Framebuffer& fb);
    void drawForecastFullscreen(Framebuffer& fb);
    void drawTimerFace(Framebuffer& fb);
    bool generateAndUploadGif();
    void buildAnimPalette(RGB* palette, int* numColors);

    static RGB tempColor(int tempF);
    static const char* iconColor(const char* icon, uint8_t& r, uint8_t& g, uint8_t& b);
    static const char* wmoToIcon(int code, bool isDay);

    void sendFrame(Framebuffer& fb, int maxColors = 12);
    void sendRegion(Framebuffer& fb, int x0, int y0, int x1, int y1, int maxColors = 2);
};

extern ClockWeatherMode clockMode;
