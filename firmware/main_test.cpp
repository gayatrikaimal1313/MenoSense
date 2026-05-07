#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <ArduinoJson.h>

// --- WIFI SETTINGS ---
const char* WIFI_SSID = "Gerard";
const char* WIFI_PASSWORD = "hola12345";
const char* BACKEND_URL = "http://192.168.86.45:8000/api/data";

// Pins
#define SDA_PIN 4
#define SCL_PIN 5
#define ADC_PIN A0
#define LED_STATUS 15

#define SHT20_ADDR 0x40

WiFiClient wifiClient;

// --- CALIBRATION TABLE ---
struct CalPoint {
  float voltage;
  float conductance;
};

CalPoint calTable[] = {
  {0.050, 20.000},
  {0.360, 4.545},
  {0.380, 10.000},
  {0.530, 2.128},
  {0.590, 1.000},
  {0.650, 0.455},
  {0.653, 0.213},
  {0.662, 0.100}
};

const int calTableSize = sizeof(calTable) / sizeof(calTable[0]);

float voltageToConductance(float voltage) {
  if (voltage <= calTable[0].voltage) return calTable[0].conductance;
  if (voltage >= calTable[calTableSize - 1].voltage) return calTable[calTableSize - 1].conductance;

  for (int i = 0; i < calTableSize - 1; i++) {
    if (voltage >= calTable[i].voltage && voltage <= calTable[i + 1].voltage) {
      float v1 = calTable[i].voltage;
      float c1 = calTable[i].conductance;
      float v2 = calTable[i + 1].voltage;
      float c2 = calTable[i + 1].conductance;
      return c1 + (voltage - v1) * (c2 - c1) / (v2 - v1);
    }
  }
  return 0.0;
}

// --- SHT20 ---
float readTemperature() {
  Wire.beginTransmission(SHT20_ADDR);
  Wire.write(0xF3);
  Wire.endTransmission();
  delay(85);

  Wire.requestFrom(SHT20_ADDR, 3);
  if (Wire.available() < 2) return NAN;

  uint16_t raw = Wire.read() << 8;
  raw |= Wire.read();
  raw &= 0xFFFC;

  return -46.85 + 175.72 * ((float)raw / 65536.0);
}

float readHumidity() {
  Wire.beginTransmission(SHT20_ADDR);
  Wire.write(0xF5);
  Wire.endTransmission();
  delay(30);

  Wire.requestFrom(SHT20_ADDR, 3);
  if (Wire.available() < 2) return NAN;

  uint16_t raw = Wire.read() << 8;
  raw |= Wire.read();
  raw &= 0xFFFC;

  return -6.0 + 125.0 * ((float)raw / 65536.0);
}

// --- WIFI ---
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 40) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    Serial.print("ESP IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_STATUS, HIGH);
  } else {
    Serial.println("\nFAILED");
    digitalWrite(LED_STATUS, LOW);
  }
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_STATUS, OUTPUT);
  digitalWrite(LED_STATUS, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);

  float testTemp = readTemperature();
  if (!isnan(testTemp)) {
    Serial.printf("SHT20 OK: %.2f C\n", testTemp);
  } else {
    Serial.println("SHT20 FAIL");
  }

  connectWiFi();
}

// --- LOOP ---
unsigned long lastPost = 0;

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  float temp = readTemperature();
  float humidity = readHumidity();

  int rawAdc = analogRead(ADC_PIN);
  float voltage = (rawAdc / 1023.0) * 3.3;

  bool edaAvailable = rawAdc > 50;
  float conductance = edaAvailable ? voltageToConductance(voltage) : 0.0;

  // SERIAL OUTPUT
  Serial.println("--- SENSOR DATA ---");
  Serial.printf("Temp: %.2f | Hum: %.2f\n", temp, humidity);
  Serial.printf("ADC: %d (%.3fV)\n", rawAdc, voltage);

  if (edaAvailable) {
    Serial.printf("Conductance: %.3f uS\n", conductance);
  } else {
    Serial.println("EDA: Unavailable");
  }

  Serial.println("-------------------");

  // POST DATA
  if (millis() - lastPost > 2000 && WiFi.status() == WL_CONNECTED) {
    lastPost = millis();

    HTTPClient http;
    http.begin(wifiClient, BACKEND_URL);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;

    // FIXED SECTION
    if (edaAvailable) {
      doc["conductance"] = conductance;
    } else {
      doc["conductance"] = nullptr;
    }

    if (isnan(temp)) {
      doc["temperature"] = nullptr;
    } else {
      doc["temperature"] = temp;
    }

    if (isnan(humidity)) {
      doc["humidity"] = nullptr;
    } else {
      doc["humidity"] = humidity;
    }

    doc["batteryVoltage"] = 3.7;
    doc["wifiConnected"] = true;
    doc["edaAvailable"] = edaAvailable;

    String body;
    serializeJson(doc, body);

    int code = http.POST(body);

    Serial.println(body);
    Serial.printf("POST: %d\n", code);

    http.end();
  }
}