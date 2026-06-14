#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "types.h"

namespace cola_m5 {

enum class PluginEventType : uint8_t {
  Reply,
  Status,
  Error,
  Invalid,
  Unknown,
};

struct PluginEvent {
  PluginEventType type = PluginEventType::Unknown;
  String title;
  String text;
  bool connected = false;
};

void writeHello(JsonDocument& doc, const DeviceIdentity& identity);
void writeMessage(JsonDocument& doc, const DeviceIdentity& identity, const String& text);
PluginEvent parsePluginPayload(const char* payload, size_t length);

}  // namespace cola_m5
