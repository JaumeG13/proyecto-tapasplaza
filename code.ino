#include <WiFi.h>
#include <WiFiMulti.h>
#include <FirebaseESP32.h>
#include <OneWire.h>              // Para los sensores de temperatura
#include <DallasTemperature.h>    // Para los sensores de temperatura
#include <HTTPClient.h>           // Para mensajes de telegram

WiFiMulti wifiMulti;

// Credenciales Firebase
#define DATABASE_URL "https://tapasplaza-3d4de-default-rtdb.europe-west1.firebasedatabase.app"
#define API_KEY "AIzaSyAwTsBGgQcTSyj2RgFCwApSiXYRgGUSCdI" 
#define USER_EMAIL "esp32tapasplaza@gmail.com"
#define USER_PASSWORD "3$p32burriana"

// API CallMeBot Telegram 
#define TELEGRAM_API_KEY "LTUyODcxMjU0MTk"

// Estructura para emparejar los ID dinámicos con nombres y límite de temperatura
struct AlertZone {
  String sensorId;       // Coincide los ID generados automáticamente (ej. "sensor_3A1FC2")
  String friendlyName;   // Nombre para mensaje de telegram (ej. "Kitchen Pizza")
  float maxTemperature;  // Temperatura límite para la alerta
  bool alertActive;      // Tiempo de espera para evitar spam
  unsigned long lastSeenMillis; // 👈 NEW: Tracks the exact time this sensor successfully reported data
  bool offlineAlertActive;      // 👈 NEW: Latch to prevent spamming your group when a sensor stays offline
};

// Aquí se asigna el nombre desde el ID de cada sensor, y su temperatura límite: -5 para congeladores y 7 para frigoríficos.
AlertZone zones[] = {
  {"sensor_00000B", "Ambient dishwashers",  50.0, false, 0, false},
  {"sensor_0000B1", "White freezer",    -5.0, false, 0, false},
  {"sensor_00000A", "White fridge",     12.0, false, 0, false},
  {"sensor_000083", "Kitchen pizza",    12.0, false, 0, false},
  {"sensor_00001E", "Kitchen big",      12.0, false, 0, false},
  {"sensor_0000A8", "Kitchen small",    12.0, false, 0, false},
  {"sensor_0000BF", "Ambient bar",      50.0, false, 0, false}
};
const int totalZones = sizeof(zones) / sizeof(zones[0]);

// Pin de datos donde se conecta el sensor
#define SENSOR_PIN 4 

OneWire oneWire(SENSOR_PIN);
DallasTemperature sensors(&oneWire);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long lastLogTime = 0;
const long logInterval = 60000; // Actualiza cada 60 segundos

// Función para convertir la dirección física a cadena de caracteres
String getSensorID(DeviceAddress deviceAddress) {
  char idBuffer[10];
  // Saca los 3 últimos bytes
  sprintf(idBuffer, "%02X%02X%02X", deviceAddress[5], deviceAddress[6], deviceAddress[7]);
  return "sensor_" + String(idBuffer);
}

// Funciones para la mensajería de Telegram
// Mensaje alerta temperatura
void sendTelegramAlert(String name, float currentTemp, float limit) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Bloque del mensaje
    String message = "[" + name + "] temperature is high!\n";
    message += "Current: " + String(currentTemp, 1) + "°C\n";

    // Convierte espacios a '+' y símbolos especiales a formato URL
    message.replace(" ", "+");
    message.replace("\n", "%0A");
    message.replace(":", "%3A");

    String url = "https://api.callmebot.com/telegram/group.php?apikey=" + String(TELEGRAM_API_KEY) + "&text=" + message;

    Serial.println(" -> Attempting to dispatch Telegram notification...");
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      Serial.printf(" -> Telegram response received: %d\n", httpCode);
    } else {
      Serial.println(" -> Telegram transmission error.");
    }
    http.end();
  }
}

// Mensaje alerta offline
void sendTelegramOfflineAlert(String name) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    // Bloque del mensaje para sensor desconectado
    String message = "[" + name + "] is offline!";

    // Convierte espacios a '+' y símbolos especiales a formato URL
    message.replace(" ", "+");
    message.replace("\n", "%0A");
    message.replace(":", "%3A");

    String url = "https://api.callmebot.com/telegram/group.php?apikey=" + String(TELEGRAM_API_KEY) + "&text=" + message;

    Serial.println(" -> Attempting to dispatch Telegram offline notification...");
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      Serial.printf(" -> Telegram offline response received: %d\n", httpCode);
    } else {
      Serial.println(" -> Telegram offline transmission error.");
    }
    http.end();
  }
}

