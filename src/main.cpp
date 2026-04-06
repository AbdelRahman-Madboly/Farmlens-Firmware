// =============================================================================
// FarmLens Firmware — Phase 1 Mock Mode
// Board: LOLIN S2 Mini (ESP32-S2)
// Framework: Arduino / PlatformIO
//
// Serves mock sensor + AI detection data over WiFi HTTP REST API.
// Flutter app polls /api/live every 5s.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <math.h>

// ─── USER CONFIGURATION ─────────────────────────────────────────────────────
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* NODE_ID   = "FL-001";
const int   LED_PIN   = 15;  // S2 Mini built-in LED
// ────────────────────────────────────────────────────────────────────────────

WebServer server(80);

// ─── MOCK STATE ──────────────────────────────────────────────────────────────
float  g_moisturePct    = 50.0;
float  g_waterPct       = 45.0;
int    g_moistureRaw    = 2048;
int    g_waterRaw       = 2048;
int    g_moistureStress = 0;
int    g_waterStress    = 0;
float  g_cs             = 0.0;
float  g_cv             = 0.3;
float  g_ccombined      = 0.18;
bool   g_alert          = false;
String g_detectionClass = "Tomato_healthy";
float  g_detectionConf  = 0.30;
String g_cycleId        = "";
int    g_cycleCount     = 0;

// Settings — updated via POST /api/settings
float  g_w1       = 0.6;
float  g_w2       = 0.4;
float  g_theta    = 0.5;
String g_cropType = "tomato";

// Log buffer — circular, last 100 cycles in RAM
const int LOG_SIZE = 100;
JsonDocument g_logBuffer[LOG_SIZE];
int g_logHead  = 0;
int g_logCount = 0;

// ─── MOCK DATA ───────────────────────────────────────────────────────────────

void addToLog() {
  JsonDocument& doc = g_logBuffer[g_logHead];
  doc.clear();
  doc["ts"]               = (unsigned long)(millis() / 1000);
  doc["node_id"]          = NODE_ID;
  doc["moisture_raw"]     = g_moistureRaw;
  doc["moisture_pct"]     = round(g_moisturePct * 10) / 10.0;
  doc["water_raw"]        = g_waterRaw;
  doc["water_pct"]        = round(g_waterPct * 10) / 10.0;
  doc["moisture_stress"]  = g_moistureStress;
  doc["water_stress"]     = g_waterStress;
  doc["cs"]               = round(g_cs * 1000) / 1000.0;
  doc["cv"]               = round(g_cv * 1000) / 1000.0;
  doc["ccombined"]        = round(g_ccombined * 1000) / 1000.0;
  doc["alert"]            = g_alert;
  doc["detection_class"]  = g_detectionClass;
  doc["detection_conf"]   = round(g_detectionConf * 1000) / 1000.0;
  doc["cycle_id"]         = g_cycleId;

  g_logHead = (g_logHead + 1) % LOG_SIZE;
  if (g_logCount < LOG_SIZE) g_logCount++;
}

