#include <Arduino.h>

#define LED1 2
#define LED2 16
#define LED3 17
#define SW1 25

volatile bool isrSW1Flag = false;

hw_timer_t *timer500ms = NULL;
volatile bool isr500msFlag = false;

int LED2_Counter = 0;

int LED3_CurrentTime = 0;
int LED3_LastTime = 0;
bool LED3_Stage = LOW;

void IRAM_ATTR isrSW1() { isrSW1Flag = true; }

void IRAM_ATTR isrTimer500ms() { isr500msFlag = true; }

void setup() {
  Serial.begin(115200);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);
  pinMode(SW1, INPUT_PULLUP);

  digitalWrite(LED1, LOW);
  digitalWrite(LED2, LOW);
  digitalWrite(LED3, LOW);

  attachInterrupt(SW1, &isrSW1, FALLING);

  timer500ms = timerBegin(0, 80, true);
  timerAlarmWrite(timer500ms, 500000, true);
  timerAttachInterrupt(timer500ms, &isrTimer500ms, true);
  timerAlarmEnable(timer500ms);
}

void loop() {
  if (isrSW1Flag == true) {
    isrSW1Flag = false;
    LED3_CurrentTime = millis();

    if (LED3_CurrentTime - LED3_LastTime > 20) {
      LED3_LastTime = LED3_CurrentTime;
      if (LED3_Stage == LOW) {
        LED3_Stage = HIGH;
        digitalWrite(LED3, !digitalRead(LED3));
      } else {
        LED3_Stage = LOW;
      }
    }
  }

  if (isr500msFlag == true) {
    isr500msFlag = false;
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