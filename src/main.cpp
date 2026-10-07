#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>
#include <DHT.h>

#include "secrets.h"

constexpr uint8_t OLED_SDA  = 14;
constexpr uint8_t OLED_SCL  = 12;
constexpr uint8_t OLED_ADDR = 0x3C;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);

constexpr uint8_t DHT_PIN = 13;                    // D7
constexpr uint32_t DHT_INTERVAL_MS = 5000;         
DHT dht(DHT_PIN, DHT22);

ESP8266WebServer server(80);

bool displayEnabled  = true;
bool displayDetected = false;

enum class WifiState { Connecting, Connected, Failed, Lost };
WifiState wifiState = WifiState::Connecting;
uint32_t connectStartedAt = 0;
constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;

float temperature = NAN;
float humidity    = NAN;
bool  sensorOk    = false;
uint8_t sensorFails = 0;           

uint64_t uptimeMs() {
  static uint32_t last = 0;
  static uint64_t total = 0;
  uint32_t now = millis();
  total += (uint32_t)(now - last);
  last = now;
  return total;
}

String formatUptime(uint64_t ms) {
  uint32_t s = ms / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02u",
           s / 86400, (s % 86400) / 3600, (s % 3600) / 60, s % 60);
  return String(buf);
}

void readSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    if (++sensorFails >= 3) {
      sensorOk = false;
      Serial.println("DHT22 read failed");
    }
    return;
  }
  sensorFails = 0;
  sensorOk = true;
  temperature = t;
  humidity = h;
}


void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  connectStartedAt = millis();
  wifiState = WifiState::Connecting;
  Serial.printf("Connecting to %s...\n", WIFI_SSID);
}

void updateWifiState() {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiState != WifiState::Connected) {
      Serial.printf("Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    }
    wifiState = WifiState::Connected;
    return;
  }

  if (wifiState == WifiState::Connected) {
    Serial.println("Wi-Fi connection lost");
    wifiState = WifiState::Lost;
    connectStartedAt = millis();
  } else if (wifiState == WifiState::Connecting &&
             millis() - connectStartedAt > CONNECT_TIMEOUT_MS) {
    Serial.println("Wi-Fi connect failed, retrying");
    wifiState = WifiState::Failed;
  } else if ((wifiState == WifiState::Failed || wifiState == WifiState::Lost) &&
             millis() - connectStartedAt > CONNECT_TIMEOUT_MS) {
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    connectStartedAt = millis();
  }
}

const char* wifiStateText() {
  switch (wifiState) {
    case WifiState::Connecting: return "Connecting...";
    case WifiState::Connected:  return "Connected";
    case WifiState::Failed:     return "Can't connect!";
    case WifiState::Lost:       return "Connection lost!";
  }
  return "?";
}

bool probeDisplay() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.beginTransmission(OLED_ADDR);
  return Wire.endTransmission() == 0;
}

void drawScreen() {
  if (!displayDetected) return;

  if (!displayEnabled) {
    display.setPowerSave(1);
    return;
  }
  display.setPowerSave(0);

  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);

  display.drawStr((display.getDisplayWidth() - display.getStrWidth(DEVICE_NAME)) / 2,
                  10, DEVICE_NAME);

  display.drawStr(0, 23, "WiFi:");
  display.drawStr(32, 23, wifiStateText());

  display.drawStr(0, 34, "IP:");
  display.drawStr(32, 34, wifiState == WifiState::Connected
                              ? WiFi.localIP().toString().c_str() : "-");

  display.drawStr(0, 46, "Up:");
  display.drawStr(32, 46, formatUptime(uptimeMs()).c_str());

  char line[24];
  if (sensorOk) {
    snprintf(line, sizeof(line), "%.1f\xb0" "C  %.1f%%", temperature, humidity);
  } else {
    snprintf(line, sizeof(line), "sensor error");
  }
  display.drawStr(0, 58, "T/H:");
  display.drawStr(32, 58, line);

  display.sendBuffer();
}


float round1(float v) { return roundf(v * 10.0f) / 10.0f; }

// Словесная оценка уровня сигнала
const char* rssiWord(int32_t rssi) {
  if (rssi > -50)  return "High";      // > -50
  if (rssi > -60)  return "Good";      // [-50 .. -60)
  if (rssi >= -70) return "Moderate";  // [-60 .. -70]
  return "Poor";                       // < -70
}

void handleStatus() {
  JsonDocument doc;
  bool connected = WiFi.status() == WL_CONNECTED;
  uint64_t up = uptimeMs();

  int32_t rssi = connected ? WiFi.RSSI() : 0;   // читаем один раз

  doc["device_name"] = DEVICE_NAME;

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["wifi_connected"] = connected;
  wifi["ssid"]           = connected ? WiFi.SSID() : String();
  wifi["ip"]             = connected ? WiFi.localIP().toString() : String();
  if (connected) wifi["rssi"] = rssi;
  else           wifi["rssi"] = nullptr;

  JsonObject uptime = doc["uptime"].to<JsonObject>();
  uptime["uptime_seconds"] = (uint32_t)(up / 1000);
  uptime["uptime"]         = formatUptime(up);

  JsonObject status = doc["status"].to<JsonObject>();
  status["display_on"] = displayDetected && displayEnabled;
  status["sensor_ok"]  = sensorOk;

  JsonObject sensors = doc["sensors"].to<JsonObject>();
  if (sensorOk) {
    sensors["temperature_c"] = round1(temperature);
    sensors["humidity"]      = round1(humidity);
  } else {
    sensors["temperature_c"] = nullptr;
    sensors["humidity"]      = nullptr;
  }
  if (connected) sensors["wifi_power"] = rssiWord(rssi);
  else           sensors["wifi_power"] = nullptr;

  String out;
  serializeJsonPretty(doc, out);
  server.send(200, "application/json", out);
}

// /display?state=on|off — включить/выключить экран
void handleDisplay() {
  String state = server.arg("state");
  if (state == "on")       displayEnabled = true;
  else if (state == "off") displayEnabled = false;
  else {
    server.send(400, "application/json", "{\"error\":\"use ?state=on|off\"}");
    return;
  }
  drawScreen();
  handleStatus();
}

void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}


void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\nBooting...");

  displayDetected = probeDisplay();
  if (displayDetected) {
    display.begin();
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(0, 12, "Booting...");
    display.sendBuffer();
  } else {
    Serial.println("OLED not found on I2C, check SDA/SCL pins");
  }

  dht.begin();
  startWifi();

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/display", HTTP_GET, handleDisplay);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();
  updateWifiState();

  static uint32_t lastSensor = 0;
  if (lastSensor == 0 || millis() - lastSensor >= DHT_INTERVAL_MS) {
    lastSensor = millis();
    readSensor();
  }

  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 1000) {
    lastDraw = millis();
    drawScreen();
  }
}
