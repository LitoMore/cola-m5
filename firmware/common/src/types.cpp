#include "cola_m5/types.h"

namespace cola_m5 {

bool hasWiFiCredentials(const WiFiCredentials& credentials) {
  return credentials.ssid.length() > 0;
}

bool hasColaEndpoint(const ConnectionSettings& settings) {
  return settings.colaHost.length() > 0 && settings.colaPort > 0;
}

}  // namespace cola_m5
