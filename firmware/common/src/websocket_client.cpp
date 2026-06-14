#include "cola_m5/websocket_client.h"

namespace cola_m5 {

void WebSocketClient::begin(
  const ConnectionSettings& settings,
  const DeviceIdentity& identity,
  const WebSocketClientCallbacks& callbacks
) {
  settings_ = settings;
  identity_ = identity;
  callbacks_ = callbacks;
  connected_ = false;

  socket_.begin(settings_.colaHost.c_str(), settings_.colaPort, "/");
  socket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
    handleEvent(type, payload, length);
  });
  socket_.setReconnectInterval(kReconnectIntervalMs);
  socket_.enableHeartbeat(15000, 3000, 2);
}

void WebSocketClient::loop() {
  socket_.loop();
}

void WebSocketClient::disconnect() {
  socket_.disconnect();
  connected_ = false;
}

bool WebSocketClient::connected() const {
  return connected_;
}

void WebSocketClient::sendMessage(const String& text) {
  JsonDocument doc;
  writeMessage(doc, identity_, text);
  sendJson(doc);
}

void WebSocketClient::sendHello() {
  JsonDocument doc;
  writeHello(doc, identity_);
  sendJson(doc);
}

void WebSocketClient::sendJson(JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  socket_.sendTXT(payload);
}

void WebSocketClient::handleEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected_ = true;
      if (callbacks_.onConnected) {
        callbacks_.onConnected();
      }
      sendHello();
      break;
    case WStype_DISCONNECTED:
      connected_ = false;
      if (callbacks_.onDisconnected) {
        callbacks_.onDisconnected();
      }
      break;
    case WStype_TEXT:
      if (callbacks_.onEvent) {
        callbacks_.onEvent(parsePluginPayload(reinterpret_cast<const char*>(payload), length));
      }
      break;
    default:
      break;
  }
}

}  // namespace cola_m5
