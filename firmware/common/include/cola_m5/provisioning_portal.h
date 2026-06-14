#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <functional>

#include "types.h"

namespace cola_m5 {

struct ProvisioningPortalConfig {
  const char* apSsid = "Cola-M5-Setup";
  const char* apPassword = "";
  const char* deviceName = "Cola M5";
  const char* defaultColaHost = "";
  uint16_t defaultColaPort = kDefaultColaPort;
  unsigned long timeoutMs = 0;
};

struct ProvisioningPortalCallbacks {
  std::function<void(const String& apSsid, const IPAddress& ip)> onStarted;
  std::function<void(const char* title, const String& body)> onStatus;
  std::function<void()> onUpdate;
};

bool runProvisioningPortal(
  const ProvisioningPortalConfig& config,
  const ProvisioningPortalCallbacks& callbacks = {}
);

String makeProvisioningApSsid(const char* prefix);

}  // namespace cola_m5
