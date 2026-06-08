#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Cardputer.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include "config.h"

namespace {
WebSocketsClient webSocket;

String inputText;
String statusText = "Booting";
String messageTitle = "Ready";
String messageBody = "Type and press Enter";
size_t messagePage = 0;
bool colaConnected = false;
unsigned long lastReconnectAttempt = 0;

constexpr uint8_t protocolVersion = 1;
constexpr unsigned long reconnectIntervalMs = 5000;
constexpr size_t maxInputLength = 240;
constexpr size_t messageCharsPerLine = 18;
constexpr uint8_t messageLinesPerPage = 2;
constexpr size_t messageCharsPerPage = messageCharsPerLine * messageLinesPerPage;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t colorBg = rgb565(8, 12, 16);
constexpr uint16_t colorPanel = rgb565(19, 29, 38);
constexpr uint16_t colorPanel2 = rgb565(14, 20, 27);
constexpr uint16_t colorText = rgb565(238, 245, 241);
constexpr uint16_t colorMuted = rgb565(132, 150, 158);
constexpr uint16_t colorAccent = rgb565(26, 212, 178);
constexpr uint16_t colorBlue = rgb565(74, 145, 255);
constexpr uint16_t colorGreen = rgb565(75, 220, 135);
constexpr uint16_t colorAmber = rgb565(255, 184, 77);
constexpr uint16_t colorRed = rgb565(255, 91, 106);

uint16_t messageAccent = colorAccent;

String normalizeText(String text) {
  text.replace("\r", " ");
  text.replace("\n", " ");

  while (text.indexOf("  ") >= 0) {
    text.replace("  ", " ");
  }

  text.trim();

  return text;
}

String compactText(String text, size_t maxChars) {
  text = normalizeText(text);

  if (text.length() <= maxChars) {
    return text;
  }

  if (maxChars <= 3) {
    return text.substring(0, maxChars);
  }

  return text.substring(0, maxChars - 3) + "...";
}

String tailText(const String& text, size_t maxChars) {
  if (text.length() <= maxChars) {
    return text;
  }

  if (maxChars <= 3) {
    return text.substring(text.length() - maxChars);
  }

  return "..." + text.substring(text.length() - (maxChars - 3));
}

size_t pageCountFor(const String& text) {
  if (text.length() == 0) {
    return 1;
  }

  return (text.length() + messageCharsPerPage - 1) / messageCharsPerPage;
}

size_t messagePageCount() {
  return pageCountFor(normalizeText(messageBody));
}

void renderApp();

String currentMessagePageText() {
  String normalized = normalizeText(messageBody);
  size_t pages = pageCountFor(normalized);

  if (messagePage >= pages) {
    messagePage = pages - 1;
  }

  size_t start = messagePage * messageCharsPerPage;
  size_t end = min(start + messageCharsPerPage, static_cast<size_t>(normalized.length()));
  return normalized.substring(start, end);
}

void nextMessagePage() {
  size_t pages = messagePageCount();

  if (pages <= 1) {
    return;
  }

  messagePage = (messagePage + 1) % pages;
  renderApp();
}

void previousMessagePage() {
  size_t pages = messagePageCount();

  if (pages <= 1) {
    return;
  }

  messagePage = messagePage == 0 ? pages - 1 : messagePage - 1;
  renderApp();
}

void drawLargeLines(String text, int x, int y, size_t charsPerLine, uint8_t maxLines) {
  text = normalizeText(text);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(colorText, colorPanel);

  for (uint8_t line = 0; line < maxLines; ++line) {
    if (text.length() == 0) {
      return;
    }

    size_t take = min(charsPerLine, static_cast<size_t>(text.length()));
    String current = text.substring(0, take);
    text = text.substring(take);

    M5Cardputer.Display.setCursor(x, y + line * 19);
    M5Cardputer.Display.print(current);
  }
}

void drawHeader() {
  M5Cardputer.Display.fillRoundRect(5, 4, 230, 24, 6, colorPanel);
  M5Cardputer.Display.fillCircle(18, 16, 5, colorAccent);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(colorText, colorPanel);
  M5Cardputer.Display.setCursor(30, 9);
  M5Cardputer.Display.print("Cola M5");

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const char* label = colaConnected ? "ONLINE" : (wifiConnected ? "WAIT" : "WIFI");
  uint16_t statusColor = colaConnected ? colorGreen : (wifiConnected ? colorAmber : colorRed);

  M5Cardputer.Display.fillRoundRect(156, 8, 72, 16, 5, statusColor);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(colorBg, statusColor);
  M5Cardputer.Display.setCursor(174, 13);
  M5Cardputer.Display.print(label);
}

void drawMessageCard() {
  String body = currentMessagePageText();
  size_t pages = messagePageCount();

  M5Cardputer.Display.fillRoundRect(5, 32, 230, 60, 7, colorPanel);
  M5Cardputer.Display.fillRoundRect(5, 32, 6, 60, 3, messageAccent);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(messageAccent, colorPanel);
  M5Cardputer.Display.setCursor(16, 38);
  M5Cardputer.Display.print(compactText(messageTitle, pages > 1 ? 18 : 26));

  if (pages > 1) {
    M5Cardputer.Display.fillRoundRect(196, 36, 31, 13, 4, colorPanel2);
    M5Cardputer.Display.setTextColor(colorMuted, colorPanel2);
    M5Cardputer.Display.setCursor(202, 40);
    M5Cardputer.Display.print(messagePage + 1);
    M5Cardputer.Display.print("/");
    M5Cardputer.Display.print(pages);
  }

  drawLargeLines(body, 16, 53, messageCharsPerLine, messageLinesPerPage);
}

void drawInputBox() {
  M5Cardputer.Display.fillRoundRect(5, 99, 230, 31, 7, colorPanel2);
  M5Cardputer.Display.drawRoundRect(5, 99, 230, 31, 7, colorAccent);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(colorMuted, colorPanel2);
  M5Cardputer.Display.setCursor(14, 104);
  M5Cardputer.Display.print("INPUT");
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(colorText, colorPanel2);
  M5Cardputer.Display.setCursor(14, 114);
  M5Cardputer.Display.print("> ");
  M5Cardputer.Display.print(tailText(inputText, 16));
}

void renderApp() {
  M5Cardputer.Display.fillScreen(colorBg);
  drawHeader();
  drawMessageCard();
  drawInputBox();
}

void setMessage(const char* title, const String& body, uint16_t accent) {
  messageTitle = title;
  messageBody = body;
  messageAccent = accent;
  messagePage = 0;
  renderApp();
}

void drawStatus(const char* line1, const char* line2 = nullptr) {
  String body = line2 == nullptr ? "" : line2;

  if (body.length() == 0) {
    body = statusText;
  }

  setMessage(line1, body, colaConnected ? colorGreen : colorAmber);
}

void drawPrompt() {
  renderApp();
}

void sendJson(JsonDocument& doc) {
  String payload;
  serializeJson(doc, payload);
  webSocket.sendTXT(payload);
}

void sendHello() {
  JsonDocument doc;
  doc["type"] = "hello";
  doc["protocolVersion"] = protocolVersion;
  doc["deviceId"] = DEVICE_ID;
  doc["deviceModel"] = DEVICE_MODEL;
  doc["firmware"] = FIRMWARE_VERSION;
  sendJson(doc);
}

void sendMessage(const String& text) {
  JsonDocument doc;
  doc["type"] = "message";
  doc["protocolVersion"] = protocolVersion;
  doc["deviceId"] = DEVICE_ID;
  doc["deviceModel"] = DEVICE_MODEL;
  doc["text"] = text;
  sendJson(doc);
}

void showIncomingText(const char* prefix, const char* text) {
  uint16_t accent = colorBlue;

  if (strcmp(prefix, "Cola") == 0) {
    accent = colorAccent;
  } else if (strcmp(prefix, "Sent") == 0) {
    accent = colorBlue;
  } else if (strstr(prefix, "error") != nullptr || strstr(prefix, "Invalid") != nullptr) {
    accent = colorRed;
  }

  setMessage(prefix, text, accent);
}

void handlePluginPayload(const char* payload, size_t length) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) {
    showIncomingText("Invalid reply", "Could not parse plugin JSON");
    return;
  }

  const char* type = doc["type"] | "";

  if (strcmp(type, "reply") == 0 || strcmp(type, "message") == 0) {
    const char* text = doc["text"] | "";
    const char* sender = doc["sender"]["name"] | nullptr;

    if (sender == nullptr || sender[0] == '\0') {
      sender = doc["sender"] | "Cola";
    }

    showIncomingText(sender, text);
    return;
  }

  if (strcmp(type, "status") == 0) {
    statusText = doc["message"] | "Connected";
    colaConnected = doc["connected"] | true;
    drawStatus("cola-m5", statusText.c_str());
    delay(800);
    drawPrompt();
    return;
  }

  if (strcmp(type, "error") == 0) {
    const char* message = doc["message"] | "Unknown error";
    showIncomingText("Plugin error", message);
    return;
  }
}

void onWebSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      colaConnected = true;
      drawStatus("Connected to plugin", "Registering device...");
      sendHello();
      break;
    case WStype_DISCONNECTED:
      colaConnected = false;
      drawStatus("Cola disconnected", "Reconnecting...");
      break;
    case WStype_TEXT:
      handlePluginPayload(reinterpret_cast<const char*>(payload), length);
      break;
    default:
      break;
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  drawStatus("Connecting Wi-Fi", WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    M5Cardputer.Display.print(".");
  }

  drawStatus("Wi-Fi connected", WiFi.localIP().toString().c_str());
  delay(700);
}

void connectCola() {
  webSocket.begin(COLA_HOST, COLA_PORT, "/");
  webSocket.onEvent(onWebSocketEvent);
  webSocket.setReconnectInterval(reconnectIntervalMs);
  webSocket.enableHeartbeat(15000, 3000, 2);
}

void submitInput() {
  inputText.trim();

  if (inputText.length() == 0) {
    drawPrompt();
    return;
  }

  if (!colaConnected) {
    drawStatus("Cola offline", "Message not sent");
    delay(900);
    drawPrompt();
    return;
  }

  sendMessage(inputText);
  showIncomingText("Sent", inputText.c_str());
  inputText = "";
}

void handleKeyboard() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

  if (status.enter) {
    if (inputText.length() == 0 && messagePageCount() > 1) {
      nextMessagePage();
      return;
    }

    submitInput();
    return;
  }

  if (status.del) {
    if (inputText.length() > 0) {
      inputText.remove(inputText.length() - 1);
      drawPrompt();
      return;
    }

    if (messagePageCount() > 1) {
      previousMessagePage();
    }

    return;
  }

  for (char key : status.word) {
    if (inputText.length() >= maxInputLength) {
      break;
    }

    inputText += key;
  }

  if (status.space && inputText.length() < maxInputLength) {
    inputText += ' ';
  }

  drawPrompt();
}
}  // namespace

void setup() {
  auto config = M5.config();
  M5Cardputer.begin(config, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextScroll(false);
  Serial.begin(115200);

  renderApp();
  connectWiFi();
  connectCola();
  drawPrompt();
}

void loop() {
  M5Cardputer.update();
  webSocket.loop();

  if (WiFi.status() != WL_CONNECTED) {
    colaConnected = false;
    unsigned long now = millis();

    if (now - lastReconnectAttempt > reconnectIntervalMs) {
      lastReconnectAttempt = now;
      WiFi.reconnect();
    }
  }

  handleKeyboard();
}
