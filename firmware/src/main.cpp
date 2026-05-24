#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <time.h>

#include "config.h"
#include "ble/SignBLE.h"
#include "http/WebServer.h"
#include "modes/ModeManager.h"

static void connectWiFi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
        // Use Google DNS as fallback for reliable resolution (e.g. api.weather.gov)
        IPAddress dns1(8, 8, 8, 8);
        IPAddress dns2(8, 8, 4, 4);
        WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);
    } else {
        Serial.println("\n[WiFi] Connection failed! Will retry...");
    }
}

static void setupNTP() {
    configTzTime(TZ_POSIX, NTP_SERVER);
    Serial.println("[NTP] Time sync configured");
}

static void setupMDNS() {
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", HTTP_PORT);
        Serial.printf("[mDNS] Advertising as %s.local\n", MDNS_HOSTNAME);
    }
}

static void setupOTA() {
    ArduinoOTA.setHostname(MDNS_HOSTNAME);
    ArduinoOTA.onStart([]() {
        Serial.println("[OTA] Update starting...");
        modeManager.setMode(ModeManager::OFF);
    });
    ArduinoOTA.onEnd([]() { Serial.println("[OTA] Done!"); });
    ArduinoOTA.onError([](ota_error_t err) {
        Serial.printf("[OTA] Error: %u\n", err);
    });
    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");
}

static const int LED_PIN = 21;

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    delay(1000);
    Serial.println("\n=== LED Sign Controller ===");
    if (psramFound()) {
        Serial.printf("[PSRAM] Found: %u bytes total, %u free\n",
            ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        Serial.println("[PSRAM] Not found");
    }

    connectWiFi();
    setupNTP();
    setupMDNS();
    setupOTA();

    delay(2000);
    Serial.println("[BLE] Starting BLE after WiFi settle...");
    signBLE.begin();
    signBLE.onNotify([](const uint8_t* data, size_t len) {
        Serial.printf("[BLE-NOTIFY] %d bytes:", len);
        for (size_t i = 0; i < len; i++) Serial.printf(" %02X", data[i]);
        Serial.println();
    });
    modeManager.begin(&signBLE);
    webServer.begin(&signBLE, &modeManager);

    // Auto-start clock mode
    modeManager.setMode(ModeManager::CLOCK_WEATHER);
    Serial.println("[Setup] Complete! Clock mode active.");
}

void loop() {
    signBLE.loop();

    if (signBLE.isReady()) {
        modeManager.loop();
    }

    // LED status indicator
    static unsigned long lastLedToggle = 0;
    unsigned long now = millis();
    switch (signBLE.getState()) {
    case SignBLE::SCANNING:
        if (now - lastLedToggle > 150) { // fast blink
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            lastLedToggle = now;
        }
        break;
    case SignBLE::CONNECTING:
    case SignBLE::DISCOVERING:
    case SignBLE::INIT_SEQ:
        if (now - lastLedToggle > 500) { // slow blink
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
            lastLedToggle = now;
        }
        break;
    case SignBLE::READY:
        digitalWrite(LED_PIN, HIGH); // solid on
        break;
    default:
        digitalWrite(LED_PIN, LOW); // off
        break;
    }

    ArduinoOTA.handle();

    // Reconnect WiFi if dropped
    static unsigned long lastWifiCheck = 0;
    if (millis() - lastWifiCheck > 30000) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Reconnecting...");
            WiFi.reconnect();
        }
    }
}
