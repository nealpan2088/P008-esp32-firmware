/**
 * P008 Environment Monitor — NodeMCU 继电器 + SG90 舵机固件
 * -----------------------------------------------------------
 * Hardware:  NodeMCU V3 (ESP8266) + 1路光耦隔离继电器模块 + SG90 舵机
 * Function:  远程控制（继电器+舵机）+ 手动按钮 + 心跳上报 + MQTT
 *
 * v1.5 新特性:
 *   - MQTT 实时指令推送（替代 HTTP 长轮询，延迟从 5s 降至 ~100ms）
 *   - HTTP 轮询降级（MQTT 断线时自动切回 HTTP 轮询）
 *   - SERVO_ANGLE / SERVO_SWEEP 指令支持 SG90 舵机控制
 *
 * 功能特性:
 *   1. 远程指令控制（POWER_ON / POWER_OFF / TOGGLE / REBOOT）
 *   2. 舵机控制（SERVO_ANGLE:角度 / SERVO_SWEEP）
 *   3. 手动按钮控制（短按切换、长按配网）
 *   4. 心跳上报（每 60 秒上报继电器 + 舵机角度）
 *   5. 本地报警（云端阈值下发 → 超限 LED 快闪）
 *   6. MQTT + HTTP 双模通信，自动降级兜底
 *
 * 接线（NodeMCU → 1路光耦隔离继电器模块）:
 *   继电器模块 IN  → D1 (GPIO5) — 控制信号（高电平触发）
 *   继电器模块 VCC → Vin (5V)
 *   继电器模块 GND → GND
 *   COM/NO/NC       → 按实际负载接
 *
 * 接线（NodeMCU → SG90 舵机）:
 *   SG90 棕色线 → GND
 *   SG90 红色线 → Vin (5V)
 *   SG90 橙色线 → D2 (GPIO4) — PWM 信号
 *
 * ⚠️ 高电平触发: 拉高=Led亮/继电器吸合；拉低=LED灭/继电器断开
 * ⚠️ 上电默认断开，避免设备误启动
 * ⚠️ SG90 舵机不要从 3.3V 取电（电流不够），必须用 Vin (5V)
 *
 * 首次配网:
 *   手机连 P008-Relay 热点 → 192.168.4.1 配 WiFi
 *
 * 换 WiFi / 重置:
 *   按住 FLASH 按钮上电 → 3 秒后释放 → 进入配网模式
 *
 * 序列号规范:
 *   RELAY-NM-{芯片8位HEX}
 *
 * 版本:
 *   见 VERSION 文件，编译时通过 build_flags 传入 FIRMWARE_VERSION
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Servo.h>

#ifdef MQTT_ENABLE
#include <PubSubClient.h>
#endif

#include "config.h"
#include "log.h"
#include "alert.h"

// ============================================================
// 全局变量
// ============================================================
WiFiClientSecure wifiClientSecure;
HTTPClient http;

WiFiClient wifiClient;          // 普通 WiFi 客户端（用于 MQTT plain）
WiFiClientSecure wifiClientTls;  // TLS WiFi 客户端（用于 MQTT over TLS）
#ifdef MQTT_ENABLE
// 根据端口选择是否使用 TLS
#if MQTT_BROKER_PORT == 8883
PubSubClient mqttClient(wifiClientTls);
#else
PubSubClient mqttClient(wifiClient);
#endif
#endif

char deviceSerial[32] = "";
char deviceKey[64]    = "";
char apiBaseUrl[128]  = API_BASE_URL;
char chipIdHex[16]    = "";
char _autoSerial[32]  = "";
char _autoKey[64]     = "";

bool _relayState = false;        // true=吸合(通), false=断开(断)
int _servoAngle = -1;            // -1=未初始化, 0-180=当前角度
Servo _servo;                    // SG90 舵机对象
unsigned long _lastPoll = 0;
unsigned long _lastHeartbeat = 0;
unsigned long _pollIntervalMs = COMMAND_POLL_INTERVAL_MS;
unsigned long _lastMqttAttempt = 0;
bool _mqttEnabled = false;

// 手动按钮消抖
unsigned long _lastBtnDebounce = 0;
bool _lastBtnState = HIGH;
bool _btnState = HIGH;
unsigned long _btnPressStart = 0;
bool _btnPressed = false;

// MQTT Topic 缓存
char _cmdTopic[64] = "";

// ============================================================
// HMAC 密钥生成
// ============================================================
#include <bearssl/bearssl_hash.h>

void generateDeviceKey(const char* input, char* out, size_t outLen) {
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, input, strlen(input));
  br_sha256_update(&ctx, HW_SECRET, strlen(HW_SECRET));
  unsigned char hash[32];
  br_sha256_out(&ctx, hash);

  char* p = out;
  for (int i = 0; i < 32 && p < out + outLen - 3; i++) {
    p += sprintf(p, "%02x", hash[i]);
  }
  *p = '\0';
}

// ============================================================
// 工具函数
// ============================================================
void generateIdentity() {
  uint32_t chipId = ESP.getChipId();
  snprintf(chipIdHex, sizeof(chipIdHex), "%08X", chipId);
  snprintf(_autoSerial, sizeof(_autoSerial), "%s%08X", DEVICE_SERIAL_PREFIX, chipId);
  generateDeviceKey(_autoSerial, _autoKey, sizeof(_autoKey));
  LOG_I("Identity", "Serial: %s", _autoSerial);
  LOG_I("Identity", "Key: %s", _autoKey);
}

void loadParams() {
  strncpy(deviceSerial, _autoSerial, sizeof(deviceSerial) - 1);
  strncpy(deviceKey, _autoKey, sizeof(deviceKey) - 1);
  LOG_I("Config", "Serial: %s", deviceSerial);

  // 构建 MQTT Topic: p008/{serial}/command
  snprintf(_cmdTopic, sizeof(_cmdTopic), "%s/%s/command", MQTT_TOPIC_PREFIX, deviceSerial);
}

// --------------- 继电器控制 ---------------
void relayOn() {
  digitalWrite(RELAY_PIN, RELAY_PULL);
  digitalWrite(LED_BUILTIN, LOW);    // LED 亮
  _relayState = true;
  LOG_I("Relay", "ON (吸合)");
}

void relayOff() {
  digitalWrite(RELAY_PIN, RELAY_RELEASE);
  digitalWrite(LED_BUILTIN, HIGH);   // LED 灭
  _relayState = false;
  LOG_I("Relay", "OFF (断开)");
}

void relayToggle() {
  if (_relayState) {
    relayOff();
  } else {
    relayOn();
  }
}

// --------------- 舵机控制（SG90）---------------
// 舵机引脚：D2 (GPIO4) — PWM 输出
// 接线：⚫GND→GND  🔴VCC→Vin(5V)  🟡信号→D2
//
// ⚠️ SG90 标准脉宽 500~2500us，但部分批次可能范围偏窄
// 如发现角度不匹配，调整 SERVO_MIN_US / SERVO_MAX_US 值
// 当前值：经过测试 500-2500 范围不够，扩大到 400-2600
#define SERVO_MIN_US   400
#define SERVO_MAX_US   2600

void servoAttach() {
  _servo.attach(4, SERVO_MIN_US, SERVO_MAX_US);  // D2 (GPIO4), 指定脉宽范围
  LOG_I("Servo", "Attached to D2 (GPIO4), range %d-%d us", SERVO_MIN_US, SERVO_MAX_US);
}

void servoSetAngle(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  _servo.write(angle);
  _servoAngle = angle;
  LOG_I("Servo", "Angle set to %d°", angle);
}

void servoDetach() {
  _servo.detach();
  LOG_I("Servo", "Detached (power save)");
}

void servoSweep() {
  LOG_I("Servo", "Sweeping 0°→180°→0°...");
  for (int a = 0; a <= 180; a += 5) {
    _servo.write(a);
    delay(15);
  }
  for (int a = 180; a >= 0; a -= 5) {
    _servo.write(a);
    delay(15);
  }
  _servoAngle = 0;
  LOG_I("Servo", "Sweep done");
}

// ============================================================
// WiFi 连接
// ============================================================
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  LOG_I("WiFi", "Connecting...");

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      LOG_W("WiFi", "Timeout after %dms", WIFI_TIMEOUT_MS);
      return false;
    }
    delay(200);
    ESP.wdtFeed();
  }

  LOG_I("WiFi", "Connected, IP: %s", WiFi.localIP().toString().c_str());
  return true;
}

// ============================================================
// 配网 Portal
// ============================================================
void startConfigPortal() {
  LOG_I("WiFiManager", "Starting config portal...");
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);  // 5 分钟超时
  wm.startConfigPortal(AP_NAME);
  LOG_I("WiFiManager", "Portal done, restarting...");
  delay(100);
  ESP.restart();
}

// ============================================================
// 心跳上报（注册 + 继电器状态）
// ============================================================
int reportHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return -1;

  char url[256];
  snprintf(url, sizeof(url), "%s/devices/%s/data", apiBaseUrl, deviceSerial);

  char body[350];
  snprintf(body, sizeof(body),
    "{\"temp\":null,\"humidity\":null,\"battery\":0,\"otherData\":{"
    "\"firmwareVer\":\"" FIRMWARE_VERSION "\",\"channel\":\"" FIRMWARE_CHANNEL "\","
    "\"chipId\":\"%s\",\"type\":\"relay\",\"relayOn\":%s,\"servoAngle\":%d"
    "}}",
    chipIdHex, _relayState ? "true" : "false", _servoAngle);

  LOG_D("Heartbeat", "POST %s", url);
  
  http.begin(wifiClient, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", deviceKey);

  int code = http.POST(body);
  String response = http.getString();
  http.end();

  // 解析心跳响应：提取 config.reportInterval + config.thresholds
  if (code == 200) {
    const char* respStr = response.c_str();
    // 解析轮询间隔
    const char* riKey = strstr(respStr, "\"reportInterval\"");
    if (riKey) {
      unsigned long newInterval = atol(riKey + 17);
      if (newInterval >= 10 && newInterval <= 3600) {
        _pollIntervalMs = newInterval * 1000UL;
      }
    }
    // 解析报警阈值
    const char* cfgKey = strstr(respStr, "\"thresholds\"");
    if (cfgKey) {
      alertParseThresholds(cfgKey);
    }
  }

  LOG_D("Heartbeat", "Code: %d", code);
  return code;
}

// ============================================================
// MQTT 回调 — 收到指令时执行
// ============================================================
#ifdef MQTT_ENABLE
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // 解析 JSON payload
  char buf[256];
  unsigned int len = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
  memcpy(buf, payload, len);
  buf[len] = '\0';

  LOG_D("MQTT", "Received: %s", buf);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buf);
  if (err) {
    LOG_W("MQTT", "JSON parse error: %s", err.c_str());
    return;
  }

  const char* command = doc["command"];
  if (!command) {
    LOG_W("MQTT", "No command field in MQTT message");
    return;
  }

  LOG_I("MQTT Cmd", "Executing: %s", command);

  if (strcmp(command, "POWER_ON") == 0) {
    relayOn();
  } else if (strcmp(command, "POWER_OFF") == 0) {
    relayOff();
  } else if (strcmp(command, "TOGGLE") == 0) {
    relayToggle();
  } else if (strcmp(command, "REBOOT") == 0) {
    LOG_I("MQTT Cmd", "Rebooting...");
    delay(500);
    ESP.restart();
  } else if (strcmp(command, "SERVO_ANGLE") == 0) {
    int angle = doc["payload"]["angle"] | doc["angle"] | 90;
    servoAttach();
    servoSetAngle(angle);
    delay(500);
    servoDetach();
  } else if (strcmp(command, "SERVO_SWEEP") == 0) {
    servoAttach();
    servoSweep();
    servoDetach();
  } else {
    LOG_W("MQTT Cmd", "Unsupported: %s", command);
  }
}

void connectMQTT() {
  if (!_mqttEnabled || WiFi.status() != WL_CONNECTED) return;

  LOG_I("MQTT", "Connecting to %s:%d...", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setServer(MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  mqttClient.setCallback(mqttCallback);

#if MQTT_BROKER_PORT == 8883
  // TLS 连接，跳过证书验证（ESP8266 无 RTC，无法验证证书有效期）
  wifiClientTls.setInsecure();
#endif

  char clientId[32];
  snprintf(clientId, sizeof(clientId), "relay-%s", deviceSerial);

  if (mqttClient.connect(clientId)) {
    LOG_I("MQTT", "Connected as %s", clientId);
    mqttClient.subscribe(_cmdTopic);
    LOG_I("MQTT", "Subscribed to %s", _cmdTopic);
  } else {
    LOG_W("MQTT", "Failed (state=%d)", mqttClient.state());
  }
}

void maintainMQTT() {
  if (!_mqttEnabled) return;
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - _lastMqttAttempt > MQTT_RECONNECT_DELAY_MS) {
      _lastMqttAttempt = now;
      connectMQTT();
    }
  } else {
    mqttClient.loop();
  }
}
#endif

// ============================================================
// 指令执行回执（前置声明）
// ============================================================
void sendCallback(const char* commandId, const char* status, const char* result);

// ============================================================
// 轮询待执行指令（HTTP 兜底）
// ============================================================
int pollCommands() {
  if (WiFi.status() != WL_CONNECTED) return -1;

  char url[256];
  snprintf(url, sizeof(url), "%s/devices/%s/commands/pending", apiBaseUrl, deviceSerial);

  
  http.begin(wifiClient, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("X-Device-Key", deviceKey);

  int code = http.GET();
  LOG_D("Poll", "GET %s → code=%d", url, code);
  if (code == 200) {
    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      LOG_W("Poll", "JSON parse error: %s", err.c_str());
      return -1;
    }

    JsonArray commands = doc["data"]["commands"];
    if (!commands || commands.size() == 0) {
      LOG_D("Poll", "No pending commands");
      return 0;
    }

    LOG_I("Poll", "Found %d command(s)", commands.size());

    for (JsonVariant cmd : commands) {
      const char* cmdId = cmd["id"];
      const char* cmdType = cmd["command"];
      JsonObject cmdPayload = cmd["payload"];

      LOG_I("Command", "Executing: %s (id=%s)", cmdType, cmdId);

      bool executed = false;
      String resultMsg;

      if (strcmp(cmdType, "POWER_ON") == 0) {
        relayOn();
        executed = true;
        resultMsg = "relay turned ON";
      } else if (strcmp(cmdType, "POWER_OFF") == 0) {
        relayOff();
        executed = true;
        resultMsg = "relay turned OFF";
      } else if (strcmp(cmdType, "TOGGLE") == 0) {
        relayToggle();
        executed = true;
        resultMsg = _relayState ? "relay toggled ON" : "relay toggled OFF";
      } else if (strcmp(cmdType, "REBOOT") == 0) {
        LOG_I("Command", "Rebooting...");
        sendCallback(cmdId, "EXECUTED", "rebooting");
        delay(500);
        ESP.restart();
        return 0;
      } else if (strcmp(cmdType, "SERVO_ANGLE") == 0) {
        int angle = cmdPayload["angle"] | 90;
        servoAttach();
        servoSetAngle(angle);
        delay(500);
        servoDetach();
        executed = true;
        resultMsg = "servo moved to " + String(angle) + "°";
      } else if (strcmp(cmdType, "SERVO_SWEEP") == 0) {
        servoAttach();
        servoSweep();
        servoDetach();
        executed = true;
        resultMsg = "servo sweep done";
      } else {
        sendCallback(cmdId, "FAILED", "unsupported command type");
        LOG_W("Command", "Unsupported: %s", cmdType);
        continue;
      }

      if (executed && cmdId) {
        sendCallback(cmdId, "EXECUTED", resultMsg.c_str());
      }
    }

    return commands.size();
  }

  if (code > 0) {
    String payload = http.getString();
    http.end();
    LOG_W("Poll", "Unexpected HTTP %d: %s", code, payload.c_str());
  } else {
    http.end();
    LOG_W("Poll", "HTTP request failed (code=%d)", code);
  }

  return -1;
}

// ============================================================
// 指令执行回执
// ============================================================
void sendCallback(const char* commandId, const char* status, const char* result) {
  if (!commandId || WiFi.status() != WL_CONNECTED) return;

  char url[256];
  snprintf(url, sizeof(url), "%s/devices/%s/commands/%s/callback", apiBaseUrl, deviceSerial, commandId);

  char body[128];
  snprintf(body, sizeof(body), "{\"status\":\"%s\",\"result\":\"%s\"}", status, result);

  WiFiClientSecure cbClient;
  HTTPClient cbHttp;
  cbClient.setInsecure();
  cbHttp.begin(cbClient, url);
  cbHttp.setTimeout(HTTP_TIMEOUT_MS);
  cbHttp.addHeader("Content-Type", "application/json");
  cbHttp.addHeader("X-Device-Key", deviceKey);

  int code = cbHttp.POST(body);
  LOG_D("Callback", "Code: %d, cmd=%s, status=%s", code, commandId, status);
  cbHttp.end();
}

// ============================================================
// 手动按钮处理
// ============================================================
void handleButton() {
  bool reading = digitalRead(BTN_PIN);

  // 消抖
  if (reading != _lastBtnState) {
    _lastBtnDebounce = millis();
  }

  if ((millis() - _lastBtnDebounce) > DEBOUNCE_MS) {
    if (reading != _btnState) {
      _btnState = reading;

      if (reading == LOW) {
        // 按钮按下
        _btnPressStart = millis();
        _btnPressed = true;
      } else {
        // 按钮释放
        if (_btnPressed) {
          unsigned long pressDuration = millis() - _btnPressStart;
          if (pressDuration >= LONG_PRESS_MS) {
            // 长按 → 配网
            LOG_I("Button", "Long press (%lums) → config portal", pressDuration);
            startConfigPortal();
          } else if (pressDuration >= DEBOUNCE_MS) {
            // 短按 → 切换继电器
            LOG_I("Button", "Short press (%lums) → toggle relay", pressDuration);
            relayToggle();
            // 手动切换后强制心跳上报
            reportHeartbeat();
            _lastHeartbeat = millis();
          }
          _btnPressed = false;
        }
      }
    }
  }

  // 长按检测（按下时持续检查）
  if (_btnState == LOW && _btnPressed) {
    if ((millis() - _btnPressStart) >= LONG_PRESS_MS) {
      LOG_I("Button", "Long press detected → config portal");
      startConfigPortal();
    }
  }

  _lastBtnState = reading;
}

// ============================================================
// setup
// ============================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(100);

  Serial.println("\n========================================\n");
  LOG_I("Boot", "P008 NodeMCU Relay v" FIRMWARE_VERSION);
  LOG_I("Boot", "Flash: %u KB", ESP.getFlashChipRealSize() / 1024);

  // ---------- 引脚初始化 ----------
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_INIT);   // 初始断开（安全态）
  _relayState = (RELAY_INIT == RELAY_PULL);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);        // LED 灭（断开状态）

  // ---------- 配网检测 ----------
  pinMode(BTN_PIN, INPUT_PULLUP);
  int holdMs = 0;
  unsigned long bootStart = millis();
  while (digitalRead(BTN_PIN) == LOW && holdMs < LONG_PRESS_MS && (millis() - bootStart) < 5000) {
    delay(10);
    holdMs += 10;
  }

  if (holdMs >= LONG_PRESS_MS) {
    LOG_I("FlashBtn", "3s hold → config portal");
    startConfigPortal();
    return;
  }

  ESP.wdtEnable(30000000UL);  // 30s 看门狗

  generateIdentity();
  loadParams();

  // ---------- 报警初始化 ----------
  alertInit();

  // ---------- WiFi ----------
  bool wifiOk = connectWiFi();
  if (!wifiOk) {
    LOG_W("Main", "WiFi failed, entering config portal...");
    startConfigPortal();
    return;
  }

#ifdef MQTT_ENABLE
  _mqttEnabled = true;
  LOG_I("MQTT", "MQTT enabled, broker=%s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
  connectMQTT();
#else
  LOG_I("MQTT", "MQTT disabled (compile without MQTT_ENABLE)");
#endif

  // ---------- 首次心跳（自动注册） ----------
  int hbCode = reportHeartbeat();
  if (hbCode == 200 || hbCode == 201) {
    LOG_I("Main", "Heartbeat success (code=%d)", hbCode);
  } else {
    LOG_W("Main", "Heartbeat code: %d (will retry in loop)", hbCode);
  }

  // LED 闪烁 2 次表示启动完成
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(150);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(150);
  }

  LOG_I("Main", "Ready. MQTT=%s, Poll=%lu ms",
    _mqttEnabled ? "ON" : "OFF",
    _pollIntervalMs);
}

// ============================================================
// loop
// ============================================================
void loop() {
  ESP.wdtFeed();

  // ---------- 手动按钮 ----------
  handleButton();

  // ---------- WiFi 检查 ----------
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("Loop", "WiFi lost, reconnecting...");
    if (!connectWiFi()) {
      delay(5000);
      return;
    }
  }

  unsigned long now = millis();

  // ---------- MQTT 保活 ----------
#ifdef MQTT_ENABLE
  maintainMQTT();
#endif

  // ---------- 指令轮询（HTTP 兜底） ----------
  // 仅在 MQTT 未连接时执行 HTTP 轮询，减少不必要的 HTTP 请求
  bool shouldPollHttp = !_mqttEnabled;
#ifdef MQTT_ENABLE
  shouldPollHttp = !mqttClient.connected();
#endif

  if (shouldPollHttp && (now - _lastPoll >= _pollIntervalMs)) {
    _lastPoll = now;
    pollCommands();
  }

  // ---------- 心跳上报 ----------
  if (now - _lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    _lastHeartbeat = now;
    int hbCode = reportHeartbeat();
    // 心跳后检查本地报警
    if (hbCode == 200) {
      alertCheck(0, 0);
    }
  }

  // ---------- 报警输出 ----------
  alertOutput();

  delay(200);  // 省 CPU
}
