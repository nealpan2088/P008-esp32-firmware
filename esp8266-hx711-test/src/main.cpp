/**
 * P008 — ESP8266 + HX711 称重传感器测试固件
 *
 * 环境: fw-scale-test
 * 用途: 验证 HX711 模块能否正常读取重量数据
 * 序列号: SCALE-TEST-{chipId}
 *
 * 接线:
 *   HX711 DT  → D2 (GPIO4)
 *   HX711 SCK → D1 (GPIO5)
 *   HX711 VCC → 3.3V
 *   HX711 GND → GND
 */

#include <Arduino.h>
#include "config.h"
#include "HX711.h"

HX711 scale;

// 运行时变量
unsigned long lastReadMs = 0;
const unsigned long READ_INTERVAL_MS = 1000; // 1 秒读一次
bool sensorReady = false;

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(100); // 等串口稳定

  Serial.println();
  Serial.println("=== P008 HX711 称重传感器 ===");
  Serial.print("固件: fw-scale-test / 序列号前缀: ");
  Serial.println(DEVICE_SERIAL_PREFIX);
  Serial.print("引脚: DT=");
  Serial.print(HX711_DT_PIN);
  Serial.print(", SCK=");
  Serial.println(HX711_SCK_PIN);

  // 初始化 HX711
  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  delay(500);

  if (scale.wait_ready_timeout(1000)) {
    scale.tare(); // 去皮归零
    scale.set_scale(CALIBRATION_FACTOR);
    sensorReady = true;
    Serial.println("HX711 就绪 ✅");
  } else {
    Serial.println("HX711 未响应 ❌");
    Serial.println("检查: 1.接线(共地?) 2.供电 3.DT/SCK 是否接反");
  }

  Serial.println();
  Serial.println("时间(ms) | 重量(g) | RAW值");
  Serial.println("--------------------");
}

void loop() {
  unsigned long now = millis();
  if (now - lastReadMs < READ_INTERVAL_MS) return;
  lastReadMs = now;

  if (!sensorReady) {
    // 每秒重试一次，看传感器有没有恢复
    if (scale.wait_ready_timeout(200)) {
      scale.tare();
      scale.set_scale(CALIBRATION_FACTOR);
      sensorReady = true;
      Serial.println("HX711 恢复就绪 ✅");
    }
    return;
  }

  if (!scale.wait_ready_timeout(200)) {
    Serial.println("HX711 超时，跳过本轮");
    sensorReady = false;
    return;
  }

  long raw = scale.read();
  float weight = scale.get_units(5); // 5 次取平均

  Serial.print(now);
  Serial.print(" | ");
  Serial.print(weight, 1);
  Serial.print(" g | ");
  Serial.println(raw);
}
