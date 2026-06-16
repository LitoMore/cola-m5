#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_sleep.h>

#include "cola_m5/connection_store.h"
#include "cola_m5/provisioning_portal.h"
#include "cola_m5/text.h"
#include "cola_m5/websocket_client.h"
#include "cola_m5/wifi_connect.h"
#include "config.h"
#include "xiaozhi_epaper.h"

namespace {
cola_m5::WebSocketClient colaClient;
cola_m5::ConnectionSettings connectionSettings;
XiaozhiEpaper epaper;

String statusText = "Booting";
String messageTitle = "Ready";
String messageBody = "Starting e-paper target";
size_t messagePage = 0;
bool colaConnected = false;
bool setupLaunchHandled = false;
bool wifiLostShown = false;
unsigned long lastReconnectAttempt = 0;
unsigned long lastHeartbeatAt = 0;

constexpr unsigned long setupHoldMs = 1800;
constexpr unsigned long sendHoldMs = 700;
constexpr unsigned long debounceMs = 30;
constexpr unsigned long doubleClickMs = 420;
constexpr int userButtonPin = 21;
constexpr int sysI2cSdaPin = 2;
constexpr int sysI2cSclPin = 1;
constexpr uint8_t i2cAddrTouch = 0x38;
constexpr uint8_t i2cAddrAudio = 0x18;
constexpr uint8_t i2cAddrCharger = 0x49;
constexpr uint8_t i2cAddrGauge = 0x55;

constexpr uint16_t colorBg = 0xFFFF;
constexpr uint16_t colorPanel = 0xFFFF;
constexpr uint16_t colorInk = 0x0000;
constexpr uint16_t colorMuted = 0x7BEF;
constexpr uint16_t colorLine = 0x39E7;
constexpr uint16_t colorWarn = 0x0000;

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

struct ButtonState {
  bool pressed = false;
  bool click = false;
  bool doubleClick = false;
  bool hold = false;
  bool setup = false;
  bool pendingClick = false;
  bool holdReported = false;
  bool setupReported = false;
  unsigned long pressedAt = 0;
  unsigned long releasedAt = 0;
  unsigned long pendingClickAt = 0;
};

ButtonState userButton;

void renderApp();

int displayWidth() {
  int width = epaper.width();
  return width > 0 ? width : 240;
}

int displayHeight() {
  int height = epaper.height();
  return height > 0 ? height : 135;
}

int bodyTextSize() {
  int shortSide = min(displayWidth(), displayHeight());

  if (shortSide >= 520) {
    return 3;
  }

  if (shortSide >= 240) {
    return 2;
  }

  return 1;
}

int smallTextSize() {
  return min(displayWidth(), displayHeight()) >= 360 ? 2 : 1;
}

int lineHeightFor(int textSize) {
  return (textSize * 8) + max(4, textSize * 3);
}

int textPixelWidth(const String& text, int textSize) {
  return static_cast<int>(text.length()) * 6 * textSize;
}

int headerHeight() {
  return 58 + ((smallTextSize() - 1) * 18);
}

int footerTop() {
  return displayHeight() - 42;
}

int messagePanelTop() {
  return headerHeight() + 8;
}

int messageBodyTop() {
  return messagePanelTop() + 34;
}

size_t charsPerLineFor(int maxWidth, int textSize) {
  int charWidth = max(6, textSize * 6);
  int chars = maxWidth / charWidth;
  return chars > 8 ? static_cast<size_t>(chars) : 8;
}

uint8_t bodyLineCount() {
  int top = messageBodyTop();
  int bottom = footerTop() - 12;
  int usable = max(24, bottom - top);
  int lines = usable / lineHeightFor(bodyTextSize());
  return lines > 1 ? static_cast<uint8_t>(lines) : 1;
}

size_t messageCharsPerPage() {
  int maxWidth = displayWidth() - 32;
  return charsPerLineFor(maxWidth, bodyTextSize()) * bodyLineCount();
}

size_t messagePageCount() {
  return cola_m5::pageCountFor(cola_m5::normalizeText(messageBody, true), messageCharsPerPage());
}

String currentMessagePageText() {
  String normalized = cola_m5::normalizeText(messageBody, true);
  size_t charsPerPage = messageCharsPerPage();
  size_t pages = cola_m5::pageCountFor(normalized, charsPerPage);

  if (messagePage >= pages) {
    messagePage = pages - 1;
  }

  size_t start = messagePage * charsPerPage;
  size_t end = min(start + charsPerPage, static_cast<size_t>(normalized.length()));
  return normalized.substring(start, end);
}

const char* boardName() {
  return "XiaozhiCard";
}

const char* statusLabel() {
  if (colaConnected) {
    return "ONLINE";
  }

  if (WiFi.status() == WL_CONNECTED) {
    return "WIFI";
  }

  return "SETUP";
}

void flushDisplay() {
  epaper.display();
}

void updateUserButton() {
  userButton.click = false;
  userButton.doubleClick = false;
  userButton.hold = false;
  userButton.setup = false;

  bool pressed = digitalRead(userButtonPin) == LOW;
  unsigned long now = millis();

  if (pressed && !userButton.pressed) {
    if (now - userButton.releasedAt < debounceMs) {
      return;
    }
    userButton.pressed = true;
    userButton.pressedAt = now;
    return;
  }

  if (!pressed && userButton.pressed) {
    userButton.pressed = false;
    userButton.releasedAt = now;
    unsigned long duration = now - userButton.pressedAt;

    if (duration >= setupHoldMs) {
      userButton.pendingClick = false;
      userButton.setup = true;
    } else if (duration >= sendHoldMs) {
      userButton.pendingClick = false;
      userButton.hold = true;
    } else if (duration >= debounceMs) {
      if (userButton.pendingClick && now - userButton.pendingClickAt <= doubleClickMs) {
        userButton.pendingClick = false;
        userButton.doubleClick = true;
      } else {
        userButton.pendingClick = true;
        userButton.pendingClickAt = now;
      }
    }
  }

  if (
    userButton.pendingClick &&
    !userButton.pressed &&
    now - userButton.pendingClickAt > doubleClickMs
  ) {
    userButton.pendingClick = false;
    userButton.click = true;
  }
}

void skipLeadingBreaks(String& text) {
  while (text.length() > 0) {
    char c = text.charAt(0);

    if (c != ' ' && c != '\n' && c != '\t') {
      return;
    }

    text.remove(0, 1);
  }
}

void drawWrappedText(
  String text,
  int x,
  int y,
  int maxWidth,
  int textSize,
  uint8_t maxLines,
  uint16_t color = colorInk,
  uint16_t background = colorPanel
) {
  text = cola_m5::normalizeText(text, true);
  size_t charsPerLine = charsPerLineFor(maxWidth, textSize);
  int lineHeight = lineHeightFor(textSize);
  epaper.setTextSize(textSize);
  epaper.setTextColor(color, background);

  for (uint8_t line = 0; line < maxLines; ++line) {
    if (text.length() == 0) {
      return;
    }

    size_t take = min(charsPerLine, static_cast<size_t>(text.length()));
    int newlineIndex = text.indexOf('\n');
    size_t breakAt = take;

    if (newlineIndex >= 0 && static_cast<size_t>(newlineIndex) < take) {
      breakAt = static_cast<size_t>(newlineIndex);
    } else if (text.length() > take) {
      int lastSpace = -1;

      for (size_t i = 0; i < take; ++i) {
        if (text.charAt(i) == ' ') {
          lastSpace = static_cast<int>(i);
        }
      }

      if (lastSpace > 4) {
        breakAt = static_cast<size_t>(lastSpace);
      }
    }

    String current = text.substring(0, breakAt);
    text = newlineIndex >= 0 && static_cast<size_t>(newlineIndex) == breakAt
      ? text.substring(breakAt + 1)
      : text.substring(breakAt);
    skipLeadingBreaks(text);

    epaper.setCursor(x, y + line * lineHeight);
    epaper.print(current);
  }
}

void drawHeader() {
  int width = displayWidth();
  int smallSize = smallTextSize();
  int titleSize = smallSize + 1;
  int margin = 10;
  int lineY = headerHeight() - 1;
  String status = statusLabel();
  String board = boardName();
  String dimensions = String(displayWidth()) + "x" + String(displayHeight());
  int statusPaddingX = 8 * smallSize;
  int statusTextWidth = textPixelWidth(status, smallSize);
  int pillWidth = max(44 * smallSize, statusTextWidth + statusPaddingX * 2);
  int pillHeight = max(18, (8 * smallSize) + 10);
  int pillX = max(margin, width - pillWidth - margin);
  int titleMaxWidth = max(0, pillX - (margin * 2));
  int resolvedTitleSize = titleSize;

  if (textPixelWidth("Cola M5", resolvedTitleSize) > titleMaxWidth && resolvedTitleSize > 1) {
    resolvedTitleSize = 1;
  }

  epaper.drawFastHLine(0, lineY, width, colorLine);
  epaper.setTextSize(resolvedTitleSize);
  epaper.setTextColor(colorInk, colorBg);
  epaper.setCursor(margin, 8);
  epaper.print("Cola M5");

  epaper.setTextSize(smallSize);
  epaper.setTextColor(colorInk, colorBg);
  int dimensionsWidth = textPixelWidth(dimensions, smallSize);
  int dimensionsX = max(margin, width - dimensionsWidth - margin);
  int boardMaxWidth = max(8, dimensionsX - (margin * 2));
  epaper.setCursor(margin, 36 + ((smallSize - 1) * 12));
  epaper.print(cola_m5::compactText(board, charsPerLineFor(boardMaxWidth, smallSize)));
  epaper.setCursor(dimensionsX, 36 + ((smallSize - 1) * 12));
  epaper.print(dimensions);

  epaper.drawRoundRect(pillX, 8, pillWidth, pillHeight, 4, colorInk);
  epaper.setCursor(pillX + statusPaddingX, 8 + ((pillHeight - (8 * smallSize)) / 2));
  epaper.print(status);
}

void drawMessagePanel() {
  int width = displayWidth();
  int textSize = bodyTextSize();
  int smallSize = smallTextSize();
  int top = messagePanelTop();
  int bottom = footerTop();
  String body = currentMessagePageText();
  size_t pages = messagePageCount();

  epaper.drawRoundRect(8, top, width - 16, max(34, bottom - top - 8), 4, colorInk);
  epaper.setTextSize(smallSize);
  epaper.setTextColor(colorInk, colorBg);
  epaper.setCursor(18, top + 10);
  epaper.print(cola_m5::compactText(messageTitle, pages > 1 ? 18 : 28));

  if (pages > 1) {
    epaper.setCursor(width - 58, top + 10);
    epaper.print(messagePage + 1);
    epaper.print("/");
    epaper.print(pages);
  }

  drawWrappedText(body, 18, messageBodyTop(), width - 36, textSize, bodyLineCount());
}

void drawFooter() {
  int width = displayWidth();
  int height = displayHeight();
  int textSize = smallTextSize();
  String shortcut = quickMessages[quickMessageIndex];

  epaper.drawFastHLine(0, height - 42, width, colorLine);
  epaper.setTextSize(textSize);
  epaper.setTextColor(colorInk, colorBg);
  epaper.setCursor(12, height - 29);
  epaper.print("NEXT ");
  epaper.print(quickMessageIndex + 1);
  epaper.print("/");
  epaper.print(quickMessageCount);
  epaper.print(" ");
  epaper.print(cola_m5::compactText(shortcut, charsPerLineFor(width - 120, textSize)));
  epaper.setCursor(max(12, width - 58), height - 29);
  epaper.print("SEND");
}

void renderApp() {
  epaper.waitDisplay();
  epaper.startWrite();
  epaper.fillScreen(colorBg);
  drawHeader();
  drawMessagePanel();
  drawFooter();
  epaper.endWrite();
  flushDisplay();
}

void setMessage(const char* title, const String& body, uint16_t = colorInk) {
  messageTitle = title;
  messageBody = body;
  messagePage = 0;
  renderApp();
}

void drawStatus(const char* line1, const char* line2 = nullptr) {
  String body = line2 == nullptr ? "" : line2;

  if (body.length() == 0) {
    body = statusText;
  }

  setMessage(line1, body);
}

void powerOff() {
  Serial.println("[xiaozhi-card-kit] power off requested");
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  gpio_num_t wakePin = static_cast<gpio_num_t>(userButtonPin);
  esp_err_t wakeResult = ESP_ERR_INVALID_ARG;

  if (esp_sleep_is_valid_wakeup_gpio(wakePin)) {
    wakeResult = esp_sleep_enable_ext0_wakeup(wakePin, 0);
  }

  if (wakeResult != ESP_OK) {
    Serial.printf("[xiaozhi-card-kit] wake pin setup failed: %s\n", esp_err_to_name(wakeResult));
    setMessage("Power off failed", "Back key cannot wake");
    delay(900);
    renderApp();
    return;
  }

  setMessage("Power off", "Press back key to wake");
  delay(300);

  colaClient.disconnect();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  btStop();

  Serial.flush();
  esp_deep_sleep_start();
}

void handlePowerShortcut() {
  if (userButton.doubleClick) {
    powerOff();
  }
}

void showIncomingText(const char* prefix, const char* text) {
  setMessage(prefix, text);
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
  String apSsid = cola_m5::makeProvisioningApSsid("Cola-Xiaozhi");
  cola_m5::ProvisioningPortalConfig config;
  config.apSsid = apSsid.c_str();
  config.deviceName = "Cola Xiaozhi Card Kit";
  config.defaultColaHost = COLA_HOST;
  config.defaultColaPort = COLA_PORT;

  cola_m5::ProvisioningPortalCallbacks callbacks;
  callbacks.onStarted = [](const String& ssid, const IPAddress& ip) {
    String body = "Wi-Fi AP:\n";
    body += ssid;
    body += "\nOpen:\nhttp://";
    body += ip.toString();
    Serial.printf("[xiaozhi-card-kit] setup ap=%s ip=%s\n", ssid.c_str(), ip.toString().c_str());
    setMessage("Setup AP", body);
  };
  callbacks.onStatus = [](const char* title, const String& body) {
    setMessage(title, body);
  };
  callbacks.onUpdate = []() {
    updateUserButton();
    handlePowerShortcut();
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
    updateUserButton();
    handlePowerShortcut();
  };

  if (!cola_m5::connectWiFi(connectionSettings.wifi, callbacks)) {
    colaConnected = false;
    setMessage("Wi-Fi failed", "Starting setup portal");
    delay(1200);
    return false;
  }

  wifiLostShown = false;
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
    setMessage("Cola host empty", "Run setup portal");
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

void startManualSetup() {
  setMessage("Setup mode", "Starting setup portal");
  delay(400);

  if (runSetupPortal()) {
    ESP.restart();
  }

  renderApp();
}

void handleButtons() {
  updateUserButton();

  if (userButton.doubleClick) {
    powerOff();
    return;
  }

  if (userButton.setup && !setupLaunchHandled) {
    setupLaunchHandled = true;
    startManualSetup();
    return;
  }

  if (!userButton.pressed) {
    setupLaunchHandled = false;
  }

  if (userButton.click) {
    if (messagePageCount() > 1) {
      nextMessagePage();
    } else {
      nextQuickMessage();
    }
    return;
  }

  if (userButton.hold) {
    sendQuickMessage();
  }
}

void handleTouch() {
}

void logBootInfo() {
  Serial.printf(
    "[xiaozhi-card-kit] board=%s display=%dx%d epd_mode=0x%02X psram=%s size=%u free=%u\n",
    boardName(),
    displayWidth(),
    displayHeight(),
    epaper.getEpdMode(),
    psramFound() ? "yes" : "no",
    static_cast<unsigned>(ESP.getPsramSize()),
    static_cast<unsigned>(ESP.getFreePsram())
  );
}

void logI2CScan() {
  bool found[120] = {};
  bool any = false;

  Serial.printf(
    "[xiaozhi-card-kit] i2c sys sda=%d scl=%d expected=0x%02X,0x%02X,0x%02X,0x%02X addr=",
    sysI2cSdaPin,
    sysI2cSclPin,
    i2cAddrAudio,
    i2cAddrTouch,
    i2cAddrCharger,
    i2cAddrGauge
  );

  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    Wire.beginTransmission(addr);
    found[addr] = Wire.endTransmission() == 0;
  }

  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    if (!found[addr]) {
      continue;
    }

    if (any) {
      Serial.print(",");
    }

    Serial.printf("0x%02X", addr);
    any = true;
  }

