#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#define MCP23017 0x27

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define LINE1 0
#define LINE2 20
#define LINE3 35
#define LINE4 50
#define RESETTEXT "               "

int lastTime = 0;

bool stateISR = true;

enum Mode { MODE_M0, MODE_M1 };
Mode currentMode = MODE_M0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

void setup() {
  Serial.begin(115200);

  // MCP23017
  Wire.begin();
  Wire.beginTransmission(MCP23017);
  Wire.write(0x00);
  Wire.write(0x00);
  Wire.endTransmission();

  Wire.beginTransmission(MCP23017);
  Wire.write(0x12);
  Wire.write(0x00);
  Wire.endTransmission();

  // OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  display.clearDisplay();

  display.setTextColor(WHITE);
  display.setTextSize(1);

  display.setCursor(0, LINE1);
  display.println(F("ESP32 I2C MCP23017"));

  display.setCursor(0, LINE2);
  display.println(F("A0: OFF"));

  display.setCursor(0, LINE3);
  display.println(F("A1: OFF"));

  display.setCursor(0, LINE4);
  display.println(F("Stop Toggle"));

  display.display();

  delay(1000);
}

void loop() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    display.setTextSize(1);
    display.setTextColor(WHITE, BLACK);
    if (cmd == "M0") {
      currentMode = MODE_M0;

      display.setCursor(0, LINE4);
      display.println(F("Stop Toggle"));

      Serial.println(">> Stop Toggle");
    } else if (cmd == "M1") {
      currentMode = MODE_M1;

      display.setCursor(0, LINE4);
      display.println(F(RESETTEXT));
    } else if (cmd.length() > 0) {
      Serial.println(">> Invalid command. Use M0 State or M1 Start Toggle.");
    }
    display.display();
  }

  int currentTime = millis();
  if (currentTime - lastTime >= 1000) {
    lastTime = currentTime;

    if (currentMode == MODE_M1) {
      // MCP23017 GPIOA
      Wire.beginTransmission(MCP23017);
      Wire.write(0x12);

      if (stateISR == true) {
        Wire.write(0x01);
      } else {
        Wire.write(0x02);
      }

      Wire.endTransmission();

      // OLED Display
      display.setTextSize(1);
      display.setTextColor(WHITE, BLACK);

      if (stateISR == true) {
        display.setCursor(0, LINE2);
        display.println(F("A0: ON "));

        display.setCursor(0, LINE3);
        display.println(F("A1: OFF"));
      } else {
        display.setCursor(0, LINE2);
        display.println(F("A0: OFF"));

        display.setCursor(0, LINE3);
        display.println(F("A1: ON "));
      }

      display.display();

      // Serial Monitor
      if (stateISR == true) {
        Serial.println(">> A0: ON, A1: OFF");
      } else {
        Serial.println(">> A0: OFF, A1: ON");
      }

      stateISR = !stateISR;
    }
  }
}