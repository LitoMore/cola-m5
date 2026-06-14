#include "cola_m5/provisioning_portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "cola_m5/connection_store.h"

namespace cola_m5 {
namespace {

constexpr byte kDnsPort = 53;

String htmlEscape(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

String buildPortalPage(
  const ProvisioningPortalConfig& config,
  const ConnectionSettings& settings,
  const String& message
) {
  String page;
  page.reserve(3400);
  page += F("<!doctype html><html><head><meta charset=\"utf-8\">");
  page += F("<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  page += F("<title>Cola M5 Setup</title><style>");
  page += F(":root{color-scheme:light dark;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}");
  page += F("body{margin:0;background:#101820;color:#eff6f3}");
  page += F("main{max-width:460px;margin:0 auto;padding:28px 18px}");
  page += F("h1{font-size:26px;margin:0 0 8px}p{color:#a9b8be;line-height:1.45}");
  page += F("label{display:block;margin:18px 0 6px;color:#cfdbdc;font-size:14px}");
  page += F("input{box-sizing:border-box;width:100%;font-size:18px;padding:12px;border-radius:8px;border:1px solid #3a4d58;background:#17232c;color:#fff}");
  page += F("button{width:100%;margin-top:22px;padding:13px;border:0;border-radius:8px;background:#24d1b5;color:#06100e;font-size:17px;font-weight:700}");
  page += F(".msg{padding:10px 12px;border-radius:8px;background:#273541;color:#ffd27a;margin-top:16px}");
  page += F("</style></head><body><main><h1>");
  page += htmlEscape(config.deviceName == nullptr ? "Cola M5" : config.deviceName);
  page += F("</h1><p>Enter the Wi-Fi network and the Cola plugin address for this device.</p>");

  if (message.length() > 0) {
    page += F("<div class=\"msg\">");
    page += htmlEscape(message);
    page += F("</div>");
  }

  page += F("<form method=\"post\" action=\"/save\">");
  page += F("<label for=\"ssid\">Wi-Fi ID</label><input id=\"ssid\" name=\"ssid\" required maxlength=\"32\" value=\"");
  page += htmlEscape(settings.wifi.ssid);
  page += F("\">");
  page += F("<label for=\"password\">Wi-Fi password</label><input id=\"password\" name=\"password\" type=\"password\" maxlength=\"64\" value=\"");
  page += htmlEscape(settings.wifi.password);
  page += F("\">");
  page += F("<label for=\"cola_host\">Cola host</label><input id=\"cola_host\" name=\"cola_host\" required placeholder=\"192.168.1.23\" value=\"");
  page += htmlEscape(settings.colaHost);
  page += F("\">");
  page += F("<label for=\"cola_port\">Cola port</label><input id=\"cola_port\" name=\"cola_port\" inputmode=\"numeric\" pattern=\"[0-9]*\" value=\"");
  page += String(settings.colaPort);
  page += F("\">");
  page += F("<button type=\"submit\">Save</button></form></main></body></html>");
  return page;
}

bool readSubmittedSettings(WebServer& server, ConnectionSettings& settings, String& error) {
  settings.wifi.ssid = server.arg("ssid");
  settings.wifi.password = server.arg("password");
  settings.colaHost = server.arg("cola_host");
  String portArg = server.arg("cola_port");

  settings.wifi.ssid.trim();
  settings.colaHost.trim();
  portArg.trim();

  if (settings.wifi.ssid.length() == 0) {
    error = "Wi-Fi ID is required.";
    return false;
  }

  if (settings.colaHost.length() == 0) {
    error = "Cola host is required.";
    return false;
  }

  if (portArg.length() == 0) {
    settings.colaPort = kDefaultColaPort;
  } else {
    long port = portArg.toInt();

    if (port <= 0 || port > 65535) {
      error = "Cola port must be between 1 and 65535.";
      return false;
    }

    settings.colaPort = static_cast<uint16_t>(port);
  }

  return true;
}

}  // namespace

String makeProvisioningApSsid(const char* prefix) {
  uint64_t mac = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06X", static_cast<uint32_t>(mac & 0xFFFFFF));

  String ssid = prefix == nullptr || prefix[0] == '\0' ? "Cola-M5" : prefix;
  ssid += "-";
  ssid += suffix;
  return ssid;
}

bool runProvisioningPortal(
  const ProvisioningPortalConfig& config,
  const ProvisioningPortalCallbacks& callbacks
) {
  ConnectionSettings settings = loadConnectionSettings(config.defaultColaHost, config.defaultColaPort);
  ConnectionSettings submittedSettings = settings;
  DNSServer dnsServer;
  WebServer server(80);
  bool submitted = false;
  bool saved = false;
  String formMessage;

  IPAddress apIP(192, 168, 4, 1);
  IPAddress netmask(255, 255, 255, 0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netmask);

  const bool hasPassword = config.apPassword != nullptr && strlen(config.apPassword) >= 8;
  bool apStarted = hasPassword
    ? WiFi.softAP(config.apSsid, config.apPassword)
    : WiFi.softAP(config.apSsid);

  if (!apStarted) {
    if (callbacks.onStatus) {
      callbacks.onStatus("Setup failed", "Could not start setup AP");
    }
    return false;
  }

  dnsServer.start(kDnsPort, "*", apIP);

  if (callbacks.onStarted) {
    callbacks.onStarted(config.apSsid, WiFi.softAPIP());
  }

  server.on("/", HTTP_GET, [&]() {
    server.send(200, "text/html", buildPortalPage(config, settings, formMessage));
  });

  server.on("/save", HTTP_POST, [&]() {
    String error;
    submittedSettings = settings;

    if (!readSubmittedSettings(server, submittedSettings, error)) {
      formMessage = error;
      settings = submittedSettings;
      server.send(400, "text/html", buildPortalPage(config, settings, formMessage));
      return;
    }

    saveConnectionSettings(submittedSettings);
    formMessage = "Saved. The device will connect to Wi-Fi now.";
    server.send(200, "text/html", buildPortalPage(config, submittedSettings, formMessage));
    submitted = true;
    saved = true;
  });

  server.on("/generate_204", HTTP_GET, [&]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.on("/hotspot-detect.html", HTTP_GET, [&]() {
    server.send(200, "text/html", buildPortalPage(config, settings, formMessage));
  });
  server.on("/fwlink", HTTP_GET, [&]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.onNotFound([&]() {
    server.send(200, "text/html", buildPortalPage(config, settings, formMessage));
  });

  server.begin();

  unsigned long startedAt = millis();
  while (!submitted) {
    dnsServer.processNextRequest();
    server.handleClient();

    if (callbacks.onUpdate) {
      callbacks.onUpdate();
    }

    if (config.timeoutMs > 0 && millis() - startedAt > config.timeoutMs) {
      break;
    }

    delay(10);
  }

  unsigned long settleUntil = millis() + 800;
  while (saved && millis() < settleUntil) {
    dnsServer.processNextRequest();
    server.handleClient();
    if (callbacks.onUpdate) {
      callbacks.onUpdate();
    }
    delay(10);
  }

  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);

  return saved;
}

}  // namespace cola_m5
