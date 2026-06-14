#include "cola_m5/connection_store.h"

#include <Preferences.h>

namespace cola_m5 {
namespace {

constexpr char kPrefsNamespace[] = "cola-m5";
constexpr char kWifiSsidKey[] = "wifiSsid";
constexpr char kWifiPasswordKey[] = "wifiPass";
constexpr char kColaHostKey[] = "colaHost";
constexpr char kColaPortKey[] = "colaPort";

uint16_t normalizePort(uint32_t port, uint16_t fallback) {
  if (port == 0 || port > 65535) {
    return fallback;
  }

  return static_cast<uint16_t>(port);
}

}  // namespace

ConnectionSettings loadConnectionSettings(const char* defaultColaHost, uint16_t defaultColaPort) {
  ConnectionSettings settings;
  settings.colaHost = defaultColaHost == nullptr ? "" : defaultColaHost;
  settings.colaPort = defaultColaPort == 0 ? kDefaultColaPort : defaultColaPort;

  Preferences preferences;

  if (!preferences.begin(kPrefsNamespace, true)) {
    return settings;
  }

  settings.wifi.ssid = preferences.getString(kWifiSsidKey, "");
  settings.wifi.password = preferences.getString(kWifiPasswordKey, "");

  String savedHost = preferences.getString(kColaHostKey, "");
  if (savedHost.length() > 0) {
    settings.colaHost = savedHost;
  }

  settings.colaPort = normalizePort(preferences.getUInt(kColaPortKey, 0), settings.colaPort);
  preferences.end();

  return settings;
}

WiFiCredentials loadWiFiCredentials() {
  return loadConnectionSettings("", kDefaultColaPort).wifi;
}

void saveWiFiCredentials(const WiFiCredentials& credentials) {
  Preferences preferences;

  if (!preferences.begin(kPrefsNamespace, false)) {
    return;
  }

  preferences.putString(kWifiSsidKey, credentials.ssid);
  preferences.putString(kWifiPasswordKey, credentials.password);
  preferences.end();
}

void saveConnectionSettings(const ConnectionSettings& settings) {
  Preferences preferences;

  if (!preferences.begin(kPrefsNamespace, false)) {
    return;
  }

  preferences.putString(kWifiSsidKey, settings.wifi.ssid);
  preferences.putString(kWifiPasswordKey, settings.wifi.password);
  preferences.putString(kColaHostKey, settings.colaHost);
  preferences.putUInt(kColaPortKey, settings.colaPort);
  preferences.end();
}

void clearConnectionSettings() {
  Preferences preferences;

  if (!preferences.begin(kPrefsNamespace, false)) {
    return;
  }

  preferences.clear();
  preferences.end();
}

}  // namespace cola_m5
