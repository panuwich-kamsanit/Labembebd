#include <Arduino.h>
#include <Wire.h>

// ที่อยู่ I2C ของ MCP23017 ตามการต่อวงจรนี้
#define MCP23017 0x27

// สถานะสลับลายบิต และตัวจับเวลาสำหรับสลับทุกวินาที
bool stateISR = true;
unsigned long period = 1000;
unsigned long last_time = 0;

void setup() {
  Wire.begin(); // เริ่ม I2C
  // จุดออกสอบ Wokwi: I2C ส่ง [address, register, data]; IODIRA 0x00=output
  Wire.beginTransmission(MCP23017); // Salve address | 7 Bit address + CMD
  Wire.write(0x00);                 // Mem address | 0x00 IODIRA
  Wire.write(0x00);                 // Data
  Wire.endTransmission();
  delay(500);
}

void loop() {
  // ใช้ผลต่าง millis() แทน delay เพื่อสลับ output ทุก period
  if (millis() - last_time > period) {
    last_time = millis();
    if (stateISR == true) {
      // เขียน 0xAA = 10101010 ลง GPIOA ทำให้ขา A สลับ HIGH/LOW
      // เขียนค่าเริ่มต้น 0 ให้ GPIOA (register 0x12)
      Wire.beginTransmission(MCP23017); //
      Wire.write(0x12);                 // GPIOA
      Wire.write(0xAA);                 // 0b 1010 1010 | A0 Low
      Wire.endTransmission(); 
    } else {
      // เขียน 0x55 = 01010101 เป็นลายตรงข้ามกับรอบก่อน
      Wire.beginTransmission(MCP23017); //
      Wire.write(0x12);                 // GPIOA
      Wire.write(0x55);                 // 0b 0101 0101 | A0 High
      Wire.endTransmission();
    }
    stateISR = !stateISR; // สลับรูปแบบบิตสำหรับรอบถัดไป
  }
}