#include <WiFi.h>
#include <WiFiMulti.h>
#include <FirebaseESP32.h>
#include <OneWire.h>
#include <DallasTemperature.h>

WiFiMulti wifiMulti;

// Firebase Credentials
#define DATABASE_URL "https://tapasplaza-3d4de-default-rtdb.europe-west1.firebasedatabase.app"
#define DATABASE_SECRET "AIzaSyAwTsBGgQcTSyj2RgFCwApSiXYRgGUSCdI"

// Sensor Settings
#define SENSOR_ID "prueba"
#define SENSOR_PIN 4 

OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long lastLogTime = 0;
const long logInterval = 5000; 

void setup() {
  Serial.begin(115200);

  // WiFi Networks
  wifiMulti.addAP("DIGIFIBRA-Zs44", "NieuwWW8");
  wifiMulti.addAP("Tapas Plaza", "fanta123");
  wifiMulti.addAP("Pixel 6 Pro Jaume", "1234cinco");

  Serial.print("Connecting to WiFi");
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected!");

  // Firebase Setup
  config.database_url = DATABASE_URL;
  config.signer.tokens.legacy_token = DATABASE_SECRET;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  sensors.begin();
}

void loop() {
  // Always check WiFi status
  if (wifiMulti.run() != WL_CONNECTED) {
    return; 
  }

  if (millis() - lastLogTime >= logInterval || lastLogTime == 0) {
    lastLogTime = millis();

    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);

    if (tempC != DEVICE_DISCONNECTED_C) {
      Serial.printf("Temp: %.2f\n", tempC);

      // 1. Update LIVE DATA
      // Path: sensors/prueba/current
      Firebase.setFloat(fbdo, "sensors/" + String(SENSOR_ID) + "/current", tempC);
      
      // 2. Update LIVE TIMESTAMP (Server-side)
      // Path: sensors/prueba/last_updated
      Firebase.setTimestamp(fbdo, "sensors/" + String(SENSOR_ID) + "/last_updated");

      // 3. Log HISTORY with Server Timestamp
      // We use a JSON object to store both the temp and the server time together
      FirebaseJson json;
      json.add("temp", tempC);
      json.set("timestamp/.sv", "timestamp"); // Firebase special syntax for server time

      // Use 'pushJSON' to create a unique ID for every entry automatically
      if (Firebase.pushJSON(fbdo, "logs/" + String(SENSOR_ID), json)) {
        Serial.println("Push successful");
      } else {
        Serial.println("Push failed: " + fbdo.errorReason());
      }

    } else {
      Serial.println("Error: Sensor disconnected");
    }
  }
}