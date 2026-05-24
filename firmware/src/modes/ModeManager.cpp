#include "ModeManager.h"
#include "ClockWeatherMode.h"

ModeManager modeManager;

static void sendPackets(SignBLE* ble, const std::vector<std::vector<uint8_t>>& pkts) {
    for (auto& pkt : pkts) {
        ble->send(pkt);
        delay(150);
    }
}

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

    unsigned long now = millis();

    if (_textActive) {
        // Check deadline
        if (_textDeadline > 0 && now > _textDeadline) {
            Serial.println("[Mode] Text duration expired");
            cancelText();
            return;
        }

        if (_textIntermittent) {
            // Intermittent: clock runs, text scrolls once every 2 min
            if (_textScrolling) {
                if (now >= _textScrollDoneAt) {
                    // Scroll finished — resume clock
                    _textScrolling = false;
                    _mode = CLOCK_WEATHER;
                    clockMode.forceUpdate();
                    Serial.println("[Mode] Intermittent scroll done, back to clock");
                }
            } else {
                // Waiting for next scroll
                if (now - _textLastSend >= TEXT_INTERVAL_MS) {
                    sendTextProgram();
                    _textScrolling = true;
                    _textScrollDoneAt = now + estimateScrollMs();
                    _mode = TEXT;
                    Serial.printf("[Mode] Intermittent scroll, ~%dms\n", estimateScrollMs());
                }
            }
        } else {
            // Continuous: re-send every 60s to keep it alive
            if (now - _textLastSend >= 60000) {
                sendTextProgram();
            }
        }
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

void ModeManager::startRepeatingText(const TextProgramConfig& cfg, int totalMinutes, bool intermittent) {
    _textCfg = cfg;
    _textCfg.duration = intermittent ? 1 : 0; // once for intermittent, infinite for continuous
    _textPackets = buildTextProgram("", _textCfg);
    _textActive = true;
    _textIntermittent = intermittent;
    _textScrolling = false;
    _textDeadline = (totalMinutes > 0) ? millis() + (unsigned long)totalMinutes * 60000UL : 0;

    if (intermittent) {
        // Send first scroll immediately, then clock resumes after it finishes
        sendTextProgram();
        _textScrolling = true;
        _textScrollDoneAt = millis() + estimateScrollMs();
        _mode = TEXT;
        _modeTimeout = 0;
        Serial.printf("[Mode] Intermittent text started (%s), scroll ~%dms\n",
                      totalMinutes > 0 ? (String(totalMinutes) + " min").c_str() : "indefinite",
                      estimateScrollMs());
    } else {
        // Continuous — just keep text on screen
        sendTextProgram();
        _mode = TEXT;
        _modeTimeout = 0;
        Serial.printf("[Mode] Continuous text started (%s)\n",
                      totalMinutes > 0 ? (String(totalMinutes) + " min").c_str() : "indefinite");
    }
}

void ModeManager::cancelText() {
    _textActive = false;
    _textIntermittent = false;
    _textScrolling = false;
    _textDeadline = 0;
    _textPackets.clear();
    setMode(CLOCK_WEATHER);
}

int ModeManager::textMinutesRemaining() const {
    if (!_textActive || _textDeadline == 0) return -1;
    unsigned long now = millis();
    if (now >= _textDeadline) return 0;
    return (_textDeadline - now) / 60000 + 1;
}

void ModeManager::sendTextProgram() {
    sendPackets(_ble, _textPackets);
    _textLastSend = millis();
    Serial.println("[Mode] Text program sent");
}

int ModeManager::estimateScrollMs() const {
    // Estimate how long the text takes to scroll across the sign
    // Sign is 96px wide. Font 16 is ~8px per char. Speed 1-20 (higher = slower on sign).
    // Empirical: at speed 10, ~450ms per character
    int textLen = 0;
    for (auto& seg : _textCfg.segments)
        textLen += seg.text.length();
    int speedFactor = max(1, (int)_textCfg.speed);
    // Base: 45ms per char per speed unit, plus sign width crossing time
    int ms = textLen * 45 * speedFactor + 3000;
    return max(5000, ms);
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
