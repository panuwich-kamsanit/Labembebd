#include <Arduino.h>

hw_timer_t *timer1s = NULL;
bool flag_timer = false;
int min_time = 0;
int sec_time = 0;
int state_clock = 0;
bool state_enable = false;

void IRAM_ATTR isr_timer() { flag_timer = true; }

void setup() {
  delay(20);

  Serial.begin(115200);

  Serial.println("Start Firmware");

  timer1s = timerBegin(0, 80, true);
  timerAlarmWrite(timer1s, 1000000, true);
  timerAttachInterrupt(timer1s, &isr_timer, true);
  timerAlarmEnable(timer1s);
}

void loop() {
  if (flag_timer) {
    flag_timer = false;

    if (state_enable) {
      if (state_clock == 0) {
        sec_time++;
      }

      if (sec_time >= 60) {
        sec_time = 0;
        min_time++;
      }

      Serial.printf("Clock RUN -> %02d:%02d", min_time, sec_time);
    } else {
      Serial.printf("Clock STOP -> %02d:%02d", min_time, sec_time);
    }
    Serial.println();
  }

  if (Serial.available()) {
    char c = Serial.read();

    if (c == '1') {
      Serial.println("Start Timer");
      state_enable = true;
    } else if (c == '2') {
      Serial.println("Stop Timer");
      state_enable = false;
    }
  }
}