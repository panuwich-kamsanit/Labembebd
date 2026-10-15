#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#define LED 26
#define SW2 32
#define SW3 33
#define SW4 34
#define ADC 35

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

enum Mode { PWM, DAC };
Mode currentMode = PWM;

bool lastRead_SW4 = HIGH;
bool state_SW4 = HIGH;
unsigned long lastDebounce_SW4 = 0;

int currentOutput = 255;
uint8_t currentDacValue = 255;
bool isFadingUp = false;
bool isFadingDown = false;
unsigned long lastFadeTime = 0;

String waveString = "Sine Wave";
unsigned long lastOledUpdate = 0;
unsigned long lastWaveUpdate = 0;

void PWM_Mode() {
  unsigned long currentMillis = millis();

  if (digitalRead(SW2) == LOW) {
    isFadingUp = true;
    isFadingDown = false;
  }
  if (digitalRead(SW3) == LOW) {
    isFadingDown = true;
    isFadingUp = false;
  }

  if (isFadingUp || isFadingDown) {
    if (currentMillis - lastFadeTime >= 4) {
      lastFadeTime = currentMillis;

      if (isFadingUp) {
        currentOutput++;
        if (currentOutput >= 255) {
          currentOutput = 255;
          isFadingUp = false;
        }
      } else if (isFadingDown) {
        currentOutput--;
        if (currentOutput <= 0) {
          currentOutput = 0;
          isFadingDown = false;
        }
      }
      analogWrite(LED, 255 - currentOutput);
    }
  }
}

void DAC_Mode() {
  // อัปเดตรูปคลื่นทุกๆ 50 ไมโครวินาที
  if (micros() - lastWaveUpdate > 50) {
    lastWaveUpdate = micros();
    static uint8_t timeIndex = 0;
    timeIndex++;

    int s2 = digitalRead(SW2);
    int s3 = digitalRead(SW3);

    if (s2 == HIGH && s3 == HIGH) {
      currentDacValue = (sin(timeIndex * PI / 128.0) + 1.0) * 127.5;
      waveString = "Sine Wave";
    } else if (s2 == HIGH && s3 == LOW) {
      if (timeIndex < 128) {
        currentDacValue = timeIndex * 2;
      } else {
        currentDacValue = 255 - ((timeIndex - 128) * 2);
      }
      waveString = "Triangular";
    } else if (s2 == LOW && s3 == HIGH) {
      currentDacValue = timeIndex;
      waveString = "Sawtooth";
    } else if (s2 == LOW && s3 == LOW) {
      currentDacValue = (timeIndex < 128) ? 255 : 0;
      waveString = "Square Wave";
    }

    dacWrite(LED, currentDacValue);
  }
}

void OLED_Display() {
  unsigned long currentMillis = millis();

  // อัปเดตจอทุก 200ms
  if (currentMillis - lastOledUpdate >= 200) {
    lastOledUpdate = currentMillis;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // คำนวณเวลา
    unsigned long sec = currentMillis / 1000;
    int m = sec / 60;
    int s = sec % 60;

    // บรรทัดที่ 1: เวลา และ Mode
    display.setCursor(0, 0);
    display.printf("%02d:%02d", m, s);
    display.setCursor(75, 0);
    display.printf("Mode:%s", currentMode == PWM ? "PWM" : "DAC");

    // บรรทัดที่ 2: ค่า PWM หรือ DAC
    display.setCursor(0, 20);
    if (currentMode == PWM) {
      display.printf("PWM:   %d", currentOutput);
    } else {
      display.printf("DAC:   %d %s", 255 -currentDacValue, waveString.c_str());
    }

    // บรรทัดที่ 3: Volt หรือ ADC
    display.setCursor(0, 35);
    int rawADC = analogRead(ADC);
    float actualVolt = (rawADC * 3.3) / 4095.0;

    float displayVolt = 3.3 - actualVolt;
    if (displayVolt < 0)
      displayVolt = 0.0; // ป้องกันค่าติดลบจากการแกว่งของสัญญาณ

    if (currentMode == PWM) {
      display.printf("Volt:  %.2fV", displayVolt);
    } else {
      display.printf("ADC:   %.2fV", displayVolt);
    }

    // บรรทัดที่ 4: สถานะสวิตช์ S2, S3, S4
    display.setCursor(0, 50);
    bool s2State = digitalRead(SW2) == LOW;
    bool s3State = digitalRead(SW3) == LOW;
    bool s4State = digitalRead(SW4) == LOW;

    display.printf("S2:%s S3:%s S4:%s", s2State ? "ON" : "OFF",
                   s3State ? "ON" : "OFF", s4State ? "ON" : "OFF");

    display.display();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);
  pinMode(SW4, INPUT);
  pinMode(LED, OUTPUT);

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  display.clearDisplay();
  display.setTextSize(1);
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Mode Selection (Toggle SW4 แบบกดปล่อยเพื่อสลับ)
  bool reading4 = digitalRead(SW4);
  if (reading4 != lastRead_SW4) {
    lastDebounce_SW4 = currentMillis;
  }
  if ((currentMillis - lastDebounce_SW4) > 50) {
    if (reading4 != state_SW4) {
      state_SW4 = reading4;
      if (state_SW4 == LOW) {
        currentMode = (currentMode == PWM) ? DAC : PWM;

        if (currentMode == PWM) {
          dacWrite(LED, 0);
          dacDisable(LED);
          pinMode(LED, OUTPUT);

          analogWrite(LED, 255 - currentOutput);
        } else {
          analogWrite(LED, 255);
        }
      }
    }
  }
  lastRead_SW4 = reading4;

  if (currentMode == PWM) {
    PWM_Mode();
  } else if (currentMode == DAC) {
    DAC_Mode();
  }

  OLED_Display();
}