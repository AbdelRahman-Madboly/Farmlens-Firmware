# FarmLens Firmware — Phase 1

Mock sensor + AI detection data served over WiFi HTTP REST API.
The Flutter app polls `/api/live` every 5 seconds.

---

## Quick Start

1. **Edit credentials** in `src/main.cpp`:
   ```cpp
   const char* WIFI_SSID = "YOUR_SSID";
   const char* WIFI_PASS = "YOUR_PASSWORD";
   ```

2. **Flash:**
   ```bash
   pio run --target upload
   ```

3. **Monitor serial output:**
   ```bash
   pio device monitor
   ```

4. **Note the IP address** printed at boot:
   ```
   [FL] IP: 192.168.1.XXX
   ```

5. **Enter that IP in the Flutter app**, port 80, tap Connect.

---

## API Endpoints

| Method | Path            | Description                          |
|--------|-----------------|--------------------------------------|
| GET    | /api/status     | Node info (uptime, heap, mode)       |
| GET    | /api/live       | Current sensor + detection reading   |
| GET    | /api/logs       | Last N cycles (`?limit=50`)          |
| GET    | /api/settings   | Fusion weights and crop type         |
| POST   | /api/settings   | Update weights (JSON body)           |

All responses include `Access-Control-Allow-Origin: *`.

### Sample `/api/live` response
```json
{
  "ts": 1712345678,
  "node_id": "FL-001",
  "moisture_raw": 2341,
  "moisture_pct": 42.3,
  "water_raw": 1820,
  "water_pct": 44.5,
  "moisture_stress": 0,
  "water_stress": 0,
  "cs": 0.0,
  "cv": 0.72,
  "ccombined": 0.43,
  "alert": false,
  "detection_class": "Tomato_healthy",
  "detection_conf": 0.72,
  "cycle_id": "FL-001-00045-30"
}
```

---

## Mock Data Behaviour

### Sensor simulation
- **Moisture** drifts 20%–80% on a ~5 minute sine wave
- **Water level** drifts independently on a ~4 minute sine wave
- **Stress flags** fire when moisture < 30% or water < 20%
- **Cs** = 0.6 × moisture_stress + 0.4 × water_stress

### AI detection simulation
- **Cv** performs a random walk (±0.07 per 30 s cycle, clamped 0.10–0.95)
- **Detection class** = disease when Cv > 0.60, otherwise `Tomato_healthy`
- Disease cycles through: `Tomato_Late_blight` → `Tomato_Early_blight` → `Strawberry_Leaf_scorch` → `Pepper_Bacterial_spot`

### Fusion
```
Ccombined = w1 × Cv + w2 × Cs   (defaults: w1=0.6, w2=0.4)
Alert     = Ccombined > theta    (default theta=0.5)
```

### Log buffer
- Circular buffer of last 100 cycles in RAM (no flash in Phase 1)
- New entry every 30 seconds
- `/api/logs` returns newest-first

---

## LED Patterns

| Pattern              | Meaning                  |
|----------------------|--------------------------|
| 2 slow blinks        | Booting                  |
| Fast alternating     | Connecting to WiFi       |
| Solid on             | WiFi connected           |
| Quick flash every 5s | Alive heartbeat          |

---

## Hardware

- **Board:** LOLIN S2 Mini (ESP32-S2)
- **Framework:** Arduino via PlatformIO
- **LED pin:** GPIO 15 (built-in)
- **No physical sensors** in Phase 1 — all data is mathematically generated

---

## Phase 2 Notes

In Phase 2 this firmware moves to the Raspberry Pi side. The ESP32 becomes a
UART sensor bridge only, forwarding real ADC readings to the Pi, which then
runs the AI inference and serves the same REST API the app already knows.