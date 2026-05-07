#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <ArduinoJson.h>

// --- SETTINGS ---
const char* AP_SSID_SETUP = "MenoSense_Setup";
const char* BACKEND_URL = "http://192.168.8.156:8000/api/data";

// Pins
#define SDA_PIN 4
#define SCL_PIN 5
#define ADC_PIN A0
#define LED_STATUS 15

#define SHT20_ADDR 0x40

ESP8266WebServer server(80);
WiFiClient wifiClient;

bool apMode = false;

struct CalPoint {
    float voltage;
    float conductance;
};

CalPoint calTable[] = {
    {0.10, 20.0},
    {0.30, 10.0},
    {0.50, 5.0},
    {0.70, 2.0},
    {0.85, 1.0},
    {0.92, 0.5},
    {0.97, 0.2},
    {1.00, 0.1}
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

    return (-46.85 + 175.72 * ((float)raw / 65536.0)) + 3.0;
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

void handleRoot() {
    String html = "<html><body><h1>MenoSense WiFi Setup</h1>";
    html += "<form action='/configure' method='POST'>";
    html += "SSID: <input type='text' name='ssid'><br>";
    html += "Password: <input type='password' name='pass'><br>";
    html += "<input type='submit' value='Connect'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

void handleConfigure() {
    if (server.hasArg("ssid") && server.hasArg("pass")) {
        String ssid = server.arg("ssid");
        String pass = server.arg("pass");

        server.send(200, "text/plain", "Trying to connect. Check Serial Monitor.");
        delay(500);

        Serial.print("User entered SSID: ");
        Serial.println(ssid);

        WiFi.mode(WIFI_STA);
        WiFi.persistent(true);
        WiFi.disconnect(true);
        delay(1000);

        WiFi.begin(ssid.c_str(), pass.c_str());

        int retry = 0;
        while (WiFi.status() != WL_CONNECTED && retry < 40) {
            delay(500);
            Serial.print(".");
            retry++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi Connected! Restarting...");
            Serial.print("IP: ");
            Serial.println(WiFi.localIP());
            delay(1500);
            ESP.restart();
        } else {
            Serial.println("\nConnection failed. Please try again.");
            WiFi.mode(WIFI_AP);
            WiFi.softAP(AP_SSID_SETUP);
            server.begin();
            apMode = true;
        }
    } else {
        server.send(400, "text/plain", "Missing SSID or password.");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n\n--- MenoSense Hardware Init ---");

    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);

    Wire.begin(SDA_PIN, SCL_PIN);

    float testTemp = readTemperature();
    if (isnan(testTemp)) {
        Serial.println("SHT20: FAILED to detect!");
    } else {
        Serial.printf("SHT20: Detected. Current Temp: %.2f C\n", testTemp);
    }

    Serial.print("WiFi: Connecting to saved credentials");

    WiFi.mode(WIFI_STA);
    WiFi.begin();

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 30) {
        delay(500);
        Serial.print(".");
        retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi: Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
        digitalWrite(LED_STATUS, HIGH);
    } else {
        Serial.println("\nWiFi: Failed to connect. Starting AP Mode...");

        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID_SETUP);

        Serial.print("AP Name: ");
        Serial.println(AP_SSID_SETUP);
        Serial.print("AP IP: ");
        Serial.println(WiFi.softAPIP());

        server.on("/", handleRoot);
        server.on("/configure", HTTP_POST, handleConfigure);
        server.begin();

        apMode = true;
    }
}

unsigned long lastPost = 0;
const unsigned long INTERVAL = 2000;

void loop() {
    float temp = readTemperature();
    float humidity = readHumidity();

    int rawAdc = analogRead(ADC_PIN);
    float voltage = (rawAdc / 1023.0) * 3.3;

    bool edaAvailable = (voltage > 0.05 && voltage < 1.05);
    float rawConductance = edaAvailable ? voltageToConductance(voltage) : 0.0;

    // smoothing
    static float smoothedConductance = 0.0;
    float alpha = 0.1;

    if (edaAvailable) {
     smoothedConductance = alpha * rawConductance + (1 - alpha) * smoothedConductance;
    }

    float conductance = smoothedConductance;
    String sensorStatus = edaAvailable ? "full-sensor" : "temperature-only";

    static unsigned long lastSerial = 0;
    if (millis() - lastSerial >= 2000) {
        lastSerial = millis();

        Serial.println("--- SENSOR DATA ---");
        Serial.printf("Mode: %s | WiFi: %d\n", apMode ? "AP (Setup)" : "Station", WiFi.status());

        if (isnan(temp)) {
            Serial.println("SHT20: READ FAILURE");
        } else {
            Serial.printf("Temp: %.2f C | Humidity: %.2f %%\n", temp, humidity);
        }

        Serial.printf("ADC: %d (%.3f V)\n", rawAdc, voltage);

        if (edaAvailable) {
            Serial.printf("Conductance: %.3f uS\n", conductance);
        } else {
            Serial.println("EDA: Unavailable");
        }

        Serial.println("-------------------");
    }

    if (apMode) {
        server.handleClient();

        static unsigned long lastBlink = 0;
        if (millis() - lastBlink > 500) {
            digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
            lastBlink = millis();
        }

        return;
    }

    if (millis() - lastPost >= INTERVAL) {
        lastPost = millis();

        HTTPClient http;
        http.begin(wifiClient, BACKEND_URL);
        http.addHeader("Content-Type", "application/json");

        StaticJsonDocument<256> doc;

        if (edaAvailable) {
            doc["conductance"] = conductance;
        } else {
            doc["conductance"] = nullptr;
        }

        doc["temperature"] = isnan(temp) ? 0.0 : temp;
        doc["humidity"] = isnan(humidity) ? 0.0 : humidity;
        doc["batteryVoltage"] = 3.7;
        doc["wifiConnected"] = true;
        doc["edaAvailable"] = edaAvailable;
        doc["sensorStatus"] = sensorStatus;

        String body;
        doc["adc"] = rawAdc;
        doc["voltage"] = voltage;
        serializeJson(doc, body);

        int code = http.POST(body);

        Serial.printf("POST Result: %d\n\n", code);

        http.end();

        if (code == 200) {
            digitalWrite(LED_STATUS, LOW);
            delay(50);
            digitalWrite(LED_STATUS, HIGH);
        }
    }
}