void updateMockData() {
  float t = millis() / 1000.0f;

  // Moisture: 20%–80%, ~5 minute sine wave
  g_moisturePct = 50.0f + 30.0f * sin(t / 300.0f * 2.0f * PI);
  g_moisturePct = constrain(g_moisturePct, 0.0f, 100.0f);
  // Map to raw ADC: dry=2800, wet=1200
  g_moistureRaw = (int)(2800 - (g_moisturePct / 100.0f) * (2800 - 1200));

  // Water: 20%–70%, ~4 minute sine wave, offset phase
  g_waterPct = 45.0f + 25.0f * sin(t / 240.0f * 2.0f * PI + 1.0f);
  g_waterPct = constrain(g_waterPct, 0.0f, 100.0f);
  // Map to raw ADC: dry=3000, wet=500
  g_waterRaw = (int)(3000 - (g_waterPct / 100.0f) * (3000 - 500));

  // Stress flags
  g_moistureStress = (g_moisturePct < 30.0f) ? 1 : 0;
  g_waterStress    = (g_waterPct    < 20.0f) ? 1 : 0;
  g_cs = 0.6f * g_moistureStress + 0.4f * g_waterStress;

  // CV random walk ±0.07 per cycle, clamped 0.10–0.95
  float delta = (float)random(-70, 71) / 1000.0f;
  g_cv = constrain(g_cv + delta, 0.10f, 0.95f);

  // Detection class
  if (g_cv > 0.60f) {
    const char* classes[] = {
      "Tomato_Late_blight",
      "Tomato_Early_blight",
      "Strawberry_Leaf_scorch",
      "Pepper_Bacterial_spot"
    };
    g_detectionClass = classes[g_cycleCount % 4];
  } else {
    g_detectionClass = "Tomato_healthy";
  }
  g_detectionConf = g_cv;

  // Combined score and alert
  g_ccombined = constrain(g_w1 * g_cv + g_w2 * g_cs, 0.0f, 1.0f);
  g_alert     = (g_ccombined > g_theta);

  // Cycle ID: FL-001-MMMMM-SS
  unsigned long upS = millis() / 1000;
  char cid[32];
  snprintf(cid, sizeof(cid), "%s-%05lu-%02lu", NODE_ID, upS / 60, upS % 60);
  g_cycleId = String(cid);

  g_cycleCount++;
  addToLog();

  Serial.printf("[FL] Cycle #%d | m=%.1f%% w=%.1f%% cv=%.2f cs=%.2f cc=%.2f alert=%s class=%s\n",
    g_cycleCount,
    g_moisturePct, g_waterPct,
    g_cv, g_cs, g_ccombined,
    g_alert ? "YES" : "no",
    g_detectionClass.c_str());
}

// ─── API HELPERS ─────────────────────────────────────────────────────────────

