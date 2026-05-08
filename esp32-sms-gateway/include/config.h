#ifndef CONFIG_H
#define CONFIG_H

// --------------- WiFi ---------------
#ifndef WIFI_SSID
#define WIFI_SSID       "your_wifi_ssid"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS       "your_wifi_password"
#endif

// --------------- MQTT ---------------
#ifndef MQTT_BROKER
#define MQTT_BROKER     "zghj.openyun.xin"
#endif
#ifndef MQTT_PORT
#define MQTT_PORT       8883
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
#define A7670C_RX_PIN   16    // ESP32 RX2 → A7670C TX
#endif
#ifndef A7670C_TX_PIN
#define A7670C_TX_PIN   17    // ESP32 TX2 → A7670C RX
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

#endif
