# SirenLink — Hybrid Emergency Vehicle Communication System
## Hardware Integration, Pinout Diagrams, and Firmware Reference Manual

This documentation provides comprehensive details for the physical and software integration of **SirenLink: A Hybrid Emergency Vehicle Communication System**.

---

## 1. System Topology Overview

```
                               ┌────────────────────────────────────────┐
                               │       SirenLink FastAPI Backend        │
                               │  (WebSockets / Cloud Central Server)   │
                               └───────┬────────────────────────┬───────┘
                                       │                        │
                     Wi-Fi / Cellular  │                        │  WebSockets (Browser)
                                       │                        │
                                       ▼                        ▼
    ┌──────────────────────────────────────┐        ┌─────────────────────────┐
    │       Ambulance Unit (ESP32)         │        │ Hospital Web Dashboard  │
    │  - Neo-6M GPS Module                 │        │ - Live Distance / ETA   │
    │  - SX1278 LoRa Transmitter (433MHz)  │        │ - 500m / 100m Proximity │
    │  - Emergency Button & Strobe LED     │        │ - Arrival Trauma Bay Chime
    └──────────────────┬───────────────────┘        └─────────────────────────┘
                       │
                       │ Direct Peer-to-Peer LoRa RF (No Internet Needed)
                       │
                       ▼
    ┌─────────────────────────────────────────────────────────────────────────┐
    │                              VEHICLE UNITS                              │
    │                                                                         │
    │  [Method A] Smart / EV Vehicles         [Method B] Legacy Vehicles      │
    │  Hardware: ESP32 + LoRa + OLED          Software: SirenLink Mobile Web  │
    │  - SX1278 LoRa Receiver (433MHz)        - Responsive Web App / GPS      │
    │  - 0.96" I2C SSD1306 OLED Display       - Web Audio 10s Dual-Tone Buzzer│
    │  - Directional LEDs (Left / Right)      - "Did you give way?" Feedback  │
    │  - 10-Second Acoustic Buzzer            - Real-time Leaflet.js Map      │
    │  - Physical "I Gave Way" Push Button                                    │
    └─────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Unit 1: Ambulance Hardware Unit

### 2.1 Component Specifications
1. **Microcontroller**: ESP32 Dev Board (ESP-WROOM-32, 38-Pin / 30-Pin)
2. **LoRa Transceiver**: Semtech SX1278 Ra-02 (433 MHz SPI)
3. **GPS Receiver**: u-blox NEO-6M GPS Module (with ceramic patch antenna)
4. **Emergency Trigger**: Momentary Push Button Switch
5. **Acoustic Indicator**: 5V Active Piezo Buzzer
6. **Visual Warning**: Ultra-Bright 5mm Red LED with 220Ω resistor

---

### 2.2 Complete Wiring Pinout Table (Ambulance Unit)

| Component | Component Pin | ESP32 Pin | Logic / Protocol | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **SX1278 LoRa** | VCC | **3.3V** | Power | **DO NOT connect to 5V (3.3V max)** |
| | GND | **GND** | Ground | Common system ground |
| | SCK | **GPIO 18** | SPI Clock | Hardware SPI |
| | MISO | **GPIO 19** | SPI Master In | Hardware SPI |
| | MOSI | **GPIO 23** | SPI Master Out | Hardware SPI |
| | NSS (CS) | **GPIO 5** | SPI Chip Select | Configured in code |
| | RST | **GPIO 14** | Reset | Configured in code |
| | DIO0 | **GPIO 2** | Interrupt / IRQ | Packet receive trigger |
| **NEO-6M GPS** | VCC | **VIN (5V) / 3.3V** | Power | Compatible with 3.3V-5V |
| | GND | **GND** | Ground | Common system ground |
| | TX | **GPIO 16** (RX2) | UART RX2 | Receives NMEA stream at 9600 baud |
| | RX | **GPIO 17** (TX2) | UART TX2 | (Optional config) |
| **Emergency Button**| Pin 1 | **GPIO 4** | Digital Input | Internal `INPUT_PULLUP` enabled |
| | Pin 2 | **GND** | Ground | Pressing connects pin to GND |
| **Piezo Buzzer** | (+) Anode | **GPIO 25** | Digital Output | Active buzzer (HIGH = Sound ON) |
| | (-) Cathode | **GND** | Ground | Common ground |
| **Siren Red LED** | (+) Anode (Long) | **GPIO 13** | Digital Output | Through 220Ω resistor |
| | (-) Cathode (Short)| **GND** | Ground | Common ground |

---

### 2.3 Ambulance Unit Circuit Schematic Diagram

```
                              +-------------------------+
                              |    ESP32 Dev Module     |
                              +-------------------------+
                                 |   |   |   |   |   |
                  +--------------+   |   |   |   |   +---------------+
                  | 3.3V             |   |   |   |              GND  |
                  |                  |   |   |   |                   |
  +---------------+-------+          |   |   |   |                   |
  |  SX1278 LoRa Module   |          |   |   |   |                   |
  |  VCC  <---------------+          |   |   |   |                   |
  |  GND  -----------------------------------------------------------+
  |  SCK  <--------------------------+ GPIO 18 (SCK)                 |
  |  MISO -------------------------------+ GPIO 19 (MISO)            |
  |  MOSI <----------------------------------+ GPIO 23 (MOSI)        |
  |  NSS  <--------------------------------------+ GPIO 5  (CS)      |
  |  RST  <------------------------------------------+ GPIO 14 (RST) |
  |  DIO0 -----------------------------------------------+ GPIO 2    |
  +-----------------------+                                          |
                                                                     |
  +-----------------------+                                          |
  |  NEO-6M GPS Module    |                                          |
  |  VCC  <---------------+ 3.3V / 5V                                |
  |  GND  -----------------------------------------------------------+
  |  TX   ---------------------------+ GPIO 16 (RX2)                 |
  |  RX   <------------------------------+ GPIO 17 (TX2)             |
  +-----------------------+              |                           |
                                         |                           |
  +-----------------------+              |                           |
  |  Emergency Button     |              |                           |
  |  Terminal 1 <------------------------+ GPIO 4 (INPUT_PULLUP)     |
  |  Terminal 2 -----------------------------------------------------+
  +-----------------------+              |                           |
                                         |                           |
  +-----------------------+              |                           |
  |  Siren Red LED (5mm)  |              |                           |
  |  (+) Anode  <---[220Ω]---------------+ GPIO 13                   |
  |  (-) Cathode ----------------------------------------------------+
  +-----------------------+              |                           |
                                         |                           |
  +-----------------------+              |                           |
  |  Piezo Buzzer (5V)    |              |                           |
  |  (+) Anode  <------------------------+ GPIO 25                   |
  |  (-) Cathode ----------------------------------------------------+
  +-----------------------+
