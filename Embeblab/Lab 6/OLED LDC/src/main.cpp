#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

// กำหนดความละเอียดจอ OLED และสร้างวัตถุแสดงผลผ่าน I2C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

void setup() {
  Serial.begin(115200); // เปิด Serial เพื่อรายงานปัญหาการเริ่มจอ

  // เริ่ม SSD1306 ที่ address 0x3C; ถ้าล้มเหลวแจ้งใน Serial
  // จุดออกสอบ Wokwi: OLED SSD1306 ใช้ I2C address 0x3C และต้อง begin ก่อนวาด
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  // display.display(); // ตัวอย่างคำสั่งที่ถูกปิดไว้ ไม่ได้ทำงาน
  // delay(2000);
  display.clearDisplay(); // ล้าง buffer ก่อนวาดข้อความใหม่

  // Test Draw Char
  display.setTextColor(WHITE); // ตั้งสีตัวอักษรเป็นสีขาว

  display.setTextSize(2); // ขยายตัวอักษร 2 เท่า
  display.setCursor(0, 0); // เริ่มวาดจากพิกัดมุมซ้ายบน
  display.println(F("Chanakan"));

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.println(F("Keaopakdee"));

  display.display(); // ส่งข้อมูลใน buffer ไปยังจอจริง

  delay(2000); // ค้าง 2 วินาทีเพื่อให้เห็นข้อความ
}

// ไม่มีงานซ้ำหลัง setup: เป็นตัวอย่างแสดงผลครั้งเดียวตอนเริ่มต้น
void loop() {}