void sendCors() {
  server.sendHeader("Access-Control-Allow-Origin",  "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

// ─── API HANDLERS ────────────────────────────────────────────────────────────

void handleOptions() {
  sendCors();
  server.send(204);
}

void handleStatus() {
  sendCors();
  JsonDocument doc;
  doc["node_id"]      = NODE_ID;
  doc["mode"]         = "MOCK";
  doc["uptime_s"]     = (unsigned long)(millis() / 1000);
  doc["free_heap"]    = (unsigned long)ESP.getFreeHeap();
  doc["wifi_clients"] = 1;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleLive() {
  sendCors();
  JsonDocument doc;
  doc["ts"]               = (unsigned long)(millis() / 1000);
  doc["node_id"]          = NODE_ID;
  doc["moisture_raw"]     = g_moistureRaw;
  doc["moisture_pct"]     = round(g_moisturePct * 10) / 10.0;
  doc["water_raw"]        = g_waterRaw;
  doc["water_pct"]        = round(g_waterPct * 10) / 10.0;
  doc["moisture_stress"]  = g_moistureStress;
  doc["water_stress"]     = g_waterStress;
  doc["cs"]               = round(g_cs    * 1000) / 1000.0;
  doc["cv"]               = round(g_cv    * 1000) / 1000.0;
  doc["ccombined"]        = round(g_ccombined * 1000) / 1000.0;
  doc["alert"]            = g_alert;
  doc["detection_class"]  = g_detectionClass;
  doc["detection_conf"]   = round(g_detectionConf * 1000) / 1000.0;
  doc["cycle_id"]         = g_cycleId;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleLogs() {
  sendCors();

  int limit = 50;
  if (server.hasArg("limit")) {
    limit = server.arg("limit").toInt();
    if (limit < 1)        limit = 1;
    if (limit > LOG_SIZE) limit = LOG_SIZE;
  }
  int count = min(limit, g_logCount);

  JsonDocument response;
  response["count"] = count;
  JsonArray logs = response["logs"].to<JsonArray>();

  // Newest first: walk backwards from logHead
  for (int i = 0; i < count; i++) {
    int idx = ((g_logHead - 1 - i) % LOG_SIZE + LOG_SIZE) % LOG_SIZE;
    // Guard against reading uninitialised slots in a not-yet-full buffer
    if (g_logCount < LOG_SIZE && idx >= g_logCount) break;
    logs.add(g_logBuffer[idx]);
  }

  String out;
  serializeJson(response, out);
  server.send(200, "application/json", out);
}

void handleGetSettings() {
  sendCors();
  JsonDocument doc;
  doc["w1"]        = g_w1;
  doc["w2"]        = g_w2;
  doc["theta"]     = g_theta;
  doc["crop_type"] = g_cropType;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handlePostSettings() {
  sendCors();
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"error\":\"invalid json\"}");
    return;
  }
  if (doc["w1"].is<float>())         g_w1       = doc["w1"].as<float>();
  if (doc["w2"].is<float>())         g_w2       = doc["w2"].as<float>();
  if (doc["theta"].is<float>())      g_theta    = doc["theta"].as<float>();
  if (doc["crop_type"].is<const char*>()) g_cropType = doc["crop_type"].as<String>();

  Serial.printf("[FL] Settings updated: w1=%.2f w2=%.2f theta=%.2f crop=%s\n",
    g_w1, g_w2, g_theta, g_cropType.c_str());

  server.send(200, "application/json", "{\"ok\":true}");
}

// ─── SETUP ───────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[FL] ======================================");
  Serial.println("[FL] FarmLens Firmware v1.0 — Phase 1 Mock");
  Serial.printf( "[FL] Node: %s\n", NODE_ID);
  Serial.println("[FL] ======================================");

  pinMode(LED_PIN, OUTPUT);

  // 2 slow blinks = booting
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_PIN, HIGH); delay(300);
    digitalWrite(LED_PIN, LOW);  delay(300);
  }

  // Connect to WiFi
  Serial.printf("[FL] Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));  // fast blink while connecting
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[FL] WiFi FAILED — restarting in 5s...");
    delay(5000);
    ESP.restart();
  }

  digitalWrite(LED_PIN, HIGH);  // solid on = connected
  Serial.println("[FL] ✓ WiFi connected");
  Serial.printf( "[FL] IP:  %s\n", WiFi.localIP().toString().c_str());
  Serial.printf( "[FL] MAC: %s\n", WiFi.macAddress().c_str());
  Serial.println("[FL] ┌─────────────────────────────────────┐");
  Serial.println("[FL] │  Enter this in the app:             │");
  Serial.printf( "[FL] │  http://%-28s │\n", WiFi.localIP().toString().c_str());
  Serial.println("[FL] │  Port: 80                           │");
  Serial.println("[FL] └─────────────────────────────────────┘");

  // Register routes
  server.on("/api/status",   HTTP_GET,     handleStatus);
  server.on("/api/live",     HTTP_GET,     handleLive);
  server.on("/api/logs",     HTTP_GET,     handleLogs);
  server.on("/api/settings", HTTP_GET,     handleGetSettings);
  server.on("/api/settings", HTTP_POST,    handlePostSettings);
  // Preflight OPTIONS for every endpoint
  server.on("/api/status",   HTTP_OPTIONS, handleOptions);
  server.on("/api/live",     HTTP_OPTIONS, handleOptions);
  server.on("/api/logs",     HTTP_OPTIONS, handleOptions);
  server.on("/api/settings", HTTP_OPTIONS, handleOptions);

  server.begin();
  Serial.println("[FL] HTTP server started on port 80");
  Serial.println("[FL] Generating first data cycle...");

  // Generate initial data immediately so /api/live is populated on first poll
  updateMockData();
}

// ─── LOOP ────────────────────────────────────────────────────────────────────

void loop() {
  server.handleClient();

  // Update mock data every 30 seconds
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 30000UL) {
    updateMockData();
    lastUpdate = millis();
  }

  // Heartbeat: quick LED flash every 5 seconds (solid on between flashes)
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink >= 5000UL) {
    digitalWrite(LED_PIN, LOW);  delay(50);
    digitalWrite(LED_PIN, HIGH);
    lastBlink = millis();
  }
}