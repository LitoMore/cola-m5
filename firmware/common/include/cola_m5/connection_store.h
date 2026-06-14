#pragma once

#include <Arduino.h>

#include "types.h"

namespace cola_m5 {

ConnectionSettings loadConnectionSettings(const char* defaultColaHost, uint16_t defaultColaPort);
WiFiCredentials loadWiFiCredentials();

void saveWiFiCredentials(const WiFiCredentials& credentials);
void saveConnectionSettings(const ConnectionSettings& settings);
void clearConnectionSettings();

}  // namespace cola_m5
