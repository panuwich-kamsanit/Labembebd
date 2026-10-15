#include <Arduino.h>

// ขาอุปกรณ์ที่ใช้ในวงจร
#define LED1 2
#define LED2 16
#define LED3 17
#define SW1 25

// volatile ใช้กับตัวแปรที่เปลี่ยนค่าใน ISR เพื่อป้องกัน compiler cache ค่าเดิม
volatile bool isrSW1Flag = false;

// timer สร้าง interrupt เป็นช่วง ๆ เพื่อกระพริบ LED
hw_timer_t *timer500ms = NULL;
volatile bool isr500msFlag = false;

int LED2_Counter = 0;

int LED3_CurrentTime = 0;
int LED3_LastTime = 0;
bool LED3_Stage = LOW;

// ISR ของปุ่ม: ตั้ง flag เท่านั้น ไม่ทำงานช้าใน interrupt
void IRAM_ATTR isrSW1() { isrSW1Flag = true; }

// ISR ของ timer ทุก 500 ms
void IRAM_ATTR isrTimer500ms() { isr500msFlag = true; }

void setup() {
  Serial.begin(115200); // เปิดพอร์ต Serial

  // กำหนดทิศทางของขา LED และ input pull-up ของสวิตช์
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(SW1, INPUT_PULLUP);

  // เริ่มต้น LED ทุกดวงที่สถานะดับ
  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

  // FALLING คือขอบสัญญาณ HIGH -> LOW ขณะกดปุ่มแบบ INPUT_PULLUP
  attachInterrupt(SW1, &isrSW1, FALLING);

  // ตั้ง timer ให้ interrupt ทุก 500 ms
  timer500ms = timerBegin(0, 80, true);
  timerAlarmWrite(timer500ms, 500000, true);
  timerAttachInterrupt(timer500ms, &isrTimer500ms, true);
  timerAlarmEnable(timer500ms);
}

void loop() {
  // จัดการเหตุการณ์จากปุ่มที่ ISR แจ้งไว้
  if (isrSW1Flag == true) {
    isrSW1Flag = false; // เคลียร์ธง
    LED3_CurrentTime = millis();

    if (LED3_CurrentTime - LED3_LastTime > 20) {
      LED3_LastTime = LED3_CurrentTime;
      // ตรวจ stage; ตาม logic ปัจจุบัน LED3 จะ toggle เมื่อ stage เป็น LOW
      // แล้วตั้งเป็น HIGH จึง toggle ในทุกเหตุการณ์กดสลับครั้ง ไม่ใช่ทุกครั้ง
      if (LED3_Stage == LOW) {
        LED3_Stage = HIGH;
        digitalWrite(LED3, !digitalRead(LED3)); // toggle LED3
      } else {
        LED3_Stage = LOW;
      }
    }
  }

  // จุดออกสอบ: ISR ไม่ควรทำงานหนัก ให้ loop() ตรวจ flag และประมวลผล
  if (isr500msFlag == true) {
    isr500msFlag = false; // เคลียร์ flag
    digitalWrite(LED1, !digitalRead(LED1));

    LED2_Counter++;
    if (LED2_Counter < 10) {
      digitalWrite(LED2, !digitalRead(LED2));
    } else {
      if (LED2_Counter == 16) {
        LED2_Counter = 0;
      }
      digitalWrite(LED2, LOW);
    }
  }
}