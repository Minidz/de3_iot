#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

// ================= WIFI & MQTT =================
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""

#define MQTT_SERVER   "broker.emqx.io"
#define MQTT_PORT     1883
#define MQTT_SUB_TOPIC "esp32/traffic/sign"
// publish topics for individual sensors
#define MQTT_PUB_TOPIC_LUX "esp32/sensor/lux"
#define MQTT_PUB_TOPIC_TEMP "esp32/sensor/temp"  // example additional sensor

// ================= HARDWARE ====================
#define LDR_PIN 34
LiquidCrystal_I2C lcd(0x27, 20, 4);
WiFiClient espClient;
PubSubClient client(espClient);

// ================= GLOBALS =====================
char currentAlert[21] = "No Alert";
unsigned long lastLuxUpdate = 0;
unsigned long alertStartTime = 0;

// ================= MQTT CALLBACK ================
void callback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<200> doc;
  deserializeJson(doc, payload, length);

  const char* name = doc["name"];
  // Format: "Alert: [Sign Name]"
  snprintf(currentAlert, sizeof(currentAlert), "Sign: %s", name);
  alertStartTime = millis();
  
  // Update line 4 immediately
  lcd.setCursor(0, 3);
  lcd.print("                    "); // Clear line
  lcd.setCursor(0, 3);
  lcd.print(currentAlert);
}

// ================= FUNCTIONS ====================
void updateWiFiStatus() {
  lcd.setCursor(0, 0);
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi: OK ");
    lcd.print(WiFi.localIP().toString().substring(0, 10)); // Show part of IP
  } else {
    lcd.print("WiFi: Disconnected");
  }
}

void updateMQTTStatus() {
  lcd.setCursor(0, 1);
  if (client.connected()) {
    lcd.print("MQTT: Connected   ");
  } else {
    lcd.print("MQTT: Connecting...");
  }
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  updateWiFiStatus();
}

void connectMQTT() {
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  if (!client.connected()) {
    if (client.connect("ESP32_AIoT_Client")) {
      client.subscribe(MQTT_SUB_TOPIC);
    }
  }
  updateMQTTStatus();
}

float readLux() {
  int adcValue = analogRead(LDR_PIN);
  return (adcValue * 3.3 / 4095.0) * 1000;
}

String classifyLux(float lux) {
  if (lux < 10) return "Rat toi";
  if (lux < 50) return "Anh sang yeu";
  if (lux < 500) return "Anh sang mo";
  if (lux < 1000) return "Tu nhien";
  return "Manh";
}

// helper that publishes a float value as a string to a given topic
void publishSensor(const char* topic, float value) {
  if (client.connected()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    client.publish(topic, buf);
  }
}

// ================= MAIN ====================
void setup() {
  pinMode(LDR_PIN, INPUT);
  lcd.init();
  lcd.backlight();
  
  lcd.setCursor(0, 0);
  lcd.print("AIoT System Start...");
  
  connectWiFi();
  connectMQTT();
}

void loop() {
  // 1. Maintain Connection
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  // 2. Persistent Status Lines (Every loop or less frequently)
  updateWiFiStatus();
  updateMQTTStatus();

  // 3. Line 3: Sensor Data (Every 1s)
  if (millis() - lastLuxUpdate > 1000) {
    float lux = readLux();
    String level = classifyLux(lux);

    lcd.setCursor(0, 2);
    lcd.print("Lux:");
    lcd.print(lux, 0);
    lcd.print(" ");
    lcd.print(level);
    lcd.print("      "); // Clear remaining

    // publish each measurement to its own topic
    publishSensor(MQTT_PUB_TOPIC_LUX, lux);
    // if you had other sensors you would call publishSensor with different topics
    // float temp = readTemp();
    // publishSensor(MQTT_PUB_TOPIC_TEMP, temp);

    lastLuxUpdate = millis();
  }

  // 4. Line 4: Alert Timeout logic (Clear after 10s if needed)
  if (alertStartTime > 0 && (millis() - alertStartTime > 10000)) {
    strcpy(currentAlert, "No Alert");
    alertStartTime = 0;
    lcd.setCursor(0, 3);
    lcd.print("                    ");
    lcd.setCursor(0, 3);
    lcd.print(currentAlert);
  } else if (alertStartTime == 0) {
    lcd.setCursor(0, 3);
    lcd.print("No Alert            ");
  }
}
