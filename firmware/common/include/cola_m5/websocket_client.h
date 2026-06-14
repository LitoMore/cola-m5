#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <functional>

#include "protocol.h"
#include "types.h"

namespace cola_m5 {

struct WebSocketClientCallbacks {
  std::function<void()> onConnected;
  std::function<void()> onDisconnected;
  std::function<void(const PluginEvent& event)> onEvent;
};

class WebSocketClient {
 public:
  void begin(
    const ConnectionSettings& settings,
    const DeviceIdentity& identity,
    const WebSocketClientCallbacks& callbacks
  );

  void loop();
  void disconnect();
  void sendMessage(const String& text);

  bool connected() const;

 private:
  void sendHello();
  void sendJson(JsonDocument& doc);
  void handleEvent(WStype_t type, uint8_t* payload, size_t length);

  WebSocketsClient socket_;
  ConnectionSettings settings_;
  DeviceIdentity identity_;
  WebSocketClientCallbacks callbacks_;
  bool connected_ = false;
};

}  // namespace cola_m5
