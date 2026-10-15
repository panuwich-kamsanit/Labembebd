#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Wire.h>

// จุดออกสอบ Wokwi: ADC 12-bit->PWM 8-bit, debounce, millis(), OLED และ filesystem
// Lab นี้รวม ADC potentiometer, PWM LED, ปุ่ม debounce, OLED,
// Preferences สำหรับข้อมูลคงอยู่ และ SPIFFS สำหรับประวัติ CSV
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
  // LED ติดได้เมื่อ ledState เป็นจริง และความสว่างมาจาก pwmValue
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
  // อ่านขาปุ่มและเริ่มจับเวลาใหม่ทุกครั้งที่สัญญาณดิบเปลี่ยน
  bool reading = digitalRead(pin);
  uint32_t currentTime = millis();

  if (reading != button.rawState) {
    button.rawState = reading;
    button.changedAt = currentTime;
  }

  // รับการเปลี่ยนสถานะหลังสัญญาณนิ่งครบ DEBOUNCE_MS; event เกิดตอนกดลง LOW
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
  // map ADC 12-bit [0,4095] เป็น PWM 8-bit [0,255]
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
  // SW2 สลับสถานะ LED; SW3 บันทึกความสว่างจากค่า potentiometer
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

  // GPIO34 ไม่มี internal pull-up ของ ESP32 จึงอ่านและ debounce เอง
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

  // ADC อ่าน 0..4095; PWM เขียน 0..255 และทำงานที่ 5 kHz
  analogReadResolution(12);
  analogWriteFrequency(5000);
  analogWriteResolution(8);

  // เปิด namespace ใน NVS แล้วอ่านค่าที่คงอยู่หลัง reset; ใช้ค่า default หากยังไม่เคยบันทึก
  preferences.begin("ledPref", false);
  ledState = preferences.getBool("ledState", false);
  pwmValue = preferences.getUChar("pwmValue", 0);
  applyLedOutput();

  // mount filesystem; สร้าง CSV header หากไฟล์ยังไม่มี และนับ records เดิม
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
  } else {
    if (ensureLogFile()) {
      loadLogCount();
      Serial.printf("SPIFFS: log.csv Count: %d\n", logCount);
    }
  }

  // เริ่ม I2C แล้วเริ่มจอ SSD1306; ออกจาก setup หาก init จอล้มเหลว
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization failed");
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void loop() {
  // อ่าน ADC ปัจจุบันหนึ่งครั้ง ใช้ร่วมกันกับปุ่มและจอในรอบนี้
  uint16_t adcValue = analogRead(POT_PIN);

  // Press R/r in Serial Monitor to print the complete CSV log.
  while (Serial.available()) {
    char command = Serial.read();
    if (command == 'R' || command == 'r') {
      readLog();
    }
  }

  handleSwitches(adcValue); // ประมวลผลปุ่มโดยไม่ใช้ delay

  // refresh จอทุก 20 ms ด้วย millis() เพื่อไม่ block การอ่านปุ่ม/Serial
  uint32_t currentTime = millis();
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
    lastDisplayUpdate = currentTime;
    updateDisplay(adcValue);
  }
}