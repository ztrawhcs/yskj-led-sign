#include "SignBLE.h"
#include "../config.h"
#include "../protocol/Commands.h"

SignBLE signBLE;

static const NimBLEUUID SVC_UUID("0000FFF0-0000-1000-8000-00805F9B34FB");
static const NimBLEUUID WRITE_UUID("0000FFF2-0000-1000-8000-00805F9B34FB");
static const NimBLEUUID NOTIFY_UUID("0000FFF1-0000-1000-8000-00805F9B34FB");

static SignBLE* _instance = nullptr;

class ScanCB : public NimBLEScanCallbacks {
    void onDiscovered(const NimBLEAdvertisedDevice* dev) override {
        if (_instance) _instance->onScanResult(dev);
    }
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (_instance) _instance->onScanResult(dev);
    }
    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        Serial.printf("[BLE] Scan ended, reason=%d, %d devices seen\n", reason, results.getCount());
    }
};
static ScanCB _scanCB;

void SignBLE::begin() {
    _instance = this;
    bool ok = NimBLEDevice::init("ESP32-SignCtrl");
    Serial.printf("[BLE] NimBLE init: %s\n", ok ? "OK" : "FAILED");
    if (!ok) {
        _state = ERR;
        _lastReconnect = millis();
        return;
    }
    NimBLEDevice::setMTU(512);
    startScan();
}

void SignBLE::loop() {
    unsigned long now = millis();

    switch (_state) {
    case CONNECTING:
        if (_connectTime > 0 && now - _connectTime > 2000) {
            discoverServices();
        }
        break;

    case INIT_SEQ:
        runInitSequence();
        break;

    case ERR:
        if (now - _lastReconnect > 3000) {
            Serial.println("[BLE] Reconnecting...");
            startScan();
        }
        break;

    case SCANNING:
        if (_foundDevice) {
            _foundDevice = false;
            NimBLEDevice::getScan()->stop();
            connectToDevice();
        } else if (_scanStartTime > 0 && now - _scanStartTime > 16000) {
            NimBLEDevice::getScan()->stop();
            Serial.println("[BLE] No FFF0 sign found. Retrying in 3s...");
            _scanStartTime = 0;
            _lastReconnect = millis();
            _state = ERR;
        }
        break;

    case READY:
        if (!_client || !_client->isConnected()) {
            Serial.println("[BLE] Connection lost, reconnecting...");
            _writeChr = nullptr;
            _notifyChr = nullptr;
            _lastReconnect = millis();
            _state = ERR;
        }
        break;

    default:
        break;
    }
}

const char* SignBLE::stateStr() {
    switch (_state) {
    case IDLE: return "Idle";
    case SCANNING: return "Scanning";
    case CONNECTING: return "Connecting";
    case DISCOVERING: return "Discovering";
    case INIT_SEQ: return "Initializing";
    case READY: return "Ready";
    case ERR: return "Error";
    }
    return "Unknown";
}

void SignBLE::startScan() {
    _state = SCANNING;
    _foundDevice = false;
    _scanAttempts++;
    if (_scanAttempts > 10) {
        Serial.println("[BLE] Too many failed scans, rebooting...");
        delay(500);
        ESP.restart();
    }

    // After 3 failed scans with no devices, full BLE stack reset
    if (_scanAttempts == 4 || _scanAttempts == 7) {
        Serial.println("[BLE] Resetting BLE stack...");
        NimBLEDevice::deinit(true);
        delay(1000);
        NimBLEDevice::init("ESP32-SignCtrl");
        NimBLEDevice::setMTU(512);
        _client = nullptr;
        _writeChr = nullptr;
        _notifyChr = nullptr;
        delay(500);
    }

    Serial.printf("[BLE] Scanning for sign (attempt %d/10)...\n", _scanAttempts);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&_scanCB, true);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(99);
    bool started = scan->start(15, false);
    Serial.printf("[BLE] Scan start: %s\n", started ? "OK" : "FAILED");
    _scanStartTime = millis();
}

void SignBLE::onScanResult(const NimBLEAdvertisedDevice* dev) {
    if (_foundDevice) return;
    String name = String(dev->getName().c_str());
    String addr = String(dev->getAddress().toString().c_str());
    bool hasTargetSvc = dev->isAdvertisingService(SVC_UUID);

    if (name.length() == 0) {
        std::string n = dev->getPayloadByType(0x09);
        if (n.empty()) n = dev->getPayloadByType(0x08);
        if (!n.empty()) name = String(n.c_str());
    }

    size_t payloadLen = dev->getPayload().size();

    Serial.printf("[BLE] Device: %s addr=%s rssi=%d svc=%s adv=%dB\n",
        name.length() > 0 ? name.c_str() : "(unnamed)", addr.c_str(),
        dev->getRSSI(), hasTargetSvc ? "FFF0" : "no", payloadLen);

    if (hasTargetSvc || name == SIGN_BLE_NAME) {
        Serial.printf("[BLE] >>> Found sign (%s): %s\n",
            hasTargetSvc ? "FFF0" : "name-match",
            name.length() > 0 ? name.c_str() : addr.c_str());
        _targetDevice = *dev;
        _foundDevice = true;
    }
}

