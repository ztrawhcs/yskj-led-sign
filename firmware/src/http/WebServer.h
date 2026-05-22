#pragma once
#include <ESPAsyncWebServer.h>
#include "../config.h"
#include "../ble/SignBLE.h"
#include "../modes/ModeManager.h"

class SignWebServer {
public:
    void begin(SignBLE* ble, ModeManager* modes);

private:
    AsyncWebServer _server{HTTP_PORT};
    SignBLE* _ble = nullptr;
    ModeManager* _modes = nullptr;
    uint32_t _requestCount = 0;

    int _brightness = 10;

    void setupRoutes();
};

extern SignWebServer webServer;
