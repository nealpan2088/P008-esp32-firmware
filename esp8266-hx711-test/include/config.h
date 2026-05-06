/**
 * ESP8266 + HX711 称重传感器 — 硬件配置
 * 
 * 接线:
 *   HX711 VCC → 3.3V
 *   HX711 GND → GND
 *   HX711 DT  → GPIO4 (D2)
 *   HX711 SCK → GPIO5 (D1)
 */

#ifndef CONFIG_H
#define CONFIG_H

// ========== 板载硬件 ==========
#ifndef SERIAL_BAUD
#define SERIAL_BAUD 115200
#endif

// ========== HX711 引脚 ==========
// 由 platformio.ini build_flags 传入，不可在 config.h 硬编码
#ifndef HX711_DT_PIN
#define HX711_DT_PIN 4   // D2
#endif

#ifndef HX711_SCK_PIN
#define HX711_SCK_PIN 5  // D1
#endif

// ========== 设备身份 ==========
// 由 platformio.ini build_flags 传入
#ifndef DEVICE_SERIAL_PREFIX
#error "DEVICE_SERIAL_PREFIX 未定义！必须在 platformio.ini build_flags 中设置"
#endif

#ifndef FIRMWARE_CHANNEL
#define FIRMWARE_CHANNEL "self"
#endif

// ========== 量程校准（需标定） ==========
#ifndef CALIBRATION_FACTOR
#define CALIBRATION_FACTOR -22360.0f
#endif

#endif // CONFIG_H
