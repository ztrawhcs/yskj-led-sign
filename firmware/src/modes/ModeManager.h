#pragma once
#include <Arduino.h>
#include "../ble/SignBLE.h"

class ModeManager {
public:
    enum Mode { OFF, CLOCK_WEATHER, NEWS_TICKER, TEXT, MESSAGE };

    void begin(SignBLE* ble);
    void loop();
    void setMode(Mode mode);
    void setModeWithTimeout(Mode mode, int seconds);
    Mode getMode() { return _mode; }
    const char* modeStr();

private:
    Mode _mode = OFF;
    SignBLE* _ble = nullptr;
    unsigned long _modeTimeout = 0;
};

extern ModeManager modeManager;
