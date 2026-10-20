# SirenLink — Hybrid Emergency Vehicle Communication System

**SirenLink** is an intelligent, low-latency **Hybrid Emergency Vehicle Communication System (EVCS)** designed to clear transit corridors for emergency response vehicles in real time.

SirenLink combines **Direct Hardware-to-Hardware LoRa RF Communication** (for modern/EV connected vehicles) with a **Cloud-connected Mobile Web Application** (for older/non-smart vehicles), while streaming real-time dispatch telemetry directly to the **Hospital Web Dashboard**.

---

## 🌟 Hybrid Architecture Overview

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
                       │ Direct Peer-to-Peer LoRa RF (Sub-10ms, No Cellular Needed)
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

## 🚀 Quick Start (Software Simulation)

1. **Install Python 3.10+**.
2. **Navigate to the backend and install dependencies**:
   ```bash
   cd backend
   pip install -r requirements.txt
   ```
3. **Start the FastAPI server**:
   ```bash
   uvicorn main:app --reload --port 8000
   ```
4. **Open in your browser**:
   ```
   http://localhost:8000
   ```
5. **Direct Dashboard Links**:
   - 🚑 **Ambulance Dashboard**: `http://localhost:8000/ambulance.html`
   - 🚗 **Vehicle Dashboard (Dynamic IDs)**: `http://localhost:8000/vehicle.html`
   - 🏥 **Hospital Dispatch**: `http://localhost:8000/hospital.html`

---

## 🛠️ Hardware Units & Pinout Reference

Complete firmware files and detailed wiring schematics are available in the [`hardware/`](file:///c:/Users/saravanan/OneDrive/Desktop/sirenlink/hardware/) folder.

### 1. Ambulance Unit (`hardware/ambulance_unit_esp32/`)
* **Microcontroller**: ESP32 WROOM-32
* **LoRa Transmitter**: SX1278 (433 MHz SPI)
* **GPS Module**: u-blox NEO-6M (Serial2: RX=GPIO 16, TX=GPIO 17)
* **Emergency Button**: GPIO 4 (Active LOW with internal pullup)
* **Buzzer**: GPIO 25
* **Siren LED**: GPIO 13 (via 220Ω resistor)

#### Ambulance Unit Wiring Table
| Component | Pin | ESP32 Pin |
| :--- | :--- | :--- |
| **SX1278 LoRa** | VCC / GND | **3.3V / GND** |
| | SCK / MISO / MOSI | **GPIO 18 / GPIO 19 / GPIO 23** |
| | NSS (CS) / RST / DIO0 | **GPIO 5 / GPIO 14 / GPIO 2** |
| **NEO-6M GPS** | VCC / GND | **3.3V (or 5V) / GND** |
| | TX / RX | **GPIO 16 (RX2) / GPIO 17 (TX2)** |
| **Emergency Button**| Terminal 1 / 2 | **GPIO 4 / GND** |
| **Piezo Buzzer** | (+) / (-) | **GPIO 25 / GND** |
| **Red Siren LED** | (+) / (-) | **GPIO 13 (220Ω) / GND** |

---

### 2. Smart / EV Vehicle Unit (`hardware/vehicle_unit_esp32/`)
* **Microcontroller**: ESP32 WROOM-32
* **LoRa Receiver**: SX1278 (433 MHz SPI)
* **Display**: 0.96" I2C SSD1306 OLED (SDA=GPIO 21, SCL=GPIO 22)
* **Directional LEDs**: Left (GPIO 12), Right (GPIO 14), Clearance (GPIO 27)
* **In-Cabin Buzzer**: GPIO 25 (10-second dual-tone alarm)
* **Given Way Button**: GPIO 4 (Active LOW)

#### Smart EV Unit Wiring Table
| Component | Pin | ESP32 Pin |
| :--- | :--- | :--- |
| **SX1278 LoRa** | VCC / GND | **3.3V / GND** |
| | SCK / MISO / MOSI | **GPIO 18 / GPIO 19 / GPIO 23** |
| | NSS (CS) / RST / DIO0 | **GPIO 5 / GPIO 15 / GPIO 2** |
| **0.96" OLED (I2C)**| VCC / GND | **3.3V / GND** |
| | SDA / SCL | **GPIO 21 / GPIO 22** |
| **Left Alert LED** | (+) / (-) | **GPIO 12 (220Ω) / GND** |
| **Right Alert LED**| (+) / (-) | **GPIO 14 (220Ω) / GND** |
| **Clearance LED** | (+) / (-) | **GPIO 27 (220Ω) / GND** |
| **In-Cabin Buzzer** | (+) / (-) | **GPIO 25 / GND** |
| **I Gave Way Button**| Terminal 1 / 2 | **GPIO 4 / GND** |

---

## 📦 Required Arduino IDE Libraries

Install these libraries from **Arduino IDE -> Tools -> Manage Libraries...**:
1. `LoRa` by Sandeep Mistry
2. `TinyGPSPlus` by Mikal Hart
3. `Adafruit SSD1306` by Adafruit
4. `Adafruit GFX Library` by Adafruit
5. `ArduinoJson` by Benoît Blanchon

---

## 📂 Project Structure

```
sirenlink/
├── backend/
│   ├── main.py          # FastAPI application, WebSocket hub, 5-state decision engine
│   ├── models.py        # SQLAlchemy database models
│   ├── database.py      # SQLite database connection
│   ├── auth.py          # JWT authentication
│   ├── schemas.py       # Pydantic schemas
│   └── requirements.txt # Python backend dependencies
├── frontend/
│   ├── index.html       # Landing page & Multi-tab launcher
│   ├── ambulance.html   # Live ambulance dashboard (OSRM routing, layers, vehicle tracking)
│   ├── vehicle.html     # Mobile/Civilian vehicle dashboard (7-state UI, 10s audio buzzer)
│   ├── hospital.html    # Hospital receiving dispatch (ETA countdown, proximity chimes)
│   ├── styles.css       # Unified dark clinical UI styles
│   └── app.js           # Single-page WebSocket client logic
├── hardware/
│   ├── HARDWARE_README.md                     # Complete hardware schematics & connection guide
│   ├── ambulance_unit_esp32/
│   │   └── ambulance_unit_esp32.ino           # Ambulance ESP32 Firmware (GPS + LoRa TX + Button + LED + Buzzer + WiFi)
│   └── vehicle_unit_esp32/
│       └── vehicle_unit_esp32.ino             # Smart EV ESP32 Firmware (LoRa RX + OLED + Directional LEDs + Buzzer + Button)
└── README.md            # Master Project Documentation
```

---

## 🎯 Real-World Workflow Summary

1. **Ambulance Activation**:
   - The driver triggers the emergency mode via hardware button or dashboard.
   - LoRa broadcasts emergency packets every 250ms; Wi-Fi pushes telemetry to the hospital backend.
2. **EV & Smart Vehicles (Hardware Direct)**:
   - Within **500 meters**, the EV's ESP32 computes relative bearing.
   - OLED shows `<- MOVE LEFT` or `MOVE RIGHT ->`, directional LEDs light up, and a 10-second buzzer sounds.
   - Driver presses `"I Gave Way"` $\rightarrow$ Buzzer mutes immediately and OLED shows `"THANK YOU!"`.
3. **Older Vehicles (Mobile App)**:
   - Civilian drivers on the web app receive an urgent popup, street auto-zoom, and 10-second acoustic alert with one-click `"Yes, I moved"` confirmation.
4. **Hospital Dispatch**:
   - Automated alerts trigger at **500m** and **100m** with ascending audio chimes, preparing emergency bays before arrival.
