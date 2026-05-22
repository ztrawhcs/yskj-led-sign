#include "ModeManager.h"
#include "ClockWeatherMode.h"

ModeManager modeManager;

void ModeManager::begin(SignBLE* ble) {
    _ble = ble;
    clockMode.begin(ble);
}

void ModeManager::loop() {
    if (_modeTimeout > 0 && millis() > _modeTimeout) {
        Serial.println("[Mode] Timeout, returning to clock");
        _modeTimeout = 0;
        setMode(CLOCK_WEATHER);
        return;
    }

    switch (_mode) {
    case CLOCK_WEATHER:
        clockMode.loop();
        break;
    case NEWS_TICKER:
        break;
    case OFF:
    case TEXT:
    case MESSAGE:
    default:
        break;
    }
}

void ModeManager::setMode(Mode mode) {
    if (mode == _mode) return;
    _mode = mode;
    _modeTimeout = 0;
    Serial.printf("[Mode] Switched to %s\n", modeStr());
    if (mode == CLOCK_WEATHER) {
        clockMode.forceUpdate();
    }
}

void ModeManager::setModeWithTimeout(Mode mode, int seconds) {
    _mode = mode;
    _modeTimeout = millis() + (unsigned long)seconds * 1000UL;
    Serial.printf("[Mode] Switched to %s (timeout %ds)\n", modeStr(), seconds);
}

const char* ModeManager::modeStr() {
    switch (_mode) {
    case OFF: return "off";
    case CLOCK_WEATHER: return "clock";
    case NEWS_TICKER: return "news";
    case TEXT: return "text";
    case MESSAGE: return "message";
    }
    return "unknown";
}
