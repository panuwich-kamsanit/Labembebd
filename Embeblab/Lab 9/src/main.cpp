#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Wire.h>

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Wi-Fi Credentials
constexpr char STA_SSID[] = "TP-Link_F952";
constexpr char STA_PASS[] = "12303465";
constexpr char AP_SSID[]  = "ESP32-LAB09C-6730300183";
constexpr char AP_PASS[]  = "12345678";

// MQTT Credentials
constexpr char MQTT_BROKER[] = "5e32f9c367654724b9e31f24aab8fb39.s1.eu.hivemq.cloud";
constexpr int  MQTT_PORT     = 8883;
constexpr char MQTT_USER[]   = "esp32";
constexpr char MQTT_PASS[]   = "12345678";

// MQTT Topics
constexpr char TOPIC_DATA[]   = "lab09/6730300183/data";
constexpr char TOPIC_CMD[]    = "lab09/6730300183/cmd";
constexpr char TOPIC_STATUS[] = "lab09/6730300183/status";

WebServer server(80);
WiFiClientSecure espClient;
PubSubClient mqtt(espClient);

uint32_t lastMqttPublish = 0;
constexpr uint32_t MQTT_PUBLISH_MS = 2000;
uint32_t lastMqttReconnectAttempt = 0;

// Pin Definitions
constexpr uint8_t POT_PIN = 36;
constexpr uint8_t LED_PIN = 5;
constexpr bool LED_ACTIVE_LOW = true;
constexpr uint8_t SW2_PIN = 32;
constexpr uint8_t SW3_PIN = 33;
constexpr uint8_t SW4_PIN = 34;

// Logging Configuration
constexpr char LOG_PATH[] = "/log.csv";

// OLED Display
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint8_t OLED_ADDRESS = 0x3C;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire);

constexpr uint32_t DISPLAY_UPDATE_MS = 20;
uint32_t lastDisplayUpdate = 0;

// Preferences stores settings that must survive reset or power loss.
Preferences preferences;

bool ledState = false;
uint8_t pwmValue = 0;

// These values are kept in RAM while the program is running.
bool loggingEnabled = false;
uint32_t logCount = 0;

// ButtonState is used to debounce mechanical switches
constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t LONG_PRESS_MS = 3000;

struct ButtonState {
  bool rawState;      // last reading from the pin.
  bool stableState;   // debounced state of the button.
  uint32_t changedAt; // time in milliseconds when the rawState last changed.
};

ButtonState sw2 = {HIGH, HIGH, 0};
ButtonState sw3 = {HIGH, HIGH, 0};
ButtonState sw4 = {HIGH, HIGH, 0};
uint32_t sw4PressedAt = 0;
bool sw4LongPressHandled = false;

// Convert the logical brightness to the PWM level required by the LED wiring.
void applyLedOutput() {
  uint8_t output = ledState ? pwmValue : 0;
  if (LED_ACTIVE_LOW) {
    output = 255 - output;
  }
  analogWrite(LED_PIN, output);
}

// Save only the setting changed by the corresponding switch.
void saveLedState() { preferences.putBool("ledState", ledState); }
void savePwmValue() { preferences.putUChar("pwmValue", pwmValue); }

// Return a press event only after the input has stayed stable long enough to
// reject mechanical switch bouncing.
bool isPressed(uint8_t pin, ButtonState &button) {
  bool reading = digitalRead(pin);
  uint32_t currentTime = millis();

  if (reading != button.rawState) {
    button.rawState = reading;
    button.changedAt = currentTime;
  }

  if (currentTime - button.changedAt >= DEBOUNCE_MS &&
      reading != button.stableState) {
    button.stableState = reading;
    return button.stableState == LOW;
  }

  return false;
}

// Return a string in the format "MM:SS" representing the elapsed time since the
// program started.
String elapsedTime() {
  uint32_t totalSeconds = millis() / 1000;
  uint32_t minutes = totalSeconds / 60;
  uint32_t seconds = totalSeconds % 60;

  char timeText[8];

  snprintf(timeText, sizeof(timeText), "%02lu:%02lu", minutes, seconds);
  return String(timeText);
}

// Return the voltage corresponding to the current PWM value, assuming a 3.3V
// supply.
float pwmVoltage() { return (static_cast<float>(pwmValue) / 255.0f) * 3.3f; }

