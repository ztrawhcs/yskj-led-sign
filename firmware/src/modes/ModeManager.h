#pragma once
#include <Arduino.h>
#include "../ble/SignBLE.h"
#include "../protocol/TextProgram.h"
#include <vector>

class ModeManager {
public:
    enum Mode { OFF, CLOCK_WEATHER, NEWS_TICKER, TEXT, MESSAGE };

    void begin(SignBLE* ble);
    void loop();
    void setMode(Mode mode);
    void setModeWithTimeout(Mode mode, int seconds);
    Mode getMode() { return _mode; }
    const char* modeStr();

    // Text display modes:
    //   continuous: text replaces clock entirely
    //   intermittent: text scrolls once every 2 min, clock shows in between
    // totalMinutes == 0 means indefinite (until cancelled)
    void startRepeatingText(const TextProgramConfig& cfg, int totalMinutes, bool intermittent = false);
    void cancelText();
    bool isTextActive() const { return _textActive; }
    bool isTextIntermittent() const { return _textIntermittent; }
    int textMinutesRemaining() const;

private:
    Mode _mode = OFF;
    SignBLE* _ble = nullptr;
    unsigned long _modeTimeout = 0;

    // Text state
    TextProgramConfig _textCfg;
    std::vector<std::vector<uint8_t>> _textPackets;
    unsigned long _textDeadline = 0;    // 0 = indefinite
    unsigned long _textLastSend = 0;
    unsigned long _textScrollDoneAt = 0; // when current scroll finishes
    bool _textActive = false;
    bool _textIntermittent = false;
    bool _textScrolling = false;        // currently mid-scroll in intermittent mode
    static const unsigned long TEXT_INTERVAL_MS = 120000; // 2 minutes

    void sendTextProgram();
    int estimateScrollMs() const;
};

extern ModeManager modeManager;
