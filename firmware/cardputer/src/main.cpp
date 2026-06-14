#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>

#include "cola_m5/connection_store.h"
#include "cola_m5/text.h"
#include "cola_m5/websocket_client.h"
#include "cola_m5/wifi_connect.h"
#include "config.h"

namespace {
cola_m5::WebSocketClient colaClient;
cola_m5::ConnectionSettings connectionSettings;

enum class InputMode : uint8_t {
  Chat,
  WiFiSsid,
  WiFiPassword,
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

constexpr size_t maxChatInputLength = 240;
constexpr size_t maxWifiSsidLength = 32;
constexpr size_t maxWifiPasswordLength = 64;
constexpr size_t messageCharsPerLine = 18;
constexpr uint8_t messageLinesPerPage = 2;
constexpr size_t messageCharsPerPage = messageCharsPerLine * messageLinesPerPage;

constexpr cola_m5::DeviceIdentity deviceIdentity = {
  DEVICE_ID,
  DEVICE_MODEL,
  FIRMWARE_VERSION,
};

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

size_t messagePageCount() {
  return cola_m5::pageCountFor(cola_m5::normalizeText(messageBody, true), messageCharsPerPage);
}

String currentMessagePageText() {
  String normalized = cola_m5::normalizeText(messageBody, true);
  size_t pages = cola_m5::pageCountFor(normalized, messageCharsPerPage);

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
  text = cola_m5::normalizeText(text, true);
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
  M5Cardputer.Display.print(cola_m5::compactText(messageTitle, pages > 1 ? 18 : 26));

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
  M5Cardputer.Display.print(cola_m5::tailText(visibleInputText(), 16));
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

void handlePluginEvent(const cola_m5::PluginEvent& event) {
  switch (event.type) {
    case cola_m5::PluginEventType::Reply:
      showIncomingText(event.title.c_str(), event.text.c_str());
      break;
    case cola_m5::PluginEventType::Status:
      statusText = event.text;
      colaConnected = event.connected;
      drawStatus("cola-m5", statusText.c_str());
      delay(800);
      drawPrompt();
      break;
    case cola_m5::PluginEventType::Error:
    case cola_m5::PluginEventType::Invalid:
      showIncomingText(event.title.c_str(), event.text.c_str());
      break;
    case cola_m5::PluginEventType::Unknown:
    default:
      break;
  }
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

cola_m5::WiFiCredentials promptForWiFiCredentials(const cola_m5::WiFiCredentials& saved) {
  cola_m5::WiFiCredentials credentials;
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

bool connectConfiguredWiFi(const cola_m5::WiFiCredentials& credentials) {
  cola_m5::WiFiConnectCallbacks callbacks;
  callbacks.onStatus = [](const char* title, const String& body) {
    drawStatus(title, body.c_str());
  };
  callbacks.onUpdate = []() {
    M5Cardputer.update();
  };
  callbacks.onProgress = []() {
    M5Cardputer.Display.print(".");
  };

  if (!cola_m5::connectWiFi(credentials, callbacks)) {
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
  cola_m5::WiFiCredentials saved = cola_m5::loadWiFiCredentials();

  while (true) {
    cola_m5::WiFiCredentials credentials = promptForWiFiCredentials(saved);
    cola_m5::saveWiFiCredentials(credentials);

    if (connectConfiguredWiFi(credentials)) {
      return;
    }

    saved = credentials;
  }
}

void connectCola() {
  connectionSettings = cola_m5::loadConnectionSettings(COLA_HOST, COLA_PORT);

  if (!cola_m5::hasColaEndpoint(connectionSettings)) {
    colaConnected = false;
    setMessage("Cola host empty", "Set COLA_HOST", colorRed);
    return;
  }

  cola_m5::WebSocketClientCallbacks callbacks;
  callbacks.onConnected = []() {
    colaConnected = true;
    drawStatus("Connected to plugin", "Registering device...");
  };
  callbacks.onDisconnected = []() {
    colaConnected = false;
    drawStatus("Cola disconnected", "Reconnecting...");
  };
  callbacks.onEvent = handlePluginEvent;

  colaClient.begin(connectionSettings, deviceIdentity, callbacks);
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

  colaClient.sendMessage(inputText);
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
  colaClient.loop();

  if (WiFi.status() != WL_CONNECTED) {
    colaConnected = false;
    cola_m5::reconnectWiFiIfNeeded(lastReconnectAttempt);
  }

  handleKeyboard();
}
