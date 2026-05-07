// ============================================================
// P008 日志系统 — 轻量级日志分级
// 与 esp8266-sensor 和 esp01-relay 保持完全一致
// ============================================================
#ifndef P008_LOG_H
#define P008_LOG_H

#include <Arduino.h>
#include "config.h"

// --------------- 日志级别 ---------------
#define LVL_NONE  0
#define LVL_ERROR 1
#define LVL_WARN  2
#define LVL_INFO  3
#define LVL_DEBUG 4

// --------------- 日志宏 ---------------
#if LOG_LEVEL >= LVL_ERROR
#define LOG_E(tag, fmt, ...)  _logPrint('E', tag, fmt, ##__VA_ARGS__)
#else
#define LOG_E(tag, fmt, ...)  do {} while(0)
#endif

#if LOG_LEVEL >= LVL_WARN
#define LOG_W(tag, fmt, ...)  _logPrint('W', tag, fmt, ##__VA_ARGS__)
#else
#define LOG_W(tag, fmt, ...)  do {} while(0)
#endif

#if LOG_LEVEL >= LVL_INFO
#define LOG_I(tag, fmt, ...)  _logPrint('I', tag, fmt, ##__VA_ARGS__)
#else
#define LOG_I(tag, fmt, ...)  do {} while(0)
#endif

#if LOG_LEVEL >= LVL_DEBUG
#define LOG_D(tag, fmt, ...)  _logPrint('D', tag, fmt, ##__VA_ARGS__)
#else
#define LOG_D(tag, fmt, ...)  do {} while(0)
#endif

// --------------- 兼容旧 Serial.printf ---------------
#define LOG_RAW(fmt, ...)     Serial.printf(fmt, ##__VA_ARGS__)

// --------------- 内部函数（static inline 避免多重定义） ---------------
static inline void _logPrint(char level, const char* tag, const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.printf("[%c][%s] %s\n", level, tag, buf);
}

#endif // P008_LOG_H
