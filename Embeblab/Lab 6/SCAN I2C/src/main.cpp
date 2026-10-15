#include <Arduino.h>
#include <Wire.h>

// จุดออกสอบ Wokwi: Scanner วน address 7-bit และดู ACK จาก endTransmission()
void setup() {
  Wire.begin(); // เริ่ม I2C
  Serial.begin(115200); // เริ่ม Serial Monitor
  Serial.println("\nI2C Scanner");
}

void loop() {
  // address 1-126 เป็นช่วงที่อยู่ 7-bit ที่ใช้ค้นหาอุปกรณ์
  byte error, address;
  int nDevices;
  Serial.println("Scanning...");
  nDevices = 0;
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address); // ส่ง START พร้อม address เป้าหมาย
    error = Wire.endTransmission(); // ส่ง STOP และรับสถานะ ACK/NACK
    if (error == 0) {
      // error 0 หมายถึงอุปกรณ์ตอบ ACK
      Serial.print("I2C device found at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
      nDevices++;
    } else if (error == 4) { // รหัส 4 หมายถึง bus/อุปกรณ์มีข้อผิดพลาด
      Serial.print("Unknow error at address 0x");
      if (address < 16) {
        Serial.print("0");
      }
      Serial.println(address, HEX);
    }
  }
  // สรุปผลว่าพบอุปกรณ์หรือไม่ แล้วพัก 5 วินาทีก่อนสแกนใหม่
  if (nDevices == 0) {
    Serial.println("No I2C devices found\n");
  } else {
    Serial.println("done\n");
  }
  delay(5000);
}