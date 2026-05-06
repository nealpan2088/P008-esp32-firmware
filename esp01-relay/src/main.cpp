/**
 * P008 Environment Monitor — ESP-01 继电器模块固件
 * -----------------------------------------------------------
 * Hardware: ESP-01 + 继电器模块
 * Function: P008 平台远程控制继电器通断（POWER_ON / POWER_OFF / REBOOT）
 *
 * 工作原理:
 *   1. 上电后自动注册设备到 P008 平台
 *   2. 每 10 秒轮询 GET /devices/{serial}/commands/pending
 *   3. 获取到 POWER_ON / POWER_OFF 指令 → 控制继电器
 *   4. 执行后 POST 回执状态
 *
 * 首次配网:
 *   手机连 P008-Relay 热点 → 192.168.4.1 配 WiFi
 *
 * 换 WiFi:
 *   按住 GPIO0(FLASH) 上电 → 释放 → 进入配网模式（同现有传感器固件）
 *
 * Pinout (ESP-01):
 *   GPIO0 — 继电器控制（模块板载已接好）
 *   GPIO2 — 板载 LED（心跳指示）
 *   TX/RX — 串口调试
 *
 * 序列号规范:
 *   RELAY-PL-{芯片8位HEX}
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

#include "config.h"
#include "log.h"
#include "alert.h"

// --------------- 全局变量 ---------------
WiFiClientSecure wifiClientSecure;
HTTPClient http;

char deviceSerial[32] = "";
char deviceKey[64]    = "";
char apiBaseUrl[128]  = API_BASE_URL;
char chipIdHex[16]    = "";
char _autoSerial[32]  = "";
char _autoKey[64]     = "";

bool _relayState = false;        // true=开, false=关
unsigned long _lastPoll = 0;
unsigned long _pollIntervalMs = COMMAND_POLL_INTERVAL_MS;

#define WIFI_TIMEOUT_MS 30000
#define WDT_TIMEOUT_US  (30000 * 1000UL)   // 30 秒看门狗（死机自动重启）

// --------------- HMAC 密钥生成 ---------------
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

// --------------- 心跳上报间隔 ---------------
#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS 60000   // 每分钟上报一次状态
#endif

// --------------- 工具函数 ---------------
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
}

// --------------- WiFi 连接 ---------------
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

// --------------- 配网 Portal ---------------
void startConfigPortal() {
  LOG_I("WiFiManager", "Starting config portal...");
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  wm.startConfigPortal(AP_NAME);
  LOG_I("WiFiManager", "Portal done, restarting...");
  delay(100);
  ESP.restart();
}

// --------------- 心跳上报（注册 + 状态） ---------------
// 首次上报自动注册设备，之后定期上报继电器 ON/OFF 状态
int reportHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return -1;

  char url[256];
  snprintf(url, sizeof(url), "%s/devices/%s/data", apiBaseUrl, deviceSerial);

  // 上报继电器状态：relayOn=true/false 放到 otherData 里
  char body[300];
  snprintf(body, sizeof(body),
    "{\"temp\":null,\"humidity\":null,\"battery\":0,\"otherData\":{"
    "\"firmwareVer\":\"" FIRMWARE_VERSION "\",\"channel\":\"" FIRMWARE_CHANNEL "\","
    "\"chipId\":\"%s\",\"type\":\"relay\",\"relayOn\":%s"
    "}}",
    chipIdHex, _relayState ? "true" : "false");

  LOG_I("Register", "POST %s", url);
  wifiClientSecure.setInsecure();
  http.begin(wifiClientSecure, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Key", deviceKey);

  int code = http.POST(body);
  String response = http.getString();
  http.end();

  // 解析心跳响应：提取 config.reportInterval + config.thresholds
  if (code == 200) {
    const char* respStr = response.c_str();
    // 解析 reportInterval
    const char* riKey = strstr(respStr, "\"reportInterval\"");
    if (riKey) {
      unsigned long newInterval = atol(riKey + 17);
      if (newInterval >= 10 && newInterval <= 3600) {
        _pollIntervalMs = newInterval * 1000UL;
      }
    }
    // 解析阈值（报警模块）
    const char* cfgKey = strstr(respStr, "\"thresholds\"");
    if (cfgKey) {
      alertParseThresholds(cfgKey);
    }
  }

  LOG_I("Register", "Code: %d, Response: %s", code, response.c_str());
  return code;
}

// --------------- 指令执行回执（前置声明） ---------------
void sendCallback(const char* commandId, const char* status, const char* result);

// --------------- 轮询待执行指令 ---------------
int pollCommands() {
  if (WiFi.status() != WL_CONNECTED) return -1;

  char url[256];
  snprintf(url, sizeof(url), "%s/devices/%s/commands/pending", apiBaseUrl, deviceSerial);

  wifiClientSecure.setInsecure();
  http.begin(wifiClientSecure, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("X-Device-Key", deviceKey);

  int code = http.GET();
  LOG_D("Poll", "HTTP GET %s → code=%d", url, code);
  if (code == 200) {
    String payload = http.getString();
    http.end();

    LOG_D("Poll", "Response: %s", payload.c_str());

    // 解析 JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      LOG_W("Poll", "JSON parse error: %s", err.c_str());
      return -1;
    }

    JsonArray commands = doc["data"]["commands"];
    if (!commands || commands.size() == 0) {
      LOG_D("Poll", "No pending commands");
      return 0;   // 无待执行指令
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
        // 开
        digitalWrite(RELAY_PIN, RELAY_ON);
        digitalWrite(LED_BUILTIN, LOW);   // LED 亮
        _relayState = true;
        executed = true;
        resultMsg = "relay turned ON";
        LOG_I("Relay", "ON");
      } else if (strcmp(cmdType, "POWER_OFF") == 0) {
        // 关
        digitalWrite(RELAY_PIN, RELAY_OFF);
        digitalWrite(LED_BUILTIN, HIGH);  // LED 灭
        _relayState = false;
        executed = true;
        resultMsg = "relay turned OFF";
        LOG_I("Relay", "OFF");
      } else if (strcmp(cmdType, "REBOOT") == 0) {
        // 重启—先回执再重启
        LOG_I("Command", "Rebooting...");
        sendCallback(cmdId, "EXECUTED", "rebooting");
        delay(500);
        ESP.restart();
        return 0;
      } else {
        // 不支持的指令
        sendCallback(cmdId, "FAILED", "unsupported command type");
        LOG_W("Command", "Unsupported: %s", cmdType);
        continue;
      }

      // 回执执行结果
      if (executed && cmdId) {
        sendCallback(cmdId, "EXECUTED", resultMsg.c_str());
      }
    }

    return commands.size();
  }

  // 非 200 响应
  if (code > 0) {
    String payload = http.getString();
    http.end();
    LOG_W("Poll", "Unexpected HTTP %d: %s", code, payload.c_str());
  } else {
    http.end();
    LOG_W("Poll", "HTTP request failed (code=%d)", code);
  }

  http.end();
  return -1;
}

// --------------- 指令执行回执 ---------------
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

// --------------- setup ---------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(100);

  Serial.println("\n========================================\n");
  LOG_I("Boot", "P008 Relay v" FIRMWARE_VERSION);
  LOG_I("Boot", "Flash: %u KB", ESP.getFlashChipRealSize() / 1024);

  // ⚠️ 引脚初始化为输出
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ON);    // 默认断开（安全态，设备不通电）
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);       // LED 灭

  // ⚠️ GPIO0 既是 RELAY_PIN 又是配网检测脚
  // 需要临时切换为输入检测配网按钮，检测完再切回输出
  pinMode(0, INPUT_PULLUP);
  int holdMs = 0;
  unsigned long bootStart = millis();
  while (digitalRead(0) == LOW && holdMs < 3000 && (millis() - bootStart) < 5000) {
    delay(10);
    holdMs += 10;
  }
  // 恢复为输出
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_ON);

  if (holdMs >= 3000) {
    LOG_I("FlashBtn", "3s hold → config portal");
    startConfigPortal();
    return;
  }

  ESP.wdtEnable(WDT_TIMEOUT_US);

  generateIdentity();
  loadParams();

  // 初始化本地报警
  alertInit();

  // WiFi
  bool wifiOk = connectWiFi();
  if (!wifiOk) {
    LOG_W("Main", "WiFi failed, entering config portal...");
    startConfigPortal();
    return;
  }

  // 自动注册（首次上报创建设备，之后更新心跳+状态）
  int hbCode = reportHeartbeat();
  if (hbCode == 200 || hbCode == 201) {
    LOG_I("Main", "Heartbeat success (code=%d)", hbCode);
  } else {
    LOG_W("Main", "Heartbeat code: %d (will retry in loop)", hbCode);
  }

  // LED 闪烁 2 次表示启动完成
  digitalWrite(LED_BUILTIN, LOW);
  delay(200);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  delay(200);
  digitalWrite(LED_BUILTIN, HIGH);

  LOG_I("Main", "Ready. Polling commands every %lu ms", _pollIntervalMs);
}

// --------------- 全局变量（补） ---------------
unsigned long _lastHeartbeat = 0;
bool _alarmCheckTriggered = false;

// --------------- loop ---------------
void loop() {
  ESP.wdtFeed();

  // 检查 WiFi
  if (WiFi.status() != WL_CONNECTED) {
    LOG_W("Loop", "WiFi lost, reconnecting...");
    if (!connectWiFi()) {
      delay(5000);
      return;
    }
  }

  unsigned long now = millis();

  // 到时间轮询指令
  if (now - _lastPoll >= _pollIntervalMs) {
    _lastPoll = now;
    pollCommands();
  }

  // 定期心跳上报（上报继电器当前状态，让后端知道在线状态+开关状态）
  if (now - _lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    _lastHeartbeat = now;
    int hbCode = reportHeartbeat();
    // 心跳后检查本地报警
    if (hbCode == 200) {
      _alarmCheckTriggered = alertCheck(0, 0, 0);
    }
  }

  // 报警输出（每 500ms 更新 LED 状态）
  alertOutput();

  delay(500);   // 省 CPU
}
