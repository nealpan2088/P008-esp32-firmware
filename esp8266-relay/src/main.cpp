/**
 * P008 Environment Monitor — NodeMCU 1路光耦隔离继电器模块固件
 * -----------------------------------------------------------
 * Hardware:  NodeMCU V3 (ESP8266) + 1路光耦隔离继电器模块
 * Function:  P008 平台远程控制 + 手动按钮控制 + 心跳上报 + 本地报警
 *
 * 功能特性:
 *   1. 远程指令控制（POWER_ON / POWER_OFF / TOGGLE / REBOOT）
 *      每 5 秒轮询 GET /devices/{serial}/commands/pending
 *   2. 手动按钮控制（短按切换、长按配网）
 *   3. 心跳上报（每 60 秒上报继电器 + 报警状态）
 *   4. 本地报警（云端阈值下发 → 超限 LED 快闪）
 *   5. 离线缓存（WiFi 断线时不丢指令）
 *
 * 接线（NodeMCU → 1路光耦隔离继电器模块）:
 *   继电器模块 IN  → D1 (GPIO5) — 控制信号（低电平触发）
 *   继电器模块 VCC → Vin (5V)   — 线圈供电（部分模块支持 3.3V）
 *   继电器模块 GND → GND
 *   COM/NO/NC       → 按实际负载接
 *
 * ⚠️ 低电平触发: 拉低=Led亮/继电器吸合；拉高=LED灭/继电器断开
 * ⚠️ 上电默认断开，避免设备误启动
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
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

#include "config.h"
#include "log.h"
#include "alert.h"

// ============================================================
// 全局变量
// ============================================================
WiFiClientSecure wifiClientSecure;
HTTPClient http;

char deviceSerial[32] = "";
char deviceKey[64]    = "";
char apiBaseUrl[128]  = API_BASE_URL;
char chipIdHex[16]    = "";
char _autoSerial[32]  = "";
char _autoKey[64]     = "";

bool _relayState = false;        // true=吸合(通), false=断开(断)
unsigned long _lastPoll = 0;
unsigned long _lastHeartbeat = 0;
unsigned long _pollIntervalMs = COMMAND_POLL_INTERVAL_MS;

// 手动按钮消抖
unsigned long _lastBtnDebounce = 0;
bool _lastBtnState = HIGH;
bool _btnState = HIGH;
unsigned long _btnPressStart = 0;
bool _btnPressed = false;

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

  char body[300];
  snprintf(body, sizeof(body),
    "{\"temp\":null,\"humidity\":null,\"battery\":0,\"otherData\":{"
    "\"firmwareVer\":\"" FIRMWARE_VERSION "\",\"channel\":\"" FIRMWARE_CHANNEL "\","
    "\"chipId\":\"%s\",\"type\":\"relay\",\"relayOn\":%s"
    "}}",
    chipIdHex, _relayState ? "true" : "false");

  LOG_D("Heartbeat", "POST %s", url);
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
// 指令执行回执（前置声明）
// ============================================================
void sendCallback(const char* commandId, const char* status, const char* result);

// ============================================================
// 轮询待执行指令
// ============================================================
int pollCommands() {
  if (WiFi.status() != WL_CONNECTED) return -1;

  char url[256];
  snprintf(url, sizeof(url), "%s/devices/%s/commands/pending", apiBaseUrl, deviceSerial);

  wifiClientSecure.setInsecure();
  http.begin(wifiClientSecure, url);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.addHeader("X-Device-Key", deviceKey);

  int code = http.GET();
  LOG_D("Poll", "GET %s → code=%d", url, code);
  if (code == 200) {
    String payload = http.getString();
    http.end();

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
  // 按住 FLASH 按钮上电 → 释放 → 进入配网
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

  LOG_I("Main", "Ready. Polling commands every %lu ms", _pollIntervalMs);
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

  // ---------- 指令轮询 ----------
  if (now - _lastPoll >= _pollIntervalMs) {
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