// Convert a 12-bit ADC reading (0-4095) to an 8-bit PWM value (0-255).
uint8_t adcToPwm(uint16_t adcValue) {
  return static_cast<uint8_t>((static_cast<uint32_t>(adcValue) * 255) / 4095);
}

// Create the CSV file and its header once. Existing records are preserved.
bool ensureLogFile() {
  if (SPIFFS.exists(LOG_PATH)) {
    return true;
  }

  File file = SPIFFS.open(LOG_PATH, FILE_WRITE);
  if (!file) {
    Serial.println("Cannot create /log.csv");
    return false;
  }

  file.println("Time,ADC,PWM,Voltage,LED_State");
  file.close();
  return true;
}

void loadLogCount() {
  logCount = 0;
  File file = SPIFFS.open(LOG_PATH, FILE_READ);
  if (!file) {
    return;
  }

  // The first line is the CSV header and is not included in logCount.
  file.readStringUntil('\n');
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      logCount++;
    }
  }
  file.close();
}

void appendLog(uint16_t adcValue) {
  if (!loggingEnabled) {
    return;
  }
  if (!SPIFFS.exists(LOG_PATH)) {
    Serial.println("Logging is disabled or /log.csv does not exist");
    return;
  }

  File file = SPIFFS.open(LOG_PATH, FILE_APPEND);
  if (!file) {
    Serial.println("Cannot append /log.csv");
    return;
  }

  // Append one record; never overwrite older history.
  file.printf("%s, %u, %u, %.2f, %s\n", elapsedTime().c_str(), adcValue,
              pwmValue, pwmVoltage(), ledState ? "ON" : "OFF");
  file.close();
  logCount++;
}

void readLog() {
  File file = SPIFFS.open(LOG_PATH, FILE_READ);
  if (!file) {
    Serial.println("Cannot open /log.csv");
    return;
  }

  Serial.println("========================================");
  Serial.println("log.csv");
  Serial.println("========================================");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  Serial.println("========================================");
  Serial.printf("Total Records : %d\n", logCount);
  Serial.println("========================================");
}

void deleteLog() {
  if (SPIFFS.exists(LOG_PATH) && !SPIFFS.remove(LOG_PATH)) {
    Serial.println("Cannot delete /log.csv");
    return;
  }

  if (!ensureLogFile()) {
    Serial.println("Cannot recreate /log.csv");
    return;
  }

  logCount = 0;
  Serial.println("Log deleted");
}

void updateDisplay(uint16_t adcValue) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("%s       POT: %u", elapsedTime().c_str(), adcValue);
  display.setCursor(0, 16);
  display.printf("SAVE:%u (%.2fV)", pwmValue, pwmVoltage());
  display.setCursor(0, 32);
  display.printf("LED Status: %s", ledState ? "ON" : "OFF");
  display.setCursor(0, 48);
  display.printf("LOG:%s    count:%lu", loggingEnabled ? "ON" : "OFF",
                 logCount);
  display.display();
}

void handleSwitches(uint16_t adcValue) {
  if (isPressed(SW2_PIN, sw2)) {
    ledState = !ledState;
    saveLedState();
    applyLedOutput();
    Serial.printf("SW2 pressed - LED: %s\n", ledState ? "ON" : "OFF");
    appendLog(adcValue);
  }

  if (isPressed(SW3_PIN, sw3)) {
    pwmValue = adcToPwm(adcValue);
    savePwmValue();
    applyLedOutput();
    Serial.printf("SW3 pressed - Brightness saved: %u\n", pwmValue);
    appendLog(adcValue);
  }

  // SW4 uses GPIO34, so it is read as an input and debounced manually.
  uint32_t currentTime = millis();
  bool sw4Reading = digitalRead(SW4_PIN);
  if (sw4Reading != sw4.rawState) {
    sw4.rawState = sw4Reading;
    sw4.changedAt = currentTime;
  }

  if (currentTime - sw4.changedAt >= DEBOUNCE_MS &&
      sw4Reading != sw4.stableState) {
    sw4.stableState = sw4Reading;
    if (sw4.stableState == LOW) {
      // Start timing when the debounced press begins.
      sw4PressedAt = currentTime;
      sw4LongPressHandled = false;
    } else if (!sw4LongPressHandled) {
      // A release shorter than LONG_PRESS_MS is a logging toggle.
      loggingEnabled = !loggingEnabled;
      Serial.printf("SW4 pressed - Logging: %s\n",
                    loggingEnabled ? "ON" : "OFF");
    }
  }

  if (sw4.stableState == LOW && !sw4LongPressHandled &&
      currentTime - sw4PressedAt >= LONG_PRESS_MS) {
    // Handle a long press once, before the button is released.
    Serial.println("SW4 long press - Deleting log");
    deleteLog();
    sw4LongPressHandled = true;
  }
}

