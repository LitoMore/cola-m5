#include <Arduino.h>
#include <ArduinoJson.h>
#include <M5Cardputer.h>
#include <Preferences.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

#include "config.h"

namespace {
WebSocketsClient webSocket;
Preferences preferences;

enum class InputMode : uint8_t {
  Chat,
  WiFiSsid,
  WiFiPassword,
};

struct WiFiCredentials {
  String ssid;
  String password;
};

String inputText;
String statusText = "Booting";
String messageTitle = "Ready";
String messageBody = "Type and press Enter";
size_t messagePage = 0;
bool colaConnected = false;
unsigned long lastReconnectAttempt = 0;
InputMode inputMode = InputMode::Chat;
bool setupInputSubmitted = false;
bool inputPrefillSelected = false;

constexpr uint8_t protocolVersion = 1;
constexpr unsigned long reconnectIntervalMs = 5000;
constexpr unsigned long wifiConnectTimeoutMs = 20000;
constexpr size_t maxChatInputLength = 240;
constexpr size_t maxWifiSsidLength = 32;
constexpr size_t maxWifiPasswordLength = 64;
constexpr size_t messageCharsPerLine = 18;
constexpr uint8_t messageLinesPerPage = 2;
constexpr size_t messageCharsPerPage = messageCharsPerLine * messageLinesPerPage;
constexpr char wifiPrefsNamespace[] = "cola-m5";
constexpr char wifiSsidKey[] = "wifiSsid";
constexpr char wifiPasswordKey[] = "wifiPass";

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

String normalizeText(String text, bool preserveLineBreaks = false) {
  text.replace("\r", preserveLineBreaks ? "\n" : " ");
  text.replace("\n", preserveLineBreaks ? "\n" : " ");

  while (text.indexOf("  ") >= 0) {
    text.replace("  ", " ");
  }

  if (preserveLineBreaks) {
    while (text.indexOf(" \n") >= 0) {
      text.replace(" \n", "\n");
    }

    while (text.indexOf("\n ") >= 0) {
      text.replace("\n ", "\n");
    }

    while (text.indexOf("\n\n") >= 0) {
      text.replace("\n\n", "\n");
    }
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
  return pageCountFor(normalizeText(messageBody, true));
}

void renderApp();
void handleKeyboard();

size_t currentMaxInputLength() {
  switch (inputMode) {
    case InputMode::WiFiSsid:
      return maxWifiSsidLength;
    case InputMode::WiFiPassword:
      return maxWifiPasswordLength;
    case InputMode::Chat:
    default:
      return maxChatInputLength;
  }
}

String currentMessagePageText() {
  String normalized = normalizeText(messageBody, true);
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
  text = normalizeText(text, true);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(colorText, colorPanel);

  for (uint8_t line = 0; line < maxLines; ++line) {
    if (text.length() == 0) {
      return;
    }

    size_t take = min(charsPerLine, static_cast<size_t>(text.length()));
    int newlineIndex = text.indexOf('\n');
    String current;

    if (newlineIndex >= 0 && static_cast<size_t>(newlineIndex) < take) {
      current = text.substring(0, newlineIndex);
      text = text.substring(newlineIndex + 1);
    } else {
      current = text.substring(0, take);
      text = text.substring(take);

      if (text.length() > 0 && text.charAt(0) == '\n') {
        text.remove(0, 1);
      }
    }

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

const char* inputLabel() {
  switch (inputMode) {
    case InputMode::WiFiSsid:
      return "WIFI ID";
    case InputMode::WiFiPassword:
      return "WIFI PASS";
    case InputMode::Chat:
    default:
      return "INPUT";
  }
}

String maskedText(const String& text) {
  String masked;
  masked.reserve(text.length());

  for (size_t i = 0; i < text.length(); ++i) {
    masked += '*';
  }

  return masked;
}

String visibleInputText() {
  if (inputMode == InputMode::WiFiPassword) {
    return maskedText(inputText);
  }

  return inputText;
}

void drawInputBox() {
  M5Cardputer.Display.fillRoundRect(5, 99, 230, 31, 7, colorPanel2);
  M5Cardputer.Display.drawRoundRect(5, 99, 230, 31, 7, colorAccent);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(colorMuted, colorPanel2);
  M5Cardputer.Display.setCursor(14, 104);
  M5Cardputer.Display.print(inputLabel());
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(colorText, colorPanel2);
  M5Cardputer.Display.setCursor(14, 114);
  M5Cardputer.Display.print("> ");
  M5Cardputer.Display.print(tailText(visibleInputText(), 16));
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

WiFiCredentials loadWiFiCredentials() {
  WiFiCredentials credentials;

  if (!preferences.begin(wifiPrefsNamespace, true)) {
    return credentials;
  }

  credentials.ssid = preferences.getString(wifiSsidKey, "");
  credentials.password = preferences.getString(wifiPasswordKey, "");
  preferences.end();

  return credentials;
}

void saveWiFiCredentials(const WiFiCredentials& credentials) {
  if (!preferences.begin(wifiPrefsNamespace, false)) {
    return;
  }

  preferences.putString(wifiSsidKey, credentials.ssid);
  preferences.putString(wifiPasswordKey, credentials.password);
  preferences.end();
}

void clearInputForChat() {
  inputMode = InputMode::Chat;
  inputText = "";
  setupInputSubmitted = false;
  inputPrefillSelected = false;
}

String promptForWiFiField(
  InputMode mode,
  const char* title,
  const String& emptyPrompt,
  const String& prefill,
  bool required,
  bool trimValue
) {
  while (true) {
    inputMode = mode;
    inputText = prefill;
    setupInputSubmitted = false;
    inputPrefillSelected = prefill.length() > 0;

    setMessage(title, prefill.length() > 0 ? "Enter to OK\nDel to clear" : emptyPrompt, colorAmber);

    while (!setupInputSubmitted) {
      M5Cardputer.update();
      handleKeyboard();
      delay(10);
    }

    String value = inputText;

    if (trimValue) {
      value.trim();
    }

    if (!required || value.length() > 0) {
      return value;
    }

    setMessage("Wi-Fi ID empty", "Type ID then Enter", colorRed);
    delay(900);
  }
}

WiFiCredentials promptForWiFiCredentials(const WiFiCredentials& saved) {
  WiFiCredentials credentials;
  credentials.ssid = promptForWiFiField(
    InputMode::WiFiSsid,
    "Wi-Fi ID",
    "Type Wi-Fi ID",
    saved.ssid,
    true,
    true
  );
  credentials.password = promptForWiFiField(
    InputMode::WiFiPassword,
    "Wi-Fi password",
    "Type password",
    saved.password,
    false,
    false
  );
  clearInputForChat();
  return credentials;
}

bool connectWiFi(const WiFiCredentials& credentials) {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());

  drawStatus("Connecting Wi-Fi", credentials.ssid.c_str());

  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < wifiConnectTimeoutMs) {
    M5Cardputer.update();
    delay(300);
    M5Cardputer.Display.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    colaConnected = false;
    setMessage("Wi-Fi failed", "Check ID/password", colorRed);
    delay(1300);
    return false;
  }

  drawStatus("Wi-Fi connected", WiFi.localIP().toString().c_str());
  delay(700);
  return true;
}

void setupWiFi() {
  WiFiCredentials saved = loadWiFiCredentials();

  while (true) {
    WiFiCredentials credentials = promptForWiFiCredentials(saved);
    saveWiFiCredentials(credentials);

    if (connectWiFi(credentials)) {
      return;
    }

    saved = credentials;
  }
}

void connectCola() {
  if (strlen(COLA_HOST) == 0) {
    colaConnected = false;
    setMessage("Cola host empty", "Set COLA_HOST", colorRed);
    return;
  }

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

void submitSetupInput() {
  setupInputSubmitted = true;
}

void prepareInputForTyping() {
  if (inputMode == InputMode::Chat || !inputPrefillSelected) {
    return;
  }

  inputText = "";
  inputPrefillSelected = false;
}

void handleKeyboard() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();
  size_t maxInputLength = currentMaxInputLength();

  if (status.enter) {
    if (inputMode != InputMode::Chat) {
      submitSetupInput();
      return;
    }

    if (inputText.length() == 0 && messagePageCount() > 1) {
      nextMessagePage();
      return;
    }

    submitInput();
    return;
  }

  if (status.del) {
    if (inputMode != InputMode::Chat && inputPrefillSelected) {
      inputText = "";
      inputPrefillSelected = false;
      drawPrompt();
      return;
    }

    if (inputText.length() > 0) {
      inputText.remove(inputText.length() - 1);
      inputPrefillSelected = false;
      drawPrompt();
      return;
    }

    if (inputMode == InputMode::Chat && messagePageCount() > 1) {
      previousMessagePage();
    }

    return;
  }

  for (char key : status.word) {
    prepareInputForTyping();

    if (inputText.length() >= maxInputLength) {
      break;
    }

    inputText += key;
  }

  if (status.space) {
    prepareInputForTyping();
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
  setupWiFi();
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
