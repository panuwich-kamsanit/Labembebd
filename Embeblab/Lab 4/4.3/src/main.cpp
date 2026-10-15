#include <Arduino.h>

// สถานะนาฬิกาแบบนาที:วินาที และ flag จาก timer
hw_timer_t *timer1s = NULL;
bool flag_timer = false;
int min_time = 0;
int sec_time = 0;
int state_clock = 0;
bool state_enable = false;

void IRAM_ATTR isr_timer() { flag_timer = true; }

void setup() {
  delay(20); // หน่วงให้ระบบพร้อม

  Serial.begin(115200); // ตั้ง UART0 สำหรับ Serial Monitor

  Serial.println("Start Firmware");

  // จุดออกสอบ: timer interrupt ทุก 1 วินาที; ISR ตั้ง flag ให้ loop นับเวลา
  timer1s = timerBegin(0, 80, true);
  timerAlarmWrite(timer1s, 1000000, true);
  timerAttachInterrupt(timer1s, &isr_timer, true);
  timerAlarmEnable(timer1s);
}

void loop() {
  // นับเวลาเฉพาะเมื่อ state_enable เป็น true
  if (flag_timer) {
    flag_timer = false;

    if (state_enable) {
      if (state_clock == 0) { // state_clock ใช้เลือกโหมดเพิ่มวินาที
        sec_time++;
      }

      if (sec_time >= 60) { // วินาทีครบ 60 ให้ทดเป็น 1 นาที
        sec_time = 0;
        min_time++;
      }

      Serial.printf("Clock RUN -> %02d:%02d", min_time, sec_time); // %02d เติม 0 ข้างหน้า
    } else {
      Serial.printf("Clock STOP -> %02d:%02d", min_time, sec_time);
    }
    Serial.println();
  }

  // รับคำสั่งตัวอักษรจาก Serial Monitor
  if (Serial.available()) {
    char c = Serial.read();

    if (c == '1') { // กดเลข 1 เพื่อเริ่มนับ
      Serial.println("Start Timer");
      state_enable = true;
    } else if (c == '2') { // กดเลข 2 เพื่อหยุดนับโดยเก็บเวลาปัจจุบัน
      Serial.println("Stop Timer");
      state_enable = false;
    }
  }
}