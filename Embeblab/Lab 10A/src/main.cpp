#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Wire.h>

#include <BluetoothSerial.h>

// จุดออกสอบ: Bluetooth Classic SPP, แยกคำสั่งตามบรรทัด และ encode/decode JSON
// Lab 10A ใช้ Bluetooth Classic SPP สื่อสารเป็นข้อความกับ dashboard
// Bluetooth Classic
BluetoothSerial SerialBT;
constexpr char BT_DEVICE_NAME[] = "ESP32-LAB10A-6730300183";

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

// Bluetooth Serial status broadcast interval
// ส่ง JSON status ให้ dashboard ทุก 1 วินาทีเมื่อมี client เชื่อมต่อ
constexpr uint32_t BT_STATUS_MS = 1000;
uint32_t lastBtStatus = 0;

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
  // รวมสถานะ on/off กับค่าความสว่าง และกลับ duty เมื่อวงจร active-low
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
  // debounce แบบไม่ block: ตรวจ raw state และยอมรับหลังนิ่งครบช่วงเวลา
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
  // ปรับช่วง ADC 12-bit ไปเป็น PWM 8-bit
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
  // ไม่บันทึกเมื่อ logging ปิด; FILE_APPEND รักษาประวัติเดิม
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

// Send log.csv contents over Bluetooth Serial, framed with LOG_BEGIN / LOG_END
void readLogBT() {
  File file = SPIFFS.open(LOG_PATH, FILE_READ);
  if (!file) {
    SerialBT.println("LOG_BEGIN");
    SerialBT.println("LOG_END");
    return;
  }

  SerialBT.println("LOG_BEGIN");
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      SerialBT.println(line);
    }
  }
  file.close();
  SerialBT.println("LOG_END");
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
  // SW2 สลับ LED; SW3 เก็บระดับ ADC เป็นค่าความสว่าง
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

  // SW4 ใช้ GPIO34 จึงอ่าน/debounce เอง; กดสั้นสลับ logging กดค้างลบ log
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

// Send JSON status over Bluetooth Serial (one line per sample).
// Format matches what the dashboard expects:
// {"adc":2048,"pwm":128,"led":true,"logging":false,"logCount":0,
//  "savedLED":true,"savedPwm":128,"uptimeSeconds":90,"sampleId":12345}
void sendBtStatus(uint16_t adcValue) {
  // ประกอบสถานะเป็น JSON บรรทัดเดียวตาม protocol ที่ dashboard คาดหวัง
  uint32_t uptimeSeconds = millis() / 1000;
  static uint32_t sampleId = 0;
  sampleId++;

  // Build JSON manually to avoid ArduinoJson dependency for this simple output
  char json[256];
  snprintf(json, sizeof(json),
           "{\"adc\":%u,\"pwm\":%u,\"led\":%s,\"logging\":%s,"
           "\"logCount\":%lu,\"savedLED\":%s,\"savedPwm\":%u,"
           "\"uptimeSeconds\":%lu,\"sampleId\":%lu}",
           adcValue, pwmValue,
           ledState ? "true" : "false",
           loggingEnabled ? "true" : "false",
           logCount,
           preferences.getBool("ledState", false) ? "true" : "false",
           preferences.getUChar("pwmValue", 0),
           uptimeSeconds,
           sampleId);

  SerialBT.println(json);
}