// Escanea la temperatura y la compara con los límites asignados en las líneas 28-35
void checkAlerts(String id, float currentTemp) {
  for (int i = 0; i < totalZones; i++) {
    if (zones[i].sensorId == id) {
      // Condición 1: Comprueba si ha superado el límite
      if (currentTemp > zones[i].maxTemperature) {
        if (!zones[i].alertActive) {
          sendTelegramAlert(zones[i].friendlyName, currentTemp, zones[i].maxTemperature);
          zones[i].alertActive = true; // Bloquea el alarma para no espamear el grupo de telegram
        }
      } 
      // Condición 2: Resetea el alarma cuando baja 1°C debajo del límite
      else if (currentTemp < (zones[i].maxTemperature - 1.0)) {
        if (zones[i].alertActive) {
          Serial.printf(" -> Bounds restored for %s. Alarms re-armed.\n", zones[i].friendlyName.c_str());
          zones[i].alertActive = false; // Resetear alarma
        }
      }
      break; // Rompe el bucle for una vez se ha encontrado el ID.
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Redes WiFi, para facilitar la fase de prueba
  wifiMulti.addAP("DIGIFIBRA-Zs44", "NieuwWW8");       // Red de mi casa
  wifiMulti.addAP("Tapas Plaza", "fanta123");          // Red del restaurante
  wifiMulti.addAP("Pixel 6 Pro Jaume", "1234cinco");   // Hotspot de mi teléfono

  Serial.print("Connecting to WiFi");
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nConnected!");

  // Configuración Firebase
  config.database_url = DATABASE_URL;
  
  // Autenticación para escritura a Firebase usando las credenciales de las líneas 11-14
  config.api_key = API_KEY;            
  auth.user.email = USER_EMAIL;         
  auth.user.password = USER_PASSWORD;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  sensors.begin();
}

void loop() {
  // Comprobar estado de la conexión WiFi
  if (wifiMulti.run() != WL_CONNECTED) {
    return; 
  }

  if (millis() - lastLogTime >= logInterval || lastLogTime == 0) {
    lastLogTime = millis();

    // Renueva el token de autenticación de Firebase
    if (Firebase.isTokenExpired()) {
      Firebase.refreshToken(&config);
      Serial.println("[System] Firebase session token expired. Generating a fresh token...");
    }

    sensors.requestTemperatures();
    int deviceCount = sensors.getDeviceCount();
    
    Serial.printf("\n--- Found %d sensors on the wire ---\n", deviceCount);

    // Busca todos los sensores
    for (int i = 0; i < deviceCount; i++) {
      DeviceAddress currentDeviceAddress;
      
      // Consigue cada dirección física  en índice 'i'
      if (sensors.getAddress(currentDeviceAddress, i)) {
        
        // Generar SENSOR_ID de forma dinámica basado en su dirección física
        String dynamicSensorID = getSensorID(currentDeviceAddress);
        float tempC = sensors.getTempC(currentDeviceAddress);

        if (tempC != DEVICE_DISCONNECTED_C) {
          Serial.printf("ID: %s | Temp: %.2f°C\n", dynamicSensorID.c_str(), tempC);

          checkAlerts(dynamicSensorID, tempC);

          // Actualizar datos en vivo con ID único
          Firebase.setFloat(fbdo, "sensors/" + dynamicSensorID + "/current", tempC);
          
          // Consigue marca de tiempo
          Firebase.setTimestamp(fbdo, "sensors/" + dynamicSensorID + "/last_updated");

          // Añade datos al log con marca de tiempo
          FirebaseJson json;
          json.add("temp", tempC);
          json.set("timestamp/.sv", "timestamp");

          if (Firebase.pushJSON(fbdo, "logs/" + dynamicSensorID, json)) {
            Serial.println(" -> Logged successfully.");
          } else {
            Serial.println(" -> Firebase Error: " + fbdo.errorReason());
          }
        } else {
          Serial.printf("Error: Sensor index %d disconnected during read\n", i);
        }
      } else {
        Serial.printf("Error: Could not read physical address for sensor index %d\n", i);
      }
    }
  }

  // Registra si algún sensor ha dejado de funcionar por más de 15 minutos
  unsigned long fifteenMinutes = 15 * 60 * 1000; // 900,000 milliseconds
  
  for (int i = 0; i < totalZones; i++) {
    // Comprueba si el sensor ha estado encendido suficiente tiempo para establecer un historial
    if (millis() > fifteenMinutes && (millis() - zones[i].lastSeenMillis > fifteenMinutes)) {
      if (!zones[i].offlineAlertActive) {
        sendTelegramOfflineAlert(zones[i].friendlyName);
        zones[i].offlineAlertActive = true; // Bloquear la alerta para que no haga spam
        Serial.printf("⚠️ Warning: %s has gone offline!\n", zones[i].friendlyName.c_str());
      }
    }
  }
}