  if (!any) {
    Serial.print("-");
  }

  Serial.println();
}

void logHeartbeat() {
  Serial.printf(
    "[xiaozhi-card-kit] heartbeat board=%s display=%dx%d wifi=%d cola=%d psram=%s free=%u\n",
    boardName(),
    displayWidth(),
    displayHeight(),
    static_cast<int>(WiFi.status()),
    colaConnected ? 1 : 0,
    psramFound() ? "yes" : "no",
    static_cast<unsigned>(ESP.getFreePsram())
  );
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println();
  Serial.println("[xiaozhi-card-kit] boot");

  pinMode(userButtonPin, INPUT_PULLUP);
  Wire.begin(sysI2cSdaPin, sysI2cSclPin);
  Wire.setClock(100000);

  if (!epaper.begin()) {
    Serial.println("[xiaozhi-card-kit] epaper init failed");
  }

  epaper.setTextScroll(false);
  epaper.setBrightness(128);

  delay(150);
  logBootInfo();
  logI2CScan();
  renderApp();
  ensureProvisioned();
  setupWiFi();
  connectCola();
  renderApp();
}

void loop() {
  colaClient.loop();

  if (WiFi.status() != WL_CONNECTED) {
    colaConnected = false;
    cola_m5::reconnectWiFiIfNeeded(lastReconnectAttempt);

    if (!wifiLostShown) {
      wifiLostShown = true;
      drawStatus("Wi-Fi lost", "Reconnecting...");
    }
  } else {
    wifiLostShown = false;
  }

  handleButtons();
  handleTouch();

  unsigned long now = millis();
  if (now - lastHeartbeatAt > 5000) {
    lastHeartbeatAt = now;
    logHeartbeat();
  }
}
