// =============================================================================
// FarmLens Firmware — Phase 2
// Board: ESP32 Dev Module
// Framework: Arduino / PlatformIO
//
// Phase 2 role: read mock sensors, POST JSON to RPi every 5 seconds.
// The RPi runs the HTTP server — the app connects to RPi on port 8000.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

// ─── USER CONFIGURATION — edit these three lines ────────────────────────────
const char* WIFI_SSID = "Arwa";
const char* WIFI_PASS = "ayA@1985";
const char* RPI_IP    = "192.168.1.106";   // RPi IP from hostname -I
const int   RPI_PORT  = 8000;
const char* NODE_ID   = "FL-001";
const int   LED_PIN   = 2;                 // ESP32 Dev Module built-in LED
// ────────────────────────────────────────────────────────────────────────────

const unsigned long SEND_INTERVAL_MS = 5000;

// ─── MOCK SENSOR STATE ───────────────────────────────────────────────────────
float g_moisturePct = 50.0;
float g_waterPct    = 45.0;
int   g_cycleCount  = 0;

// ─── LED HELPERS ─────────────────────────────────────────────────────────────
void blinkLED(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);  delay(ms);
    digitalWrite(LED_PIN, HIGH); delay(ms);
  }
}

// ─── MOCK SENSOR UPDATE ──────────────────────────────────────────────────────
void updateMockSensors() {
  float t = millis() / 1000.0f;
  g_moisturePct = constrain(50.0f + 30.0f * sin(t / 300.0f * 2.0f * PI), 0.0f, 100.0f);
  g_waterPct    = constrain(45.0f + 25.0f * sin(t / 240.0f * 2.0f * PI + 1.0f), 0.0f, 100.0f);
}

// ─── SEND SENSOR DATA TO RPI ─────────────────────────────────────────────────
bool sendSensorData() {
  updateMockSensors();
  g_cycleCount++;

  int mRaw    = (int)(2800 - (g_moisturePct / 100.0f) * 1600);
  int wRaw    = (int)(3000 - (g_waterPct    / 100.0f) * 2500);
  int mStress = (g_moisturePct < 30.0f) ? 1 : 0;
  int wStress = (g_waterPct    < 20.0f) ? 1 : 0;
  float cs    = 0.6f * mStress + 0.4f * wStress;

  StaticJsonDocument<256> doc;
  doc["node_id"]          = NODE_ID;
  doc["moisture_raw"]     = mRaw;
  doc["moisture_pct"]     = round(g_moisturePct * 10) / 10.0;
  doc["water_raw"]        = wRaw;
  doc["water_pct"]        = round(g_waterPct * 10) / 10.0;
  doc["moisture_stress"]  = mStress;
  doc["water_stress"]     = wStress;
  doc["ts"]               = (int)(millis() / 1000);
  doc["fault"]            = 0;

  String body;
  serializeJson(doc, body);

  String url = String("http://") + RPI_IP + ":" + RPI_PORT + "/api/sensor";

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(4000);
  int code = http.POST(body);
  http.end();

  if (code == 200) {
    Serial.printf("[FL] POST OK  → m=%.1f%% w=%.1f%% cs=%.2f\n",
                  g_moisturePct, g_waterPct, cs);
    return true;
  } else {
    Serial.printf("[FL] POST FAIL → HTTP %d  (is RPi running?)\n", code);
    return false;
  }
}

// ─── SETUP ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n[FL] ======================================");
  Serial.println("[FL] FarmLens ESP32 — Phase 2");
  Serial.printf( "[FL] Node: %s\n", NODE_ID);
  Serial.println("[FL] ======================================");

  pinMode(LED_PIN, OUTPUT);
  blinkLED(2, 300);   // 2 slow blinks = booting

  // WiFi
  Serial.printf("[FL] Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[FL] WiFi FAILED — restarting...");
    delay(3000);
    ESP.restart();
  }

  digitalWrite(LED_PIN, HIGH);  // solid = connected
  Serial.println("[FL] ✓ WiFi connected");
  Serial.printf( "[FL] ESP32 IP : %s\n", WiFi.localIP().toString().c_str());

  // mDNS (still useful for identification)
  if (MDNS.begin("farmlens-esp32")) {
    Serial.println("[FL] ✓ mDNS: farmlens-esp32.local");
  }

  Serial.println("[FL] ┌──────────────────────────────────────────────┐");
  Serial.printf( "[FL] │  Sending sensor data to RPi every 5s        │\n");
  Serial.printf( "[FL] │  RPi target: http://%s:%d     │\n", RPI_IP, RPI_PORT);
  Serial.println("[FL] │  App should connect to RPi on port 8000     │");
  Serial.println("[FL] └──────────────────────────────────────────────┘");
}

// ─── LOOP ────────────────────────────────────────────────────────────────────
void loop() {
  static unsigned long lastSend = 0;

  if (millis() - lastSend >= SEND_INTERVAL_MS) {
    bool ok = sendSensorData();
    // LED feedback: 1 quick blink = OK, 3 rapid = failed
    if (ok) {
      digitalWrite(LED_PIN, LOW); delay(60); digitalWrite(LED_PIN, HIGH);
    } else {
      blinkLED(3, 80);
      digitalWrite(LED_PIN, HIGH);
    }
    lastSend = millis();
  }

  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[FL] WiFi lost — reconnecting...");
    WiFi.reconnect();
    delay(2000);
  }
}