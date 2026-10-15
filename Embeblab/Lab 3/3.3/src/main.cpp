#include <Arduino.h>

#define A 17
#define B 5
#define C 18
#define D 19
#define E 21
#define F 23
#define G 22

#define SW 16
#define SWReset 4

int currentNumber = 0;

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

bool isrSWFlag = false;
bool isrSWResetFlag = false;

hw_timer_t *timer1s = NULL;
volatile bool isr1sFlag = false;

int SW_CurrentTime = 0;
int SW_LastTime = 0;
int SW_Stage = LOW;

void IRAM_ATTR isrSW() { isrSWFlag = true; }

void IRAM_ATTR isrSWReset() { isrSWResetFlag = true; }

void IRAM_ATTR isrTimer1s() { isr1sFlag = true; }

void displayNumber(int num) {
  byte bits = numDigits[num];
  for (int i = 0; i < 7; i++) {
    digitalWrite(segmentPins[i], bitRead(bits, i));
  }
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 7; i++) {
    pinMode(segmentPins[i], OUTPUT);
    digitalWrite(segmentPins[i], LOW);
  }

  pinMode(SW, INPUT_PULLUP);
  pinMode(SWReset, INPUT_PULLUP);

  attachInterrupt(SW, &isrSW, FALLING);
  attachInterrupt(SWReset, &isrSWReset, FALLING);

  timer1s = timerBegin(0, 80, true);
  timerAlarmWrite(timer1s, 1000000, true);
  timerAttachInterrupt(timer1s, &isrTimer1s, true);
  timerAlarmEnable(timer1s);
}

void loop() {
  if (isr1sFlag) {
    isr1sFlag = false;
    displayNumber(currentNumber);
    currentNumber++;
    if (currentNumber > 9) {
      currentNumber = 0;
    }
  }

  if (isrSWFlag) {
    isrSWFlag = false;
    SW_CurrentTime = millis();

    if (SW_CurrentTime - SW_LastTime > 20) {
      SW_LastTime = SW_CurrentTime;
      if (SW_Stage == LOW) {
        SW_Stage = HIGH;

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

  if (isrSWResetFlag) {
    isrSWResetFlag = false;
    currentNumber = 0;
    displayNumber(currentNumber);
  }
}