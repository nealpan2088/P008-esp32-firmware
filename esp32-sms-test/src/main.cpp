// ESP32-S3 + A7670C 串口测试 v2（带调试输出）
// 显示每步操作，方便排查问题

#include <Arduino.h>

#define UART2_RX  16
#define UART2_TX  17
#define LED  LED_BUILTIN

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  delay(500);
  Serial.begin(115200);
  delay(100);

  Serial.println("\n==== A7670C Test v2 ====");
  Serial.printf("UART2: RX=%d, TX=%d\n", UART2_RX, UART2_TX);

  Serial2.begin(115200, SERIAL_8N1, UART2_RX, UART2_TX);

  // 一启动就发 AT 测试 A7670C
  Serial.println("Sending: AT");
  Serial2.println("AT");
}

void loop() {
  static int count = 0;

  // 读 Serial（来自调试器）
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial.printf(">>> Forwarding: %s\n", cmd.c_str());
      Serial2.println(cmd);
    }
  }

  // 读 Serial2（来自 A7670C）
  if (Serial2.available()) {
    String resp = Serial2.readStringUntil('\n');
    resp.trim();
    if (resp.length() > 0) {
      Serial.printf("<<< A7670C: %s\n", resp.c_str());
    }
  }

  // 每 3 秒闪一下灯表示活着
  delay(3000);
  digitalWrite(LED, !digitalRead(LED));
  Serial.printf("[%d] .\n", ++count);
}
