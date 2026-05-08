// ============================================================
// P008 — ESP32 + A7670C 短信报警网关
// ============================================================
// 通过 MQTT 接收后端报警消息，驱动 A7670C 4G 模块发送短信
//
// 工作流程:
//   1. 连接 WiFi
//   2. 连接 MQTT Broker，订阅 p008/sms/alert
//   3. 初始化 A7670C 模块，检查 SIM 卡和信号
//   4. 收到 MQTT 消息 → 解析 JSON → 驱动 A7670C 发短信
//   5. 发送结果通过 MQTT 回执到 p008/sms/status
//   6. 每 60 秒上报心跳（在线状态 + 信号强度 + SIM 状态）

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "log.h"

// ============================================================
// 全局变量
// ============================================================
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// A7670C 串口
HardwareSerial A7670CSerial(2);   // UART2

// 系统状态
unsigned long _lastHeartbeat = 0;
int _signalStrength = 0;          // 0-31, 99=未知
bool _simReady = false;
bool _mqttConnected = false;
unsigned long _lastReconnectAttempt = 0;

// 待发送短信队列（最多缓存 10 条）
#define SMS_QUEUE_MAX 10
struct SmsTask {
  char phone[20];
  char message[200];
  char alertId[64];
  bool active;
};
SmsTask _smsQueue[SMS_QUEUE_MAX];
int _smsQueueHead = 0;
int _smsQueueTail = 0;
unsigned long _lastSmsSend = 0;

// ============================================================
// A7670C AT 指令操作
// ============================================================

// 发送 AT 指令并等待期望的响应
bool sendAT(const char* cmd, const char* expect, unsigned long timeoutMs) {
  A7670CSerial.print(cmd);
  A7670CSerial.print("\r\n");

  unsigned long start = millis();
  char buf[128];
  int pos = 0;
  memset(buf, 0, sizeof(buf));

  while (millis() - start < timeoutMs) {
    while (A7670CSerial.available()) {
      char c = A7670CSerial.read();
      if (pos < (int)sizeof(buf) - 1) buf[pos++] = c;
    }
    if (strstr(buf, expect)) {
      LOG_D("A7670C", "AT OK: %s → %s", cmd, buf);
      return true;
    }
    delay(10);
  }

  LOG_W("A7670C", "AT TIMEOUT: %s (expect=%s, got=%s)", cmd, expect, buf);
  return false;
}

// 初始化 A7670C
bool initA7670C() {
  LOG_I("A7670C", "Initializing...");

  // 复位模块
  sendAT("AT+CRESET", "OK", 3000);
  delay(3000);

  // 检查模块响应
  if (!sendAT("AT", "OK", 2000)) {
    LOG_E("A7670C", "Module not responding");
    return false;
  }

  // 查询信号质量
  if (sendAT("AT+CSQ", "+CSQ:", 3000)) {
    // 解析信号值
    char buf[64];
    int pos = 0;
    memset(buf, 0, sizeof(buf));
    while (A7670CSerial.available() && pos < 63) {
      buf[pos++] = A7670CSerial.read();
    }
    // 提取 +CSQ 后面的数字
    char* csqStr = strstr(buf, "+CSQ:");
    if (csqStr) {
      csqStr += 6; // skip "+CSQ: "
      _signalStrength = atoi(csqStr);
    }
  }

  // 检查 SIM 卡
  _simReady = sendAT("AT+CPIN?", "+CPIN: READY", 3000);

  // 设置短信格式为 TEXT 模式
  if (!sendAT("AT+CMGF=1", "OK", 2000)) {
    LOG_W("A7670C", "Failed to set SMS text mode");
  }

  // 设置字符集
  sendAT("AT+CSCS=\"GSM\"", "OK", 2000);

  LOG_I("A7670C", "Init done. Signal: %d, SIM: %s", _signalStrength, _simReady ? "READY" : "NO SIM");
  return true;
}

// 发送短信
bool sendSms(const char* phone, const char* message) {
  LOG_I("SMS", "Sending to %s: %s", phone, message);

  // AT+CMGS="13800138000"
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", phone);
  if (!sendAT(cmd, ">", SMS_TIMEOUT_MS)) {
    LOG_E("SMS", "Failed to enter SMS mode");
    return false;
  }

  // 发送短信内容 + Ctrl+Z 结束
  // 注意：TEXT 模式下内容不能包含某些特殊字符
  A7670CSerial.print(message);
  A7670CSerial.write(26);  // Ctrl+Z
  A7670CSerial.print("\r\n");

  // 等待发送完成响应
  delay(3000);
  if (sendAT("", "+CMGS:", 8000)) {
    LOG_I("SMS", "Sent successfully");
    return true;
  }

  LOG_E("SMS", "Send failed (timeout or error)");
  return false;
}

// 查询信号强度（更新 _signalStrength）
void updateSignal() {
  if (sendAT("AT+CSQ", "+CSQ:", 2000)) {
    delay(100);
    char buf[64];
    int pos = 0;
    memset(buf, 0, sizeof(buf));
    while (A7670CSerial.available() && pos < 63) {
      buf[pos++] = A7670CSerial.read();
    }
    char* csqStr = strstr(buf, "+CSQ:");
    if (csqStr) {
      csqStr += 6;
      _signalStrength = atoi(csqStr);
    }
  }
}

