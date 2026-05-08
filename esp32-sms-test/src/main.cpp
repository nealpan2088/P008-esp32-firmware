// ESP32 + A7670C 串口测试程序
// UART2 (GPIO16 RX, GPIO17 TX) 连 A7670C
// 串口监视器输入 AT 指令 → 转发给 A7670C → 回显

#include <Arduino.h>

#define UART2_RX  16
#define UART2_TX  17

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\r\n==========================");
  Serial.println("ESP32 + A7670C Serial Test");
  Serial.println("==========================");
  Serial.printf("UART2: RX=GPIO%d, TX=GPIO%d, Baud=115200\r\n", UART2_RX, UART2_TX);
  Serial.println();
  Serial.println("Commands: AT, AT+CSQ, AT+CPIN?, AT+CMGF=1");
  Serial.println("Type AT commands and press Enter:");
  Serial.println("==========================\r\n");

  Serial2.begin(115200, SERIAL_8N1, UART2_RX, UART2_TX);
}

void loop() {
  // 监视器 → A7670C（一次性读取整行）
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial2.println(cmd);
      Serial.printf(">> %s\r\n", cmd.c_str());
    }
  }

  // A7670C → 监视器
  if (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.printf("<< %s\r\n", line.c_str());
    }
  }
}
