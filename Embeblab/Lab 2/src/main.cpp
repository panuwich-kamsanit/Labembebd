#include <Arduino.h>

#define LED1 5
#define LED2 4
#define SW1 25
#define SW2 26

void setup() {
  delay(20);
  Serial.begin(115200);

  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);

  Serial.println("----- Start Firmware -----");
}

void loop() {
  if (digitalRead(SW1) == LOW) {
    digitalWrite(LED1, LOW);
  } else {
    digitalWrite(LED1, HIGH);
  }

  if (digitalRead(SW2) == LOW) {
    digitalWrite(LED2, HIGH);
  } else {
    digitalWrite(LED2, LOW);
  }
}
