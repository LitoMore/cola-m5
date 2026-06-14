#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>

#include "cola_m5/connection_store.h"
#include "cola_m5/provisioning_portal.h"
#include "cola_m5/text.h"
#include "cola_m5/websocket_client.h"
#include "cola_m5/wifi_connect.h"
#include "config.h"

namespace {
cola_m5::WebSocketClient colaClient;
cola_m5::ConnectionSettings connectionSettings;

String statusText = "Booting";
String messageTitle = "Ready";
String messageBody = "Select a shortcut";
size_t messagePage = 0;
bool colaConnected = false;
bool setupLaunchHandled = false;
unsigned long lastReconnectAttempt = 0;

constexpr size_t messageCharsPerLine = 18;
constexpr uint8_t messageLinesPerPage = 3;
constexpr size_t messageCharsPerPage = messageCharsPerLine * messageLinesPerPage;
constexpr unsigned long setupHoldMs = 1800;

constexpr cola_m5::DeviceIdentity deviceIdentity = {
  DEVICE_ID,
  DEVICE_MODEL,
  FIRMWARE_VERSION,
};

const char* quickMessages[] = {
  "Continue",
  "Summarize this",
  "What should I do next?",
  "Stop",
};
constexpr size_t quickMessageCount = sizeof(quickMessages) / sizeof(quickMessages[0]);
size_t quickMessageIndex = 0;

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

int displayWidth() {
  return M5.Display.width();
}

int displayHeight() {
  return M5.Display.height();
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
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(colorText, colorPanel);

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

    M5.Display.setCursor(x, y + line * 19);
    M5.Display.print(current);
  }
}

void drawHeader() {
  int width = displayWidth();
  M5.Display.fillRoundRect(5, 4, width - 10, 24, 6, colorPanel);
  M5.Display.fillCircle(18, 16, 5, colorAccent);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(colorText, colorPanel);
  M5.Display.setCursor(30, 9);
  M5.Display.print("Cola M5");

  bool wifiConnected = WiFi.status() == WL_CONNECTED;
  const char* label = colaConnected ? "ONLINE" : (wifiConnected ? "WAIT" : "WIFI");
  uint16_t statusColor = colaConnected ? colorGreen : (wifiConnected ? colorAmber : colorRed);

  M5.Display.fillRoundRect(width - 79, 8, 72, 16, 5, statusColor);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(colorBg, statusColor);
  M5.Display.setCursor(width - 61, 13);
  M5.Display.print(label);
}

void drawMessageCard() {
  int width = displayWidth();
  int height = displayHeight();
  int cardHeight = height - 65;
  String body = currentMessagePageText();
  size_t pages = messagePageCount();

  M5.Display.fillRoundRect(5, 32, width - 10, cardHeight, 7, colorPanel);
  M5.Display.fillRoundRect(5, 32, 6, cardHeight, 3, messageAccent);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(messageAccent, colorPanel);
  M5.Display.setCursor(16, 38);
  M5.Display.print(cola_m5::compactText(messageTitle, pages > 1 ? 18 : 26));

  if (pages > 1) {
    M5.Display.fillRoundRect(width - 44, 36, 31, 13, 4, colorPanel2);
    M5.Display.setTextColor(colorMuted, colorPanel2);
    M5.Display.setCursor(width - 38, 40);
    M5.Display.print(messagePage + 1);
    M5.Display.print("/");
    M5.Display.print(pages);
  }

  drawLargeLines(body, 16, 53, messageCharsPerLine, messageLinesPerPage);
}

