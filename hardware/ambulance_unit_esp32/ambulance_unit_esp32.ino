/*
 * =========================================================================================
 * SirenLink — Ambulance Unit Firmware (ESP32)
 * =========================================================================================
 * 
 * Hardware Description:
 *  - Microcontroller : ESP32 Dev Module (WROOM-32)
 *  - LoRa Module     : SX1278 (433MHz) or SX1276 (868/915MHz) SPI
 *  - GPS Module      : u-blox NEO-6M / NEO-8M (Hardware Serial 2: RX2=GPIO 16, TX2=GPIO 17)
 *  - Emergency Input : Push Button on GPIO 4 (Active LOW with internal pull-up / external pull-down)
 *  - Visual Output   : LED on GPIO 2 (Builtin) & GPIO 13 (High-brightness Red Siren LED)
 *  - Acoustic Output : Active Piezo Buzzer on GPIO 25
 *  - Cloud Uplink    : WiFi + HTTP / WebSocket to SirenLink Backend
 * 
 * Functions:
 *  1. Reads live GPS coordinates (Lat, Lng, Speed, Heading) via TinyGPS++.
 *  2. Listens for Emergency Toggle button press with debouncing.
 *  3. Broadcasts direct peer-to-peer LoRa packets to nearby EV / Smart Vehicle units.
 *  4. Streams live telemetry to the SirenLink central FastAPI backend server via WiFi.
 * =========================================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <LoRa.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

// =========================================================================================
// 1. PIN DEFINITIONS & CONSTANTS
// =========================================================================================

// --- LoRa SX1278 SPI Pins ---
#define LORA_SCK     18
#define LORA_MISO    19
#define LORA_MOSI    23
#define LORA_SS      5     // NSS / CS
#define LORA_RST     14    // RESET
#define LORA_DIO0    26    // IRQ (Interrupt) — moved from GPIO2 to avoid LED conflict
#define LORA_BAND    433E6 // 433E6 for Asia/Europe, 868E6 or 915E6 for US

// --- GPS Module (NEO-6M) Serial2 Pins ---
#define GPS_RX_PIN   16    // ESP32 RX2 connects to GPS TX
#define GPS_TX_PIN   17    // ESP32 TX2 connects to GPS RX
#define GPS_BAUDRATE 9600

// --- User Interface Pins ---
#define EMERGENCY_BTN_PIN  4   // Tactile push button (Active LOW with internal PULLUP)
#define SIREN_LED_PIN      13  // External bright Red Emergency LED
#define BUZZER_PIN         25  // Active Piezo Buzzer
#define STATUS_LED_PIN     2   // Built-in ESP32 LED

// --- Identification & Config ---
const char* AMBULANCE_ID = "AMB-A01";
const char* WIFI_SSID     = "Ray";     // Change to your Wi-Fi SSID
const char* WIFI_PASSWORD = "987654321"; // Change to your Wi-Fi Password
const char* SERVER_URL    = "https://sirenlink.onrender.com/api/esp32/update"; // SirenLink backend API

// =========================================================================================
// 2. GLOBAL OBJECTS & VARIABLES
// =========================================================================================
TinyGPSPlus gps;
HardwareSerial gpsSerial(2); // Hardware UART 2

bool emergencyActive = false;
bool lastButtonState = HIGH;  // Global — will be set to actual pin state in setup()
unsigned long lastButtonPress = 0;
unsigned long lastLoRaBroadcast = 0;
unsigned long lastCloudSync = 0;
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

// Fallback / Simulated coordinates if GPS has no indoor fix
double currentLat = 19.0760;
double currentLng = 72.8777;
float  currentSpeed = 45.0; // km/h
float  currentHeading = 90.0;

// =========================================================================================
// 3. SETUP
// =========================================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=============================================");
  Serial.println("🚑 SirenLink Ambulance Unit Initializing...");
  Serial.println("=============================================");

  // Configure GPIOs
  pinMode(EMERGENCY_BTN_PIN, INPUT_PULLUP);
  pinMode(SIREN_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);

  digitalWrite(SIREN_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(STATUS_LED_PIN, LOW);

  // Read ACTUAL button pin state at boot — prevents fake falling edge detection
  delay(50); // Short settle delay for pin to stabilize
  lastButtonState = digitalRead(EMERGENCY_BTN_PIN);
  Serial.printf("🔘 Button pin at boot: %s\n", lastButtonState == HIGH ? "HIGH (not pressed)" : "LOW (check wiring!)");

  // Initialize GPS Serial
  gpsSerial.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("📡 GPS UART2 initialized on Pins RX=16, TX=17");

  // Initialize LoRa Module (SPI)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("❌ ERROR: Starting LoRa module failed! Check wiring.");
  } else {
    LoRa.setTxPower(20);          // Max power (20dBm) for extended range
    LoRa.setSpreadingFactor(7);   // SF7 for low latency & ~1-2km range in urban setting
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    Serial.println("📻 LoRa SX1278 initialized @ 433MHz!");
  }

  // Connect to Wi-Fi for Cloud telemetry (if available)
  connectWiFi();

  // Beep twice to confirm readiness
  beepTone(2, 100);
  Serial.println("✅ Ambulance Unit Ready. Press Emergency Button to start.");
}

// =========================================================================================
// 4. MAIN LOOP
// =========================================================================================
void loop() {
  // 1. Read incoming GPS data continuously
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  // Update location values if GPS has valid fix
  if (gps.location.isValid()) {
    currentLat = gps.location.lat();
    currentLng = gps.location.lng();
    currentSpeed = gps.speed.kmph();
    currentHeading = gps.course.deg();
  }

  // 2. Check Emergency Toggle Button (Debounced)
  checkButton();

  // 3. Handle Siren, Strobe LED, and Buzzer
  handleSirenAndAlarm();

  // 4. LoRa Direct Broadcast (Every 250ms when Emergency Active, or every 2s for Heartbeat)
  unsigned long loraInterval = emergencyActive ? 250 : 2000;
  if (millis() - lastLoRaBroadcast >= loraInterval) {
    lastLoRaBroadcast = millis();
    broadcastLoRaPacket();
  }

  // 5. Cloud Telemetry Uplink (Every 1000ms over Wi-Fi)
  if (millis() - lastCloudSync >= 1000) {
    lastCloudSync = millis();
    sendCloudTelemetry();
  }
}

// =========================================================================================
// 5. HELPER FUNCTIONS
// =========================================================================================

void checkButton() {
  // Ignore button during first 2 seconds after boot (prevents false trigger on startup)
  if (millis() < 2000) return;

  bool currentState = digitalRead(EMERGENCY_BTN_PIN);

  // Only trigger on FALLING EDGE (HIGH → LOW), not while held down
  if (currentState == LOW && lastButtonState == HIGH) {
    if (millis() - lastButtonPress > 800) { // 800ms debounce window
      lastButtonPress = millis();
      emergencyActive = !emergencyActive;

      Serial.println("\n---------------------------------------------");
      if (emergencyActive) {
        Serial.println("🚨 EMERGENCY MODE ACTIVATED!");
        beepTone(3, 150);
      } else {
        Serial.println("🟢 Emergency Mode Deactivated (STANDBY).");
        digitalWrite(SIREN_LED_PIN, LOW);
        digitalWrite(BUZZER_PIN, LOW);
        beepTone(1, 400);
      }
      Serial.println("---------------------------------------------\n");
    }
  }

  lastButtonState = currentState; // Save state for next loop
}

void handleSirenAndAlarm() {
  if (emergencyActive) {
    // Strobe siren LED and buzzer alternating every 120ms
    if (millis() - lastBuzzerToggle >= 120) {
      lastBuzzerToggle = millis();
      buzzerState = !buzzerState;
      digitalWrite(SIREN_LED_PIN, buzzerState ? HIGH : LOW);
      digitalWrite(STATUS_LED_PIN, buzzerState ? HIGH : LOW);
      digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
    }
  } else {
    digitalWrite(SIREN_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(STATUS_LED_PIN, (WiFi.status() == WL_CONNECTED) ? HIGH : LOW);
  }
}

void broadcastLoRaPacket() {
  // Construct compact JSON / String payload for high-speed transmission
  StaticJsonDocument<128> doc;
  doc["id"]     = AMBULANCE_ID;
  doc["em"]     = emergencyActive ? 1 : 0;
  doc["lat"]    = currentLat;
  doc["lng"]    = currentLng;
  doc["spd"]    = (int)currentSpeed;
  doc["hdg"]    = (int)currentHeading;

  String payload;
  serializeJson(doc, payload);

  // Transmit over LoRa RF
  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  if (emergencyActive) {
    Serial.print("📡 [LoRa TX] -> ");
    Serial.println(payload);
  }
}

void sendCloudTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> doc;
  doc["device_id"]      = AMBULANCE_ID;
  doc["latitude"]       = currentLat;
  doc["longitude"]      = currentLng;
  doc["speed"]          = currentSpeed;
  doc["heading"]        = currentHeading;
  doc["buzzer_status"]  = emergencyActive;

  String reqBody;
  serializeJson(doc, reqBody);

  int httpResponseCode = http.POST(reqBody);
  if (httpResponseCode > 0) {
    // Telemetry successfully pushed to SirenLink backend
  } else {
    Serial.printf("⚠️ Cloud Sync Failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  http.end();
}

void connectWiFi() {
  if (String(WIFI_SSID) == "YOUR_WIFI_SSID") {
    Serial.println("ℹ️ WiFi Credentials not configured. Running in Standalone LoRa Mode.");
    return;
  }
  Serial.printf("Connecting to Wi-Fi: %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 15) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n🌐 Wi-Fi Connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n⚠️ Wi-Fi timeout. Continuing in LoRa-only mode.");
  }
}

void beepTone(int count, int delayMs) {
  for (int i = 0; i < count; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(SIREN_LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(SIREN_LED_PIN, LOW);
    if (i < count - 1) delay(delayMs);
  }
}