extern const uint8_t webserver_html_start[] asm("_binary_src_webserver_html_start");
extern const uint8_t webserver_html_end[] asm("_binary_src_webserver_html_end");

void handleRoot() {
  server.send(200, "text/html", (const char*)webserver_html_start);
}

void handleApiStatus() {
  JsonDocument doc;
  doc["adc"] = analogRead(POT_PIN);
  doc["potVoltage"] = analogRead(POT_PIN) * 3.3 / 4095.0;
  doc["pwm"] = pwmValue;
  doc["pwmVoltage"] = pwmVoltage();
  doc["led"] = ledState;
  doc["logging"] = loggingEnabled;
  doc["logCount"] = logCount;
  doc["uptime"] = elapsedTime();
  doc["apSSID"] = AP_SSID;
  doc["apIP"] = WiFi.softAPIP().toString();
  doc["clients"] = WiFi.softAPgetStationNum();

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleApiLed() {
  if (server.method() == HTTP_POST) {
    ledState = !ledState;
    saveLedState();
    applyLedOutput();
    if (loggingEnabled) {
      appendLog(analogRead(POT_PIN));
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void handleApiLogging() {
  if (server.method() == HTTP_POST) {
    loggingEnabled = !loggingEnabled;
    server.send(200, "text/plain", "OK");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void handleApiPwm() {
  if (server.method() == HTTP_POST) {
    if (server.hasArg("value")) {
      pwmValue = server.arg("value").toInt();
      savePwmValue();
      applyLedOutput();
      if (loggingEnabled) {
        appendLog(analogRead(POT_PIN));
      }
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Bad Request");
    }
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void handleApiDeleteLog() {
  if (server.method() == HTTP_POST) {
    deleteLog();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Ensure payload is null-terminated for safe string operations
  char* message = new char[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';
  
  Serial.printf("MQTT Received [%s]: %s\n", topic, message);
  
  if (strcmp(topic, TOPIC_CMD) == 0) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error.c_str());
      delete[] message;
      return;
    }
    
    const char* cmd = doc["cmd"];
    const char* val = doc["value"];
    
    if (cmd != nullptr) {
      if (strcmp(cmd, "led") == 0) {
        if (val && strcmp(val, "toggle") == 0) ledState = !ledState;
        else if (val && strcmp(val, "on") == 0) ledState = true;
        else if (val && strcmp(val, "off") == 0) ledState = false;
        saveLedState();
        applyLedOutput();
        lastMqttPublish = 0; // Force telemetry update
      } else if (strcmp(cmd, "logging") == 0) {
        if (val && strcmp(val, "toggle") == 0) loggingEnabled = !loggingEnabled;
        lastMqttPublish = 0;
      } else if (strcmp(cmd, "pwm") == 0) {
        if (!doc["value"].isNull()) {
          pwmValue = doc["value"].as<uint8_t>();
          savePwmValue();
          applyLedOutput();
        }
        lastMqttPublish = 0;
      } else if (strcmp(cmd, "deleteLog") == 0) {
        deleteLog();
        lastMqttPublish = 0;
      } else if (strcmp(cmd, "request") == 0) {
        lastMqttPublish = 0; // Force telemetry update
      }
    }
  }
  
  delete[] message;
}

bool mqttReconnect() {
  Serial.print("Attempting MQTT connection...");
  String clientId = "ESP32Client-";
  clientId += String(random(0xffff), HEX);
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, TOPIC_STATUS, 1, true, "offline")) {
    Serial.println("connected");
    mqtt.publish(TOPIC_STATUS, "online", true);
    mqtt.subscribe(TOPIC_CMD);
    return true;
  } else {
    Serial.print("failed, rc=");
    Serial.print(mqtt.state());
    Serial.println(" try again in 5 seconds");
    return false;
  }
}

void handleMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  if (!mqtt.connected()) {
    uint32_t now = millis();
    if (now - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = now;
      if (mqttReconnect()) {
        lastMqttReconnectAttempt = 0;
      }
    }
  } else {
    mqtt.loop();
  }
}

void setup() {
  Serial.begin(115200);

  // Configure the potentiometer, LED, and switches.
  pinMode(POT_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(SW2_PIN, INPUT_PULLUP);
  pinMode(SW3_PIN, INPUT_PULLUP);
  pinMode(SW4_PIN, INPUT);

  // Initialize the switch states to avoid false triggers on startup.
  sw2.rawState = sw2.stableState = digitalRead(SW2_PIN);
  sw3.rawState = sw3.stableState = digitalRead(SW3_PIN);
  sw4.rawState = sw4.stableState = digitalRead(SW4_PIN);
  sw2.changedAt = sw3.changedAt = sw4.changedAt = millis();

  // Configure the ADC as 12-bit and PWM output as 8-bit.
  analogReadResolution(12);
  analogWriteFrequency(5000);
  analogWriteResolution(8);

  // Load saved settings; defaults are used when the keys do not exist yet.
  preferences.begin("ledPref", false);
  ledState = preferences.getBool("ledState", false);
  pwmValue = preferences.getUChar("pwmValue", 0);
  applyLedOutput();

  // Mount SPIFFS. The true argument permits formatting if mounting fails.
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
  } else {
    if (ensureLogFile()) {
      loadLogCount();
      Serial.printf("SPIFFS: log.csv Count: %d\n", logCount);
    }
  }

  // Start I2C and initialize the OLED.
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization failed");
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Initialize WiFi AP and STA
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP Address: ");
  Serial.println(WiFi.softAPIP());
  
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.println("Connecting to STA Wi-Fi in background...");
  
  // Web Server setup
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/led", HTTP_POST, handleApiLed);
  server.on("/api/logging", HTTP_POST, handleApiLogging);
  server.on("/api/pwm", HTTP_POST, handleApiPwm);
  server.on("/api/delete-log", HTTP_POST, handleApiDeleteLog);
  server.serveStatic("/log.csv", SPIFFS, "/log.csv");
  server.begin();
  Serial.println("HTTP server started");
  
  // MQTT setup
  espClient.setInsecure(); // Bypass TLS certificate validation
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024);
}

void loop() {
  uint16_t adcValue = analogRead(POT_PIN);

  // Press R/r in Serial Monitor to print the complete CSV log.
  while (Serial.available()) {
    char command = Serial.read();
    if (command == 'R' || command == 'r') {
      readLog();
    }
  }

  // Print Wi-Fi connection status every 5 seconds if not connected
  static uint32_t lastWifiLog = 0;
  static bool wasConnected = false;
  if (WiFi.status() != WL_CONNECTED) {
    uint32_t now = millis();
    if (now - lastWifiLog > 5000) {
      lastWifiLog = now;
      Serial.println("Wi-Fi STA disconnected or still connecting...");
    }
    wasConnected = false;
  } else {
    if (!wasConnected) {
      Serial.println("Wi-Fi STA connected!");
      Serial.print("STA IP Address: ");
      Serial.println(WiFi.localIP());
      wasConnected = true;
    }
  }

  handleSwitches(adcValue);
  server.handleClient();
  handleMqtt();

  // Periodically publish telemetry to MQTT
  if (mqtt.connected()) {
    uint32_t now = millis();
    if (now - lastMqttPublish >= MQTT_PUBLISH_MS) {
      lastMqttPublish = now;
      
      JsonDocument doc;
      doc["adc"] = adcValue;
      doc["pwm"] = pwmValue;
      doc["voltage"] = pwmVoltage();
      doc["logCount"] = logCount;
      doc["led"] = ledState;
      doc["logging"] = loggingEnabled;
      doc["mqtt"] = true;
      doc["staConnected"] = (WiFi.status() == WL_CONNECTED);
      doc["rssi"] = WiFi.RSSI();
      doc["staIP"] = WiFi.localIP().toString();
      doc["uptime"] = elapsedTime();
      
      String payload;
      serializeJson(doc, payload);
      mqtt.publish(TOPIC_DATA, payload.c_str());
    }
  }

  // Refresh OLED every 20 ms without pausing button or Serial processing.
  uint32_t currentTime = millis();
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
    lastDisplayUpdate = currentTime;
    updateDisplay(adcValue);
  }
}