void SignBLE::connectToDevice() {
    _state = CONNECTING;
    _mtuRetries = 0;

    if (_client == nullptr) {
        _client = NimBLEDevice::createClient();
    }

    Serial.printf("[BLE] Connecting to %s...\n", _targetDevice.getAddress().toString().c_str());

    if (!_client->connect(&_targetDevice)) {
        Serial.println("[BLE] Connect failed");
        _state = ERR;
        _lastReconnect = millis();
        return;
    }

    Serial.printf("[BLE] Connected, MTU=%d\n", _client->getMTU());
    _scanAttempts = 0;
    _connectTime = millis();
}

void SignBLE::discoverServices() {
    _state = DISCOVERING;
    _connectTime = 0;

    uint16_t mtu = _client->getMTU();
    Serial.printf("[BLE] Post-delay MTU=%d\n", mtu);

    _effectiveMTU = mtu;
    Serial.printf("[BLE] Using MTU=%d (chunk size=%d)\n", mtu, mtu - 3);

    NimBLERemoteService* svc = _client->getService(SVC_UUID);
    if (!svc) {
        Serial.println("[BLE] FFF0 service not found!");
        _state = ERR;
        _lastReconnect = millis();
        return;
    }
    Serial.println("[BLE] Found FFF0 service");

    _writeChr = svc->getCharacteristic(WRITE_UUID);
    if (!_writeChr) {
        Serial.println("[BLE] FFF2 write char not found!");
        _state = ERR;
        _lastReconnect = millis();
        return;
    }
    Serial.println("[BLE] Found FFF2 write char");

    _notifyChr = svc->getCharacteristic(NOTIFY_UUID);
    if (_notifyChr && _notifyChr->canNotify()) {
        _notifyChr->subscribe(true, [](NimBLERemoteCharacteristic* chr,
                                        uint8_t* data, size_t len, bool isNotify) {
            if (_instance) {
                _instance->_lastResponse.assign(data, data + len);
                if (_instance->_notifyCB) {
                    _instance->_notifyCB(data, len);
                }
            }
        });
        Serial.println("[BLE] Subscribed to FFF1 notifications");
    }

    _state = INIT_SEQ;
    _initStep = 0;
    _initStepTime = millis();
}

void SignBLE::runInitSequence() {
    unsigned long now = millis();
    if (now - _initStepTime < 500) return;

    switch (_initStep) {
    case 0: {
        Serial.println("[BLE] Init: param_dev query");
        auto pkt = Commands::initParamDev();
        send(pkt);
        _initStep++;
        _initStepTime = now;
        break;
    }
    case 1: {
        Serial.println("[BLE] Init: config");
        auto pkt = Commands::initConfig();
        send(pkt);
        _initStep++;
        _initStepTime = now;
        break;
    }
    case 2: {
        Serial.println("[BLE] Init: font query");
        auto pkt = Commands::initFontQuery();
        send(pkt);
        _initStep++;
        _initStepTime = now;
        break;
    }
    default:
        if (now - _initStepTime > 500) {
            Serial.println("[BLE] Init complete, READY");
            _state = READY;
        }
        break;
    }
}

bool SignBLE::send(const uint8_t* data, size_t len) {
    if (!_writeChr || !_client || !_client->isConnected()) return false;
    size_t chunkSize = _effectiveMTU - 3;
    if (chunkSize < 20) chunkSize = 20;
    for (size_t offset = 0; offset < len; offset += chunkSize) {
        size_t n = (len - offset < chunkSize) ? len - offset : chunkSize;
        bool ok = _writeChr->writeValue(data + offset, n, false);
        if (!ok) return false;
        if (offset + n < len) delay(20);
    }
    _cmdCount++;
    return true;
}

bool SignBLE::send(const std::vector<uint8_t>& pkt) {
    return send(pkt.data(), pkt.size());
}

void SignBLE::disconnect() {
    if (_client && _client->isConnected()) {
        _client->disconnect();
    }
    _writeChr = nullptr;
    _notifyChr = nullptr;
    _state = IDLE;
}