```

---

## 3. Unit 2: Smart / EV Vehicle Hardware Unit

### 3.1 Component Specifications
1. **Microcontroller**: ESP32 Dev Board (ESP-WROOM-32)
2. **LoRa Receiver**: Semtech SX1278 Ra-02 (433 MHz SPI)
3. **In-Cabin Display**: 0.96" Monochrome I2C OLED (SSD1306, 128×64 resolution)
4. **Directional Indicators**: 1× Amber/Red Left LED (GPIO 12), 1× Amber/Red Right LED (GPIO 14), 1× Green Clearance LED (GPIO 27)
5. **In-Cabin Alert Siren**: Active Piezo Buzzer (GPIO 25)
6. **Feedback Button**: Momentary Tactile Switch (GPIO 4) labeled "I Gave Way"

---

### 3.2 Complete Wiring Pinout Table (Smart EV Unit)

| Component | Component Pin | ESP32 Pin | Logic / Protocol | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **SX1278 LoRa** | VCC | **3.3V** | Power | **DO NOT connect to 5V** |
| | GND | **GND** | Ground | Common system ground |
| | SCK | **GPIO 18** | SPI Clock | Hardware SPI |
| | MISO | **GPIO 19** | SPI Master In | Hardware SPI |
| | MOSI | **GPIO 23** | SPI Master Out | Hardware SPI |
| | NSS (CS) | **GPIO 5** | SPI Chip Select | Chip Select |
| | RST | **GPIO 15** | Reset | Reset pin |
| | DIO0 | **GPIO 2** | Interrupt / IRQ | Trigger for incoming packet |
| **SSD1306 OLED** | VCC | **3.3V** | Power | 3.3V / 5V tolerant |
| | GND | **GND** | Ground | Common system ground |
| | SCL | **GPIO 22** | I2C Clock | Default I2C SCL |
| | SDA | **GPIO 21** | I2C Data | Default I2C SDA |
| **Left Alert LED** | (+) Anode | **GPIO 12** | Digital Output | Through 220Ω resistor |
| | (-) Cathode | **GND** | Ground | Common ground |
| **Right Alert LED**| (+) Anode | **GPIO 14** | Digital Output | Through 220Ω resistor |
| | (-) Cathode | **GND** | Ground | Common ground |
| **Clearance LED** | (+) Anode (Green)| **GPIO 27** | Digital Output | Through 220Ω resistor |
| | (-) Cathode | **GND** | Ground | Common ground |
| **In-Cabin Buzzer** | (+) Anode | **GPIO 25** | Digital Output | Active buzzer (HIGH = Sound ON) |
| | (-) Cathode | **GND** | Ground | Common ground |
| **Given Way Button**| Pin 1 | **GPIO 4** | Digital Input | Internal `INPUT_PULLUP` enabled |
| | Pin 2 | **GND** | Ground | Pressing mutes buzzer & thanks driver |

---

### 3.3 Smart EV Vehicle Circuit Schematic Diagram

```
                              +-------------------------+
                              |    ESP32 Dev Module     |
                              +-------------------------+
                                 |   |   |   |   |   |
                  +--------------+   |   |   |   |   +---------------+
                  | 3.3V             |   |   |   |              GND  |
                  |                  |   |   |   |                   |
  +---------------+-------+          |   |   |   |                   |
  |  SX1278 LoRa Module   |          |   |   |   |                   |
  |  VCC  <---------------+          |   |   |   |                   |
  |  GND  -----------------------------------------------------------+
  |  SCK  <--------------------------+ GPIO 18 (SCK)                 |
  |  MISO -------------------------------+ GPIO 19 (MISO)            |
  |  MOSI <----------------------------------+ GPIO 23 (MOSI)        |
  |  NSS  <--------------------------------------+ GPIO 5  (CS)      |
  |  RST  <------------------------------------------+ GPIO 15 (RST) |
  |  DIO0 -----------------------------------------------+ GPIO 2    |
  +-----------------------+                                          |
                                                                     |
  +-----------------------+                                          |
  |  0.96" I2C OLED       |                                          |
  |  VCC  <---------------+ 3.3V                                     |
  |  GND  -----------------------------------------------------------+
  |  SDA  <--------------------------+ GPIO 21 (I2C SDA)             |
  |  SCL  <------------------------------+ GPIO 22 (I2C SCL)         |
  +-----------------------+              |                           |
                                         |                           |
  +-----------------------+              |                           |
  |  Left Alert LED (Red) |              |                           |
  |  (+) Anode  <---[220Ω]---------------+ GPIO 12                   |
  |  (-) Cathode ----------------------------------------------------+
  +-----------------------+              |                           |
                                         |                           |
  +-----------------------+              |                           |
  | Right Alert LED (Red) |              |                           |
  |  (+) Anode  <---[220Ω]---------------+ GPIO 14                   |
  |  (-) Cathode ----------------------------------------------------+
  +-----------------------+              |                           |
                                         |                           |
  +-----------------------+              |                           |
  | Clearance LED (Green) |              |                           |
  |  (+) Anode  <---[220Ω]---------------+ GPIO 27                   |
  |  (-) Cathode ----------------------------------------------------+
  +-----------------------+              |                           |
                                         |                           |
  +-----------------------+              |                           |
  | In-Cabin Piezo Buzzer |              |                           |
  |  (+) Anode  <------------------------+ GPIO 25                   |
  |  (-) Cathode ----------------------------------------------------+
  +-----------------------+              |                           |
                                         |                           |
  +-----------------------+              |                           |
  | "I Gave Way" Button   |              |                           |
  |  Terminal 1 <------------------------+ GPIO 4 (INPUT_PULLUP)     |
  |  Terminal 2 -----------------------------------------------------+
  +-----------------------+
