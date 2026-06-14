#include "cola_m5/wifi_connect.h"

#include <WiFi.h>

namespace cola_m5 {

bool connectWiFi(
  const WiFiCredentials& credentials,
  const WiFiConnectCallbacks& callbacks,
  unsigned long timeoutMs
) {
  if (credentials.ssid.length() == 0) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  if (callbacks.onStatus) {
    callbacks.onStatus("Connecting Wi-Fi", credentials.ssid);
  }

  WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());

  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
    if (callbacks.onUpdate) {
      callbacks.onUpdate();
    }

    delay(300);

    if (callbacks.onProgress) {
      callbacks.onProgress();
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    return false;
  }

  return true;
}

void reconnectWiFiIfNeeded(unsigned long& lastReconnectAttempt, unsigned long reconnectIntervalMs) {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  unsigned long now = millis();

  if (now - lastReconnectAttempt > reconnectIntervalMs) {
    lastReconnectAttempt = now;
    WiFi.reconnect();
  }
}

}  // namespace cola_m5
