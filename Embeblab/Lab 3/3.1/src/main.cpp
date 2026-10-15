#include <Arduino.h>

// กำหนดขา LED และสวิตช์
#define LED1 2
#define LED2 16
#define LED3 17
#define SW1 25

// ตัวจัดการฮาร์ดแวร์ timer ซึ่งตั้งให้แจ้งเหตุการณ์ทุก 500 ms
hw_timer_t *timer500ms = NULL;
// ธงที่ ISR ตั้งไว้ให้ loop() ทราบว่า timer หมดเวลาแล้ว
bool isr500msFlag = false;

// นับจำนวน tick เพื่อกำหนดรูปแบบการกระพริบ LED2
int LED2_Counter = 0;

// ตัวแปรจับเวลาการกด SW1 และกันการเปลี่ยนสถานะจากสัญญาณสั่น
int LED3_CurrentTime = 0;
int LED3_LastTime = 0;
bool LED3_Stage = LOW;

// ISR ควรทำงานสั้น ๆ: ตั้งธงไว้ แล้วให้ loop() ทำงานจริง
void IRAM_ATTR isrTimer500ms() { isr500msFlag = true; }

void setup() {
  Serial.begin(115200); // เปิด Serial Monitor

  // ตั้งทิศทางขา I/O
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(SW1, INPUT_PULLUP);

  // เริ่มต้นให้ LED ทั้งหมดดับ
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

  // ตั้ง timer หมายเลข 0; prescaler 80 ทำให้ tick ของ ESP32 เป็น 1 us
  timer500ms = timerBegin(0, 80, true);
  timerAlarmWrite(timer500ms, 500000, true); // alarm ทุก 500,000 us = 500 ms
  timerAttachInterrupt(timer500ms, &isrTimer500ms, true); // ผูก ISR เข้ากับ timer
  timerAlarmEnable(timer500ms); // เริ่มนับเวลา
}

void loop() {
  // อ่าน SW1 แบบ polling และตรวจว่าค่าคงที่เกิน 50 ms ก่อนยอมรับ
  bool SW1State = digitalRead(SW1);
  if (LED3_Stage != SW1State) {
    LED3_CurrentTime = millis();

    if (LED3_CurrentTime - LED3_LastTime > 50) {
      LED3_LastTime = LED3_CurrentTime;
      if (SW1State == LOW) { // LOW คือกด เพราะเปิด INPUT_PULLUP
        LED3_Stage = LOW;
        digitalWrite(LED3, !digitalRead(LED3)); // กลับสถานะ LED3 ทุกครั้งที่กด
      } else {
        LED3_Stage = HIGH;
      }
    }
  }

  // จุดออกสอบ: ISR ตั้ง flag เท่านั้น; งานหลักทำใน loop()
  if (isr500msFlag == true) {
    isr500msFlag = false; // เคลียร์ flag เพื่อรอ tick รอบถัดไป
    digitalWrite(LED1, !digitalRead(LED1)); // สลับ LED1 ทุก 500 ms

    LED2_Counter++;
    if (LED2_Counter < 10) { // ช่วงนับ 1-9 ให้ LED2 สลับทุก tick
      digitalWrite(LED2, !digitalRead(LED2));
    } else {
      if (LED2_Counter == 16) { // ครบรอบ 16 tick ให้เริ่มนับใหม่
        LED2_Counter = 0;
      }
      digitalWrite(LED2, LOW);
    }
  }
}