#include "cola_m5/protocol.h"

namespace cola_m5 {

void writeHello(JsonDocument& doc, const DeviceIdentity& identity) {
  doc["type"] = "hello";
  doc["protocolVersion"] = kProtocolVersion;
  doc["deviceId"] = identity.deviceId;
  doc["deviceModel"] = identity.deviceModel;
  doc["firmware"] = identity.firmware;
}

void writeMessage(JsonDocument& doc, const DeviceIdentity& identity, const String& text) {
  doc["type"] = "message";
  doc["protocolVersion"] = kProtocolVersion;
  doc["deviceId"] = identity.deviceId;
  doc["deviceModel"] = identity.deviceModel;
  doc["text"] = text;
}

PluginEvent parsePluginPayload(const char* payload, size_t length) {
  PluginEvent event;
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    event.type = PluginEventType::Invalid;
    event.title = "Invalid reply";
    event.text = "Could not parse plugin JSON";
    return event;
  }

  const char* type = doc["type"] | "";

  if (strcmp(type, "reply") == 0 || strcmp(type, "message") == 0) {
    const char* text = doc["text"] | "";
    const char* sender = doc["sender"]["name"] | nullptr;

    if (sender == nullptr || sender[0] == '\0') {
      sender = doc["sender"] | "Cola";
    }

    event.type = PluginEventType::Reply;
    event.title = sender;
    event.text = text;
    event.connected = true;
    return event;
  }

  if (strcmp(type, "status") == 0) {
    event.type = PluginEventType::Status;
    event.title = "cola-m5";
    event.text = doc["message"] | "Connected";
    event.connected = doc["connected"] | true;
    return event;
  }

  if (strcmp(type, "error") == 0) {
    event.type = PluginEventType::Error;
    event.title = "Plugin error";
    event.text = doc["message"] | "Unknown error";
    event.connected = true;
    return event;
  }

  event.type = PluginEventType::Unknown;
  event.title = "Unknown event";
  event.text = type;
  return event;
}

}  // namespace cola_m5
