// ============================================================
// P008 NodeMCU 1路光耦隔离继电器模块 — 配置文件
// ============================================================
// 硬件: NodeMCU V3 (ESP8266) + 1路光耦隔离继电器模块
// 功能: 远程控制、定时开关、周期通断、本地报警
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
// 例: RELAY-NM-A1B2C3D4 (NodeMCU 继电器版)
// 由 platformio.ini 的 build_flags 传入
#ifndef DEVICE_SERIAL_PREFIX
#define DEVICE_SERIAL_PREFIX "RELAY-NM-"
#endif

// --------------- 安全配置 ---------------
#ifndef HW_SECRET
#define HW_SECRET "P008@2026!SecretKey"
#endif

// --------------- 固件版本 ---------------
// 版本号唯一来源：hardware/esp8266-relay/VERSION
// 由 platformio.ini 的 build_flags 传入
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0"
#endif

// --------------- 固件发布渠道 ---------------
#ifndef FIRMWARE_CHANNEL
#define FIRMWARE_CHANNEL "self"
#endif

// --------------- 引脚定义 ---------------
// NodeMCU 引脚映射:
//   D0=GPIO16  D1=GPIO5  D2=GPIO4  D3=GPIO0
//   D4=GPIO2   D5=GPIO14 D6=GPIO12 D7=GPIO13 D8=GPIO15
//
// 1路光耦隔离继电器模块（默认低电平触发）:
//   RELAY IO  → D1 (GPIO5) — 继电器控制信号
//   模块 VCC  → Vin (5V)   — 继电器线圈供电（部分模块支持 3.3V，看模块型号）
//   模块 GND  → GND
//   光耦侧无需额外接线，模块板载已集成
//
// ⚠️ 低电平触发: digitalWrite(RELAY_PIN, LOW)=吸合(导通)，HIGH=断开
// ⚠️ 上电初始默认高电平(断开)，避免误触发
#ifndef RELAY_PIN
#define RELAY_PIN       5   // D1 (GPIO5) — 继电器控制
#endif
#ifndef LED_BUILTIN
#define LED_BUILTIN     2   // D4 (GPIO2) — 板载 LED（低电平亮）
#endif

// --------------- 继电器逻辑 ---------------
// 高电平触发继电器：
//   RELAY_PULL = HIGH  → 吸合（设备通电）
//   RELAY_RELEASE = LOW → 断开（设备断电）
//   INITIAL_STATE = RELAY_RELEASE → 上电默认断开（安全）
#ifndef RELAY_PULL
#define RELAY_PULL      HIGH    // 吸合
#endif
#ifndef RELAY_RELEASE
#define RELAY_RELEASE   LOW     // 断开
#endif
#ifndef RELAY_INIT
#define RELAY_INIT      RELAY_RELEASE  // 初始状态：断开
#endif

// --------------- 时间配置 ---------------
#ifndef SERIAL_BAUD
#define SERIAL_BAUD     115200
#endif
#ifndef HTTP_TIMEOUT_MS
#define HTTP_TIMEOUT_MS  5000
#endif
#ifndef WIFI_TIMEOUT_MS
#define WIFI_TIMEOUT_MS 30000
#endif

// --------------- 指令轮询 ---------------
#ifndef COMMAND_POLL_INTERVAL_MS
#define COMMAND_POLL_INTERVAL_MS 5000   // 每 5 秒轮询一次待执行指令
#endif

// --------------- 心跳上报 ---------------
#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS 60000     // 每 60 秒上报一次在线状态
#endif

// --------------- MQTT ---------------
// 公网连接：zghj.openyun.xin:8883（TLS）
// 内网连接：192.168.x.x:1883（原生）
#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST "zghj.openyun.xin"
#endif
#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT 8883
#endif
#ifndef MQTT_TOPIC_PREFIX
#define MQTT_TOPIC_PREFIX "p008"
#endif
#ifndef MQTT_RECONNECT_DELAY_MS
#define MQTT_RECONNECT_DELAY_MS 5000    // MQTT 重连间隔
#endif

// --------------- 本地控制（手动开关按钮） ---------------
// 如需手动开关按钮：
//   物理按钮一端接 GND，一端接此引脚
//   短按(<1s)切换继电器状态
//   长按(>3s)进入配网模式
// 默认 D3(GPIO0/FLASH) — NodeMCU 板载 FLASH 按钮
#ifndef BTN_PIN
#define BTN_PIN         0   // D3 (GPIO0) — FLASH 按钮
#endif
#ifndef DEBOUNCE_MS
#define DEBOUNCE_MS     50  // 消抖时间
#endif
#ifndef LONG_PRESS_MS
#define LONG_PRESS_MS   3000  // 长按进入配网
#endif

// --------------- AP 热点名 ---------------
#ifndef AP_NAME
#define AP_NAME "P008-Relay"
#endif

// --------------- 日志级别 ---------------
#ifndef LOG_LEVEL
#define LOG_LEVEL 3  // 3=INFO, 4=DEBUG
#endif

#endif /* CONFIG_H */

// ============================================================
// 自动化约束
// ============================================================
#ifndef DEVICE_SERIAL_PREFIX
  #error "❌ DEVICE_SERIAL_PREFIX 未定义！"
#endif
