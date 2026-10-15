#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

// ตั้ง address ของ MCP23017 และพิกัดแต่ละบรรทัดของ OLED
#define MCP23017 0x27

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define LINE1 0
#define LINE2 20
#define LINE3 35
#define LINE4 50
#define RESETTEXT "               "

// เวลาเปลี่ยนสถานะล่าสุด ใช้สร้างช่วงเวลาแบบไม่ block
int lastTime = 0;

// true สลับให้ A0 ติด; false สลับให้ A1 ติด
bool stateISR = true;

// M0 หยุด toggle, M1 เริ่ม toggle
enum Mode { MODE_M0, MODE_M1 };
Mode currentMode = MODE_M0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

void setup() {
  Serial.begin(115200); // เปิด Serial Monitor

  // จุดออกสอบ Wokwi: ตั้ง IODIRA เป็น output แล้วเขียน GPIOA ผ่าน register address
  Wire.begin();
  Wire.beginTransmission(MCP23017);
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.endTransmission();

  Wire.beginTransmission(MCP23017);
  Wire.write(0x12);
  Wire.write(0x00);
  Wire.endTransmission();

  // เริ่ม OLED ที่ address 0x3C; หากเริ่มไม่ได้ให้พิมพ์ error
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  display.clearDisplay(); // ล้าง buffer ภาพก่อนเขียนข้อความ

  display.setTextColor(WHITE);
  display.setTextSize(1);

  display.setCursor(0, LINE1);
  display.println(F("ESP32 I2C MCP23017"));

  display.setCursor(0, LINE2);
  display.println(F("A0: OFF"));

  display.setCursor(0, LINE3);
  display.println(F("A1: OFF"));

  display.setCursor(0, LINE4);
  display.println(F("Stop Toggle"));

  display.display(); // ส่ง buffer ไปแสดงบน OLED

  delay(1000);
}

void loop() {
  // อ่านข้อความ serial ทั้งบรรทัดเพื่อนำไปเลือก M0 หรือ M1
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    display.setTextSize(1);
    display.setTextColor(WHITE, BLACK);
    if (cmd == "M0") { // หยุดสลับขา และแสดงสถานะหยุด
      currentMode = MODE_M0;

      display.setCursor(0, LINE4);
      display.println(F("Stop Toggle"));

      Serial.println(">> Stop Toggle");
    } else if (cmd == "M1") { // อนุญาตให้ส่วนจับเวลาสลับ A0/A1
      currentMode = MODE_M1;

      display.setCursor(0, LINE4);
      display.println(F(RESETTEXT));
    } else if (cmd.length() > 0) {
      Serial.println(">> Invalid command. Use M0 State or M1 Start Toggle.");
    }
    display.display();
  }

  // ตรวจเวลาแบบ non-blocking; เข้าเงื่อนไขทุก 1000 ms
  int currentTime = millis();
  if (currentTime - lastTime >= 1000) {
    lastTime = currentTime;

    if (currentMode == MODE_M1) {
      // เลือก register GPIOA แล้วส่ง mask เปิดเฉพาะ A0 หรือ A1
      Wire.beginTransmission(MCP23017);
      Wire.write(0x12);

      if (stateISR == true) {
        Wire.write(0x01);
      } else {
        Wire.write(0x02);
      }

      Wire.endTransmission();

      // อัปเดตข้อความบน OLED ให้ตรงกับขาที่กำลังเปิด
      display.setTextSize(1);
      display.setTextColor(WHITE, BLACK);

      if (stateISR == true) {
        display.setCursor(0, LINE2);
        display.println(F("A0: ON "));

        display.setCursor(0, LINE3);
        display.println(F("A1: OFF"));
      } else {
        display.setCursor(0, LINE2);
        display.println(F("A0: OFF"));

        display.setCursor(0, LINE3);
        display.println(F("A1: ON "));
      }

      display.display();

      // พิมพ์สถานะเดียวกันออก Serial Monitor
      if (stateISR == true) {
        Serial.println(">> A0: ON, A1: OFF");
      } else {
        Serial.println(">> A0: OFF, A1: ON");
      }

      stateISR = !stateISR; // สลับเป้าหมายสำหรับหนึ่งวินาทีถัดไป
    }
  }
}