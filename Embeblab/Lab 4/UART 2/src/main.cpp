#include <Arduino.h>

hw_timer_t *timer1s = NULL;
bool flag_timer = false;
int sec_time = 0;

void IRAM_ATTR isr_timer() { flag_timer = true; }

void setup() {
  delay(20);

  Serial.begin(115200);
  Serial2.begin(9600);

  Serial.println("Start Firmware");
  Serial2.println("Start Firmware");

  timer1s = timerBegin(0, 80, true);
  timerAlarmWrite(timer1s, 1000000, true);
  timerAttachInterrupt(timer1s, &isr_timer, true);
  timerAlarmEnable(timer1s);
}

void loop() {
  if (flag_timer) {
    flag_timer = false;
    Serial.println("T on UART0");
    Serial2.print("Peter");
  }

  if (Serial.available()) {
    char c = Serial.read();
    Serial.print("Received: ");
    Serial.println(c, DEC);

    int num = (int)c - '0';

    Serial.println(num);
  }
}