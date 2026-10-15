#include <Arduino.h>
#include <SPI.h>

#define PIN_SCLK 18
#define PIN_MISO 19
#define PIN_MOSI 23
#define PIN_CS 14

uint16_t ADS1118_CONFIGS[4] = {0xC38B, 0xD38B, 0xE38B, 0xF38B};
const float ADS1118_FSR = 4.096;
SPISettings spiSettings(1000000, MSBFIRST, SPI_MODE1);

enum Mode { MODE_STOP, MODE_M0, MODE_M1 };

Mode currentMode = MODE_STOP;
unsigned long previousMillis = 0;

float readADS1118_AIN(uint16_t channel) {
  uint16_t config = ADS1118_CONFIGS[channel];

  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  SPI.transfer(config >> 8);
  SPI.transfer(config & 0xFF);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();

  delay(9);

  SPI.beginTransaction(spiSettings);
  digitalWrite(PIN_CS, LOW);
  uint8_t dataMSB = SPI.transfer(0x00);
  uint8_t dataLSB = SPI.transfer(0x00);
  digitalWrite(PIN_CS, HIGH);
  SPI.endTransaction();

  int16_t adcValue = (dataMSB << 8) | dataLSB;
  return adcValue * ADS1118_FSR / 32768.0;
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  pinMode(PIN_CS, OUTPUT);
  digitalWrite(PIN_CS, HIGH);
  SPI.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_CS);

  Serial.println("System Ready. Send M0, M1 or M9 to control.");
}

void loop() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "M0") {
      currentMode = MODE_M0;
    } else if (cmd == "M1") {
      currentMode = MODE_M1;
    } else if (cmd == "M9") {
      currentMode = MODE_STOP;
      Serial.println(">> ADS1118 reading stopped.");
    } else if (cmd.length() > 0) {
      Serial.println(">> Invalid command. Use M0, M1 or M9.");
    }
  }

  if (currentMode != MODE_STOP) {
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= 100) {
      previousMillis = currentMillis;

      if (currentMode == MODE_M0) {
        float AIN0 = readADS1118_AIN(0);
        Serial.print(">> AIN0 = ");
        Serial.print(AIN0, 4);
        Serial.println(" V");
      } else if (currentMode == MODE_M1) {
        float AIN0 = readADS1118_AIN(0);
        float AIN1 = readADS1118_AIN(1);
        float AIN2 = readADS1118_AIN(2);
        float AIN3 = readADS1118_AIN(3);

        Serial.print(">> AIN0 = ");
        Serial.print(AIN0, 4);
        Serial.print(" V, ");
        Serial.print("AIN1 = ");
        Serial.print(AIN1, 4);
        Serial.print(" V, ");
        Serial.print("AIN2 = ");
        Serial.print(AIN2, 4);
        Serial.print(" V, ");
        Serial.print("AIN3 = ");
        Serial.print(AIN3, 4);
        Serial.println(" V");
      }
    }
  }
}