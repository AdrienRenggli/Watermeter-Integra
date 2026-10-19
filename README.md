# Integra Aquastream NFC Water Meter Reader

This project uses an ESP32 and an RFID-RC522 module to continuously read live water consumption data from an **Integra Aquastream** smart water meter. It extracts the volume and timestamp from the meter's internal Mifare Ultralight NFC tag and publishes it to Home Assistant via MQTT.

## Features
*   **Live Volume Reading:** Extracts the exact total water consumption (in m³) directly from the meter's memory.
*   **Flow Rate Calculation:** Estimates the real-time flow rate (L/min) based on periodic readings.
*   **Home Assistant Auto-Discovery:** Automatically creates an "Aquastream Meter" device in Home Assistant with grouped entities.
*   **Energy Dashboard Compatible:** The volume sensor is pre-configured with the correct `state_class` and `device_class` to work out-of-the-box with the Home Assistant Energy dashboard.
*   **Continuous Polling:** Uses a hardware reset trick to continuously read the passive NFC tag without it going to sleep permanently.

## Hardware Requirements
1.  **ESP32 Development Board** (e.g., ESP32-WROOM-32)
2.  **RFID-RC522 Module** (13.56 MHz)
3.  **Integra Aquastream** Water Meter

## Wiring Diagram
Connect the RC522 to the ESP32 using the standard VSPI pins.

**⚠️ WARNING:** Do not connect the RC522 to a 5V pin. It requires exactly 3.3V.

![Wiring Diagram](resources/Wiring%20diagram.png)

| RC522 Pin | ESP32 Pin | Note |
| :--- | :--- | :--- |
| **3.3V** | 3V3 | Power |
| **RST** | GPIO 22 | Reset |
| **GND** | GND | Ground |
| **IRQ** | - | *Leave Disconnected* |
| **MISO** | GPIO 19 | SPI MISO |
| **MOSI** | GPIO 23 | SPI MOSI |
| **SCK** | GPIO 18 | SPI Clock |
| **SDA** | GPIO 5 | SPI SS/CS |

## 3D Printed Mount
To ensure reliable readings, the RC522 antenna must be held tightly against the NFC logo on the white plastic housing of the Aquastream meter. 

You can find a custom-designed support bracket in the `CAD` folder. The folder contains a `.step` file that you can import into your slicer or CAD software of choice to 3D print a permanent mounting solution.

## Software Setup
This project is built using PlatformIO.

1. Clone this repository and open it in VS Code with the PlatformIO extension.
2. In `src/main.cpp`, update the configuration variables at the top of the file:
   ```cpp
   const char* WIFI_SSID = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   const char* MQTT_SERVER = "192.168.1.XXX"; 
   const char* MQTT_USER = "YOUR_MQTT_USER";
   const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";
   ```
3. Build and upload to your ESP32.

### Dependencies
The `platformio.ini` automatically installs the following required libraries:
*   `miguelbalboa/MFRC522`
*   `knolleary/PubSubClient`
*   `bblanchon/ArduinoJson`

## Home Assistant Integration
Once the ESP32 connects to your WiFi and MQTT broker, it will automatically send configuration payloads to Home Assistant. 
Navigate to **Settings > Devices & Services > MQTT > Devices**, and you will find a new device named **Aquastream Meter** containing the following sensors:
*   Water Volume (m³)
*   Flow Rate (L/min)
*   Last Meter Sync (Timestamp)
*   Uptime (s)

## AI Disclosure
*Note: AI assistance was used for troubleshooting and documentation.*