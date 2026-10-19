#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>

// --- Configuration ---
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_SERVER = "192.168.1.XXX";
const int   MQTT_PORT = 1883;
const char* MQTT_USER = "YOUR_MQTT_USER";
const char* MQTT_PASSWORD = "YOUR_MQTT_PASSWORD";

// --- Hardware Pins ---
#define RST_PIN 22
#define SS_PIN 5

// --- MQTT Topics ---
const char* STATE_TOPIC = "homeassistant/sensor/aquastream_water/state";
const char* CONFIG_TOPIC = "homeassistant/sensor/aquastream_water/config";

MFRC522 mfrc522(SS_PIN, RST_PIN);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- State Variables ---
float lastVolume = -1.0;
unsigned long lastReadTime = 0;

void setupWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  
  // Initialize NTP Time (Swiss Timezone CET/CEST)
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org");
}

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect("ESP32_WaterMeter", MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("connected.");
      
      // Shared Device JSON to group all sensors into one physical device in HA
      String deviceJson = "\"device\": {\"identifiers\": [\"aquastream_nfc\"], \"name\": \"Aquastream Meter\", \"manufacturer\": \"Integra\"}";
      
      // 1. Volume Sensor (Energy Dashboard compatible)
      String volConfig = "{\"name\": \"Water Volume\", \"state_topic\": \"" + String(STATE_TOPIC) + "\", \"unit_of_measurement\": \"m³\", \"device_class\": \"water\", \"state_class\": \"total_increasing\", \"value_template\": \"{{ value_json.volume_m3 }}\", \"unique_id\": \"aqua_vol_01\", " + deviceJson + "}";
      mqttClient.publish("homeassistant/sensor/aquastream_volume/config", volConfig.c_str(), true);

      // 2. Flow Rate Sensor
      String flowConfig = "{\"name\": \"Flow Rate\", \"state_topic\": \"" + String(STATE_TOPIC) + "\", \"unit_of_measurement\": \"L/min\", \"icon\": \"mdi:water-pump\", \"value_template\": \"{{ value_json.flow_rate_lpm }}\", \"unique_id\": \"aqua_flow_01\", " + deviceJson + "}";
      mqttClient.publish("homeassistant/sensor/aquastream_flow/config", flowConfig.c_str(), true);

      // 3. Uptime Sensor (Diagnostic)
      String uptimeConfig = "{\"name\": \"Uptime\", \"state_topic\": \"" + String(STATE_TOPIC) + "\", \"unit_of_measurement\": \"s\", \"entity_category\": \"diagnostic\", \"value_template\": \"{{ value_json.uptime_s }}\", \"unique_id\": \"aqua_up_01\", " + deviceJson + "}";
      mqttClient.publish("homeassistant/sensor/aquastream_uptime/config", uptimeConfig.c_str(), true);
      
      // 4. Meter Timestamp
      String timeConfig = "{\"name\": \"Last Meter Sync\", \"state_topic\": \"" + String(STATE_TOPIC) + "\", \"icon\": \"mdi:clock-outline\", \"value_template\": \"{{ value_json.meter_timestamp }}\", \"unique_id\": \"aqua_time_01\", " + deviceJson + "}";
      mqttClient.publish("homeassistant/sensor/aquastream_time/config", timeConfig.c_str(), true);

    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  Serial.println("Booting...");
  
  setupWiFi();
  
  mqttClient.setBufferSize(1024); // Ensure JSON fits in buffer
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  
  SPI.begin();
  mfrc522.PCD_Init();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  
  Serial.println("System ready. Waiting for NFC tag...");
}

void loop() {
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  
  Serial.println("Looking for tag...");
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    delay(1000);
    return;
  }

  Serial.println("Tag found!");
  byte buffer[18];
  byte size = sizeof(buffer);

  if (mfrc522.MIFARE_Read(14, buffer, &size) == MFRC522::STATUS_OK) {
    // Read water meter (m3)
    uint32_t liters = ((uint32_t)buffer[3] << 24) | ((uint32_t)buffer[2] << 16) | 
                      ((uint32_t)buffer[1] << 8)  | (uint32_t)buffer[0];
                      
    float currentVolume = liters / 1000.0;
    
    // Read Pages 6 (Date) and 7 (Time) for Meter Timestamp
    char meterTime[20] = "Unknown";
    byte dateBuf[18], timeBuf[18];
    byte sizeDate = sizeof(dateBuf);
    byte sizeTime = sizeof(timeBuf);
    
    if (mfrc522.MIFARE_Read(6, dateBuf, &sizeDate) == MFRC522::STATUS_OK && 
        mfrc522.MIFARE_Read(7, timeBuf, &sizeTime) == MFRC522::STATUS_OK) {
      sprintf(meterTime, "%02X%02X-%02X-%02X %02X:%02X", 
              dateBuf[0], dateBuf[1], dateBuf[2], dateBuf[3], timeBuf[0], timeBuf[1]);
    }

    // Calculate Flow Rate
    float flowRateLPM = 0.0;
    unsigned long currentMillis = millis();
    if (lastVolume >= 0) {
      float deltaLiters = (currentVolume - lastVolume) * 1000.0;
      float deltaMinutes = (currentMillis - lastReadTime) / 60000.0;
      if (deltaMinutes > 0) {
        flowRateLPM = deltaLiters / deltaMinutes;
      }
    }
    lastVolume = currentVolume;
    lastReadTime = currentMillis;

    // Get Current ESP32 Time
    struct tm timeinfo;
    char espTime[30] = "Syncing...";
    if (getLocalTime(&timeinfo, 10)) {
      strftime(espTime, sizeof(espTime), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    // Construct JSON Payload
    StaticJsonDocument<256> doc;
    doc["volume_m3"] = currentVolume;
    doc["flow_rate_lpm"] = flowRateLPM;
    doc["meter_timestamp"] = meterTime;
    doc["esp_time"] = espTime;
    doc["uptime_s"] = millis() / 1000;

    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);
    
    Serial.println(jsonBuffer);
    mqttClient.publish(STATE_TOPIC, jsonBuffer);
    
    Serial.println("Putting the tag to sleep.");
    mfrc522.PICC_HaltA(); // Put tag to sleep
    delay(10000);         // Wait 10 seconds before next reading
    
    // Soft reset the reader hardware after the delay. 
    // This allows the high-speed loop to detect the resting tag again!
    Serial.println("Resetting the reader...");
    mfrc522.PCD_Init(); 
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
    
  } else {
    // Collision occurred. Halt and quick reset to try again on the next fast loop.
    Serial.println("An error occured, trying again...");
    mfrc522.PICC_HaltA();
    delay(500);
    mfrc522.PCD_Init();
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
  }
}