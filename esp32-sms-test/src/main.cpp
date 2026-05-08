// ESP32-S3 + A7670C 串口测试（简化版）
// 只使用 Serial (UART0) 和 Serial2 (UART2)
// A7670C: TX→GPIO16, RX→GPIO17
// 不要在 loop 里打印多余内容，只在收到 AT 回复时输出

#include <Arduino.h>

#define UART2_RX  16
#define UART2_TX  17

void setup() {
  delay(500);  // 等 USB CDC 稳定
  Serial.begin(115200);
  delay(100);

  Serial.println("\nESP32-S3 A7670C Test Ready");
  Serial.println("Type AT+xxx in SSCOM:");

  Serial2.begin(115200, SERIAL_8N1, UART2_RX, UART2_TX);
}

void loop() {
  static unsigned long lastPrint = 0;

  // 从 Serial（SSCOM/COM口）读取，转发给 A7670C
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial2.println(cmd);
      // 不转发回 Serial，避免刷屏
    }
  }

  // 从 A7670C 读取，转发回 Serial
  if (Serial2.available()) {
    String resp = Serial2.readStringUntil('\n');
    resp.trim();
    if (resp.length() > 0 && !resp.startsWith("AT")) {
      Serial.println(resp);
    }
  }

  // 每隔 5 秒打印一个 . 表示程序还活着
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    Serial.print(".");
  }
}