// Parse and execute commands received over Bluetooth Serial.
// Protocol (from dashboard 10A):
//   LED 1 / LED 0   — turn LED on/off
//   PWM <value>     — set PWM 0-255
//   LOG 1 / LOG 0   — start/stop logging
//   SAVE            — save LED + PWM to Preferences
//   READLOG         — send log.csv over BT
//   DELLOG          — delete log.csv
//   READ            — force a status update
void handleBtCommand(String cmd) {
  // แปลคำสั่งข้อความจาก dashboard เช่น LED, PWM, LOG, SAVE, READLOG
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.printf("BT RX: %s\n", cmd.c_str());

  if (cmd.startsWith("LED ")) {
    int val = cmd.substring(4).toInt();
    ledState = (val == 1);
    saveLedState();
    applyLedOutput();
    Serial.printf("BT - LED: %s\n", ledState ? "ON" : "OFF");
    // Force immediate status update
    lastBtStatus = 0;

  } else if (cmd.startsWith("PWM ")) {
    int val = cmd.substring(4).toInt();
    pwmValue = constrain(val, 0, 255);
    savePwmValue();
    applyLedOutput();
    Serial.printf("BT - PWM: %u\n", pwmValue);
    lastBtStatus = 0;

  } else if (cmd.startsWith("LOG ")) {
    int val = cmd.substring(4).toInt();
    loggingEnabled = (val == 1);
    Serial.printf("BT - Logging: %s\n", loggingEnabled ? "ON" : "OFF");
    lastBtStatus = 0;

  } else if (cmd == "SAVE") {
    saveLedState();
    savePwmValue();
    Serial.println("BT - Preferences saved");
    lastBtStatus = 0;

  } else if (cmd == "READLOG") {
    Serial.println("BT - Sending log.csv");
    readLogBT();

  } else if (cmd == "DELLOG") {
    deleteLog();
    Serial.println("BT - Log deleted");
    lastBtStatus = 0;

  } else if (cmd == "READ") {
    // Force immediate status update
    lastBtStatus = 0;

  } else {
    Serial.printf("BT - Unknown command: %s\n", cmd.c_str());
  }
}

void setup() {
  Serial.begin(115200); // เปิด Serial Monitor

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

  // ตั้ง resolution ของ ADC/PWM ให้ตรงกับช่วงค่าที่ใช้ในโปรแกรม
  analogReadResolution(12);
  analogWriteFrequency(5000);
  analogWriteResolution(8);

  // โหลดสถานะและค่าความสว่างจาก Preferences
  preferences.begin("ledPref", false);
  ledState = preferences.getBool("ledState", false);
  pwmValue = preferences.getUChar("pwmValue", 0);
  applyLedOutput();

  // mount SPIFFS และเตรียม/นับแถวในไฟล์ CSV
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

  // เริ่ม Bluetooth Classic Serial Port Profile เพื่อรับ/ส่งข้อความ
  if (!SerialBT.begin(BT_DEVICE_NAME)) {
    Serial.println("Bluetooth initialization failed!");
  } else {
    Serial.printf("Bluetooth started: %s\n", BT_DEVICE_NAME);
    Serial.println("Pair this device in your Bluetooth settings, then connect via the dashboard.");
  }
}

void loop() {
  // อ่าน potentiometer หนึ่งครั้งแล้วส่งค่าเดียวกันให้ฟังก์ชันต่าง ๆ
  uint16_t adcValue = analogRead(POT_PIN);

  // Press R/r in Serial Monitor to print the complete CSV log.
  while (Serial.available()) {
    char command = Serial.read();
    if (command == 'R' || command == 'r') {
      readLog();
    }
  }

  // สะสม byte จน newline แล้วจึงส่งคำสั่งครบชุดไปให้ parser
  static String btBuffer = "";
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\n' || c == '\r') {
      if (btBuffer.length() > 0) {
        handleBtCommand(btBuffer);
        btBuffer = "";
      }
    } else {
      btBuffer += c;
    }
  }

  handleSwitches(adcValue); // ตรวจปุ่มบนบอร์ด

  // ส่ง status เมื่อ client ต่ออยู่และครบช่วง BT_STATUS_MS
  uint32_t now = millis();
  if (SerialBT.hasClient() && now - lastBtStatus >= BT_STATUS_MS) {
    lastBtStatus = now;
    sendBtStatus(adcValue);
  }

  // อัปเดต OLED แบบไม่หน่วง loop
  uint32_t currentTime = millis();
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
    lastDisplayUpdate = currentTime;
    updateDisplay(adcValue);
  }
}