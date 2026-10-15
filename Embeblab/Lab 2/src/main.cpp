#include <Arduino.h>

// กำหนดหมายเลขขา LED และสวิตช์ที่ต่อกับ ESP32
#define LED1 5
#define LED2 4
#define SW1 25
#define SW2 26

// setup() ทำงานครั้งเดียวเมื่อเริ่มโปรแกรม
void setup() {
  delay(20); // หน่วงสั้น ๆ หลังเปิดบอร์ด
  Serial.begin(115200); // เปิด Serial Monitor ที่ baud rate 115200

  // LED เป็นขาเอาต์พุต ส่วนสวิตช์ใช้ pull-up ภายใน
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);

  // แจ้งว่าโปรแกรมเริ่มทำงานแล้ว
  Serial.println("----- Start Firmware -----");
}

// loop() ทำซ้ำตลอดเวลาเพื่ออ่านสวิตช์และกำหนดสถานะ LED
void loop() {
  // INPUT_PULLUP ทำให้ปล่อยปุ่มอ่านได้ HIGH และกดปุ่มอ่านได้ LOW
  // จุดออกสอบ: เงื่อนไข LOW คือ "กด" เมื่อใช้ INPUT_PULLUP
  if (digitalRead(SW1) == LOW) {
    digitalWrite(LED1, LOW); // SW1 กด: ดับ LED1
  } else {
    digitalWrite(LED1, HIGH); // SW1 ปล่อย: ติด LED1
  }

  if (digitalRead(SW2) == LOW) {
    digitalWrite(LED2, HIGH); // SW2 กด: ติด LED2
  } else {
    digitalWrite(LED2, LOW); // SW2 ปล่อย: ดับ LED2
  }
}