// ============================================================
// MQTT 回调 — 收到报警消息
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // 转成 null-terminated 字符串
  char buf[512];
  unsigned int len = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = '\0';

  LOG_I("MQTT", "Received: %s", buf);

  // 解析 JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, buf);
  if (error) {
    LOG_E("MQTT", "JSON parse error: %s", error.c_str());
    return;
  }

  const char* phone = doc["to"];
  const char* message = doc["message"];
  const char* alertId = doc["id"];

  if (!phone || !message) {
    LOG_E("MQTT", "Missing required fields (to, message)");
    return;
  }

  // 加入发送队列
  int nextTail = (_smsQueueTail + 1) % SMS_QUEUE_MAX;
  if (nextTail == _smsQueueHead) {
    LOG_E("SMS", "Queue full, dropping: %s", alertId);
    return;
  }

  strncpy(_smsQueue[_smsQueueTail].phone, phone, sizeof(_smsQueue[_smsQueueTail].phone) - 1);
  strncpy(_smsQueue[_smsQueueTail].message, message, sizeof(_smsQueue[_smsQueueTail].message) - 1);
  strncpy(_smsQueue[_smsQueueTail].alertId, alertId ? alertId : "", sizeof(_smsQueue[_smsQueueTail].alertId) - 1);
  _smsQueue[_smsQueueTail].active = true;
  _smsQueueTail = nextTail;

  LOG_I("SMS", "Queued: to=%s, id=%s (queue size=%d)", phone, alertId, (_smsQueueTail - _smsQueueHead + SMS_QUEUE_MAX) % SMS_QUEUE_MAX);
}

// ============================================================
// MQTT 连接管理
// ============================================================
bool connectMQTT() {
  if (!mqttClient.connected()) {
    LOG_I("MQTT", "Connecting to %s:%d...", MQTT_BROKER, MQTT_PORT);

    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      LOG_I("MQTT", "Connected as %s", MQTT_CLIENT_ID);

      // 订阅报警 Topic
      mqttClient.subscribe(MQTT_TOPIC_ALERT);
      LOG_I("MQTT", "Subscribed to %s", MQTT_TOPIC_ALERT);

      _mqttConnected = true;
      return true;
    } else {
      LOG_W("MQTT", "Connect failed, rc=%d", mqttClient.state());
      _mqttConnected = false;
      return false;
    }
  }
  return true;
}

// 发送状态回执
void sendStatus(const char* alertId, const char* status, const char* error) {
  if (!mqttClient.connected()) return;

  JsonDocument doc;
  doc["id"] = alertId;
  doc["status"] = status;
  if (error) doc["error"] = error;
  doc["timestamp"] = millis() / 1000;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(MQTT_TOPIC_STATUS, buf);
  LOG_D("MQTT", "Status sent: %s", buf);
}

// ============================================================
// 处理短信队列
// ============================================================
void processSmsQueue() {
  if (_smsQueueHead == _smsQueueTail) return; // 队列为空

  // 节流：两次发送间隔至少 5 秒
  if (millis() - _lastSmsSend < SMS_BATCH_INTERVAL_MS) return;

  SmsTask& task = _smsQueue[_smsQueueHead];

  if (task.active) {
    bool ok = sendSms(task.phone, task.message);
    sendStatus(task.alertId, ok ? "sent" : "failed", ok ? nullptr : "SMS send timeout");
    task.active = false;
    _lastSmsSend = millis();
  }

  _smsQueueHead = (_smsQueueHead + 1) % SMS_QUEUE_MAX;
}

// ============================================================
// 心跳上报
// ============================================================
void reportHeartbeat() {
  if (!mqttClient.connected()) return;

  updateSignal();

  JsonDocument doc;
  doc["clientId"] = MQTT_CLIENT_ID;
  doc["signal"] = _signalStrength;
  doc["simReady"] = _simReady;
  doc["uptime"] = millis() / 1000;
  doc["queueSize"] = (_smsQueueTail - _smsQueueHead + SMS_QUEUE_MAX) % SMS_QUEUE_MAX;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish("p008/sms/status", buf);
  LOG_D("Heartbeat", "Sent: %s", buf);
}

// ============================================================
// setup
// ============================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  LOG_I("Boot", "P008 SMS Gateway v1.0");
  LOG_I("Boot", "Flash: %d KB", ESP.getFlashChipSize() / 1024);

  // ---------- 初始化 A7670C ----------
  A7670CSerial.begin(A7670C_BAUD, SERIAL_8N1, A7670C_RX_PIN, A7670C_TX_PIN);
  LOG_I("A7670C", "UART2 initialized (RX=%d, TX=%d, baud=%d)", A7670C_RX_PIN, A7670C_TX_PIN, A7670C_BAUD);

  // ---------- 连接 WiFi ----------
  LOG_I("WiFi", "Connecting to %s...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setAutoReconnect(true);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(1000);
    retries++;
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    LOG_E("WiFi", "Failed to connect after 30s");
  } else {
    LOG_I("WiFi", "Connected, IP: %s", WiFi.localIP().toString().c_str());
  }

  // ---------- 初始化 MQTT ----------
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(30);
  connectMQTT();

  // ---------- 初始化 A7670C ----------
  initA7670C();

  LOG_I("Main", "Ready. WiFi=%s, MQTT=%s, A7670C=%s",
    WiFi.status() == WL_CONNECTED ? "ON" : "OFF",
    mqttClient.connected() ? "ON" : "OFF",
    _simReady ? "ON" : "OFF");
}

// ============================================================
// loop
// ============================================================
void loop() {
  // ---------- MQTT 保活 ----------
  if (!mqttClient.connected()) {
    long now = millis();
    if (now - _lastReconnectAttempt > 30000) {
      _lastReconnectAttempt = now;
      connectMQTT();
    }
  } else {
    mqttClient.loop();
  }

  // ---------- 处理短信发送队列 ----------
  processSmsQueue();

  // ---------- 心跳上报 ----------
  if (millis() - _lastHeartbeat > HEARTBEAT_INTERVAL_MS) {
    _lastHeartbeat = millis();
    reportHeartbeat();
  }
}
