#include <Arduino.h>
#include <Wire.h>

#define MCP23017 0x27

bool stateISR = true;
unsigned long period = 1000;
unsigned long last_time = 0;

void setup() {
  Wire.begin();
  // IODIRA 0x00 ALLOutput 0x00
  Wire.beginTransmission(MCP23017); // Salve address | 7 Bit address + CMD
  Wire.write(0x00);                 // Mem address | 0x00 IODIRA
  Wire.write(0x00);                 // Data
  Wire.endTransmission();
  delay(500);
}

void loop() {
  if (millis() - last_time > period) {
    last_time = millis();
    if (stateISR == true) {
      // PORTA 0x12 -> 1010 1010 -> 0xAA
      Wire.beginTransmission(MCP23017); //
      Wire.write(0x12);                 // GPIOA
      Wire.write(0xAA);                 // 0b 1010 1010 | A0 Low
      Wire.endTransmission(); 
    } else {
      // PORTA 0x12 -> 0101 0101 -> 0x55
      Wire.beginTransmission(MCP23017); //
      Wire.write(0x12);                 // GPIOA
      Wire.write(0x55);                 // 0b 0101 0101 | A0 High
      Wire.endTransmission();
    }
    stateISR = !stateISR;
  }
}