void drawShortcutBar() {
  int width = displayWidth();
  int height = displayHeight();
  String shortcut = quickMessages[quickMessageIndex];

  M5.Display.fillRoundRect(5, height - 28, width - 10, 23, 7, colorPanel2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(colorMuted, colorPanel2);
  M5.Display.setCursor(14, height - 22);
  M5.Display.print("A");
  M5.Display.setTextColor(colorText, colorPanel2);
  M5.Display.print(" ");
  M5.Display.print(quickMessageIndex + 1);
  M5.Display.print("/");
  M5.Display.print(quickMessageCount);
  M5.Display.print(" ");
  M5.Display.print(cola_m5::compactText(shortcut, 22));
  M5.Display.setTextColor(colorAccent, colorPanel2);
  M5.Display.setCursor(width - 42, height - 22);
  M5.Display.print("B GO");
}

void renderApp() {
  M5.Display.fillScreen(colorBg);
  drawHeader();
  drawMessageCard();
  drawShortcutBar();
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
      delay(700);
      renderApp();
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

bool runSetupPortal() {
  String apSsid = cola_m5::makeProvisioningApSsid("Cola-StickS3");
  cola_m5::ProvisioningPortalConfig config;
  config.apSsid = apSsid.c_str();
  config.deviceName = "Cola StickS3";
  config.defaultColaHost = COLA_HOST;
  config.defaultColaPort = COLA_PORT;

  cola_m5::ProvisioningPortalCallbacks callbacks;
  callbacks.onStarted = [](const String& ssid, const IPAddress& ip) {
    String body = ssid;
    body += "\n";
    body += ip.toString();
    setMessage("Setup AP", body, colorAmber);
  };
  callbacks.onStatus = [](const char* title, const String& body) {
    setMessage(title, body, colorRed);
  };
  callbacks.onUpdate = []() {
    M5.update();
  };

  return cola_m5::runProvisioningPortal(config, callbacks);
}

void loadSettings() {
  connectionSettings = cola_m5::loadConnectionSettings(COLA_HOST, COLA_PORT);
}

bool needsProvisioning() {
  return !cola_m5::hasWiFiCredentials(connectionSettings.wifi) ||
         !cola_m5::hasColaEndpoint(connectionSettings);
}

void ensureProvisioned() {
  loadSettings();

  while (needsProvisioning()) {
    runSetupPortal();
    loadSettings();
  }
}

bool connectConfiguredWiFi() {
  cola_m5::WiFiConnectCallbacks callbacks;
  callbacks.onStatus = [](const char* title, const String& body) {
    drawStatus(title, body.c_str());
  };
  callbacks.onUpdate = []() {
    M5.update();
  };

  if (!cola_m5::connectWiFi(connectionSettings.wifi, callbacks)) {
    colaConnected = false;
    setMessage("Wi-Fi failed", "Starting setup", colorRed);
    delay(1200);
    return false;
  }

  drawStatus("Wi-Fi connected", WiFi.localIP().toString().c_str());
  delay(700);
  return true;
}

void setupWiFi() {
  while (!connectConfiguredWiFi()) {
    runSetupPortal();
    loadSettings();
  }
}

void connectCola() {
  if (!cola_m5::hasColaEndpoint(connectionSettings)) {
    colaConnected = false;
    setMessage("Cola host empty", "Run setup", colorRed);
    return;
  }

  cola_m5::WebSocketClientCallbacks callbacks;
  callbacks.onConnected = []() {
    colaConnected = true;
    drawStatus("Connected", "Registering device...");
  };
  callbacks.onDisconnected = []() {
    colaConnected = false;
    drawStatus("Disconnected", "Reconnecting...");
  };
  callbacks.onEvent = handlePluginEvent;

  colaClient.begin(connectionSettings, deviceIdentity, callbacks);
}

void nextQuickMessage() {
  quickMessageIndex = (quickMessageIndex + 1) % quickMessageCount;
  renderApp();
}

void sendQuickMessage() {
  String text = quickMessages[quickMessageIndex];

  if (!colaConnected) {
    drawStatus("Cola offline", "Message not sent");
    delay(700);
    renderApp();
    return;
  }

  colaClient.sendMessage(text);
  showIncomingText("Sent", text.c_str());
}

void handleButtons() {
  if (
    M5.BtnA.pressedFor(setupHoldMs) &&
    M5.BtnB.pressedFor(setupHoldMs) &&
    !setupLaunchHandled
  ) {
    setupLaunchHandled = true;
    setMessage("Setup mode", "Starting portal", colorAmber);
    delay(400);

    if (runSetupPortal()) {
      ESP.restart();
    }

    renderApp();
    return;
  }

  if (!M5.BtnA.isPressed() || !M5.BtnB.isPressed()) {
    setupLaunchHandled = false;
  }

  if (M5.BtnA.wasClicked()) {
    if (messagePageCount() > 1) {
      nextMessagePage();
    } else {
      nextQuickMessage();
    }
    return;
  }

  if (M5.BtnA.wasHold()) {
    previousMessagePage();
    return;
  }

  if (M5.BtnB.wasClicked()) {
    sendQuickMessage();
  }
}
}  // namespace

void setup() {
  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.setTextScroll(false);
  M5.BtnA.setHoldThresh(700);
  M5.BtnB.setHoldThresh(700);
  Serial.begin(115200);

  renderApp();
  ensureProvisioned();
  setupWiFi();
  connectCola();
  renderApp();
}

void loop() {
  M5.update();
  colaClient.loop();

  if (WiFi.status() != WL_CONNECTED) {
    colaConnected = false;
    cola_m5::reconnectWiFiIfNeeded(lastReconnectAttempt);
  }

  handleButtons();
}
