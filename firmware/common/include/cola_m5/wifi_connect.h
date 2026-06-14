#pragma once

#include <Arduino.h>
#include <functional>

#include "types.h"

namespace cola_m5 {

struct WiFiConnectCallbacks {
  std::function<void(const char* title, const String& body)> onStatus;
  std::function<void()> onUpdate;
  std::function<void()> onProgress;
};

bool connectWiFi(
  const WiFiCredentials& credentials,
  const WiFiConnectCallbacks& callbacks = {},
  unsigned long timeoutMs = kWifiConnectTimeoutMs
);

void reconnectWiFiIfNeeded(
  unsigned long& lastReconnectAttempt,
  unsigned long reconnectIntervalMs = kReconnectIntervalMs
);

}  // namespace cola_m5
