#ifndef CONFIG_H
#define CONFIG_H

// --------------- WiFi 配置 ---------------
// 通过 WiFiManager 配网，不需要硬编码
// 首次烧录后，ESP32 会开热点 "P008-SMS-Gateway"，连上后配置 WiFi
#ifndef WIFI_MANAGER_AP_NAME
#define WIFI_MANAGER_AP_NAME   "P008-SMS-Gateway"
#endif
#ifndef WIFI_MANAGER_AP_PASS
#define WIFI_MANAGER_AP_PASS   "config123"
#endif

// --------------- MQTT 配置 ---------------
// Broker 地址，可在 platformio.ini build_flags 中覆盖
#ifndef MQTT_BROKER
#define MQTT_BROKER     "zghj.openyun.xin"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT       8883    // TLS MQTT（阿里云安全组已开放）
#endif
#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID  "sms-gateway-01"
#endif
#ifndef MQTT_TOPIC_ALERT
#define MQTT_TOPIC_ALERT  "p008/sms/alert"
#endif
#ifndef MQTT_TOPIC_STATUS
#define MQTT_TOPIC_STATUS "p008/sms/status"
#endif

// --------------- A7670C 串口 ---------------
#ifndef A7670C_RX_PIN
#define A7670C_RX_PIN   18    // ESP32 RX2 → A7670C TX
#endif
#ifndef A7670C_TX_PIN
#define A7670C_TX_PIN   19    // ESP32 TX2 → A7670C RX
#endif
#ifndef A7670C_BAUD
#define A7670C_BAUD     115200
#endif

// --------------- 心跳 ---------------
#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS  60000   // 60 秒上报一次在线状态
#endif

// --------------- 短信 ---------------
#ifndef SMS_TIMEOUT_MS
#define SMS_TIMEOUT_MS  10000    // 发送单条短信超时
#endif
#ifndef SMS_BATCH_INTERVAL_MS
#define SMS_BATCH_INTERVAL_MS  5000   // 批量发送间隔
#endif

// --------------- WiFiManager ---------------
#ifndef WM_PORTAL_TIMEOUT
#define WM_PORTAL_TIMEOUT   180    // 配网页面超时（秒）
#endif

#endif
