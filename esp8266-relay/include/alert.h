/**
 * P008 — 本地报警模块（继电器版）
 * ========================================
 * 功能：
 *   1. 解析云端下发的阈值（心跳响应的 config.thresholds）
 *   2. 本地判断：温度/湿度是否超限
 *   3. 报警输出：板载 LED 快闪
 *   4. 报警状态变化时强制心跳上报
 *
 * 使用方式：
 *   #include "alert.h"
 *   // 在 loop() 中每周期调用一次
 *   alertCheck(0, temp, humidity);
 *
 * 宏开关（build_flags）:
 *   -D ALARM_ENABLE=1   开启报警功能
 *   -D ALARM_PIN=引脚    报警输出引脚（默认复用 LED_BUILTIN）
 *
 * 阈值通过心跳响应 config.thresholds 动态更新：
 *   { "temp":    { "max": 50,  "min": -10 },
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
  ALARM_NONE    = 0,
  ALARM_TEMP    = 1,
  ALARM_HUMID   = 2,
};

// --------------- 全局变量 ---------------
static AlarmState _alarmState = ALARM_NONE;
static unsigned long _alertBlinkLast = 0;
static bool _alertLedState = false;

// 云端下发的阈值（由 alertParseThresholds() 更新）
static float _threshTempMax     = 50.0;
static float _threshTempMin     = -10.0;
static float _threshHumidityMax = 90.0;
static float _threshHumidityMin = 10.0;

// --------------- 解析阈值 ---------------
void alertParseThresholds(const char* jsonPayload) {
  if (!jsonPayload || strlen(jsonPayload) < 10) return;

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

  LOG_I("Alert", "Thresholds: temp=[%.0f~%.0f]C hum=[%.0f~%.0f]%%",
        _threshTempMin, _threshTempMax,
        _threshHumidityMin, _threshHumidityMax);
}

// --------------- 报警输出 ---------------
void alertOutput() {
#if ALARM_ACTIVE
  if (_alarmState == ALARM_NONE) {
    digitalWrite(ALARM_PIN, LOW);   // 正常：LED 常亮
    return;
  }

  unsigned long now = millis();
  if (now - _alertBlinkLast >= 500) {
    _alertBlinkLast = now;
    _alertLedState = !_alertLedState;
    digitalWrite(ALARM_PIN, _alertLedState ? HIGH : LOW);
  }
#endif
}

// --------------- 主检查函数 ---------------
bool alertCheck(float temp, float humidity) {
#if !ALARM_ACTIVE
  return false;
#endif

  // 判断是否超限
  bool triggered = false;
  if ((temp > _threshTempMax || (temp > -50 && temp < _threshTempMin)) ||
      (humidity > _threshHumidityMax || (humidity > 0 && humidity < _threshHumidityMin))) {
    triggered = true;
  }

  AlarmState newState = triggered ? ALARM_TEMP : ALARM_NONE;

  if (newState != _alarmState) {
    _alarmState = newState;
    if (triggered) {
      LOG_W("Alert", "TRIGGERED: temp=%.1fC hum=%.1f%%", temp, humidity);
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
  digitalWrite(ALARM_PIN, LOW);
  LOG_I("Alert", "Local alarm enabled on GPIO%d", ALARM_PIN);
#else
  LOG_I("Alert", "Local alarm disabled");
#endif
}

#endif // ALERT_H
