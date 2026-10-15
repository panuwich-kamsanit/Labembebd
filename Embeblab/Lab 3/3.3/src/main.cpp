#include <Arduino.h>

// ขาของ 7-segment แบบ common cathode และปุ่มนับ/รีเซ็ต
#define A 17
#define B 5
#define C 18
#define D 19
#define E 21
#define F 23
#define G 22

#define SW 16
#define SWReset 4

// เลขที่จะแสดง เริ่มต้นที่ 0
int currentNumber = 0;

// จุดออกสอบ Wokwi: แปลงเลขเป็น bit pattern แล้ว map บิตไปยังขา 7-segment
const int segmentPins[7] = {A, B, C, D, E, F, G};

// Common Cathode (1 = ติด, 0 = ดับ)
// ลำดับภายในบิตคือ {g, f, e, d, c, b, a}
const byte numDigits[10] = {
    B0111111, // 0
    B0000110, // 1
    B1011011, // 2
    B1001111, // 3
    B1100110, // 4
    B1101101, // 5
    B1111101, // 6
    B0000111, // 7
    B1111111, // 8
    B1101111  // 9
};

// ISR ของปุ่มสองปุ่มและ timer จะตั้ง flag เพื่อให้ loop จัดการต่อ
bool isrSWFlag = false;
bool isrSWResetFlag = false;

hw_timer_t *timer1s = NULL;
volatile bool isr1sFlag = false;

int SW_CurrentTime = 0;
int SW_LastTime = 0;
int SW_Stage = LOW;

// ISR ต้องสั้น: ตั้งธง ไม่วาดจอหรือทำงานหนักใน interrupt
void IRAM_ATTR isrSW() { isrSWFlag = true; }

void IRAM_ATTR isrSWReset() { isrSWResetFlag = true; }

void IRAM_ATTR isrTimer1s() { isr1sFlag = true; }

void displayNumber(int num) {
  // เลือกรูปแบบบิตของเลข แล้วส่งแต่ละบิตไปยังขา segment
  byte bits = numDigits[num];
  for (int i = 0; i < 7; i++) {
    digitalWrite(segmentPins[i], bitRead(bits, i));
  }
}

void setup() {
  Serial.begin(115200); // เริ่ม Serial Monitor

  // ตั้งขาทั้ง 7 segment เป็น output และเริ่มต้นดับ
  for (int i = 0; i < 7; i++) {
    pinMode(segmentPins[i], OUTPUT);
    digitalWrite(segmentPins[i], LOW);
  }

  // ปุ่ม pull-up อ่าน LOW เมื่อกด
  pinMode(SW, INPUT_PULLUP);
  pinMode(SWReset, INPUT_PULLUP);

  // FALLING คือขอบ HIGH->LOW ซึ่งเกิดเมื่อกดปุ่ม pull-up
  attachInterrupt(SW, &isrSW, FALLING);
  attachInterrupt(SWReset, &isrSWReset, FALLING);

  // ตั้ง timer ให้สร้าง event ทุก 1 วินาที
  timer1s = timerBegin(0, 80, true);
  timerAlarmWrite(timer1s, 1000000, true);
  timerAttachInterrupt(timer1s, &isrTimer1s, true);
  timerAlarmEnable(timer1s);
}

void loop() {
  // ทุกวินาทีแสดงเลขปัจจุบัน แล้วนับเพิ่มวนกลับจาก 9 ไป 0
  if (isr1sFlag) {
    isr1sFlag = false;
    displayNumber(currentNumber);
    currentNumber++;
    if (currentNumber > 9) {
      currentNumber = 0;
    }
  }

  // debounce ปุ่มด้วย millis() เพื่อไม่ให้การเด้งของหน้าสัมผัสทำซ้ำ
  if (isrSWFlag) {
    isrSWFlag = false;
    SW_CurrentTime = millis();

    if (SW_CurrentTime - SW_LastTime > 20) {
      SW_LastTime = SW_CurrentTime;
      if (SW_Stage == LOW) {
        SW_Stage = HIGH;

        // กดปุ่มสลับการทำงานของ timer (start/stop)
        if (timerStarted(timer1s)) {
          timerStop(timer1s);
        } else {
          timerStart(timer1s);
        }
      } else {
        SW_Stage = LOW;
      }
    }
  }

  // ปุ่ม reset ตั้งเลขเป็น 0 และแสดงทันที
  if (isrSWResetFlag) {
    isrSWResetFlag = false;
    currentNumber = 0;
    displayNumber(currentNumber);
  }
}