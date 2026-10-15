#include <Arduino.h>

// Timer สร้าง event ทุกวินาที และ sec_time เก็บจำนวนวินาที (ตัวอย่างตัวแปร)
hw_timer_t *timer1s = NULL;
bool flag_timer = false;
int sec_time = 0;

void IRAM_ATTR isr_timer() { flag_timer = true; }

void setup() {
  delay(20); // รอให้บอร์ดเริ่มทำงาน

  Serial.begin(115200); // จุดออกสอบ: UART0 ต่อ Serial Monitor
  Serial2.begin(9600); // UART2 เป็น serial อีกชุด ต้องตั้ง baud rate ตรงกับปลายทาง

  // ส่งข้อความทดสอบไปยังพอร์ตทั้งสอง
  Serial.println("Start Firmware");
  Serial2.println("Start Firmware");

  // ตั้ง timer ให้ส่ง flag ทุก 1 วินาที
  timer1s = timerBegin(0, 80, true);
  timerAlarmWrite(timer1s, 1000000, true);
  timerAttachInterrupt(timer1s, &isr_timer, true);
  timerAlarmEnable(timer1s);
}

void loop() {
  // เมื่อครบวินาที แสดงที่ UART0 และส่งคำว่า Peter ทาง UART2
  if (flag_timer) {
    flag_timer = false;
    Serial.println("T on UART0");
    Serial2.print("Peter"); // print ไม่เติม newline ต่อท้าย
  }

  // รับอักขระจาก UART0 แล้วแสดงค่า ASCII และแปลงเลขอักขระเป็นจำนวนเต็ม
  if (Serial.available()) {
    char c = Serial.read();
    Serial.print("Received: ");
    Serial.println(c, DEC);

    int num = (int)c - '0'; // ใช้ได้ตรง ๆ เมื่อ c เป็นอักขระ '0' ถึง '9'

    Serial.println(num);
  }
}