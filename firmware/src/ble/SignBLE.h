#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>
#include <functional>

class SignBLE {
public:
    enum State { IDLE, SCANNING, CONNECTING, DISCOVERING, INIT_SEQ, READY, ERR };

    void begin();
    void loop();
    bool isReady() { return _state == READY; }
    bool send(const uint8_t* data, size_t len);
    bool send(const std::vector<uint8_t>& pkt);
    State getState() { return _state; }
    const char* stateStr();
    String deviceInfo() { return _deviceInfo; }
    uint32_t commandCount() { return _cmdCount; }

    using NotifyCB = std::function<void(const uint8_t*, size_t)>;
    void onNotify(NotifyCB cb) { _notifyCB = cb; }

    void onScanResult(const NimBLEAdvertisedDevice* dev);

private:
    State _state = IDLE;
    NimBLEClient* _client = nullptr;
    NimBLERemoteCharacteristic* _writeChr = nullptr;
    NimBLERemoteCharacteristic* _notifyChr = nullptr;
    NimBLEAdvertisedDevice _targetDevice;
    NimBLEAdvertisedDevice _candidateDevice;
    bool _foundDevice = false;
    bool _candidateFound = false;

    uint32_t _cmdCount = 0;
    String _deviceInfo;
    uint16_t _effectiveMTU = 23;
    uint8_t _mtuRetries = 0;
    unsigned long _lastReconnect = 0;
    unsigned long _connectTime = 0;
    unsigned long _scanStartTime = 0;
    uint8_t _scanAttempts = 0;
    uint8_t _initStep = 0;
    unsigned long _initStepTime = 0;
    NotifyCB _notifyCB;
    std::vector<uint8_t> _lastResponse;

    void startScan();
    void connectToDevice();
    void discoverServices();
    void runInitSequence();
    void disconnect();
};

extern SignBLE signBLE;
