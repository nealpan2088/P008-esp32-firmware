// ESP32-S3 + A7670C 串口测试
// ESP32-S3 的 USB 口可能是 USB CDC，要用 Serial.begin + USB CDC 模式
// UART2 (GPIO16 RX, GPIO17 TX) 连 A7670C

#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32S3)
  // ESP32-S3 原生 USB CDC 需要延时等待 USB 连接
  #define WAIT_FOR_SERIAL  delay(3000)
#else
  #define WAIT_FOR_SERIAL  delay(500)
#endif

#define UART2_RX  16
#define UART2_TX  17

void setup() {
  // ESP32-S3: 启用 USB CDC 作为 Serial
  #if defined(USB_CDC) || defined(CONFIG_USB_CDC)
    USBSerial.begin(115200);
    delay(100);
    USBSerial.println("\r\n==========================");
    USBSerial.println("ESP32-S3 + A7670C Serial Test");
    USBSerial.println("==========================");
    USBSerial.printf("UART2: RX=GPIO%d, TX=GPIO%d, Baud=115200\r\n", UART2_RX, UART2_TX);
    USBSerial.println();
    USBSerial.println("Type AT commands and press Enter:");
    USBSerial.println("==========================\r\n");
  #endif

  WAIT_FOR_SERIAL;

  Serial.begin(115200);
  delay(100);

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
  // 从 Serial（UART）读取
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial2.println(cmd);
      Serial.printf(">> %s\r\n", cmd.c_str());
    }
  }

  // 从 USBSerial（USB CDC）读取
  #if defined(USB_CDC) || defined(CONFIG_USB_CDC)
  if (USBSerial.available()) {
    String cmd = USBSerial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial2.println(cmd);
      USBSerial.printf(">> %s\r\n", cmd.c_str());
    }
  }
  #endif

  // A7670C → 串口监视器
  if (Serial2.available()) {
    String line = Serial2.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      Serial.printf("<< %s\r\n", line.c_str());
      #if defined(USB_CDC) || defined(CONFIG_USB_CDC)
      USBSerial.printf("<< %s\r\n", line.c_str());
      #endif
    }
  }
}
