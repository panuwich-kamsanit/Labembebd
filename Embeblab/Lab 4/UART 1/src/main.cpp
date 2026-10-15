#include <Arduino.h>

hw_timer_t *timer1s = NULL;
bool flag_timer = false;

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
    Serial.println("T");
  }

  if (Serial.available()) {
    char c = Serial.read();
    Serial.print("Received: ");
    Serial.println(c, DEC);

    int num = (int)c - '0';

    Serial.println(num);
  }
}