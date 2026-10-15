#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <FS.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <Wire.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// จุดออกสอบ: BLE GATT UUID/property, callback, notify และจัดรูป status packet
// Lab 10B ใช้ BLE GATT service/characteristics แทน Bluetooth Classic SPP
// ─── BLE UUIDs ──────────────────────────────────────────────────────────────
#define SERVICE_UUID        "8e7c0001-6c7d-4b2f-9f5a-1d2e3f4a5b60"
#define STATUS_UUID         "8e7c0002-6c7d-4b2f-9f5a-1d2e3f4a5b60"
#define COMMAND_UUID        "8e7c0003-6c7d-4b2f-9f5a-1d2e3f4a5b60"
#define LOG_UUID            "8e7c0004-6c7d-4b2f-9f5a-1d2e3f4a5b60"

constexpr char BLE_DEVICE_NAME[] = "ESP32-LAB10B-6730300183";

// ─── Pin Definitions ────────────────────────────────────────────────────────
constexpr uint8_t POT_PIN       = 36;
constexpr uint8_t LED_PIN       = 5;
constexpr bool    LED_ACTIVE_LOW = true;
constexpr uint8_t SW2_PIN       = 32;
constexpr uint8_t SW3_PIN       = 33;
constexpr uint8_t SW4_PIN       = 34;

// ─── Logging Configuration ─────────────────────────────────────────────────
constexpr char LOG_PATH[] = "/log.csv";

// ─── OLED Display ───────────────────────────────────────────────────────────
constexpr uint8_t OLED_WIDTH   = 128;
constexpr uint8_t OLED_HEIGHT  = 64;
constexpr uint8_t OLED_ADDRESS = 0x3C;

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire);

constexpr uint32_t DISPLAY_UPDATE_MS = 20;
uint32_t lastDisplayUpdate = 0;

// ─── BLE Status Broadcast Interval ─────────────────────────────────────────
constexpr uint32_t BLE_STATUS_MS = 1000;
uint32_t lastBleStatus = 0;

// ─── Preferences ────────────────────────────────────────────────────────────
Preferences preferences;

bool    ledState = false;
uint8_t pwmValue = 0;

bool     loggingEnabled = false;
uint32_t logCount       = 0;

// ─── Button Debounce ────────────────────────────────────────────────────────
constexpr uint32_t DEBOUNCE_MS    = 40;
constexpr uint32_t LONG_PRESS_MS  = 3000;

struct ButtonState {
  bool     rawState;
  bool     stableState;
  uint32_t changedAt;
};

ButtonState sw2 = {HIGH, HIGH, 0};
ButtonState sw3 = {HIGH, HIGH, 0};
ButtonState sw4 = {HIGH, HIGH, 0};
uint32_t sw4PressedAt        = 0;
bool     sw4LongPressHandled = false;

// BLE objects และ flags ใช้ประสาน event จาก callback กับ loop หลัก
// ─── BLE Globals ────────────────────────────────────────────────────────────
BLEServer*         pServer         = nullptr;
BLECharacteristic* pStatusChar     = nullptr;
BLECharacteristic* pCommandChar    = nullptr;
BLECharacteristic* pLogChar        = nullptr;

bool     bleConnected     = false;
bool     statusNotify     = false;
bool     logNotify        = false;
bool     sendLogRequested = false;
uint32_t sampleId         = 0;

// ─── Forward Declarations ───────────────────────────────────────────────────
void applyLedOutput();
void saveLedState();
void savePwmValue();
void buildStatusPacket(uint8_t* buf, uint16_t adcValue);
void sendLogFile();

// ═══════════════════════════════════════════════════════════════════════════
//  Helper Functions
// ═══════════════════════════════════════════════════════════════════════════

void applyLedOutput() {
  // แปลง state/brightness เป็น duty ที่เหมาะกับ LED active-low
  uint8_t output = ledState ? pwmValue : 0;
  if (LED_ACTIVE_LOW) {
    output = 255 - output;
  }
  analogWrite(LED_PIN, output);
}

void saveLedState() { preferences.putBool("ledState", ledState); }
void savePwmValue() { preferences.putUChar("pwmValue", pwmValue); }

bool isPressed(uint8_t pin, ButtonState &button) {
  // debounce ปุ่มด้วยเวลาคงที่; คืน true เฉพาะขอบการกดลง
  bool     reading     = digitalRead(pin);
  uint32_t currentTime = millis();

  if (reading != button.rawState) {
    button.rawState  = reading;
    button.changedAt = currentTime;
  }

  if (currentTime - button.changedAt >= DEBOUNCE_MS &&
      reading != button.stableState) {
    button.stableState = reading;
    return button.stableState == LOW;
  }

  return false;
}

