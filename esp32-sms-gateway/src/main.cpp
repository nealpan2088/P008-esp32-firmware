// ============================================================
// P008 — ESP32 + A7670C 短信报警网关
// ============================================================
// 通过 MQTT 接收后端报警消息，驱动 A7670C 4G 模块发送短信
//
// 工作流程:
//   1. WiFiManager 配网（首次烧录后开热点配 WiFi）
//   2. 连接 MQTT Broker，订阅 p008/sms/alert
//   3. 初始化 A7670C 模块，检查 SIM 卡和信号
//   4. 收到 MQTT 消息 → 解析 JSON → 驱动 A7670C 发短信
//   5. 发送结果通过 MQTT 回执到 p008/sms/status
//   6. 每 60 秒上报心跳（在线状态 + 信号强度 + SIM 状态）

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "log.h"

// ============================================================
// 全局变量
// ============================================================
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// A7670C 串口
HardwareSerial A7670CSerial(2);   // UART2

// 系统状态
unsigned long _lastHeartbeat = 0;
int _signalStrength = 99;          // 0-31, 99=未知
bool _simReady = false;
unsigned long _lastReconnectAttempt = 0;
bool _wifiConfigured = false;

// WiFiManager
WiFiManager _wifiManager;

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
      LOG_D("A7670C", "AT OK: %s", cmd);
      return true;
    }
    delay(10);
  }

  LOG_W("A7670C", "AT TIMEOUT: %s (expect=%s)", cmd, expect);
  return false;
}

// 读取 AT 命令返回的完整响应
void readATResponse(char* buf, int bufSize, unsigned long timeoutMs) {
  unsigned long start = millis();
  int pos = 0;
  memset(buf, 0, bufSize);

  while (millis() - start < timeoutMs) {
    while (A7670CSerial.available() && pos < bufSize - 1) {
      char c = A7670CSerial.read();
      if (c >= 32 && c <= 126) buf[pos++] = c;  // 只保留可打印字符
    }
    if (pos > 0 && (strstr(buf, "OK") || strstr(buf, "ERROR"))) break;
    delay(10);
  }
}

// 初始化 A7670C
bool initA7670C() {
  LOG_I("A7670C", "Initializing...");

  // 先尝试发 AT，看模块是否已经开机
  if (sendAT("AT", "OK", 2000)) {
    LOG_I("A7670C", "Module already awake");
  } else {
    LOG_I("A7670C", "Module not responding yet, sending AT again...");
    delay(1000);
    if (!sendAT("AT", "OK", 3000)) {
      LOG_E("A7670C", "Module not responding, check power and PWR_KEY");
      return false;
    }
  }

  // 查询信号质量
  char buf[128];
  sendAT("AT+CSQ", "+CSQ:", 2000);
  readATResponse(buf, sizeof(buf), 1000);
  char* csqStr = strstr(buf, "+CSQ:");
  if (csqStr) {
    csqStr += 6;
    while (*csqStr && (*csqStr < '0' || *csqStr > '9')) csqStr++;
    if (*csqStr) _signalStrength = atoi(csqStr);
  }

  // 检查 SIM 卡
  _simReady = sendAT("AT+CPIN?", "+CPIN: READY", 3000);

  LOG_I("A7670C", "Init done. Signal: %d, SIM: %s", _signalStrength, _simReady ? "READY" : "NO SIM");
  return _simReady;
}

// UTF-8 → UCS2 HEX（4 位 HEX 表示一个 Unicode 字符）
void utf8ToUcs2Hex(const char* utf8, char* hexOut, size_t maxLen) {
  size_t pos = 0;
  while (*utf8 && pos < maxLen - 5) {
    uint32_t codepoint = 0;
    uint8_t c = (uint8_t)*utf8;

    if (c < 0x80) {
      codepoint = c;
      utf8++;
    } else if ((c & 0xE0) == 0xC0) {
      codepoint = c & 0x1F;
      codepoint = (codepoint << 6) | ((uint8_t)utf8[1] & 0x3F);
      utf8 += 2;
    } else if ((c & 0xF0) == 0xE0) {
      codepoint = c & 0x0F;
      codepoint = (codepoint << 6) | ((uint8_t)utf8[1] & 0x3F);
      codepoint = (codepoint << 6) | ((uint8_t)utf8[2] & 0x3F);
      utf8 += 3;
    } else {
      utf8++;
      continue;
    }

    if (codepoint <= 0xFFFF) {
      pos += snprintf(hexOut + pos, maxLen - pos, "%04X", (unsigned int)codepoint);
    }
  }
  hexOut[pos] = '\0';
}

