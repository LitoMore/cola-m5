#pragma once

#include <Arduino.h>

namespace cola_m5 {

constexpr uint8_t kProtocolVersion = 1;
constexpr unsigned long kReconnectIntervalMs = 5000;
constexpr unsigned long kWifiConnectTimeoutMs = 20000;
constexpr uint16_t kDefaultColaPort = 8787;

struct WiFiCredentials {
  String ssid;
  String password;
};

struct ConnectionSettings {
  WiFiCredentials wifi;
  String colaHost;
  uint16_t colaPort = kDefaultColaPort;
};

struct DeviceIdentity {
  const char* deviceId;
  const char* deviceModel;
  const char* firmware;

  constexpr DeviceIdentity(
    const char* id = "",
    const char* model = "",
    const char* version = ""
  )
    : deviceId(id), deviceModel(model), firmware(version) {}
};

bool hasWiFiCredentials(const WiFiCredentials& credentials);
bool hasColaEndpoint(const ConnectionSettings& settings);

}  // namespace cola_m5
