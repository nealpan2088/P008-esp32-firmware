/**
 * P008 — 本地报警模块
 * ========================================
 * 功能：
 *   1. 解析云端下发的阈值（心跳响应的 config.thresholds）
 *   2. 本地判断：电流/温度/湿度是否超限
 *   3. 报警输出：复用板载 LED（快闪）或独立 GPIO
 *   4. 报警状态：触发和恢复时强制心跳上报
 *
 * 使用方式：
 *   #include "alert.h"
 *   // 在 loop() 中每周期调用一次
 *   alertCheck(currentA, temp, humidity);
 *
 * 宏开关（build_flags）:
 *   -D ALARM_ENABLE=1   开启报警功能
 *   -D ALARM_PIN=2      报警输出引脚（默认 GPIO2）
 *
 * 阈值通过心跳响应 config.thresholds 动态更新：
 *   { "current": { "max": 8.0, "min": 0.1 },
 *     "temp":    { "max": 50,  "min": -10 },
 *     "humidity": { "max": 90,  "min": 10 } }
 */

#ifndef ALERT_H
#define ALERT_H

#include <Arduino.h>

#include "log.h"

// --------------- 开关控制 ---------------
#ifdef ALARM_ENABLE
#define ALARM_ACTIVE 1
#else
#define ALARM_ACTIVE 0
#endif

// --------------- 报警状态 ---------------
enum AlarmState {
  ALARM_NONE    = 0,     // 正常
  ALARM_CURRENT = 1,     // 电流异常
  ALARM_TEMP    = 2,     // 温度异常
  ALARM_HUMID   = 3,     // 湿度异常
};

// --------------- 全局变量 ---------------
static AlarmState _alarmState = ALARM_NONE;
static unsigned long _alertBlinkLast = 0;
static bool _alertLedState = false;

// 云端下发的阈值（由 handleConfigUpdate() 更新）
static float _threshCurrentMax = 8.0;    // 电流上限 (A)
static float _threshCurrentMin = 0.05;  // 电流下限 (A) — 空载噪声约 0.03A
static float _threshTempMax     = 50.0;  // 温度上限 (°C)
static float _threshTempMin     = -10.0; // 温度下限 (°C)
static float _threshHumidityMax = 90.0;  // 湿度上限 (%)
static float _threshHumidityMin = 10.0;  // 湿度下限 (%)

// --------------- 从心跳响应解析阈值 ---------------
void alertParseThresholds(const char* jsonPayload) {
  if (!jsonPayload || strlen(jsonPayload) < 10) return;

  // 查找 "current":{"max": 或 "current":{"min":
  const char* keyCurrent = strstr(jsonPayload, "\"current\"");
  if (keyCurrent) {
    const char* keyMax = strstr(keyCurrent, "\"max\"");
    if (keyMax) {
      float v = atof(keyMax + 6);
      if (v > 0 && v < 100) _threshCurrentMax = v;
    }
    const char* keyMin = strstr(keyCurrent, "\"min\"");
    if (keyMin) {
      float v = atof(keyMin + 6);
      if (v >= 0 && v < 100) _threshCurrentMin = v;
    }
  }

  const char* keyTemp = strstr(jsonPayload, "\"temp\"");
  if (keyTemp) {
    const char* keyMax = strstr(keyTemp, "\"max\"");
    if (keyMax) { float v = atof(keyMax + 6); if (v > -50 && v < 200) _threshTempMax = v; }
    const char* keyMin = strstr(keyTemp, "\"min\"");
    if (keyMin) { float v = atof(keyMin + 6); if (v > -50 && v < 200) _threshTempMin = v; }
  }

  const char* keyHumid = strstr(jsonPayload, "\"humidity\"");
  if (keyHumid) {
    const char* keyMax = strstr(keyHumid, "\"max\"");
    if (keyMax) { float v = atof(keyMax + 6); if (v > 0 && v <= 100) _threshHumidityMax = v; }
    const char* keyMin = strstr(keyHumid, "\"min\"");
    if (keyMin) { float v = atof(keyMin + 6); if (v >= 0 && v <= 100) _threshHumidityMin = v; }
  }

  LOG_I("Alert", "Thresholds: cur=[%.1f~%.1f]A temp=[%.0f~%.0f]C hum=[%.0f~%.0f]%%",
        _threshCurrentMin, _threshCurrentMax,
        _threshTempMin, _threshTempMax,
        _threshHumidityMin, _threshHumidityMax);
}

// --------------- 判断是否报警 ---------------
bool alertShouldTrigger(float currentA, float temp, float humidity) {
  if (currentA > _threshCurrentMax || (currentA > 0 && currentA < _threshCurrentMin))
    return true;
  if (temp > _threshTempMax || (temp > -50 && temp < _threshTempMin))
    return true;
  if (humidity > _threshHumidityMax || (humidity > 0 && humidity < _threshHumidityMin))
    return true;
  return false;
}

// --------------- 获取当前报警原因 ---------------
AlarmState alertGetReason(float currentA, float temp, float humidity) {
  if (currentA > _threshCurrentMax || (currentA > 0 && currentA < _threshCurrentMin))
    return ALARM_CURRENT;
  if (temp > _threshTempMax || (temp > -50 && temp < _threshTempMin))
    return ALARM_TEMP;
  if (humidity > _threshHumidityMax || (humidity > 0 && humidity < _threshHumidityMin))
    return ALARM_HUMID;
  return ALARM_NONE;
}

// --------------- 报警输出 ---------------
void alertOutput() {
#if ALARM_ACTIVE
  if (_alarmState == ALARM_NONE) {
    // 正常：LED 常亮（恢复心跳指示）
    digitalWrite(ALARM_PIN, LOW);
    return;
  }

  // 报警：500ms 快闪
  unsigned long now = millis();
  if (now - _alertBlinkLast >= 500) {
    _alertBlinkLast = now;
    _alertLedState = !_alertLedState;
    digitalWrite(ALARM_PIN, _alertLedState ? HIGH : LOW);
  }
#endif
}

// --------------- 主检查函数（loop 中调用） ---------------
bool alertCheck(float currentA, float temp, float humidity) {
#if !ALARM_ACTIVE
  return false;
#endif

  AlarmState newState = alertGetReason(currentA, temp, humidity);

  if (newState != _alarmState) {
    // 状态变化：触发/恢复
    _alarmState = newState;
    if (newState != ALARM_NONE) {
      LOG_W("Alert", "TRIGGERED: reason=%d cur=%.2fA temp=%.1fC hum=%.1f%%",
            newState, currentA, temp, humidity);
    } else {
      LOG_I("Alert", "RECOVERED: all normal");
    }
    return true;  // 状态变化，调用者应强制上报
  }

  alertOutput();
  return false;
}

// --------------- 初始化 ---------------
void alertInit() {
#if ALARM_ACTIVE
  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(ALARM_PIN, LOW);  // 初始：LED 常亮
  LOG_I("Alert", "Local alarm enabled on GPIO%d", ALARM_PIN);
#else
  LOG_I("Alert", "Local alarm disabled");
#endif
}

#endif // ALERT_H