// 发送短信（TEXT 模式 + UCS2 编码，支持中文）
// AT 指令序列（实测有效）：
//   AT+CMGF=1
//   AT+CSCS="UCS2"
//   AT+CSMP=17,167,2,25     ← 必须设，2=UCS2, 25=有效期
//   AT+CMGS="<UCS2手机号>"  ← 手机号也要 UCS2 HEX！
//   > <UCS2内容><Ctrl+Z>
bool sendSms(const char* phone, const char* message) {
  LOG_I("SMS", "Sending to %s: %s", phone, message);

  // 1. TEXT 模式
  if (!sendAT("AT+CMGF=1", "OK", 2000)) {
    LOG_E("SMS", "Failed to set TEXT mode");
    return false;
  }

  // 2. UCS2 编码
  if (!sendAT("AT+CSCS=\"UCS2\"", "OK", 2000)) {
    LOG_E("SMS", "Failed to set UCS2 encoding");
    return false;
  }

  // 3. 设置 SMS 参数：17=默认,167=默认,2=UCS2,25=有效期
  if (!sendAT("AT+CSMP=17,167,2,25", "OK", 2000)) {
    LOG_W("SMS", "CSMP not supported, continuing anyway...");
  }

  // 4. 手机号转 UCS2 HEX
  char phoneUcs2[64];
  utf8ToUcs2Hex(phone, phoneUcs2, sizeof(phoneUcs2));

  // AT+CMGS="<UCS2手机号>"
  char cmd[128];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", phoneUcs2);
  if (!sendAT(cmd, ">", SMS_TIMEOUT_MS)) {
    LOG_E("SMS", "Failed to enter SMS mode");
    return false;
  }

  // 5. 短信内容转 UCS2 HEX 并发送 + Ctrl+Z
  char msgUcs2[512];
  utf8ToUcs2Hex(message, msgUcs2, sizeof(msgUcs2));

  A7670CSerial.print(msgUcs2);
  A7670CSerial.write(26);  // Ctrl+Z

  // 等待 +CMGS 响应
  if (sendAT("", "+CMGS:", 15000)) {
    LOG_I("SMS", "Sent successfully");
    return true;
  }

  LOG_E("SMS", "Send failed (timeout)");
  return false;
}

// 更新信号强度
void updateSignal() {
  char buf[64];
  sendAT("AT+CSQ", "+CSQ:", 2000);
  readATResponse(buf, sizeof(buf), 1000);
  char* csqStr = strstr(buf, "+CSQ:");
  if (csqStr) {
    csqStr += 6;
    while (*csqStr && (*csqStr < '0' || *csqStr > '9')) csqStr++;
    if (*csqStr) _signalStrength = atoi(csqStr);
  }
}

// ============================================================
// MQTT 回调 — 收到报警消息
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char buf[512];
  unsigned int len = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = '\0';

  LOG_I("MQTT", "Received: %s", buf);

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

  LOG_I("SMS", "Queued: to=%s (queue=%d)", phone, (_smsQueueTail - _smsQueueHead + SMS_QUEUE_MAX) % SMS_QUEUE_MAX);
}

// ============================================================
// MQTT 连接管理
// ============================================================
bool connectMQTT() {
  if (!mqttClient.connected()) {
    LOG_I("MQTT", "Connecting to %s:%d...", MQTT_BROKER, MQTT_PORT);

    // 设置遗嘱消息：网关离线时通知
    bool ok = mqttClient.connect(MQTT_CLIENT_ID, MQTT_TOPIC_STATUS, 0, true, "{\"status\":\"offline\"}");

    if (ok) {
      LOG_I("MQTT", "Connected as %s", MQTT_CLIENT_ID);
      mqttClient.subscribe(MQTT_TOPIC_ALERT);
      LOG_I("MQTT", "Subscribed: %s", MQTT_TOPIC_ALERT);
      return true;
    } else {
      LOG_W("MQTT", "Connect failed, rc=%d", mqttClient.state());
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

  if (mqttClient.beginPublish(MQTT_TOPIC_STATUS, strlen(buf), false)) {
    mqttClient.print(buf);
    mqttClient.endPublish();
  }
}

// ============================================================
// 处理短信队列
// ============================================================
void processSmsQueue() {
  if (_smsQueueHead == _smsQueueTail) return;

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

  if (mqttClient.beginPublish(MQTT_TOPIC_STATUS, strlen(buf), false)) {
    mqttClient.print(buf);
    mqttClient.endPublish();
  }
}

// ============================================================
// WiFiManager 配置保存回调
// ============================================================
void wifiSaveCallback() {
  LOG_I("WiFi", "Configuration saved, restarting...");
  _wifiConfigured = true;
}

// ============================================================
// setup
// ============================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  LOG_I("Boot", "P008 SMS Gateway v0.1.0");
  LOG_I("Boot", "Flash: %d KB", ESP.getFlashChipSize() / 1024);

  // ---------- 初始化 A7670C 串口 ----------
  A7670CSerial.begin(A7670C_BAUD, SERIAL_8N1, A7670C_RX_PIN, A7670C_TX_PIN);
  LOG_I("A7670C", "UART2 (RX=%d, TX=%d, baud=%d)", A7670C_RX_PIN, A7670C_TX_PIN, A7670C_BAUD);

  // ---------- WiFiManager 配网 ----------
  LOG_I("WiFi", "Starting WiFiManager...");
  _wifiManager.setTitle("P008 SMS Gateway");
  _wifiManager.setConfigPortalTimeout(WM_PORTAL_TIMEOUT);
  _wifiManager.setSaveConfigCallback(wifiSaveCallback);
  _wifiManager.setConnectTimeout(15);

  // 不保存 WiFi 凭据到额外参数，WiFiManager 自动管理
  bool connected = _wifiManager.autoConnect(WIFI_MANAGER_AP_NAME);

  if (!connected) {
    LOG_E("WiFi", "WiFiManager failed after %d seconds", WM_PORTAL_TIMEOUT);
    LOG_E("WiFi", "Restarting to try again...");
    delay(2000);
    ESP.restart();
  }

  LOG_I("WiFi", "Connected, IP: %s", WiFi.localIP().toString().c_str());

  // ---------- 初始化 MQTT ----------
  wifiClient.setInsecure();  // TLS 跳过证书验证
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