```

---

## 4. Required Arduino IDE Libraries

Before uploading the `.ino` firmware files to the ESP32 boards, install the following official libraries via **Arduino IDE -> Tools -> Manage Libraries...**:

1. **LoRa** by Sandeep Mistry (Version `0.8.0` or higher) — SPI interface for Semtech SX1276/77/78/79 modules.
2. **TinyGPSPlus** by Mikal Hart (Version `1.0.3` or higher) — NMEA GPS parser.
3. **Adafruit SSD1306** by Adafruit (Version `2.5.7` or higher) — Hardware driver for OLED display.
4. **Adafruit GFX Library** by Adafruit (Version `1.11.5` or higher) — Core graphics primitives.
5. **ArduinoJson** by Benoît Blanchon (Version `6.21.3` or Version `7.x`) — Compact JSON serialization/deserialization.

---

## 5. System Operation & Execution Flow

### Step 1: Normal Standby Mode
* **Ambulance**: GPS streams live location. LoRa transmits heartbeat packets every 2 seconds. Emergency mode is `OFF`.
* **Smart EV**: OLED displays `"Corridor Normal | STANDBY"`. Green clearance LED is lit. Buzzer is silent.
* **Hospital Dashboard**: Live distance counter shows `--`. Status is `STANDBY`.

### Step 2: Emergency Triggered
* **Ambulance**: Driver presses the emergency toggle button (GPIO 4). The red siren LED strobes and buzzer sounds. LoRa broadcasts high-frequency packets every 250ms with `em: 1`.
* **Telemetry**: Location is pushed to the SirenLink FastAPI cloud backend.

### Step 3: Peer-to-Peer EV Preemption (< 500 Meters)
* When an approaching ambulance enters the **500m radius**, the Smart EV's ESP32 computes the relative angle and distance using the Haversine formula and bearing calculation:
  * **If vehicle is on the right of the ambulance path**: The left directional LED lights up, OLED shows `<- MOVE LEFT`, and the 10-second buzzer sounds.
  * **If vehicle is on the left of the ambulance path**: The right directional LED lights up, OLED shows `MOVE RIGHT ->`, and the 10-second buzzer sounds.

### Step 4: Driver Interaction & Acoustic Muting
* **Given Way**: The driver moves their vehicle and presses the `"I Gave Way"` tactile button (GPIO 4).
* The 10-second buzzer **mutes immediately**. The OLED displays a warm confirmation: `"THANK YOU! You helped save a life! ❤️🚑"`.

### Step 5: Hospital Preparation & Proximity Alerts
* At **500m**, the hospital dashboard triggers an automated notification banner and plays an ascending audio chime.
* At **100m**, an urgent preparation alert sounds.
* At **< 30m**, the full-screen `"AMBULANCE ARRIVED"` overlay activates, confirming bay readiness.
