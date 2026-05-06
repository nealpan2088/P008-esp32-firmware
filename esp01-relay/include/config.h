// ============================================================
// ESP-01 继电器模块 — 配置文件
// ============================================================
#ifndef CONFIG_H
#define CONFIG_H

// --------------- WiFi ---------------
#ifndef WIFI_SSID
#define WIFI_SSID     "your_wifi_ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "your_wifi_password"
#endif

// --------------- API ---------------
#ifndef API_BASE_URL
#define API_BASE_URL  "https://zghj.openyun.xin/api/v1"
#endif

// --------------- 自动序列号前缀 ---------------
// 命名规范: {类型}-{供电}-{芯片ID}
// 例: RELAY-PL-A1B2C3D4
// 由 platformio.ini 的 build_flags 传入
#ifndef DEVICE_SERIAL_PREFIX
#define DEVICE_SERIAL_PREFIX "RELAY-PL-"
#endif

// --------------- 安全配置 ---------------
#ifndef HW_SECRET
#define HW_SECRET "P008@2026!SecretKey"
#endif

// --------------- 固件版本 ---------------
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0"
#endif

// --------------- 固件发布渠道 ---------------
#ifndef FIRMWARE_CHANNEL
#define FIRMWARE_CHANNEL "self"
#endif

// --------------- 引脚定义 ---------------
// ESP-01 引脚（模块板载已接好继电器模块）
#ifndef RELAY_PIN
#define RELAY_PIN       0   // GPIO0 — 继电器控制（模块板载已连好）
#endif
#ifndef LED_BUILTIN
#define LED_BUILTIN     2   // GPIO2 — 板载 LED（低电平亮）
#endif

// --------------- 继电器逻辑 ---------------
// NC（常闭）型继电器模块：NC-COM 默认导通
// 取反逻辑：POWER_ON=断开继电器（设备断电安全态），POWER_OFF=吸合（设备通电）
#ifndef RELAY_ON
#define RELAY_ON  HIGH   // 断开继电器 → 设备断电
#endif
#ifndef RELAY_OFF
#define RELAY_OFF LOW    // 吸合继电器 → 设备通电
#endif

// --------------- 时间配置 ---------------
#ifndef SERIAL_BAUD
#define SERIAL_BAUD   115200
#endif
#ifndef HTTP_TIMEOUT_MS
#define HTTP_TIMEOUT_MS 5000
#endif

// --------------- 指令轮询间隔 ---------------
#ifndef COMMAND_POLL_INTERVAL_MS
#define COMMAND_POLL_INTERVAL_MS 5000   // 每 5 秒轮询一次，更快的响应
#endif

// --------------- AP 热点名 ---------------
#ifndef AP_NAME
#define AP_NAME "P008-Relay"
#endif

// --------------- 日志级别 ---------------
#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#endif /* CONFIG_H */

// ============================================================
// 自动化约束
// ============================================================
#ifndef DEVICE_SERIAL_PREFIX
  #error "❌ DEVICE_SERIAL_PREFIX 未定义！"
#endif
