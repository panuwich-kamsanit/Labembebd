#include <Arduino.h>

// ใช้ flag ให้ ISR แจ้ง loop() เมื่อ timer ครบ 1 วินาที
hw_timer_t *timer1s = NULL;
bool flag_timer = false;

// ISR ทำงานสั้น ๆ เพื่อให้การ interrupt มีเวลาทำงานน้อย
void IRAM_ATTR isr_timer() { flag_timer = true; }

void setup() {
  delay(20); // รอระบบเริ่มต้น

  Serial.begin(115200); // Serial Monitor ต้องตั้ง baud rate ให้ตรงกัน

  Serial.println("Start Firmware"); // แสดงข้อความตอนบูต

  // จุดออกสอบ Wokwi: prescaler 80 -> tick 1 us; 1,000,000 us = 1 วินาที
  timer1s = timerBegin(0, 80, true);
  timerAlarmWrite(timer1s, 1000000, true);
  timerAttachInterrupt(timer1s, &isr_timer, true);
  timerAlarmEnable(timer1s);
}

void loop() {
  // เมื่อ timer แจ้งครบ 1 วินาที ให้พิมพ์ T หนึ่งครั้ง
  if (flag_timer) {
    flag_timer = false; // เคลียร์ flag ก่อนรอรอบถัดไป
    Serial.println("T");
  }

  // available() บอกว่ามี byte เข้ามาหรือไม่; read() อ่านครั้งละ 1 ตัวอักษร
  if (Serial.available()) {
    char c = Serial.read();
    Serial.print("Received: ");
    Serial.println(c, DEC);

    // แปลงตัวอักษรเลขเป็นค่าจำนวนเต็มด้วยการลบ ASCII ของ '0'
    int num = (int)c - '0';

    Serial.println(num); // แสดงค่าตัวเลขที่แปลงแล้ว
  }
}