/*
 * =========================================================================================
 * SirenLink — EV / Smart Vehicle Unit Firmware (ESP32)
 * =========================================================================================
 * 
 * Hardware Description:
 *  - Microcontroller : ESP32 Dev Module (WROOM-32)
 *  - LoRa Module     : SX1278 (433MHz) or SX1276 (868/915MHz) SPI Receiver
 *  - Display         : 0.96" I2C OLED (SSD1306 128x64) - SDA=GPIO 21, SCL=GPIO 22
 *  - In-Cabin Buzzer : Active Piezo Buzzer on GPIO 25
 *  - Status LEDs     : Left Alert LED (GPIO 12), Right Alert LED (GPIO 14), Passing (GPIO 27)
 *  - GPS Module (Opt): NEO-6M on Serial2 (RX=16, TX=17) OR fixed vehicle coordinates
 *  - Ack Button      : "Given Way / Moved" Feedback Button on GPIO 4
 * 
 * Functions:
 *  1. Continuously listens for peer-to-peer LoRa packets from Ambulance Unit.
 *  2. Evaluates distance (Haversine formula) & relative heading bearing.
 *  3. Computes clear, actionable driver preemption instructions:
 *     - 🟢 STANDBY
 *     - ⬅️ MOVE LEFT (Left LED + OLED arrow + 10s Buzzer)
 *     - ➡️ MOVE RIGHT (Right LED + OLED arrow + 10s Buzzer)
 *     - 🚨 AMBULANCE PASSING (Rapid Strobe)
 *     - ✅ ROAD CLEARED / THANK YOU (Green status)
 *  4. Physical "Given Way" Button stops buzzer immediately and acknowledges clearance.
 * =========================================================================================
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
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
#define LORA_RST     15    // RESET
#define LORA_DIO0    2     // IRQ (Interrupt)
#define LORA_BAND    433E6 // 433E6 for Asia/Europe, 868E6 or 915E6 for US

// --- OLED Display (SSD1306 128x64 I2C) ---
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C // Usually 0x3C or 0x3D
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Directional LEDs & Audio ---
#define LED_LEFT_PIN     12  // Amber/Red Left Indicator
#define LED_RIGHT_PIN    14  // Amber/Red Right Indicator
#define LED_CLEAR_PIN    27  // Green Clearance Indicator
#define BUZZER_PIN       25  // In-Cabin Alert Buzzer
#define GIVEN_WAY_BTN    4   // "I Gave Way" Push Button (Active LOW)

// --- GPS Serial2 Pins ---
#define GPS_RX_PIN       16
#define GPS_TX_PIN       17
#define GPS_BAUDRATE     9600

// --- Vehicle Identification & Static / Live Coordinates ---
const char* VEHICLE_ID = "EV-VEH01";
double myLat = 19.0765; // Live or pre-set EV location
double myLng = 72.8785;
float  myHeading = 90.0;

// =========================================================================================
// 2. ENUMS & STATE VARIABLES
// =========================================================================================
enum AlertState {
  STATE_STANDBY,
  STATE_MOVE_LEFT,
  STATE_MOVE_RIGHT,
  STATE_PASSING,
  STATE_ROAD_CLEARED,
  STATE_THANK_YOU
};

AlertState currentState = STATE_STANDBY;
AlertState lastState    = STATE_STANDBY;

TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

// Alert Tracking
unsigned long alertStartTime = 0;
bool buzzerActive = false;
bool userGaveWay = false;
unsigned long lastBuzzerTone = 0;
bool toneHigh = false;
double lastAmbDist = 9999.0;
String activeAmbulanceId = "";

// =========================================================================================
// 3. MATH UTILITIES (Haversine & Bearing)
// =========================================================================================

double toRadians(double degree) {
  return degree * (PI / 180.0);
}

double toDegrees(double radian) {
  return radian * (180.0 / PI);
}

double calculateHaversineDistance(double lat1, double lon1, double lat2, double lon2) {
  double R = 6371000.0; // Earth radius in meters
  double dLat = toRadians(lat2 - lat1);
  double dLon = toRadians(lon2 - lon1);
  double a = sin(dLat / 2.0) * sin(dLat / 2.0) +
             cos(toRadians(lat1)) * cos(toRadians(lat2)) *
             sin(dLon / 2.0) * sin(dLon / 2.0);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return R * c; // Distance in meters
}

double calculateBearing(double lat1, double lon1, double lat2, double lon2) {
  double dLon = toRadians(lon2 - lon1);
  double y = sin(dLon) * cos(toRadians(lat2));
  double x = cos(toRadians(lat1)) * sin(toRadians(lat2)) -
             sin(toRadians(lat1)) * cos(toRadians(lat2)) * cos(dLon);
  double brng = toDegrees(atan2(y, x));
  return fmod((brng + 360.0), 360.0);
}

// =========================================================================================
// 4. SETUP
// =========================================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=============================================");
  Serial.println("🚗 SirenLink Smart EV Vehicle Unit Initializing...");
  Serial.println("=============================================");

  // Pin Configurations
  pinMode(LED_LEFT_PIN, OUTPUT);
  pinMode(LED_RIGHT_PIN, OUTPUT);
  pinMode(LED_CLEAR_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GIVEN_WAY_BTN, INPUT_PULLUP);

  digitalWrite(LED_LEFT_PIN, LOW);
  digitalWrite(LED_RIGHT_PIN, LOW);
  digitalWrite(LED_CLEAR_PIN, HIGH); // Green on Standby
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize OLED Display
  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("❌ ERROR: SSD1306 OLED allocation failed");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(15, 10);
    display.println("SIRENLINK SMART EV");
    display.setCursor(20, 30);
    display.println("LoRa Receiver Ready");
    display.setCursor(35, 48);
    display.println(VEHICLE_ID);
    display.display();
    delay(1500);
    Serial.println("📺 OLED Display Initialized (128x64)");
  }

  // Initialize GPS (Optional live GPS)
  gpsSerial.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  // Initialize LoRa Module (SPI)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("❌ ERROR: Starting LoRa Receiver failed! Check connections.");
    displayMessage("LoRa Init Error!", "Check Wiring");
  } else {
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    Serial.println("📻 LoRa SX1278 Receiver Ready @ 433MHz!");
    drawStandbyScreen();
  }
}

// =========================================================================================
// 5. MAIN LOOP
// =========================================================================================
void loop() {
  // 1. Read live GPS if available
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
  if (gps.location.isValid()) {
    myLat = gps.location.lat();
    myLng = gps.location.lng();
  }

  // 2. Check for Incoming LoRa Packets from Ambulance
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    processLoRaPacket(incoming);
  }

  // 3. Check "Given Way" Button
  checkGivenWayButton();

  // 4. Handle Alert Buzzer Audio Pattern (10-second timeout or stops on give way)
  handleBuzzerSound();

  // 5. Revert to Standby if ambulance passes out of range (> 600m or no packet for 6s)
  if (currentState != STATE_STANDBY && currentState != STATE_THANK_YOU) {
    if (millis() - alertStartTime > 15000 && lastAmbDist > 550.0) {
      currentState = STATE_STANDBY;
      userGaveWay = false;
      buzzerActive = false;
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_LEFT_PIN, LOW);
      digitalWrite(LED_RIGHT_PIN, LOW);
      digitalWrite(LED_CLEAR_PIN, HIGH);
      drawStandbyScreen();
    }
  }
}

// =========================================================================================
// 6. LORA PACKET PROCESSOR & DECISION ENGINE
// =========================================================================================

void processLoRaPacket(String payload) {
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("❌ JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  const char* ambId = doc["id"];
  int isEmergency   = doc["em"];
  double ambLat     = doc["lat"];
  double ambLng     = doc["lng"];
  int ambSpeed      = doc["spd"];
  int ambHeading    = doc["hdg"];

  activeAmbulanceId = String(ambId);

  // If Ambulance is NOT in Emergency Mode
  if (isEmergency == 0) {
    if (currentState != STATE_STANDBY) {
      currentState = STATE_ROAD_CLEARED;
      drawRoadClearedScreen();
    }
    return;
  }

  // Compute Distance between Ambulance and EV
  double distance = calculateHaversineDistance(ambLat, ambLng, myLat, myLng);
  lastAmbDist = distance;

  Serial.printf("📡 [LoRa RX from %s] Dist: %.1fm | Amb Hdg: %d°\n", ambId, distance, ambHeading);

  // --- SIRENLINK DECISION LOGIC ---
  if (distance > 500.0) {
    // Outside 500m alert zone
    if (currentState != STATE_STANDBY && currentState != STATE_THANK_YOU) {
      currentState = STATE_STANDBY;
      drawStandbyScreen();
    }
  }
  else if (distance > 50.0 && distance <= 500.0) {
    // Within 500m Zone -> Calculate Preemption Direction
    double bearingToVehicle = calculateBearing(ambLat, ambLng, myLat, myLng);
    double diff = fmod((bearingToVehicle - ambHeading + 360.0), 360.0);

    AlertState newState;
    if (diff >= 45.0 && diff < 180.0) {
      // Vehicle is to ambulance's RIGHT -> Vehicle moves LEFT
      newState = STATE_MOVE_LEFT;
    } else if (diff >= 180.0 && diff < 315.0) {
      // Vehicle is to ambulance's LEFT -> Vehicle moves RIGHT
      newState = STATE_MOVE_RIGHT;
    } else {
      // Directly ahead/behind -> Default to MOVE LEFT
      newState = STATE_MOVE_LEFT;
    }

    if (currentState != newState && !userGaveWay) {
      currentState = newState;
      alertStartTime = millis();
      buzzerActive = true; // Trigger 10-second buzzer
      Serial.printf("🚨 ALERT TRIGGERED: %s (Distance: %.1fm)\n", (currentState == STATE_MOVE_LEFT ? "MOVE LEFT" : "MOVE RIGHT"), distance);
      drawAlertScreen(currentState, distance);
    } else if (currentState == newState) {
      // Update distance on OLED
      drawAlertScreen(currentState, distance);
    }
  }
  else {
    // Critical Proximity <= 50m (Ambulance is passing)
    if (currentState != STATE_PASSING && !userGaveWay) {
      currentState = STATE_PASSING;
      drawPassingScreen(distance);
    }
  }
}

// =========================================================================================
// 7. DISPLAY SCREENS (OLED)
// =========================================================================================

void drawStandbyScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SirenLink | " + String(VEHICLE_ID));
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(20, 22);
  display.println("STANDBY");

  display.setTextSize(1);
  display.setCursor(15, 48);
  display.println("Corridor Normal");
  display.display();

  digitalWrite(LED_LEFT_PIN, LOW);
  digitalWrite(LED_RIGHT_PIN, LOW);
  digitalWrite(LED_CLEAR_PIN, HIGH);
}

void drawAlertScreen(AlertState state, double dist) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("EMERGENCY (%.0fm)", dist);
  display.drawLine(0, 10, 128, 10, SSD1306_WHITE);

  if (state == STATE_MOVE_LEFT) {
    display.setTextSize(2);
    display.setCursor(0, 20);
    display.println("<- MOVE");
    display.setCursor(45, 40);
    display.println("LEFT");

    digitalWrite(LED_LEFT_PIN, HIGH);
    digitalWrite(LED_RIGHT_PIN, LOW);
    digitalWrite(LED_CLEAR_PIN, LOW);
  } else if (state == STATE_MOVE_RIGHT) {
    display.setTextSize(2);
    display.setCursor(20, 20);
    display.println("MOVE ->");
    display.setCursor(30, 40);
    display.println("RIGHT");

    digitalWrite(LED_LEFT_PIN, LOW);
    digitalWrite(LED_RIGHT_PIN, HIGH);
    digitalWrite(LED_CLEAR_PIN, LOW);
  }
  display.display();
}

void drawPassingScreen(double dist) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 2);
  display.println("!! CRITICAL WARNING !!");
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(15, 22);
  display.println("AMBULANCE");
  display.setCursor(25, 42);
  display.println("PASSING");
  display.display();

  digitalWrite(LED_LEFT_PIN, HIGH);
  digitalWrite(LED_RIGHT_PIN, HIGH);
  digitalWrite(LED_CLEAR_PIN, LOW);
}

void drawThankYouScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(25, 5);
  display.println("THANK YOU!");
  display.drawLine(0, 15, 128, 15, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(10, 26);
  display.println("You gave way to save");
  display.setCursor(30, 40);
  display.println("a life! ❤️🚑");
  display.display();

  digitalWrite(LED_LEFT_PIN, LOW);
  digitalWrite(LED_RIGHT_PIN, LOW);
  digitalWrite(LED_CLEAR_PIN, HIGH);
}

void drawRoadClearedScreen() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(15, 10);
  display.println("CORRIDOR CLEARED");
  display.setTextSize(2);
  display.setCursor(10, 30);
  display.println("PROCEED OK");
  display.display();

  digitalWrite(LED_LEFT_PIN, LOW);
  digitalWrite(LED_RIGHT_PIN, LOW);
  digitalWrite(LED_CLEAR_PIN, HIGH);
  delay(2000);
  drawStandbyScreen();
}

void displayMessage(const char* line1, const char* line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 20);
  display.println(line1);
  display.setCursor(10, 40);
  display.println(line2);
  display.display();
}

// =========================================================================================
// 8. BUTTON & BUZZER CONTROLLERS
// =========================================================================================

void checkGivenWayButton() {
  if (digitalRead(GIVEN_WAY_BTN) == LOW) {
    delay(50); // Debounce
    if (digitalRead(GIVEN_WAY_BTN) == LOW) {
      if (currentState == STATE_MOVE_LEFT || currentState == STATE_MOVE_RIGHT || currentState == STATE_PASSING) {
        userGaveWay = true;
        buzzerActive = false;
        digitalWrite(BUZZER_PIN, LOW);
        currentState = STATE_THANK_YOU;
        Serial.println("✅ Driver Pressed 'I GAVE WAY' Button. Buzzer Muted.");
        drawThankYouScreen();
      }
    }
  }
}

void handleBuzzerSound() {
  // Buzzer plays dual-tone 10-second alarm while emergency is active
  if (buzzerActive) {
    if (millis() - alertStartTime >= 10000) { // 10-Second Max Duration
      buzzerActive = false;
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("🔕 10-Second Buzzer Timeout Reached.");
      return;
    }

    // Alternating Dual-Tone Alarm (850Hz & 650Hz cadence)
    if (millis() - lastBuzzerTone >= 150) {
      lastBuzzerTone = millis();
      toneHigh = !toneHigh;
      digitalWrite(BUZZER_PIN, toneHigh ? HIGH : LOW);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}