String elapsedTime() {
  uint32_t totalSeconds = millis() / 1000;
  uint32_t minutes      = totalSeconds / 60;
  uint32_t seconds      = totalSeconds % 60;
  char timeText[8];
  snprintf(timeText, sizeof(timeText), "%02lu:%02lu", minutes, seconds);
  return String(timeText);
}

float pwmVoltage() {
  return (static_cast<float>(pwmValue) / 255.0f) * 3.3f;
}

uint8_t adcToPwm(uint16_t adcValue) {
  // map 12-bit ADC เป็น 8-bit PWM
  return static_cast<uint8_t>((static_cast<uint32_t>(adcValue) * 255) / 4095);
}

// ═══════════════════════════════════════════════════════════════════════════
//  SPIFFS / Log Helpers
// ═══════════════════════════════════════════════════════════════════════════

bool ensureLogFile() {
  if (SPIFFS.exists(LOG_PATH)) return true;

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
  if (!file) return;

  file.readStringUntil('\n');  // skip header
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) logCount++;
  }
  file.close();
}

void appendLog(uint16_t adcValue) {
  // เพิ่มแถว CSV ต่อท้ายเมื่อ logging เปิดอยู่
  if (!loggingEnabled) return;
  if (!SPIFFS.exists(LOG_PATH)) {
    Serial.println("Logging is disabled or /log.csv does not exist");
    return;
  }

  File file = SPIFFS.open(LOG_PATH, FILE_APPEND);
  if (!file) {
    Serial.println("Cannot append /log.csv");
    return;
  }

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

// ═══════════════════════════════════════════════════════════════════════════
//  OLED Display
// ═══════════════════════════════════════════════════════════════════════════

void updateDisplay(uint16_t adcValue) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("%s       POT: %u", elapsedTime().c_str(), adcValue);
  display.setCursor(0, 16);
  display.printf("SAVE:%u (%.2fV)", pwmValue, pwmVoltage());
  display.setCursor(0, 32);
  display.printf("LED Status: %s", ledState ? "ON" : "OFF");
  display.setCursor(0, 48);
  display.printf("LOG:%s    count:%lu", loggingEnabled ? "ON" : "OFF", logCount);
  display.display();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Button Handling
// ═══════════════════════════════════════════════════════════════════════════

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

  uint32_t currentTime = millis();
  bool sw4Reading = digitalRead(SW4_PIN);
  if (sw4Reading != sw4.rawState) {
    sw4.rawState  = sw4Reading;
    sw4.changedAt = currentTime;
  }

  if (currentTime - sw4.changedAt >= DEBOUNCE_MS &&
      sw4Reading != sw4.stableState) {
    sw4.stableState = sw4Reading;
    if (sw4.stableState == LOW) {
      sw4PressedAt        = currentTime;
      sw4LongPressHandled = false;
    } else if (!sw4LongPressHandled) {
      loggingEnabled = !loggingEnabled;
      Serial.printf("SW4 pressed - Logging: %s\n",
                    loggingEnabled ? "ON" : "OFF");
    }
  }

  if (sw4.stableState == LOW && !sw4LongPressHandled &&
      currentTime - sw4PressedAt >= LONG_PRESS_MS) {
    Serial.println("SW4 long press - Deleting log");
    deleteLog();
    sw4LongPressHandled = true;
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  BLE Status Packet (20 bytes)
// ═══════════════════════════════════════════════════════════════════════════

void buildStatusPacket(uint8_t* buf, uint16_t adcValue) {
  // สร้าง packet 20 byte ตาม protocol: version/flags/ADC/PWM/count/time/id
  memset(buf, 0, 20);

  // Byte 0: Protocol version
  buf[0] = 1;

  // Byte 1: Flags
  uint8_t flags = 0;
  if (ledState)                               flags |= 0x01;  // bit0 LED
  if (loggingEnabled)                         flags |= 0x02;  // bit1 Logging
  if (preferences.getBool("ledState", false)) flags |= 0x04;  // bit2 savedLED
  buf[1] = flags;

  // Byte 2-3: ADC (uint16 little-endian)
  buf[2] = adcValue & 0xFF;
  buf[3] = (adcValue >> 8) & 0xFF;

  // Byte 4: PWM
  buf[4] = pwmValue;

  // Byte 5: savedPWM
  buf[5] = preferences.getUChar("pwmValue", 0);

  // Byte 6-7: Reserved
  buf[6] = 0;
  buf[7] = 0;

  // Byte 8-11: logCount (uint32 little-endian)
  buf[8]  =  logCount        & 0xFF;
  buf[9]  = (logCount >> 8)  & 0xFF;
  buf[10] = (logCount >> 16) & 0xFF;
  buf[11] = (logCount >> 24) & 0xFF;

  // Byte 12-15: uptimeSeconds (uint32 little-endian)
  uint32_t uptime = millis() / 1000;
  buf[12] =  uptime        & 0xFF;
  buf[13] = (uptime >> 8)  & 0xFF;
  buf[14] = (uptime >> 16) & 0xFF;
  buf[15] = (uptime >> 24) & 0xFF;

  // Byte 16-19: sampleId (uint32 little-endian)
  buf[16] =  sampleId        & 0xFF;
  buf[17] = (sampleId >> 8)  & 0xFF;
  buf[18] = (sampleId >> 16) & 0xFF;
  buf[19] = (sampleId >> 24) & 0xFF;
}

// ═══════════════════════════════════════════════════════════════════════════
//  BLE Log Transfer (via Log Characteristic Notify)
// ═══════════════════════════════════════════════════════════════════════════

void sendLogFile() {
  // ส่ง CSV ผ่าน notify เป็น START, DATA chunks และ END frame
  if (!logNotify) {
    Serial.println("Log Notify not enabled, cannot send log");
    return;
  }

  File file = SPIFFS.open(LOG_PATH, FILE_READ);

  // --- START frame: [type=1][seq=0x0000] ---
  uint8_t startFrame[3] = {0x01, 0x00, 0x00};
  pLogChar->setValue(startFrame, 3);
  pLogChar->notify();
  delay(20);

  uint16_t seq = 1;

  if (file) {
    // Read file in chunks of up to 17 bytes (20 - 3 header bytes)
    const size_t CHUNK_SIZE = 17;
    uint8_t dataFrame[20];

    while (file.available()) {
      size_t bytesRead = file.readBytes((char*)(dataFrame + 3), CHUNK_SIZE);
      if (bytesRead == 0) break;

      // --- DATA frame: [type=2][seq][payload] ---
      dataFrame[0] = 0x02;
      dataFrame[1] = seq & 0xFF;
      dataFrame[2] = (seq >> 8) & 0xFF;

      pLogChar->setValue(dataFrame, 3 + bytesRead);
      pLogChar->notify();
      delay(20);  // small delay to avoid BLE congestion

      seq = (seq + 1) & 0xFFFF;
    }
    file.close();
  }

  // --- END frame: [type=3][seq] ---
  uint8_t endFrame[3];
  endFrame[0] = 0x03;
  endFrame[1] = seq & 0xFF;
  endFrame[2] = (seq >> 8) & 0xFF;
  pLogChar->setValue(endFrame, 3);
  pLogChar->notify();

  Serial.printf("Log sent: %u data frames\n", seq - 1);
}

// ═══════════════════════════════════════════════════════════════════════════
//  BLE Callbacks
// ═══════════════════════════════════════════════════════════════════════════

class ServerCallbacks : public BLEServerCallbacks {
  // callback ทำงานเมื่อ client BLE เชื่อมต่อหรือตัดการเชื่อมต่อ
  void onConnect(BLEServer* pServer) override {
    bleConnected = true;
    Serial.println("BLE Client connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    bleConnected  = false;
    statusNotify  = false;
    logNotify     = false;
    Serial.println("BLE Client disconnected");
    // Restart advertising so the next client can find us
    BLEDevice::startAdvertising();
  }
};

// Callback for Command Characteristic writes
class CommandCallbacks : public BLECharacteristicCallbacks {
  // แปล byte opcode และ value ที่ client เขียนลง Command characteristic
  void onWrite(BLECharacteristic* pChar) override {
    std::string rxData = pChar->getValue();
    if (rxData.empty()) return;

    uint8_t op = rxData[0];
    uint8_t val = rxData.size() > 1 ? rxData[1] : 0;

    Serial.printf("BLE CMD: op=0x%02X val=0x%02X len=%u\n", op, val,
                  rxData.size());

    switch (op) {
      case 0x01:  // LED
        ledState = (val == 1);
        saveLedState();
        applyLedOutput();
        Serial.printf("BLE - LED: %s\n", ledState ? "ON" : "OFF");
        lastBleStatus = 0;  // force immediate status update
        break;

      case 0x02:  // PWM
        pwmValue = val;
        savePwmValue();
        applyLedOutput();
        Serial.printf("BLE - PWM: %u\n", pwmValue);
        lastBleStatus = 0;
        break;

      case 0x03:  // LOG ON/OFF
        loggingEnabled = (val == 1);
        Serial.printf("BLE - Logging: %s\n", loggingEnabled ? "ON" : "OFF");
        lastBleStatus = 0;
        break;

      case 0x04:  // SAVE
        saveLedState();
        savePwmValue();
        Serial.println("BLE - Preferences saved");
        lastBleStatus = 0;
        break;

      case 0x05:  // READ LOG
        Serial.println("BLE - Log read requested");
        sendLogRequested = true;  // handle in loop to avoid callback stall
        break;

      case 0x06:  // DELETE LOG
        deleteLog();
        Serial.println("BLE - Log deleted");
        lastBleStatus = 0;
        break;

      default:
        Serial.printf("BLE - Unknown command: 0x%02X\n", op);
        break;
    }
  }
};

// Descriptor callback to track Notify subscription state
class StatusDescriptorCallbacks : public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* pDescriptor) override {
    uint8_t* val = pDescriptor->getValue();
    if (val && pDescriptor->getLength() >= 2) {
      statusNotify = (val[0] & 0x01);
      Serial.printf("Status Notify: %s\n", statusNotify ? "ON" : "OFF");
    }
  }
};

class LogDescriptorCallbacks : public BLEDescriptorCallbacks {
  void onWrite(BLEDescriptor* pDescriptor) override {
    uint8_t* val = pDescriptor->getValue();
    if (val && pDescriptor->getLength() >= 2) {
      logNotify = (val[0] & 0x01);
      Serial.printf("Log Notify: %s\n", logNotify ? "ON" : "OFF");
    }
  }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Setup
// ═══════════════════════════════════════════════════════════════════════════

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

  // ตั้งช่วง ADC 0..4095 และ PWM 0..255
  analogReadResolution(12);
  analogWriteFrequency(5000);
  analogWriteResolution(8);

  // โหลดการตั้งค่าที่เก็บถาวรจาก Preferences
  preferences.begin("ledPref", false);
  ledState = preferences.getBool("ledState", false);
  pwmValue = preferences.getUChar("pwmValue", 0);
  applyLedOutput();

  // mount SPIFFS และกู้จำนวน records ของไฟล์ log
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
  } else {
    if (ensureLogFile()) {
      loadLogCount();
      Serial.printf("SPIFFS: log.csv Count: %d\n", logCount);
    }
  }

  // เริ่ม I2C/OLED เพื่อแสดงสถานะบนอุปกรณ์
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED initialization failed");
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // เริ่ม BLE server, สร้าง service และลักษณะข้อมูล status/command/log
  // ─── Initialize BLE ──────────────────────────────────────────────────
  BLEDevice::init(BLE_DEVICE_NAME);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Status Characteristic — Read + Notify
  pStatusChar = pService->createCharacteristic(
      STATUS_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  BLE2902* statusDescriptor = new BLE2902();
  statusDescriptor->setCallbacks(new StatusDescriptorCallbacks());
  pStatusChar->addDescriptor(statusDescriptor);

  // Command Characteristic — Write
  pCommandChar = pService->createCharacteristic(
      COMMAND_UUID,
      BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_WRITE_NR);
  pCommandChar->setCallbacks(new CommandCallbacks());

  // Log Characteristic — Notify
  pLogChar = pService->createCharacteristic(
      LOG_UUID,
      BLECharacteristic::PROPERTY_NOTIFY);
  BLE2902* logDescriptor = new BLE2902();
  logDescriptor->setCallbacks(new LogDescriptorCallbacks());
  pLogChar->addDescriptor(logDescriptor);

  pService->start();

  // ประกาศ service เพื่อให้ client ค้นหาและเชื่อมต่อได้
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // helps with iPhone connections
  BLEDevice::startAdvertising();

  Serial.printf("BLE started: %s\n", BLE_DEVICE_NAME);
  Serial.println("Waiting for BLE client connection...");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Main Loop
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
  // อ่าน ADC ทุก loop เพื่อใช้กับปุ่ม, status packet และจอ
  uint16_t adcValue = analogRead(POT_PIN);

  // Serial Monitor: press R to print CSV log
  while (Serial.available()) {
    char command = Serial.read();
    if (command == 'R' || command == 'r') {
      readLog();
    }
  }

  handleSwitches(adcValue); // ตรวจปุ่มใน loop หลัก

  // callback ตั้ง flag; ส่งไฟล์ใน loop แทนเพื่อไม่ทำงานยาวใน callback
  if (sendLogRequested) {
    sendLogRequested = false;
    sendLogFile();
  }

  // สร้าง status packet ตามช่วงเวลา; notify เฉพาะเมื่อ client subscribe แล้ว
  uint32_t now = millis();
  if (bleConnected && now - lastBleStatus >= BLE_STATUS_MS) {
    lastBleStatus = now;
    sampleId++;

    uint8_t statusBuf[20];
    buildStatusPacket(statusBuf, adcValue);
    pStatusChar->setValue(statusBuf, 20);

    if (statusNotify) {
      pStatusChar->notify();
    }
  }

  // รีเฟรช OLED ทุก 20 ms แบบไม่ block
  uint32_t currentTime = millis();
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_MS) {
    lastDisplayUpdate = currentTime;
    updateDisplay(adcValue);
  }
}